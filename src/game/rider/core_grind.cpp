// The skater's core physics component: getting onto a rail - a point rail's kick or spin, a lip trick, a grind.

#include <array>
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
#include "game/rider/skater_score.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "game/rider/rider_shared.h"

namespace skater {

using namespace core_fields;

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

const maths::Vec4 kUpAxis = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};


// Facing away from the velocity (the skater pointing against it).
bool moving_against_facing(objects::GameEntity* object)
{
    const maths::Vec4& v = object->vel();
    const maths::Vec4& f = core::at(object->basis_frame().row, 2);
    const double dot =
        (static_cast<double>(v.z) * f.z + static_cast<double>(v.y) * f.y) + static_cast<double>(f.x) * v.x;
    return dot < 0.0;
}

float jump_adjust_speed() { return objects::FootMovement::get_param(kJumpAdjustSpeed); }

} // namespace

// The rail's terrain (the node being ground) told to the flags (0x005196A0) and the sound component.
void SkaterPhysicsModule::set_rail_terrain()
{
    const uint8_t terrain = rail_node_->surface_type();
    state_->set_surface_kind(terrain);
    sound_->set_surface_type(terrain);
}

// The landing sound (0x00518CF0): by speed (a fraction of the max max speed) and flag +0xE1.
void SkaterPhysicsModule::play_land_sound()
{
    objects::GameEntity* object = object_of(this);
    auto speed = static_cast<float>(length_of(object->vel()));
    const float fraction = speed / core::down_cast<objects::Rider>(object)->get_scaled_physics(kSpeedCapStat);
    static_cast<objects::RiderSfxPlayer*>(sound_)->play_land(fraction, state_->surface_type());
}

// The rail node's TriggerScript run for `events` (with what the skater rides on, if anything still there).
void SkaterPhysicsModule::trigger_rail_node(const std::array<int, 3>& events)
{
    const objects::Entity* ride_object = nullptr;
    objects::MovingSurfaceModule* ride = ride_;
    if (auto* tracker = ride->ride().get()) {
        if (tracker->entity().get_pull())
            ride_object = tracker->entity().get_pull();
        else
            static_cast<objects::MovingSurfaceModule*>(ride)->stop_riding();
    }
    static_cast<objects::ActivationZoneModule*>(trigger_)
        ->get_trigger_node(events, rail_script_node_, nullptr, ride_object);
}

// A feeler from the skater to the point: false when it hits something more than 6 from the point. The distance
// squared as the original: from the differences unrounded, z first (`from_vector`: the point less the hit as a
// vector - rounded - x first).
bool SkaterPhysicsModule::clear_to_point(const maths::Vec4* point, bool from_vector)
{
    probe_start_ = object_of(this)->at_point();
    probe_end_ = *point;
    if (!get_feel(this, 0x10, 0))
        return true;
    const maths::Vec4& hit = feeler_.contact_pos();
    double distance = 0.0;
    if (from_vector) {
        const maths::Vec4 d = *point - hit;
        distance = (static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y) + static_cast<double>(d.z) * d.z;
    } else {
        const double dx = static_cast<double>(point->x) - hit.x;
        const double dy = static_cast<double>(point->y) - hit.y;
        const double dz = static_cast<double>(point->z) - hit.z;
        distance = (dz * dz + dy * dy) + dx * dx;
    }
    return !(distance > kNearPoint);
}

// What every way onto a rail does first: the skater at the point, off vert, not wall riding (as it ends: 0x004FE860
// with the up straight and the display matrix reset - `always` for a point rail spin), into the rail state.
void SkaterPhysicsModule::onto_rail(const maths::Vec4* point, bool always)
{
    object_of(this)->at_point() = *point;
    set_flag(this, PoseFlag::kVertAir, 0);
    set_flag(this, PoseFlag::kVertWall, 0);
    transfer_blocked_ = 0;
    if (always || state_of(this) == kWallRide) {
        turn_to_normal(kUpAxis);
        display_matrix_ = object_of(this)->basis_frame();
    }
    set_physics_state(kRail);
}

