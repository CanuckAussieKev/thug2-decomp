// The skater against walls and ceilings: the vert wall plant, leaning to push off a wall, keeping the head out of
// ceilings, the feeler ahead and bouncing off what it hits.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/objects/trigger_component.h"
#include "engine/script/cscript.h"
#include "game/rider/collision_events.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/rider.h"
#include "game/rider/skater_sound.h"
#include "game/rider/tagging.h"
#include "game/rider/trick_component.h"
#include "engine/objects/trigger_events.h"

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kTrickKeyName = 0x7323E97C;           // x (the trick button's name)
constexpr uint32_t kWallRideWindow = 0x5D241B00;         // Wall_Ride_Triangle_Window (seconds)
constexpr uint32_t kGroundSnapUp = 0xE4D79235;           // Physics_Ground_Snap_Up
constexpr uint32_t kWallplantDistance = 0xDC2C15AD;      // Physics_vert_wallplant_distance
constexpr uint32_t kWallplantVelocityLimit = 0x273C9920; // Physics_vert_wallplant_vertical_velocity_limit
constexpr uint32_t kWallplantBoost = 0x751DBCB4;         // Physics_vert_wallplant_boost
constexpr uint32_t kVertWallPlant = 0x3722E9BF;          // Vert_WallPlant (an event)
constexpr uint32_t kMinLeanPushSpeed = 0xBC33D268;       // Skate_min_wall_lean_push_speed
constexpr uint32_t kLeanPushTime = 0x435F0653;           // Skate_wall_lean_push_time
constexpr uint32_t kLeanPushLength = 0x4B8BAB12;         // Skate_wall_lean_push_length
constexpr uint32_t kLeanPushHeight = 0xBFBBD0AF;         // Skate_wall_lean_push_height
constexpr uint32_t kEyeLevelStat = 0x542CF0C7;           // Skater_default_head_height
constexpr uint32_t kIgnoreCeilings = 0x2757ED2C;         // Physics_Ignore_Ceilings_After_Wallplant_Duration (ms)
constexpr uint32_t kForwardHeight = 0xD4205C9B;          // Skater_First_Forward_Collision_Height
constexpr uint32_t kForwardLength = 0x20102726;          // Skater_First_Forward_Collision_Length
constexpr uint32_t kDontSlowAngle = 0x1483FD01;          // Wall_Bounce_Dont_Slow_Angle (degrees)
constexpr uint32_t kDontFlailSpeed = 0xBE0A58A0;         // Wall_Bounce_Dont_Flail_Speed
constexpr uint32_t kFlailLeft = 0xB4101D70;              // FlailLeft (an event)
constexpr uint32_t kFlailRight = 0x756A7535;             // FlailRight (an event)
constexpr uint32_t kMaxMaxSpeedStat = 0xCC5F87AA;        // Skater_Max_Max_Speed_Stat
constexpr uint32_t kVehicleLandSound = 0xD478C96D;       // vehicle_play_land_sound (a script)

constexpr uint32_t kDriving = 0x2ED;
constexpr uint32_t kSurface = 0xFA; // (the feeler's hit: a byte of its face)
constexpr uint32_t kHitObjectChecksum = 0x100;
constexpr uint32_t kWallplanted = 0x4E8; // (a vert wall plant done: not again until reset)
constexpr uint32_t kWallplantTime = 0x4E0;
constexpr uint32_t kWallDirection = 0x38C;  // (the wall last ridden or planted on, it seems)
constexpr uint32_t kTakeOffHeight = 0x11C;  // (in the flags block)
constexpr uint32_t kTrickRotation = 0x13A4; // (the skater component's rotation total)

