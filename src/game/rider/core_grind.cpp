// The skater's core physics component: getting onto a rail - a point rail's kick or spin, a lip trick, a grind.

#include <cmath>
#include <cstdint>
#include <new>

#include "engine/core/dbg.h"
#include "engine/maths/maths.h"
#include "engine/maths/mat4.h"
#include "engine/maths/random.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/node_components.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/trigger_component.h"
#include "engine/objects/walk_hang.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/gap_entry.h"
#include "game/rider/rider.h"
#include "game/rider/skater_sound.h"
#include "game/rider/skater_state.h"
#include "game/rider/trick_component.h"
#include "game/rider/walk_physics.h"
#include "platform/game_clock.h"
#include "game/rider/balance_trick.h"
#include "game/skate/point_tally.h"
#include "game/park_editing/park_editor.h"
#include "game/skate/skate_module.h"
#include "engine/objects/trigger_events.h"

namespace skater {

using namespace core_fields;

maths::Vec4 g_acid_hold;

namespace {

constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A; // (a component)
constexpr uint32_t kSkaterGap = 0x11B4C67F;           // (a component)
constexpr uint32_t kTriggerScript = 0x2CA8A299;
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kNatasDelay = 0x0A477668;            // Physics_Natas_Delay (seconds)
constexpr uint32_t kKickUpwardAngle = 0xBB357ECB;       // Physics_Point_Rail_Kick_Upward_Angle
constexpr uint32_t kPointRailSpeedBoost = 0xA3EF4833;   // Point_Rail_Speed_Boost
constexpr uint32_t kRailSpeedBoost = 0x457BB395;        // Rail_Speed_Boost
constexpr uint32_t kRailRegrabDelay = 0x2B4645AD;       // Rail_minimum_rerail_time
constexpr uint32_t kJumpAdjustSpeed = 0x896C8888;       // jump_adjust_speed
constexpr uint32_t kSpeedCapStat = 0xCC5F87AA;          // Skater_Max_Max_Speed_Stat
constexpr uint32_t kLipAllowAngle = 0x8157C5D9;         // LipAllowAngle
constexpr uint32_t kLipAllowAngleOverride = 0xF3B6F95E; // LipAllowAngle_Override
constexpr uint32_t kLipHorizontalAngle = 0xE2A85FBB;    // LipPlayerHorizontalAngle
constexpr uint32_t kLipRampVertAngle = 0x61079462;      // LipRampVertAngle
constexpr uint32_t kLipTrick = 0x1647CF96;              // LipTrick (a script)
constexpr uint32_t kGroundSnapUp = 0xE4D79235;          // Physics_Ground_Snap_Up
constexpr uint32_t kBadLedgeDropDown = 0x9CD7ED5C;      // Rail_Bad_Ledge_Drop_Down_Dist
constexpr uint32_t kBadLedgeSide = 0x31669752;          // Rail_Bad_Ledge_Side_Dist
constexpr uint32_t kRailTolerance = 0xD0688D4E;         // Rail_Tolerance
constexpr uint32_t kPointRail = 0xB8048F1D;             // PointRail (event)
constexpr uint32_t kPointRailSpin = 0xDA82D6D6;         // PointRailSpin (event)
constexpr uint32_t kDisallowGrind = 0x96DCA7DC;         // Physics_Wallplant_Disallow_Grind_Duration
constexpr uint32_t kCornerLeaveAngle = 0x76C1DA15;      // Rail_Corner_Leave_Angle
constexpr uint32_t kRailManagerType = 0x1CDFB2A3;       // RailManager (a component)
constexpr uint32_t kParkEditorLevel = 0xD443A2BC;       // (the park editor's level)

// The trigger events (const int[3]).

constexpr float kDegrees = 0.0174532924f; // (degrees to radians, 0x00645464)
constexpr float kNearPoint = 36.0f;       // (6 squared: how far from the point a hit may be)

// The acid drop's hold velocity (debugging: "GRINDING FROM ACID DROP").
maths::Vec4& acid_hold() { return g_acid_hold; }

const maths::Vec4 kUpAxis = {0.0f, 1.0f, 0.0f, 1.0f};

void change_phase(void* component, int state)
{
    static_cast<SkaterPhysicsModule*>(component)->set_physics_state(state);
}
void* get_component(void* component, uint32_t offset) { return get_at<void*>(component, offset); }

// The rail's terrain (the node being ground) told to the flags (0x005196A0) and the sound component.
void set_rail_terrain(void* component)
{
    uint8_t terrain = get_at<objects::GrindPoint*>(component, kRailNode)->surface_type_;
    reinterpret_cast<objects::RiderPoseState*>(flags_of(component))->set_surface_kind(terrain);
    get_at<uint8_t>(get_component(component, kSfxModuleSlot), 0x1C) = terrain;
}

// The landing sound (0x00518CF0): by speed (a fraction of the max max speed) and flag +0xE1.
void play_land_sound(void* component)
{
    void* object = object_of(component);
    float speed = static_cast<float>(length_of(vector_at(object, kVelocity)));
    float fraction = speed / static_cast<objects::Rider*>(object)->get_scaled_physics(kSpeedCapStat);
    static_cast<objects::RiderSfxPlayer*>(get_component(component, kSfxModuleSlot))
        ->play_land(fraction, bit_mark(component, 0xE1));
}

// The rail node's TriggerScript run for `events` (with what the skater rides on, if anything still there).
void trigger_rail_node(void* component, const int* events)
{
    const uint8_t* ride_object = nullptr;
    void* ride = get_component(component, kRide);
    if (auto* tracker = get_at<objects::RideTracker*>(ride, 0x18)) {
        if (tracker->entity_.owner_object_)
            ride_object = reinterpret_cast<const uint8_t*>(tracker->entity_.owner_object_);
        else
            static_cast<objects::MovingSurfaceModule*>(ride)->stop_riding();
    }
    static_cast<objects::ActivationZoneModule*>(get_component(component, kActivationZoneSlot))
        ->get_trigger_node(events, get_at<uint32_t>(component, kRailScriptNode), nullptr, ride_object);
}

// A feeler from the skater to the point: false when it hits something more than 6 from the point. The distance
// squared as the original: from the differences unrounded, z first (`from_vector`: the point less the hit as a
// vector - rounded - x first).
bool clear_to_point(void* component, const maths::Vec4* point, bool from_vector)
{
    vector_at(component, kProbeOrigin) = vector_at(object_of(component), kPosition);
    vector_at(component, kProbeTip) = *point;
    if (!get_feel(component, 0x10, 0))
        return true;
    const maths::Vec4& hit = vector_at(component, kHitPoint);
    double distance;
    if (from_vector) {
        maths::Vec4 d = *point - hit;
        distance = (static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y) + static_cast<double>(d.z) * d.z;
    } else {
        double dx = static_cast<double>(point->x) - hit.x;
        double dy = static_cast<double>(point->y) - hit.y;
        double dz = static_cast<double>(point->z) - hit.z;
        distance = (dz * dz + dy * dy) + dx * dx;
    }
    return !(distance > kNearPoint);
}

// What every way onto a rail does first: the skater at the point, off vert, not wall riding (as it ends: 0x004FE860
// with the up straight and the display matrix reset - `always` for a point rail spin), into the rail state.
void onto_rail(void* component, const maths::Vec4* point, bool always)
{
    vector_at(object_of(component), kPosition) = *point;
    set_flag(component, 0x30, 0);
    set_flag(component, 0x38, 0);
    get_at<uint8_t>(component, 0x2D0) = 0;
    if (always || state_of(component) == kWallRide) {
        static_cast<SkaterPhysicsModule*>(component)->turn_to_normal(kUpAxis);
        matrix_at(component, kDisplayMatrix) = matrix_at(object_of(component), kMatrix);
    }
    change_phase(component, kRail);
}

// Barely moving: the velocity's x and z from the facing (a point rail: within 0.01; a rail: exactly 0). Not from an
// object: a fast skater's velocity turned level (its speed kept).
void prepare_velocity(void* component, bool exactly_still, bool from_object)
{
    void* object = object_of(component);
    maths::Vec4& v = vector_at(object, kVelocity);
    bool still = exactly_still ? v.x == 0.0f && v.z == 0.0f : std::fabs(v.x) < 0.01f && std::fabs(v.z) < 0.01f;
    if (still) {
        v.x = vector_at(object, kFacing).x;
        vector_at(object_of(component), kVelocity).z = vector_at(object_of(component), kFacing).z;
    }
    if (!from_object) {
        maths::Vec4& w = vector_at(object_of(component), kVelocity);
        if (static_cast<double>(w.z) * w.z + static_cast<double>(w.x) * w.x > 100.0f)
            w.align_to_plane(kUpAxis);
    }
}

// Facing away from the velocity (the skater pointing against it).
bool moving_against_facing(void* object)
{
    const maths::Vec4& v = vector_at(object, kVelocity);
    const maths::Vec4& f = vector_at(object, kFacing);
    double dot = (static_cast<double>(v.z) * f.z + static_cast<double>(v.y) * f.y) + static_cast<double>(f.x) * v.x;
    return dot < 0.0;
}

float jump_adjust_speed() { return objects::FootMovement::get_param(kJumpAdjustSpeed); }

// The flags a new rail clears (the time of getting on kept at +0x234).
void clear_rail_flags(void* component)
{
    get_at<int>(component, 0x234) = get_now();
    set_flag(component, 0x78, 0);
    set_flag(component, 0xA8, 0);
    set_flag(component, 0x80, 0);
    set_flag(component, 0xB0, 0);
}

// (0x0050A770, a node on its own) Kicked off a point rail: up into the air along the move (or the facing, when
// from an object and too slow against it), at the kick angle, with the point rail boost.
void kick_off_point_rail(SkaterPhysicsModule* c, const maths::Vec4* point, bool from_object)
{
    if (!clear_to_point(c, point, false))
        return;
    prepare_velocity(c, false, from_object);
    maths::Vec4 along = vector_at(object_of(c), kVelocity);
    along.y = 0.0f;
    along.make_unit();
    double angle = static_cast<double>(script::fetch_float(kKickUpwardAngle)) * kDegrees;
    double cosine = std::cos(angle);
    maths::Vec4 kick;
    kick.w = 1.0f;
    kick.x = static_cast<float>(along.x * cosine);
    kick.y = static_cast<float>(std::sin(angle));
    kick.z = static_cast<float>(along.z * cosine);

    script::Struct* script = level_nodes::fetch_vertex(get_at<objects::GrindPoint*>(c, kRailNode)->label_, true);
    script->read_hash_value(kName, &get_at<uint32_t>(c, kRailScriptNode), 1);
    static_cast<objects::MoveExecutor*>(get_component(c, kSkaterComponent))
        ->pass_to_trick_objects(get_at<uint32_t>(c, kRailScriptNode));
    uint32_t trigger_script = 0;
    if (script->read_hash_value(kTriggerScript, &trigger_script, 0))
        trigger_rail_node(c, kGrindEvents);
    onto_rail(c, point, false);
    set_rail_terrain(c);
    if (!get_at<uint8_t>(c, 0x2ED))
        play_land_sound(c);

    maths::Vec4& v = vector_at(object_of(c), kVelocity);
    float kept_y = v.y;
    v.project_onto(along);
    if (from_object && moving_against_facing(object_of(c))) {
        float adjust = static_cast<float>(jump_adjust_speed() * 1.1f);
        const maths::Vec4& w = vector_at(object_of(c), kVelocity);
        double speed =
            (static_cast<double>(w.x) * w.x + static_cast<double>(w.y) * w.y) + static_cast<double>(w.z) * w.z;
        if (static_cast<double>(adjust) * adjust > speed) {
            vector_at(object_of(c), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
            along = vector_at(object_of(c), kFacing);
            along.y = 0.0f;
            along.make_unit();
        }
    }
    vector_at(object_of(c), kVelocity).y = kept_y;
    float boost = script::fetch_float(kPointRailSpeedBoost);
    maths::Vec4& u = vector_at(object_of(c), kVelocity);
    float y = along.y * boost, z = along.z * boost, w = along.w * boost;
    u.x = static_cast<float>(static_cast<double>(along.x) * boost + u.x);
    u.y += y;
    u.z += z;
    u.w += w;
    clear_rail_flags(c);
    get_at<int>(c, 0x23C) = static_cast<int>(script::fetch_float(kRailRegrabDelay));
    set_flag(c, 0x50, 0);
    vector_at(object_of(c), kVelocity).change_heading(kick);
    if (trigger_script)
        trigger_rail_node(c, kKickEvents);
    change_phase(c, kAir);
    maths::Vec4& position = vector_at(object_of(c), kPosition);
    position.y = position.y + 1.0f;
    static_cast<objects::Entity*>(object_of(c))->post_own_event(kPointRail, nullptr);
}

// (0x0050A770, a node on its own, a spin button held) A point rail spin (a Natas spin): stopped on the point.
void spin_on_point_rail(SkaterPhysicsModule* c, const maths::Vec4* point)
{
    get_at<uint8_t>(c, 0x2EC) = 1;
    get_at<uint8_t>(c, 0x1500) = input_of(c)[0x100] != 0;
    if (!clear_to_point(c, point, true))
        return;
    onto_rail(c, point, true);
    static_cast<SkaterPhysicsModule*>(c)->set_surface_kind(get_at<objects::GrindPoint*>(c, kRailNode)->surface_type_);
    if (!get_at<uint8_t>(c, 0x2ED))
        play_land_sound(c);
    maths::Vec4& v = vector_at(object_of(c), kVelocity);
    v.x = 0.0f;
    vector_at(object_of(c), kVelocity).y = 0.0f;
    vector_at(object_of(c), kVelocity).z = 0.0f;
    clear_rail_flags(c);
    static_cast<objects::Entity*>(object_of(c))->post_own_event(kPointRailSpin, nullptr);
    c->trigger_rail_script(kSpinEvents);
    get_at<int>(c, 0x1504) = get_now();
}

// (0x0050A770) A lip trick on the rail at the top of a vert ramp: stopped there, upright along the ramp's top, the
// LipTrick script.
void start_lip_trick(SkaterPhysicsModule* c, const maths::Vec4* point)
{
    vector_at(object_of(c), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    set_flag(c, 0x30, 0);
    set_flag(c, 0x38, 0);
    set_flag(c, 0x40, 0);
    set_flag(c, 0x48, 0);
    get_at<uint8_t>(c, 0x2D0) = 0;
    static_cast<objects::RiderTeeterMoves*>(get_component(c, 0x1524))->halt();
    vector_at(c, 0x258) = vector_at(object_of(c), kPosition); // (where it started)
    change_phase(c, kLip);
    vector_at(object_of(c), kPosition) = *point;
    // Up: the ramp's normal, level; facing straight up; right across.
    const maths::Vec4& ground = vector_at(c, kFloorUpVector);
    maths::Vec4 up = {ground.x, 0.0f, ground.z, ground.w};
    up.make_unit();
    void* object = object_of(c);
    vector_at(object, kUp) = up;
    vector_at(object_of(c), kFacing) = kUpAxis;
    maths::Vec4& right = vector_at(object_of(c), kRight);
    const maths::Vec4& u = vector_at(object_of(c), kUp);
    right.y = 0.0f;
    right.w = 0.0f;
    right.x = -u.z;
    right.z = u.x;
    matrix_at(c, kDisplayMatrix) = matrix_at(object_of(c), kMatrix);
    auto* gap = static_cast<objects::RiderGapTracker*>(
        static_cast<objects::GameEntity*>(object_of(c))->fetch_module(kSkaterGap));
    gap->scan_for_gaps(0, 0x100000);
    c->trigger_rail_script(kLipEvents);
    get_at<uint8_t>(get_component(c, kSkaterComponent), 0xA24) = 0;
    auto* skater = static_cast<objects::Entity*>(object_of(c));
    if (!skater->own_script_) {
        void* memory = script::Script::operator new(0xF4);
        static_cast<objects::Entity*>(object_of(c))->own_script_ =
            memory ? static_cast<script::Script*>(memory)->make_new() : nullptr;
    }
    static_cast<objects::Entity*>(object_of(c))
        ->own_script_->bind_script(kLipTrick, nullptr, static_cast<objects::Entity*>(object_of(c)));
    static_cast<objects::Entity*>(object_of(c))->own_script_->tick_frame();
    get_at<uint8_t>(flags_of(c), 0xE0) = 1; // (without its time)
    set_rail_terrain(c);
}

// (0x0050A770) A grind along the rail from `node` to the next: the way the skater goes along it.
void grind_along_rail(SkaterPhysicsModule* c, const maths::Vec4* point, bool from_object)
{
    // (not original: THAW 0x005E0EAD - the velocity as the grind start began, for the bike's stall)
    bool bike = thaw_bmx::live_now(c);
    maths::Vec4 start_velocity = vector_at(object_of(c), kVelocity);
    set_rail_terrain(c);
    auto* node = get_at<objects::GrindPoint*>(c, kRailNode);
    objects::GrindPoint* next = node->succ_;
    auto* manager = get_at<objects::GrindRailHub*>(c, kRailManager);
    maths::Vec4 from, to;
    const maths::Vec4* a = manager->get_absolute_pos(&from, node);
    const maths::Vec4* b = get_at<objects::GrindRailHub*>(c, kRailManager)->get_absolute_pos(&to, next);
    maths::Vec4 direction = {b->x - a->x, b->y - a->y, b->z - a->z, b->w - a->w};
    direction.make_unit();
    if (!clear_to_point(c, point, false))
        return;
    if (bit_mark(c, 0xA8)) {
        // (From an acid drop: its hold velocity added.)
        debug::printf("M:%s:%i: %s\n", "mponents\\SkaterCorePhysicsComponent.cpp", 9116,
                      "GRINDING FROM ACID DROP"); // "M:%s:%i: %s" ... "GRINDING FROM ACID DROP"
        const maths::Vec4& hold = acid_hold();
        debug::printf("D:%s:%i: acid_hold = %g, %g, %g\n", "mponents\\SkaterCorePhysicsComponent.cpp", 9117,
                      static_cast<double>(hold.x), static_cast<double>(hold.y), static_cast<double>(hold.z));
        maths::Vec4& v = vector_at(object_of(c), kVelocity);
        v.x = acid_hold().x + v.x;
        v.y = acid_hold().y + v.y;
        v.z = acid_hold().z + v.z;
        v.w = acid_hold().w + v.w;
    }
    prepare_velocity(c, true, from_object);

    // Which way: along the velocity; straight across it, a coin toss. At the rail's end that way: nothing.
    const maths::Vec4& v = vector_at(object_of(c), kVelocity);
    double along = (static_cast<double>(direction.z) * v.z + static_cast<double>(direction.y) * v.y) +
                   static_cast<double>(direction.x) * v.x;
    bool forward = along == 0.0 ? maths::rnd(2) != 0 : !(along < 0.0);
    float sign;
    maths::Vec4 end;
    if (forward) {
        sign = 1.0f;
        if (!next->succ_ && *get_at<objects::GrindRailHub*>(c, kRailManager)->get_absolute_pos(&end, next) == *point)
            return;
    } else {
        sign = -1.0f;
        if (!node->last_value_ &&
            *get_at<objects::GrindRailHub*>(c, kRailManager)->get_absolute_pos(&end, node) == *point)
            return;
    }
    c->trigger_rail_script(kGrindEvents);
    onto_rail(c, point, false);
    if (get_since(get_at<uint32_t>(get_component(c, kTriggerState), 0x1C)) != 0 && !get_at<uint8_t>(c, 0x2ED))
        play_land_sound(c);

    // The speed along the rail, the height kept; plus the rail boost. From an object, too slow against the facing:
    // from standing, the way the facing points along the rail.
    maths::Vec4& velocity = vector_at(object_of(c), kVelocity);
    float kept_y = velocity.y;
    velocity.project_onto(direction);
    void* object = object_of(c);
    if (from_object && moving_against_facing(object)) {
        double adjust = static_cast<double>(jump_adjust_speed()) * 1.1f;
        const maths::Vec4& w = vector_at(object, kVelocity);
        double speed =
            (static_cast<double>(w.z) * w.z + static_cast<double>(w.x) * w.x) + static_cast<double>(w.y) * w.y;
        if (adjust * adjust > speed) {
            vector_at(object_of(c), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
            const maths::Vec4& f = vector_at(object_of(c), kFacing);
            sign = maths::get_sign(
                static_cast<float>((static_cast<double>(direction.z) * f.z + static_cast<double>(direction.y) * f.y) +
                                   static_cast<double>(direction.x) * f.x));
        }
    }
    vector_at(object_of(c), kVelocity).y = kept_y;
    maths::Vec4 way = {sign * direction.x, direction.y * sign, direction.z * sign, direction.w * sign};
    float boost = script::fetch_float(kRailSpeedBoost);
    {
        maths::Vec4& u = vector_at(object_of(c), kVelocity);
        float y = way.y * boost, z = way.z * boost, w = way.w * boost;
        u.x = static_cast<float>(static_cast<double>(way.x) * boost + u.x);
        u.y = y + u.y;
        u.z = z + u.z;
        u.w = w + u.w;
    }
    get_at<uint8_t>(c, 0x34C) = 0;
    if (sign < 0.0f)
        direction = {-direction.x, -direction.y, -direction.z, -direction.w};

    // Which side of the rail the jump to it started (+0x338).
    const maths::Vec4& jumped = vector_at(c, 0x338);
    double across = -(static_cast<double>(point->x) - jumped.x) * direction.z +
                    (static_cast<double>(point->z) - jumped.z) * direction.x;
    bool left_side = across < 0.0;

    // A bad ledge: feelers down beside the rail, either side.
    maths::Vec4& start = vector_at(c, kProbeOrigin);
    maths::Vec4& stop = vector_at(c, kProbeTip);
    start.y = tuning_float_lookup(kGroundSnapUp, get_at<void*>(c, kObject));
    start.x = 0.0f, start.z = 0.0f, start.w = 0.0f;
    stop.y = -script::fetch_float(kBadLedgeDropDown);
    stop.x = 0.0f, stop.z = 0.0f, stop.w = 0.0f;
    start.x = point->x + start.x;
    start.y = start.y + point->y;
    start.z = start.z + point->z;
    start.w = start.w + point->w;
    stop.x = point->x + stop.x;
    stop.y = stop.y + point->y;
    stop.z = stop.z + point->z;
    stop.w = stop.w + point->w;
    float side_distance = script::fetch_float(kBadLedgeSide);
    float zero = 0.0f * side_distance;
    maths::Vec4 side = {direction.z * side_distance, zero, -direction.x * side_distance, zero};
    start.x = side.x + start.x, start.y = side.y + start.y, start.z = side.z + start.z, start.w = side.w + start.w;
    stop.x = side.x + stop.x, stop.y = side.y + stop.y, stop.z = side.z + stop.z, stop.w = side.w + stop.w;
    bool left_hit = get_feel(c, 0x10, 0);
    maths::Vec4 twice = {side.x + side.x, side.y + side.y, side.z + side.z, side.w + side.w};
    start.x -= twice.x, start.y -= twice.y, start.z -= twice.z, start.w -= twice.w;
    stop.x -= twice.x, stop.y -= twice.y, stop.z -= twice.z, stop.w -= twice.w;
    bool right_hit = get_feel(c, 0x10, 0);
    get_at<uint8_t>(c, 0x249) = (left_hit && !left_side) || (right_hit && left_side);
    get_at<uint8_t>(c, 0x248) = left_hit || right_hit;
    if (thaw_bmx::live_now(
            c)) // (not original: THAW 0x005E27F5 - on the bike, a bad ledge on that side: the other side)
        left_side = thaw_bmx::grind_side(c, left_side, get_at<uint8_t>(c, 0x249) != 0);

    // Grinding sideways (across the skater's right) or which way round.
    void* o = object_of(c);
    const maths::Vec4& right = vector_at(o, kRight);
    const maths::Vec4& facing = vector_at(o, kFacing);
    float across_right =
        static_cast<float>((static_cast<double>(direction.z) * right.z + static_cast<double>(direction.y) * right.y) +
                           static_cast<double>(direction.x) * right.x);
    float along_facing =
        static_cast<float>((static_cast<double>(direction.z) * facing.z + static_cast<double>(direction.y) * facing.y) +
                           static_cast<double>(direction.x) * facing.x);
    float across_amount = std::fabs(across_right);
    bool sideways = false;
    if (script::fetch_float(kRailTolerance) > across_amount) {
        sideways = true;
        if (along_facing < 0.0f)
            get_at<uint8_t>(c, 0x34C) = 1;
    } else {
        float turned = bit_mark(c, 0x28) ? -across_right : across_right;
        if (turned < 0.0f)
            get_at<uint8_t>(c, 0x34C) = 1;
    }
    void* skater = get_component(c, kSkaterComponent);
    get_at<uint8_t>(skater, 0xA24) = 0;
    // The grind trick: the skater component's (0x00522E80), else the physics' pick by the d-pad (0x005004B0).
    if (!static_cast<objects::MoveExecutor*>(skater)->do_extra_grind_trick(
            left_side, sideways, get_at<uint8_t>(c, 0x34C) != 0, bit_mark(c, 0x28) != 0)) {
        if (bike) // (not original: THAW 0x005E2935 - on the bike R1 held: the stall)
            thaw_bmx::grind_stall(c, start_velocity, direction);
        const uint8_t* input = input_of(c);
        int pad = objects::d_pad_direction(input[0x20], input[0x40], input[0x60], input[0x80]);
        c->start_grind_trick(static_cast<uint32_t>(pad), left_side, sideways, get_at<uint8_t>(c, 0x34C) != 0,
                             bit_mark(c, 0x28) != 0);
    }
    get_at<int>(c, 0x234) = get_now();
}

} // namespace

// 0x0050A770
void SkaterPhysicsModule::start_grind(const maths::Vec4* point, objects::GrindPoint* node,
                                      objects::GrindRailHub* manager, bool no_lip_trick, bool from_object)
{
    const uint8_t* input = input_of(this);
    auto* flip_and_rotate = static_cast<objects::RiderSpinAndFlip*>(
        static_cast<objects::GameEntity*>(object_of(this))->fetch_module(kSkaterFlipAndRotate));
    flip_and_rotate->do_after_anim();
    if (get_at<objects::GrindPoint*>(this, kRailNode) != node) {
        // (A new rail: its node's index to 0x005627B0 of +0x1520's +0x18.)
        uint32_t index =
            static_cast<uint32_t>(reinterpret_cast<uint8_t*>(node) -
                                  reinterpret_cast<uint8_t*>(get_at<objects::GrindPoint*>(manager, 0x40))) /
            sizeof(objects::GrindPoint);
        static_cast<modules::PointTally*>(get_at<void*>(get_component(this, 0x1520), 0x18))
            ->refresh_bot_check(static_cast<int>(index));
    }
    get_at<objects::GrindRailHub*>(this, kRailManager) = manager;
    get_at<objects::GrindPoint*>(this, kRailNode) = node;

    if (!node->last_value_ && !node->succ_) {
        // A point rail: a spin when a spin button is held, long enough after the last one; else a kick.
        if (input[0x100] || input[0xA0]) {
            int elapsed = static_cast<int>(get_since(get_at<uint32_t>(this, 0x1504)));
            if (!(static_cast<double>(script::fetch_float(kNatasDelay)) * 1000.0f > static_cast<float>(elapsed))) {
                spin_on_point_rail(this, point);
                return;
            }
        }
        kick_off_point_rail(this, point, from_object);
        return;
    }

    // A lip trick: going up, upright (the up within the horizontal angle), facing up, on a ramp near vertical, the
    // skater's right near level (the allow angle - its override for nodes flagged 1).
    float allow = static_cast<float>(std::sin(static_cast<double>(script::fetch_float(kLipAllowAngle)) * kDegrees));
    if (node->flag_bits_ & 1)
        allow =
            static_cast<float>(std::sin(static_cast<double>(script::fetch_float(kLipAllowAngleOverride)) * kDegrees));
    float horizontal =
        static_cast<float>(std::sin(static_cast<double>(script::fetch_float(kLipHorizontalAngle)) * kDegrees));
    float ramp_vert =
        static_cast<float>(std::cos(static_cast<double>(script::fetch_float(kLipRampVertAngle)) * kDegrees));
    bool lip = true;
    if (!get_at<uint8_t>(this, 0x375)) {
        void* object = object_of(this);
        lip = vector_at(object, kVelocity).y > 0.0f && std::fabs(vector_at(object, kUp).y) < horizontal &&
              vector_at(object, kFacing).y > 0.0f && std::fabs(vector_at(this, kFloorUpVector).y) < ramp_vert &&
              std::fabs(vector_at(object, kRight).y) < allow;
    }
    // (not original: THAW 0x005E1B31 - no lip trick on the bike)
    if (lip && !no_lip_trick && !get_at<uint8_t>(this, 0x2ED) && !thaw_bmx::live_now(this))
        start_lip_trick(this, point);
    else
        grind_along_rail(this, point, from_object);
}

namespace {

// 0x00500020: whether the rail `node` (of `manager`) may be ground.
bool can_grind(void* component, objects::GrindPoint* node, objects::GrindRailHub* manager)
{
    return static_cast<SkaterPhysicsModule*>(component)->can_grind_rail(node, manager, false);
}

} // namespace

// 0x0050DAE0
void SkaterPhysicsModule::try_grind(bool always)
{
    if (state_of(this) != kAir && !always)
        return;
    if (bit_mark(this, 0x70) || get_at<uint8_t>(this, 0x3A4) || !input_of(this)[0x1A0])
        return;
    if (get_since(get_at<uint32_t>(this, 0x4E0)) < static_cast<uint32_t>(script::fetch_int_value(kDisallowGrind)))
        return;
    void* object = object_of(this);
    maths::Vec4 last = vector_at(object, kOldPosition);
    maths::Vec4 now = vector_at(object, kPosition);
    // Soon after leaving a rail (within +0x23C of +0x234; not in the park editor), only a rail lining up.
    float min_alignment = 1.0f;
    if (get_at<uint32_t>(reinterpret_cast<uint8_t*>(park_editing::g_park_editor), 0x40) != kParkEditorLevel &&
        static_cast<uint32_t>(frame_clock::game_clock_time_since(get_at<int>(this, 0x234))) <
            get_at<uint32_t>(this, 0x23C))
        min_alignment =
            static_cast<float>(std::cos(static_cast<double>(script::fetch_float(kCornerLeaveAngle)) * kDegrees));
    auto* manager = get_at<objects::GrindRailHub*>(reinterpret_cast<uint8_t*>(modules::g_skate_module), 0x24);
    maths::Vec4 point;
    objects::GrindPoint* node;
    if (manager->find_grind_rail(&last, &now, &point, &node, nullptr, min_alignment, 0)) {
        void* o = object_of(this);
        tracking_print(2, &vector_at(o, kOldPosition), &vector_at(o, kPosition));
        stop_riding(this);
        if (can_grind(this, node, manager))
            start_grind(&point, node, manager, false, false);
        return;
    }
    // The rails on moving objects: the move in the object's space.
    for (auto* rails = objects::object_hub_slot()->find_first_module_of_kind(kRailManagerType); rails;
         rails = get_at<objects::ObjectModule*>(rails, 0x14)) {
        auto* object_rails = reinterpret_cast<objects::GrindRailHub*(__thiscall*)(void*)>(
            (*reinterpret_cast<void***>(rails))[13])(rails);
        maths::Mat4 world;
        static_cast<objects::GrindPathLinkModule*>(rails)->update_rail_matrix(&world);
        maths::Mat4 inverse;
        inverse = world;
        inverse.upend();
        last.w = 1.0f;
        now.w = 1.0f;
        maths::Vec4 local_last, local_now;
        inverse.xform(local_last, last);
        inverse.xform(local_now, now);
        if (object_rails->find_grind_rail(&local_last, &local_now, &point, &node, nullptr, min_alignment, 0)) {
            point.w = 1.0f;
            maths::Vec4 in_world;
            point = world.xform(in_world, point);
            if (!can_grind(this, node, object_rails))
                return;
            start_grind(&point, node, object_rails, false, false);
            get_at<walk::MovableContact*>(this, kRide)->start_riding(get_at<objects::GameEntity*>(rails, 0xC));
            get_at<objects::MovingSurfaceModule*>(this, kRide)->update_contact_point();
            vector_at(object_of(this), kVelocity) -= vector_at(get_tracker(this)->entity_.owner_object_, kVelocity);
            return;
        }
    }
}

} // namespace skater