// Barely moving: the velocity's x and z from the facing (a point rail: within 0.01; a rail: exactly 0). Not from an
// object: a fast skater's velocity turned level (its speed kept).
void SkaterPhysicsModule::prepare_velocity(bool exactly_still, bool from_object)
{
    objects::GameEntity* object = object_of(this);
    maths::Vec4& v = object->vel();
    const bool still = exactly_still ? v.x == 0.0f && v.z == 0.0f : std::fabs(v.x) < 0.01f && std::fabs(v.z) < 0.01f;
    if (still) {
        v.x = core::at(object->basis_frame().row, 2).x;
        object_of(this)->vel().z = core::at(object_of(this)->basis_frame().row, 2).z;
    }
    if (!from_object) {
        maths::Vec4& w = object_of(this)->vel();
        if (static_cast<double>(w.z) * w.z + static_cast<double>(w.x) * w.x > 100.0f)
            w.align_to_plane(kUpAxis);
    }
}

// The flags a new rail clears (the time of getting on kept at +0x234).
void SkaterPhysicsModule::clear_rail_flags()
{
    rail_time_ = get_now();
    set_flag(this, PoseFlag::kAcidDrop, 0);
    set_flag(this, PoseFlag::kFromAcidDrop, 0);
    set_flag(this, PoseFlag::kRecoverInAir, 0);
    set_flag(this, PoseFlag::kNoAcidDrop, 0);
}

// (0x0050A770, a node on its own) Kicked off a point rail: up into the air along the move (or the facing, when
// from an object and too slow against it), at the kick angle, with the point rail boost.
void SkaterPhysicsModule::kick_off_point_rail(const maths::Vec4* point, bool from_object)
{
    if (!clear_to_point(point, false))
        return;
    prepare_velocity(false, from_object);
    maths::Vec4 along = object_of(this)->vel();
    along.y = 0.0f;
    along.make_unit();
    const double angle = static_cast<double>(script::fetch_float(kKickUpwardAngle)) * kDegrees;
    const double cosine = std::cos(angle);
    maths::Vec4 kick;
    kick.w = 1.0f;
    kick.x = static_cast<float>(along.x * cosine);
    kick.y = static_cast<float>(std::sin(angle));
    kick.z = static_cast<float>(along.z * cosine);

    script::Struct* script = level_nodes::fetch_vertex(rail_node_->label(), true);
    script->read_hash_value(kName, &rail_script_node_, 1);
    static_cast<objects::MoveExecutor*>(trick_)->pass_to_trick_objects(rail_script_node_);
    uint32_t trigger_script = 0;
    if (script->read_hash_value(kTriggerScript, &trigger_script, 0))
        trigger_rail_node(kGrindEvents);
    onto_rail(point, false);
    set_rail_terrain();
    if (!on_vehicle_)
        play_land_sound();

    maths::Vec4& v = object_of(this)->vel();
    const float kept_y = v.y;
    v.project_onto(along);
    if (from_object && moving_against_facing(object_of(this))) {
        auto adjust = static_cast<float>(jump_adjust_speed() * 1.1f);
        const maths::Vec4& w = object_of(this)->vel();
        const double speed =
            (static_cast<double>(w.x) * w.x + static_cast<double>(w.y) * w.y) + static_cast<double>(w.z) * w.z;
        if (static_cast<double>(adjust) * adjust > speed) {
            object_of(this)->vel() = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
            along = core::at(object_of(this)->basis_frame().row, 2);
            along.y = 0.0f;
            along.make_unit();
        }
    }
    object_of(this)->vel().y = kept_y;
    const float boost = script::fetch_float(kPointRailSpeedBoost);
    maths::Vec4& u = object_of(this)->vel();
    const float y = along.y * boost;
    const float z = along.z * boost;
    const float w = along.w * boost;
    u.x = static_cast<float>(static_cast<double>(along.x) * boost + u.x);
    u.y += y;
    u.z += z;
    u.w += w;
    clear_rail_flags();
    rail_wait_ = static_cast<int>(script::fetch_float(kRailRegrabDelay));
    set_flag(this, PoseFlag::kLipToAir, 0);
    object_of(this)->vel().change_heading(kick);
    if (trigger_script)
        trigger_rail_node(kKickEvents);
    set_physics_state(kAir);
    maths::Vec4& position = object_of(this)->at_point();
    position.y = position.y + 1.0f;
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kPointRail, nullptr);
}

// (0x0050A770, a node on its own, a spin button held) A point rail spin (a Natas spin): stopped on the point.
void SkaterPhysicsModule::spin_on_point_rail(const maths::Vec4* point)
{
    point_rail_spin_ = 1;
    natas_counter_clockwise_ = input_->pad_primary().shoulder_right_a().held_down() != 0;
    if (!clear_to_point(point, true))
        return;
    onto_rail(point, true);
    set_surface_kind(rail_node_->surface_type());
    if (!on_vehicle_)
        play_land_sound();
    maths::Vec4& v = object_of(this)->vel();
    v.x = 0.0f;
    object_of(this)->vel().y = 0.0f;
    object_of(this)->vel().z = 0.0f;
    clear_rail_flags();
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kPointRailSpin, nullptr);
    trigger_rail_script(kSpinEvents);
    natas_spin_time_ = get_now();
}