constexpr float kToMilliseconds = 1000.0f;         // 0x006453C0
constexpr float kFeelerDepth = -200.0f;            // (0xC3480000)
constexpr float kMinWallplantHeight = 60.0f;       // 0x00645EA0
constexpr float kMaxWallplantTilt = 0.1f;          // 0x00645D74
constexpr float kMilliseconds = 0.001f;            // 0x006461D8
constexpr float kUnsignedOffset = 4294967296.0f;   // 0x006453B4
constexpr float kCeilingSteepest = -0.1f;          // 0x0064BD44
constexpr float kCeilingNudge = 0.001f;            // (0x3A83126F)
constexpr float kNoHeadHeight = 6.0f;              // 0x00646284
constexpr float kWallOffset = 18.0f;               // 0x0064AAAC
constexpr float kWallFlat = 0.01f;                 // 0x0064544C
constexpr float kWallPushOut = 0.1f;               // 0x00645D74
constexpr float kBounceBack = 6.0f;                // 0x00646284
constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kHalfPi = 1.57079637f;             // 0x00645484
constexpr float kPi = 3.14159274f;                 // 0x0064BB7C

objects::GameEntity* skater_object(void* component) { return static_cast<objects::GameEntity*>(object_of(component)); }

// 0x00434BF0 (GameObjects::ActivationZoneModule::probe_touches_zone): the triggers of what the feeler hit, for `kind`.
void check_triggers(void* component, const int* kind)
{
    static_cast<objects::ActivationZoneModule*>(get_at<void*>(component, kActivationZoneSlot))
        ->probe_touches_zone(kind, &get_at<render::ProbeRay>(component, kFeeler));
}

// Whether a trigger's script took over (+0x1530's +0x26).
bool trigger_took_over(void* component)
{
    return get_at<uint8_t>(get_at<void*>(component, kStateModuleSlot), 0x26) != 0;
}

// The velocity turned by `angle` about the y axis, as the original's inline x87 code.
void turn_velocity(maths::Vec4& velocity, double angle)
{
    double s = x87_sin(angle), c = x87_cos(angle);
    double x = velocity.x, z = velocity.z;
    velocity.x = static_cast<float>(c * x + s * z);
    velocity.z = static_cast<float>(c * z + -(x * s));
}

} // namespace

// 0x00503710
bool SkaterPhysicsModule::vert_wallplant()
{
    // (not original: THAW 0x005D6960 - never a vert wallplant on the bike)
    if (get_at<uint8_t>(this, kDriving) || thaw_bmx::live_now(this))
        return false;
    // The trick button pressed (or pressed within Wall_Ride_Triangle_Window).
    void* skater = get_at<void*>(this, kSkaterComponent);
    if (!static_cast<objects::MoveExecutor*>(skater)->is_key_held(kTrickKeyName)) {
        uint32_t window = ftol(static_cast<double>(script::fetch_float(kWallRideWindow)) * kToMilliseconds);
        if (!static_cast<objects::MoveExecutor*>(get_at<void*>(this, kSkaterComponent))
                 ->find_press(kTrickKeyName, window, 0xFFFFFFFFu))
            return false;
    }
    uint8_t* flags = flags_of(this);
    if (!flags[0x30] || !flags[0xD8])
        return false;
    if (!(get_at<float>(get_at<void*>(this, kSkaterComponent), kTrickRotation) == 0.0f) ||
        get_at<uint8_t>(this, kWallplanted))
        return false;
    // The wall within reach below the skater's up axis.
    void* object = object_of(this);
    float snap = tuning_float_lookup(kGroundSnapUp, object);
    vector_at(this, kProbeOrigin) = vector_at(object, kUp) * snap;
    vector_at(this, kProbeTip) = vector_at(object_of(this), kUp) * kFeelerDepth;
    vector_at(this, kProbeOrigin) += vector_at(object_of(this), kPosition);
    vector_at(this, kProbeTip) += vector_at(object_of(this), kPosition);
    if (!probe_touches_zone(0x10, 0))
        return false;
    float distance =
        static_cast<float>(length_unrounded(vector_at(object_of(this), kPosition) - vector_at(this, kHitPoint)));
    if (static_cast<double>(script::fetch_int_value(kWallplantDistance)) < distance)
        return false;
    object = object_of(this);
    if (static_cast<double>(script::fetch_int_value(kWallplantVelocityLimit)) > vector_at(object, kVelocity).y)
        return false;
    if (get_at<float>(flags_of(this), kTakeOffHeight) < kMinWallplantHeight)
        return false;
    if (!(get_at<uint16_t>(this, kHitFlags) & 8) && std::fabs(vector_at(this, kHitNormal).y) > kMaxWallplantTilt)
        return false;
    if (!static_cast<objects::TaggingControlComponent*>(get_at<void*>(this, 0x1538))->slap_sticker(true, false, false))
        return false;
    check_triggers(this, kJumpTriggerEvents);
    if (trigger_took_over(this))
        return true;
    object = object_of(this);
    if (vector_at(object, kVelocity).y >= 0.0f)
        vector_at(object, kVelocity).y = static_cast<float>(script::fetch_int_value(kWallplantBoost) +
                                                            static_cast<double>(vector_at(object, kVelocity).y));
    skater_object(this)->post_own_event(kVertWallPlant, nullptr);
    static_cast<objects::MoveExecutor*>(get_at<void*>(this, kSkaterComponent))
        ->pass_to_trick_objects(get_at<uint32_t>(this, kHitObjectChecksum));
    set_timed_flag(this, 0xC0, 0);
    set_timed_flag(this, 0xB0, 0);
    get_at<uint8_t>(this, kWallplanted) = 1;
    return true;
}