// (0x0050A770) A lip trick on the rail at the top of a vert ramp: stopped there, upright along the ramp's top, the
// LipTrick script.
void SkaterPhysicsModule::start_lip_trick(const maths::Vec4* point)
{
    object_of(this)->vel() = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    set_flag(this, PoseFlag::kVertAir, 0);
    set_flag(this, PoseFlag::kVertWall, 0);
    set_flag(this, PoseFlag::kOnVert, 0);
    set_flag(this, PoseFlag::kLeftVert, 0);
    transfer_blocked_ = 0;
    static_cast<objects::RiderTeeterMoves*>(balance_)->halt();
    lip_pos_ = object_of(this)->at_point(); // (where it started)
    set_physics_state(kLip);
    object_of(this)->at_point() = *point;
    // Up: the ramp's normal, level; facing straight up; right across.
    const maths::Vec4& ground = floor_normal_;
    maths::Vec4 up = {.x = ground.x, .y = 0.0f, .z = ground.z, .w = ground.w};
    up.make_unit();
    objects::GameEntity* object = object_of(this);
    core::at(object->basis_frame().row, 1) = up;
    core::at(object_of(this)->basis_frame().row, 2) = kUpAxis;
    maths::Vec4& right = core::at(object_of(this)->basis_frame().row, 0);
    const maths::Vec4& u = core::at(object_of(this)->basis_frame().row, 1);
    right.y = 0.0f;
    right.w = 0.0f;
    right.x = -u.z;
    right.z = u.x;
    display_matrix_ = object_of(this)->basis_frame();
    auto* gap = core::down_cast<objects::RiderGapTracker>(
        static_cast<objects::GameEntity*>(object_of(this))->fetch_module(kSkaterGap));
    gap->scan_for_gaps(0, 0x100000);
    trigger_rail_script(kLipEvents);
    trick_->set_starting_special(false);
    auto* skater = static_cast<objects::Entity*>(object_of(this));
    if (!skater->own_script()) {
        static_cast<objects::Entity*>(object_of(this))->set_own_script(script::Script::create());
    }
    static_cast<objects::Entity*>(object_of(this))
        ->own_script()->bind_script(kLipTrick, nullptr, static_cast<objects::Entity*>(object_of(this)));
    static_cast<objects::Entity*>(object_of(this))->own_script()->tick_frame();
    state_->set_in_trick_move(1); // (without its time)
    set_rail_terrain();
}