// 0x00503B30
void SkaterPhysicsModule::lean_push_off_wall()
{
    float speed = static_cast<float>(length_unrounded(vector_at(object_of(this), kVelocity)));
    if (script::fetch_float(kMinLeanPushSpeed) < speed)
        return;
    // Held against a wall to the left or right: a feeler that far out, Skate_wall_lean_push_length after
    // Skate_wall_lean_push_time.
    uint8_t* pad = input_of(this) + 0x20;
    int held;
    if (pad[0x40])
        held = time_since_press(pad + 0x40);
    else if (pad[0x60])
        held = time_since_press(pad + 0x60);
    else
        return;
    double held_time = held;
    if (held < 0)
        held_time += kUnsignedOffset; // (unsigned)
    float seconds = static_cast<float>(held_time * kMilliseconds);
    float push_time = script::fetch_float(kLeanPushTime);
    if (seconds > push_time)
        seconds = push_time;
    if (pad[0x60])
        seconds = -seconds;
    double length = script::fetch_float(kLeanPushLength);
    float reach = static_cast<float>(length * (static_cast<double>(seconds) / push_time));
    double height = script::fetch_float(kLeanPushHeight);
    void* object = object_of(this);
    const maths::Vec4& up = vector_at(object, kUp);
    const maths::Vec4& position = vector_at(object_of(this), kPosition);
    maths::Vec4& start = vector_at(this, kProbeOrigin);
    maths::Vec4& end = vector_at(this, kProbeTip);
    float hx = static_cast<float>(height * up.x), hy = static_cast<float>(height * up.y),
          hz = static_cast<float>(height * up.z);
    double hw = height * up.w;
    start.z = hz + position.z;
    start.w = static_cast<float>(hw + position.w);
    start.x = hx + position.x;
    start.y = hy + position.y;
    const maths::Vec4& side = vector_at(object_of(this), kMatrix);
    float ry = static_cast<float>(static_cast<double>(reach) * side.y),
          rz = static_cast<float>(static_cast<double>(reach) * side.z),
          rw = static_cast<float>(static_cast<double>(reach) * side.w);
    end.z = rz + start.z;
    end.w = rw + start.w;
    end.x = static_cast<float>(static_cast<double>(reach) * side.x + start.x);
    end.y = ry + start.y;
    if (!probe_touches_zone(0x10, 0))
        return;
    // Moved along the wall's normal to where it is (one up from the skater's position), unless that's blocked.
    const maths::Vec4& point = vector_at(this, kHitPoint);
    maths::Vec4 normal = vector_at(this, kHitNormal);
    maths::Vec4 along = {point.x - end.x, point.y - end.y, point.z - end.z, point.w - end.w};
    along.project_onto(normal);
    object = object_of(this);
    const maths::Vec4& up2 = vector_at(object, kUp);
    const maths::Vec4& position2 = vector_at(object, kPosition);
    start = {up2.x + position2.x, up2.y + position2.y, up2.z + position2.z, up2.w + position2.w};
    end = {along.x + start.x, along.y + start.y, along.z + start.z, along.w + start.w};
    if (probe_touches_zone(0x10, 0))
        return;
    object = object_of(this);
    maths::Vec4& p = vector_at(object, kPosition);
    const maths::Vec4& u = vector_at(object, kUp);
    p = {end.x - u.x, end.y - u.y, end.z - u.z, end.w - u.w};
}

// 0x00504EE0
bool SkaterPhysicsModule::keep_off_ceiling()
{
    void* object = object_of(this);
    if (vector_at(object, kUp).y < 0.0f)
        return false;
    // A feeler up the skater's height (just 6 for a while after a wall plant).
    float head_height = tuning_float_lookup(kEyeLevelStat, object);
    uint32_t elapsed = get_since(get_at<uint32_t>(this, kWallplantTime));
    double reach =
        elapsed > static_cast<uint32_t>(script::fetch_int_value(kIgnoreCeilings)) ? head_height : kNoHeadHeight;
    object = object_of(this);
    const maths::Vec4& up = vector_at(object, kUp);
    double x = reach * up.x, y = reach * up.y;
    float z = static_cast<float>(reach * up.z), w = static_cast<float>(reach * up.w);
    maths::Vec4& start = vector_at(this, kProbeOrigin);
    maths::Vec4& end = vector_at(this, kProbeTip);
    start = vector_at(object, kPosition);
    end = vector_at(object_of(this), kPosition);
    end.x = static_cast<float>(x + end.x);
    end.y = static_cast<float>(y + end.y);
    end.z = z + end.z;
    end.w = w + end.w;
    if (!probe_touches_zone(0x10, 0))
        return false;
    maths::Vec4 normal = vector_at(this, kHitNormal);
    if (normal.y > kCeilingSteepest)
        return false;
    // A ceiling: the skater slid along it to below it, its velocity into its plane.
    const maths::Vec4& point = vector_at(this, kHitPoint);
    maths::Vec4 along = {point.x - end.x, point.y - end.y, point.z - end.z, point.w - end.w};
    along.project_onto(normal);
    start = vector_at(object_of(this), kPosition);
    const maths::Vec4& position = vector_at(object_of(this), kPosition);
    end = {along.x + position.x, along.y + position.y, along.z + position.z, along.w + position.w};
    if (probe_touches_zone(0x10, 0)) // (nudged along the new hit's normal)
        vector_at(object_of(this), kPosition) =
            vector_at(this, kHitPoint) + kCeilingNudge * vector_at(this, kHitNormal);
    else
        vector_at(object_of(this), kPosition) = end;
    const maths::Vec4 entry_velocity = vector_at(object_of(this), kVelocity); // (not original: for note_path)
    vector_at(object_of(this), kVelocity).flatten_onto_plane(normal);
    collision_events::note_path(this, collision_events::RouteWay::kCeiling, entry_velocity); // (not original)
    return true;
}