// (0x0050A770) A grind along the rail from `node` to the next: the way the skater goes along it.
void SkaterPhysicsModule::grind_along_rail(const maths::Vec4* point, bool from_object)
{
    // (not original: THAW 0x005E0EAD - the velocity as the grind start began, for the bike's stall)
    const bool bike = thaw_bmx::live_now(this);
    const maths::Vec4 start_velocity = object_of(this)->vel();
    set_rail_terrain();
    auto* node = rail_node_;
    const objects::GrindPoint* next = node->succ();
    auto* manager = rail_manager_;
    maths::Vec4 from, to;
    const maths::Vec4* a = manager->get_absolute_pos(&from, node);
    const maths::Vec4* b = rail_manager_->get_absolute_pos(&to, next);
    maths::Vec4 direction = {.x = b->x - a->x, .y = b->y - a->y, .z = b->z - a->z, .w = b->w - a->w};
    direction.make_unit();
    if (!clear_to_point(point, false))
        return;
    if (bit_mark(this, PoseFlag::kFromAcidDrop)) {
        // (From an acid drop: its hold velocity added.)
        debug::printf("M:%s:%i: %s\n", "mponents\\SkaterCorePhysicsComponent.cpp", 9116,
                      "GRINDING FROM ACID DROP"); // "M:%s:%i: %s" ... "GRINDING FROM ACID DROP"
        const maths::Vec4& hold = rider_shared().acid_hold();
        debug::printf("D:%s:%i: acid_hold = %g, %g, %g\n", "mponents\\SkaterCorePhysicsComponent.cpp", 9117,
                      static_cast<double>(hold.x), static_cast<double>(hold.y), static_cast<double>(hold.z));
        maths::Vec4& v = object_of(this)->vel();
        v.x = rider_shared().acid_hold().x + v.x;
        v.y = rider_shared().acid_hold().y + v.y;
        v.z = rider_shared().acid_hold().z + v.z;
        v.w = rider_shared().acid_hold().w + v.w;
    }
    prepare_velocity(true, from_object);

    // Which way: along the velocity; straight across it, a coin toss. At the rail's end that way: nothing.
    const maths::Vec4& v = object_of(this)->vel();
    const double along = (static_cast<double>(direction.z) * v.z + static_cast<double>(direction.y) * v.y) +
                         static_cast<double>(direction.x) * v.x;
    const bool forward = along == 0.0 ? maths::rnd(2) != 0 : !(along < 0.0);
    float sign = 0.0f;
    maths::Vec4 end;
    if (forward) {
        sign = 1.0f;
        if (!next->succ() && *rail_manager_->get_absolute_pos(&end, next) == *point)
            return;
    } else {
        sign = -1.0f;
        if (!node->last_value() && *rail_manager_->get_absolute_pos(&end, node) == *point)
            return;
    }
    trigger_rail_script(kGrindEvents);
    onto_rail(point, false);
    if (get_since(static_cast<uint32_t>(physics_control_->switch_time())) != 0 && !on_vehicle_)
        play_land_sound();

    // The speed along the rail, the height kept; plus the rail boost. From an object, too slow against the facing:
    // from standing, the way the facing points along the rail.
    maths::Vec4& velocity = object_of(this)->vel();
    const float kept_y = velocity.y;
    velocity.project_onto(direction);
    objects::GameEntity* object = object_of(this);
    if (from_object && moving_against_facing(object)) {
        const double adjust = static_cast<double>(jump_adjust_speed()) * 1.1f;
        const maths::Vec4& w = object->vel();
        const double speed =
            (static_cast<double>(w.z) * w.z + static_cast<double>(w.x) * w.x) + static_cast<double>(w.y) * w.y;
        if (adjust * adjust > speed) {
            object_of(this)->vel() = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
            const maths::Vec4& f = core::at(object_of(this)->basis_frame().row, 2);
            sign = maths::get_sign(
                static_cast<float>((static_cast<double>(direction.z) * f.z + static_cast<double>(direction.y) * f.y) +
                                   static_cast<double>(direction.x) * f.x));
        }
    }
    object_of(this)->vel().y = kept_y;
    const maths::Vec4 way = {.x = sign * direction.x, .y = direction.y * sign, .z = direction.z * sign, .w = direction.w * sign};
    const float boost = script::fetch_float(kRailSpeedBoost);
    {
        maths::Vec4& u = object_of(this)->vel();
        const float y = way.y * boost;
        const float z = way.z * boost;
        const float w = way.w * boost;
        u.x = static_cast<float>(static_cast<double>(way.x) * boost + u.x);
        u.y = y + u.y;
        u.z = z + u.z;
        u.w = w + u.w;
    }
    rail_backwards_ = 0;
    if (sign < 0.0f)
        direction = {.x = -direction.x, .y = -direction.y, .z = -direction.z, .w = -direction.w};

    // Which side of the rail the jump to it started (+0x338).
    const maths::Vec4& jumped = last_ground_pos_;
    const double across = -(static_cast<double>(point->x) - jumped.x) * direction.z +
                          (static_cast<double>(point->z) - jumped.z) * direction.x;
    bool left_side = across < 0.0;

    // A bad ledge: feelers down beside the rail, either side.
    maths::Vec4& start = probe_start_;
    maths::Vec4& stop = probe_end_;
    start.y = tuning_float_lookup(kGroundSnapUp, entity());
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
    const float side_distance = script::fetch_float(kBadLedgeSide);
    const float zero = 0.0f * side_distance;
    const maths::Vec4 side = {.x = direction.z * side_distance, .y = zero, .z = -direction.x * side_distance, .w = zero};
    start.x = side.x + start.x, start.y = side.y + start.y, start.z = side.z + start.z, start.w = side.w + start.w;
    stop.x = side.x + stop.x, stop.y = side.y + stop.y, stop.z = side.z + stop.z, stop.w = side.w + stop.w;
    const bool left_hit = get_feel(this, 0x10, 0);
    const maths::Vec4 twice = {.x = side.x + side.x, .y = side.y + side.y, .z = side.z + side.z, .w = side.w + side.w};
    start.x -= twice.x, start.y -= twice.y, start.z -= twice.z, start.w -= twice.w;
    stop.x -= twice.x, stop.y -= twice.y, stop.z -= twice.z, stop.w -= twice.w;
    const bool right_hit = get_feel(this, 0x10, 0);
    bad_ledge_ = (left_hit && !left_side) || (right_hit && left_side);
    on_ledge_ = left_hit || right_hit;
    if (thaw_bmx::live_now(
            this)) // (not original: THAW 0x005E27F5 - on the bike, a bad ledge on that side: the other side)
        left_side = thaw_bmx::grind_side(this, left_side, bad_ledge_ != 0);

    // Grinding sideways (across the skater's right) or which way round.
    const objects::GameEntity* o = object_of(this);
    const maths::Vec4& right = core::at(o->basis_frame().row, 0);
    const maths::Vec4& facing = core::at(o->basis_frame().row, 2);
    auto across_right =
        static_cast<float>((static_cast<double>(direction.z) * right.z + static_cast<double>(direction.y) * right.y) +
                           static_cast<double>(direction.x) * right.x);
    auto along_facing =
        static_cast<float>((static_cast<double>(direction.z) * facing.z + static_cast<double>(direction.y) * facing.y) +
                           static_cast<double>(direction.x) * facing.x);
    const float across_amount = std::fabs(across_right);
    bool sideways = false;
    if (script::fetch_float(kRailTolerance) > across_amount) {
        sideways = true;
        if (along_facing < 0.0f)
            rail_backwards_ = 1;
    } else {
        const float turned = bit_mark(this, PoseFlag::kFlipped) ? -across_right : across_right;
        if (turned < 0.0f)
            rail_backwards_ = 1;
    }
    objects::MoveExecutor* skater = trick_;
    skater->set_starting_special(false);
    // The grind trick: the skater component's (0x00522E80), else the physics' pick by the d-pad (0x005004B0).
    if (!static_cast<objects::MoveExecutor*>(skater)->do_extra_grind_trick(left_side, sideways, rail_backwards_ != 0,
                                                                           bit_mark(this, PoseFlag::kFlipped) != 0)) {
        if (bike) // (not original: THAW 0x005E2935 - on the bike R1 held: the stall)
            thaw_bmx::grind_stall(this, start_velocity, direction);
        const objects::PadInputModule* input = input_;
        const int pad = objects::d_pad_direction(
            input->pad_primary().up_dir().held_down(), input->pad_primary().btn_down().held_down(),
            input->pad_primary().left_side().held_down(), input->pad_primary().right_side().held_down());
        start_grind_trick(static_cast<uint32_t>(pad), left_side, sideways, rail_backwards_ != 0,
                             bit_mark(this, PoseFlag::kFlipped) != 0);
    }
    rail_time_ = get_now();
}

// 0x0050A770
void SkaterPhysicsModule::start_grind(const maths::Vec4* point, objects::GrindPoint* node,
                                      objects::GrindRailHub* manager, bool no_lip_trick, bool from_object)
{
    const objects::PadInputModule* input = input_;
    auto* flip_and_rotate = core::down_cast<objects::RiderSpinAndFlip>(
        static_cast<objects::GameEntity*>(object_of(this))->fetch_module(kSkaterFlipAndRotate));
    flip_and_rotate->do_after_anim();
    if (rail_node_ != node) {
        // (A new rail: its node's index to 0x005627B0 of +0x1520's +0x18.)
        const auto index =
            static_cast<uint32_t>(node - manager->nodes());
        static_cast<modules::PointTally*>(score_->points_held())->refresh_bot_check(static_cast<int>(index));
    }
    rail_manager_ = manager;
    rail_node_ = node;

    if (!node->last_value() && !node->succ()) {
        // A point rail: a spin when a spin button is held, long enough after the last one; else a kick.
        if (input->pad_primary().shoulder_right_a().held_down() || input->pad_primary().shoulder_left_a().held_down()) {
            const int elapsed = static_cast<int>(get_since(static_cast<uint32_t>(natas_spin_time_)));
            if (!(static_cast<double>(script::fetch_float(kNatasDelay)) * 1000.0f > static_cast<float>(elapsed))) {
                spin_on_point_rail(point);
                return;
            }
        }
        kick_off_point_rail(point, from_object);
        return;
    }

    // A lip trick: going up, upright (the up within the horizontal angle), facing up, on a ramp near vertical, the
    // skater's right near level (the allow angle - its override for nodes flagged 1).
    auto allow = static_cast<float>(std::sin(static_cast<double>(script::fetch_float(kLipAllowAngle)) * kDegrees));
    if (node->flag_bits() & 1)
        allow =
            static_cast<float>(std::sin(static_cast<double>(script::fetch_float(kLipAllowAngleOverride)) * kDegrees));
    auto horizontal =
        static_cast<float>(std::sin(static_cast<double>(script::fetch_float(kLipHorizontalAngle)) * kDegrees));
    auto ramp_vert =
        static_cast<float>(std::cos(static_cast<double>(script::fetch_float(kLipRampVertAngle)) * kDegrees));
    bool lip = true;
    if (!allow_lip_no_grind_) {
        const objects::GameEntity* object = object_of(this);
        lip = object->vel().y > 0.0f && std::fabs(core::at(object->basis_frame().row, 1).y) < horizontal &&
              core::at(object->basis_frame().row, 2).y > 0.0f && std::fabs(floor_normal_.y) < ramp_vert &&
              std::fabs(core::at(object->basis_frame().row, 0).y) < allow;
    }
    // (not original: THAW 0x005E1B31 - no lip trick on the bike)
    if (lip && !no_lip_trick && !on_vehicle_ && !thaw_bmx::live_now(this))
        start_lip_trick(point);
    else
        grind_along_rail(point, from_object);
}