// 0x005051B0
bool SkaterPhysicsModule::keep_off_ceiling_at_wall()
{
    // A feeler up the skater's height from 18 out along +0x38C.
    void* object = object_of(this);
    const maths::Vec4& wall = vector_at(this, kWallDirection);
    double x = static_cast<double>(wall.x) * kWallOffset;
    float y = wall.y * kWallOffset, z = wall.z * kWallOffset, w = wall.w * kWallOffset;
    const maths::Vec4& position = vector_at(object, kPosition);
    maths::Vec4& start = vector_at(this, kProbeOrigin);
    maths::Vec4& end = vector_at(this, kProbeTip);
    start.z = z + position.z;
    start.w = w + position.w;
    start.x = static_cast<float>(x + position.x);
    start.y = y + position.y;
    end = start;
    end.y = static_cast<float>(static_cast<double>(tuning_float_lookup(kEyeLevelStat, object)) + end.y);
    if (!probe_touches_zone(0x10, 0))
        return false;
    maths::Vec4 normal = vector_at(this, kHitNormal);
    if (normal.y > kCeilingSteepest)
        return false;
    const maths::Vec4& point = vector_at(this, kHitPoint);
    maths::Vec4 along = {point.x - end.x, point.y - end.y, point.z - end.z, point.w - end.w};
    along.project_onto(normal);
    start = vector_at(object_of(this), kPosition);
    const maths::Vec4& p = vector_at(object_of(this), kPosition);
    end = {along.x + p.x, along.y + p.y, along.z + p.z, along.w + p.w};
    maths::Vec4& position2 = vector_at(object_of(this), kPosition);
    if (probe_touches_zone(0x10, 0)) {
        const maths::Vec4& hit = vector_at(this, kHitPoint);
        const maths::Vec4& n = vector_at(this, kHitNormal);
        position2 = {n.x + hit.x, n.y + hit.y, n.z + hit.z, n.w + hit.w};
    } else {
        position2 = end;
    }
    const maths::Vec4 entry_velocity = vector_at(object_of(this), kVelocity); // (not original: for note_path)
    vector_at(object_of(this), kVelocity).flatten_onto_plane(normal);
    collision_events::note_path(this, collision_events::RouteWay::kCeiling, entry_velocity); // (not original)
    return true;
}

// 0x00506E50
void SkaterPhysicsModule::check_wall_ahead()
{
    void* object = object_of(this);
    const maths::Vec4& position = vector_at(object, kPosition);
    const maths::Vec4& old = vector_at(object, kOldPosition);
    if (position.x == old.x && position.y == old.y && position.z == old.z && position.w == old.w)
        return;
    // A feeler Skater_First_Forward_Collision_Length along the move, from Skater_First_Forward_Collision_Height up
    // the skater at its last position to that height at its new one.
    maths::Vec4 move = {position.x - old.x, position.y - old.y, position.z - old.z, position.w - old.w};
    move.make_unit();
    object = object_of(this);
    double height = tuning_float_lookup(kForwardHeight, object);
    const maths::Vec4& up = vector_at(object, kUp);
    maths::Vec4 lift = {static_cast<float>(height * up.x), static_cast<float>(height * up.y),
                        static_cast<float>(height * up.z), static_cast<float>(height * up.w)};
    const maths::Vec4& last = vector_at(object_of(this), kOldPosition);
    maths::Vec4& start = vector_at(this, kProbeOrigin);
    start.z = lift.z + last.z;
    start.w = lift.w + last.w;
    start.x = lift.x + last.x;
    start.y = lift.y + last.y;
    double length = tuning_float_lookup(kForwardLength, object);
    const maths::Vec4& now = vector_at(object_of(this), kPosition);
    float ax = static_cast<float>(move.x * length), ay = static_cast<float>(move.y * length),
          az = static_cast<float>(move.z * length);
    double aw = length * move.w;
    double x = static_cast<double>(lift.x) + now.x; // (left unrounded)
    float y = lift.y + now.y, z = lift.z + now.z, w = lift.w + now.w;
    maths::Vec4& end = vector_at(this, kProbeTip);
    end.z = z + az;
    end.w = static_cast<float>(w + aw);
    end.x = static_cast<float>(x + ax);
    end.y = y + ay;
    if (!probe_touches_zone(0x10, 0))
        return;
    maths::Vec4 normal = vector_at(this, kHitNormal);
    if (get_at<uint8_t>(this, kHitSkatable)) {
        // Skatable and not flat against the ground: pushed out of it a little.
        const maths::Vec4& ground = vector_at(this, kFloorUpVector);
        double into = static_cast<double>(normal.z) * ground.z + static_cast<double>(normal.y) * ground.y +
                      static_cast<double>(normal.x) * ground.x;
        if (!(std::fabs(into) < kWallFlat)) {
            maths::Vec4 out = {normal.x * kWallPushOut, normal.y * kWallPushOut, normal.z * kWallPushOut,
                               normal.w * kWallPushOut};
            vector_at(object_of(this), kPosition) = vector_at(this, kHitPoint);
            vector_at(object_of(this), kPosition) += out;
            collision_events::note_path(this, collision_events::RouteWay::kWallAheadClimb,
                                        vector_at(object_of(this), kVelocity)); // (not original)
            return;
        }
    }
    check_triggers(this, kBonkEvents);
    if (!trigger_took_over(this))
        bounce_off_wall_ahead(&normal);
}