// 0x00500020: whether the rail `node` (of `manager`) may be ground.
bool SkaterPhysicsModule::can_grind(objects::GrindPoint* node, objects::GrindRailHub* manager)
{
    return can_grind_rail(node, manager, false);
}

// 0x0050DAE0
void SkaterPhysicsModule::try_grind(bool always)
{
    if (state_of(this) != kAir && !always)
        return;
    if (bit_mark(this, PoseFlag::kBailing) || no_rail_tricks_ || !input_->pad_primary().button_wedge().held_down())
        return;
    if (get_since(wallplant_time_) < static_cast<uint32_t>(script::fetch_int_value(kDisallowGrind)))
        return;
    const objects::GameEntity* object = object_of(this);
    maths::Vec4 last = object->old_pos();
    maths::Vec4 now = object->at_point();
    // Soon after leaving a rail (within +0x23C of +0x234; not in the park editor), only a rail lining up.
    float min_alignment = 1.0f;
    if (park_editing::ParkBuilderMode::sole()->editor_state() != kParkEditorLevel &&
        static_cast<uint32_t>(frame_clock::game_clock_time_since(rail_time_)) < static_cast<uint32_t>(rail_wait_))
        min_alignment =
            static_cast<float>(std::cos(static_cast<double>(script::fetch_float(kCornerLeaveAngle)) * kDegrees));
    auto* manager = modules::Skate::sole()->rail_manager().get();
    maths::Vec4 point;
    objects::GrindPoint* node = nullptr;
    if (manager->find_grind_rail(&last, &now, &point, &node, nullptr, {.min_alignment_ = min_alignment, .side = 0})) {
        const objects::GameEntity* o = object_of(this);
        tracking_print(2, &o->old_pos(), &o->at_point());
        stop_riding(this);
        if (can_grind(node, manager))
            start_grind(&point, node, manager, false, false);
        return;
    }
    // The rails on moving objects: the move in the object's space.
    for (auto* rails = objects::object_hub_slot()->find_first_module_of_kind(kRailManagerType); rails;
         rails = rails->next_of_type()) {
        auto* object_rails = core::down_cast<objects::GrindPathLinkModule>(rails)->get_rail_groups();
        maths::Mat4 world;
        core::down_cast<objects::GrindPathLinkModule>(rails)->update_rail_matrix(&world);
        maths::Mat4 inverse;
        inverse = world;
        inverse.upend();
        last.w = 1.0f;
        now.w = 1.0f;
        maths::Vec4 local_last, local_now;
        inverse.xform(local_last, last);
        inverse.xform(local_now, now);
        if (object_rails->find_grind_rail(&local_last, &local_now, &point, &node, nullptr, {.min_alignment_ = min_alignment, .side = 0})) {
            point.w = 1.0f;
            maths::Vec4 in_world;
            point = world.xform(in_world, point);
            if (!can_grind(node, object_rails))
                return;
            start_grind(&point, node, object_rails, false, false);
            ride_->ride_on(rails->entity());
            ride_->update_contact_point();
            entity()->vel() -= get_tracker(this)->entity().get_pull()->vel();
            return;
        }
    }
}

} // namespace skater