// 0x005059D0
void SkaterPhysicsModule::bounce_off_wall_ahead(const maths::Vec4* normal)
{
    const maths::Vec4 entry_velocity = vector_at(object_of(this), kVelocity); // (not original: for note_path)
    if (wall_push_off()) {
        collision_events::note_path(this, collision_events::RouteWay::kWallAheadPlant,
                                    entry_velocity); // (not original)
        return;
    }
    void* object = object_of(this);
    const maths::Vec4& position = vector_at(object, kPosition);
    const maths::Vec4& old = vector_at(object, kOldPosition);
    // The move (rounded), and its direction.
    maths::Vec4 move = {position.x - old.x, position.y - old.y, position.z - old.z, 0.0f};
    maths::Vec4 direction = {move.x, move.y, move.z, position.w - old.w};
    direction.make_unit();
    object = object_of(this);
    double height = tuning_float_lookup(kForwardHeight, object);
    const maths::Vec4& up = vector_at(object, kUp);
    maths::Vec4 lift = {static_cast<float>(height * up.x), static_cast<float>(height * up.y),
                        static_cast<float>(height * up.z), static_cast<float>(height * up.w)};
    float turn;
    float angle = deflect_from_wall(normal, &turn, 1.0f);

    // Slowed the more head-on the wall is (unless within Wall_Bounce_Dont_Slow_Angle); fast enough, a flail.
    double dont_slow = static_cast<double>(script::fetch_float(kDontSlowAngle)) * kRadiansPerDegree;
    double off = std::fabs(angle);
    if (off > dont_slow) {
        maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
        float speed = static_cast<float>(length_unrounded(velocity));
        double keep = 1.0f - (off - dont_slow) / (kHalfPi - dont_slow);
        velocity.x = static_cast<float>(keep * velocity.x);
        velocity.y = static_cast<float>(keep * velocity.y);
        velocity.z = static_cast<float>(keep * velocity.z);
        velocity.w = static_cast<float>(keep * velocity.w);
        if (script::fetch_float(kDontFlailSpeed) < speed) {
            uint32_t left_turn = angle < 0.0f ? 1 : 0;
            bool same = (flags_of(this)[0x28] ^ left_turn) == 0; // (flag +0x28 against the turn's side)
            skater_object(this)->post_own_event(same ? kFlailRight : kFlailLeft, nullptr);
            if (!get_at<uint8_t>(this, kDriving)) {
                // (The original passes the surface byte in a word whose upper bytes are the angle's.)
                uint32_t angle_bits;
                std::memcpy(&angle_bits, &angle, 4);
                uint32_t surface = (angle_bits & 0xFFFFFF00u) | get_at<uint8_t>(this, kSurface);
                float impact = static_cast<float>(speed / stat_value(object_of(this), kMaxMaxSpeedStat));
                static_cast<objects::RiderSfxPlayer*>(get_at<void*>(this, kOtherComponent))
                    ->play_bonk(impact, static_cast<int>(surface));
            } else {
                script::launch_script(kVehicleLandSound, nullptr, skater_object(this), false, nullptr);
            }
        }
    }

    // Back out of the wall: the hit point less the lift, 6 out along the normal.
    object = object_of(this);
    maths::Vec4& p = vector_at(object, kPosition);
    const maths::Vec4& hit = vector_at(this, kHitPoint);
    p.z = hit.z - lift.z;
    p.w = hit.w - lift.w;
    p.x = hit.x - lift.x;
    p.y = hit.y - lift.y;
    double nx = static_cast<double>(normal->x) * kBounceBack, ny = static_cast<double>(normal->y) * kBounceBack;
    float nz = normal->z * kBounceBack, nw = normal->w * kBounceBack;
    p.x = static_cast<float>(nx + p.x);
    p.y = static_cast<float>(ny + p.y);
    p.z = nz + p.z;
    p.w = nw + p.w;
    collision_events::note_path(this, collision_events::RouteWay::kWallAhead, entry_velocity); // (not original)

    // Then a feeler from there (twice the lift up) along the velocity, as far as the skater moved plus
    // Skater_First_Forward_Collision_Length.
    object = object_of(this);
    float moved =
        static_cast<float>(std::sqrt(static_cast<double>(move.x) * move.x + static_cast<double>(move.z) * move.z +
                                     static_cast<double>(move.y) * move.y));
    maths::Vec4 heading = vector_at(object, kVelocity);
    heading.make_unit();
    maths::Vec4 travel = {heading.x * moved, heading.y * moved, heading.z * moved, heading.w * moved};
    maths::Vec4& start = vector_at(this, kProbeOrigin);
    const maths::Vec4& here = vector_at(object, kPosition);
    start.z = lift.z + here.z;
    start.w = lift.w + here.w;
    start.x = lift.x + here.x;
    start.y = lift.y + here.y;
    start.x = lift.x + start.x;
    start.y = lift.y + start.y;
    start.z = lift.z + start.z;
    start.w = lift.w + start.w;
    double length = tuning_float_lookup(kForwardLength, object);
    const maths::Vec4& now = vector_at(object_of(this), kPosition);
    float ax = static_cast<float>(heading.x * length), ay = static_cast<float>(heading.y * length),
          az = static_cast<float>(heading.z * length);
    double aw = length * heading.w;
    double bx = static_cast<double>(lift.x) + now.x, by = static_cast<double>(lift.y) + now.y;
    float bz = lift.z + now.z, bw = lift.w + now.w;
    maths::Vec4 base = {static_cast<float>(bx + travel.x), static_cast<float>(by + travel.y), bz + travel.z,
                        bw + travel.w};
    maths::Vec4& end = vector_at(this, kProbeTip);
    end.x = static_cast<float>(static_cast<double>(base.x) + ax);
    end.y = static_cast<float>(static_cast<double>(base.y) + ay);
    end.z = base.z + az;
    end.w = static_cast<float>(base.w + aw);
    if (!probe_touches_zone(0x10, 0) || !get_at<uint8_t>(object_of(this), 0xBC))
        return;
    // Into a corner: turned back the other way, half as fast; fast enough, a flail.
    double back = static_cast<double>(kPi) - turn;
    float back_angle = static_cast<float>(back);
    turn_velocity(vector_at(object_of(this), kVelocity), back);
    get_at<maths::Mat4>(object_of(this), kMatrix).yaw_locally(back_angle);
    get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
    vector_at(object_of(this), kVelocity) *= 0.5f;
    collision_events::note_path(this, collision_events::RouteWay::kWallAheadCorner, entry_velocity); // (not original)
    if (script::fetch_float(kDontFlailSpeed) < moved)
        skater_object(this)->post_own_event(kFlailLeft, nullptr);
}

} // namespace skater
