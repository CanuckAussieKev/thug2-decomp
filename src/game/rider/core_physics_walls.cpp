// The skater against walls and ceilings: the vert wall plant, leaning to push off a wall, keeping the head out of
// ceilings, the feeler ahead and bouncing off what it hits.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <bit>
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
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"

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

objects::GameEntity* skater_object(SkaterPhysicsModule* component)
{
    return static_cast<objects::GameEntity*>(object_of(component));
}

// 0x00434BF0 (GameObjects::ActivationZoneModule::probe_touches_zone): the triggers of what the feeler hit, for `kind`.
void check_triggers(SkaterPhysicsModule* component, const std::array<int, 3>& kind)
{
    static_cast<objects::ActivationZoneModule*>(component->trigger_)->probe_touches_zone(kind, &component->feeler_);
}

// Whether a trigger's script took over (+0x1530's +0x26).
bool trigger_took_over(SkaterPhysicsModule* component) { return component->physics_control_->flag26_ != 0; }

// The velocity turned by `angle` about the y axis, as the original's inline x87 code.
void turn_velocity(maths::Vec4& velocity, double angle)
{
    const double s = std::sin(angle);
    const double c = std::cos(angle);
    const double x = velocity.x;
    const double z = velocity.z;
    velocity.x = static_cast<float>(c * x + s * z);
    velocity.z = static_cast<float>(c * z + -(x * s));
}

} // namespace

// 0x00503710
bool SkaterPhysicsModule::vert_wallplant()
{
    // (not original: THAW 0x005D6960 - never a vert wallplant on the bike)
    if (on_vehicle_ || thaw_bmx::live_now(this))
        return false;
    // The trick button pressed (or pressed within Wall_Ride_Triangle_Window).
    void* skater = trick_;
    if (!static_cast<objects::MoveExecutor*>(skater)->is_key_held(kTrickKeyName)) {
        const uint32_t window = ftol(static_cast<double>(script::fetch_float(kWallRideWindow)) * kToMilliseconds);
        if (!static_cast<objects::MoveExecutor*>(trick_)->find_press(kTrickKeyName, window, 0xFFFFFFFFu))
            return false;
    }
    objects::RiderPoseState* flags = state_;
    if (!flags->flag(PoseFlag::kVertAir).on || !flags->flag(PoseFlag::kJumped).on)
        return false;
    if (!(trick_->spin_total_ == 0.0f) || vert_wallplanted_)
        return false;
    // The wall within reach below the skater's up axis.
    objects::GameEntity* object = object_of(this);
    const float snap = tuning_float_lookup(kGroundSnapUp, object);
    probe_start_ = core::at(object->basis_frame_.row, 1) * snap;
    probe_end_ = core::at(entity_->basis_frame_.row, 1) * kFeelerDepth;
    probe_start_ += entity_->at_point_;
    probe_end_ += entity_->at_point_;
    if (!probe_touches_zone(0x10, 0))
        return false;
    const float distance = static_cast<float>(length_unrounded(entity_->at_point_ - feeler_.contact_pos_));
    if (static_cast<double>(script::fetch_int_value(kWallplantDistance)) < distance)
        return false;
    object = object_of(this);
    if (static_cast<double>(script::fetch_int_value(kWallplantVelocityLimit)) > object->vel_.y)
        return false;
    if (state_->height_above_ground_ < kMinWallplantHeight)
        return false;
    if (!(feeler_.hit_.face_flags_ & 8) && std::fabs(feeler_.surface_dir_.y) > kMaxWallplantTilt)
        return false;
    if (!static_cast<objects::TaggingControlComponent*>(tagging_component_)->slap_sticker(true, false, false))
        return false;
    check_triggers(this, kJumpTriggerEvents);
    if (trigger_took_over(this))
        return true;
    object = object_of(this);
    if (object->vel_.y >= 0.0f)
        object->vel_.y =
            static_cast<float>(script::fetch_int_value(kWallplantBoost) + static_cast<double>(object->vel_.y));
    skater_object(this)->post_own_event(kVertWallPlant, nullptr);
    static_cast<objects::MoveExecutor*>(trick_)->pass_to_trick_objects(feeler_.hit_.object_checksum_);
    set_timed_flag(this, PoseFlag::kNoSecondSpin, 0);
    set_timed_flag(this, PoseFlag::kNoAcidDrop, 0);
    vert_wallplanted_ = 1;
    return true;
}

// 0x00503B30
void SkaterPhysicsModule::lean_push_off_wall()
{
    auto speed = static_cast<float>(length_unrounded(entity_->vel_));
    if (script::fetch_float(kMinLeanPushSpeed) < speed)
        return;
    // Held against a wall to the left or right: a feeler that far out, Skate_wall_lean_push_length after
    // Skate_wall_lean_push_time.
    objects::PadSnapshot* pad = &input_->pad_primary_;
    int held = 0;
    if (pad->left_side_.held_down_)
        held = time_since_press(&pad->left_side_);
    else if (pad->right_side_.held_down_)
        held = time_since_press(&pad->right_side_);
    else
        return;
    double held_time = held;
    if (held < 0)
        held_time += kUnsignedOffset; // (unsigned)
    auto seconds = static_cast<float>(held_time * kMilliseconds);
    const float push_time = script::fetch_float(kLeanPushTime);
    if (seconds > push_time)
        seconds = push_time;
    if (pad->right_side_.held_down_)
        seconds = -seconds;
    const double length = script::fetch_float(kLeanPushLength);
    auto reach = static_cast<float>(length * (static_cast<double>(seconds) / push_time));
    const double height = script::fetch_float(kLeanPushHeight);
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& up = core::at(object->basis_frame_.row, 1);
    const maths::Vec4& position = entity_->at_point_;
    maths::Vec4& start = probe_start_;
    maths::Vec4& end = probe_end_;
    auto hx = static_cast<float>(height * up.x), hy = static_cast<float>(height * up.y),
         hz = static_cast<float>(height * up.z);
    const double hw = height * up.w;
    start.z = hz + position.z;
    start.w = static_cast<float>(hw + position.w);
    start.x = hx + position.x;
    start.y = hy + position.y;
    const maths::Vec4& side = core::at(entity_->basis_frame_.row, 0);
    auto ry = static_cast<float>(static_cast<double>(reach) * side.y),
         rz = static_cast<float>(static_cast<double>(reach) * side.z),
         rw = static_cast<float>(static_cast<double>(reach) * side.w);
    end.z = rz + start.z;
    end.w = rw + start.w;
    end.x = static_cast<float>(static_cast<double>(reach) * side.x + start.x);
    end.y = ry + start.y;
    if (!probe_touches_zone(0x10, 0))
        return;
    // Moved along the wall's normal to where it is (one up from the skater's position), unless that's blocked.
    const maths::Vec4& point = feeler_.contact_pos_;
    const maths::Vec4 normal = feeler_.surface_dir_;
    maths::Vec4 along = {point.x - end.x, point.y - end.y, point.z - end.z, point.w - end.w};
    along.project_onto(normal);
    object = object_of(this);
    const maths::Vec4& up2 = core::at(object->basis_frame_.row, 1);
    const maths::Vec4& position2 = object->at_point_;
    start = {up2.x + position2.x, up2.y + position2.y, up2.z + position2.z, up2.w + position2.w};
    end = {along.x + start.x, along.y + start.y, along.z + start.z, along.w + start.w};
    if (probe_touches_zone(0x10, 0))
        return;
    object = object_of(this);
    maths::Vec4& p = object->at_point_;
    const maths::Vec4& u = core::at(object->basis_frame_.row, 1);
    p = {end.x - u.x, end.y - u.y, end.z - u.z, end.w - u.w};
}

// 0x00504EE0
bool SkaterPhysicsModule::keep_off_ceiling()
{
    objects::GameEntity* object = object_of(this);
    if (core::at(object->basis_frame_.row, 1).y < 0.0f)
        return false;
    // A feeler up the skater's height (just 6 for a while after a wall plant).
    const float head_height = tuning_float_lookup(kEyeLevelStat, object);
    const uint32_t elapsed = get_since(wallplant_time_);
    const double reach =
        elapsed > ints::as_unsigned(script::fetch_int_value(kIgnoreCeilings)) ? head_height : kNoHeadHeight;
    object = object_of(this);
    const maths::Vec4& up = core::at(object->basis_frame_.row, 1);
    const double x = reach * up.x;
    const double y = reach * up.y;
    auto z = static_cast<float>(reach * up.z), w = static_cast<float>(reach * up.w);
    maths::Vec4& start = probe_start_;
    maths::Vec4& end = probe_end_;
    start = object->at_point_;
    end = entity_->at_point_;
    end.x = static_cast<float>(x + end.x);
    end.y = static_cast<float>(y + end.y);
    end.z = z + end.z;
    end.w = w + end.w;
    if (!probe_touches_zone(0x10, 0))
        return false;
    const maths::Vec4 normal = feeler_.surface_dir_;
    if (normal.y > kCeilingSteepest)
        return false;
    // A ceiling: the skater slid along it to below it, its velocity into its plane.
    const maths::Vec4& point = feeler_.contact_pos_;
    maths::Vec4 along = {point.x - end.x, point.y - end.y, point.z - end.z, point.w - end.w};
    along.project_onto(normal);
    start = entity_->at_point_;
    const maths::Vec4& position = entity_->at_point_;
    end = {along.x + position.x, along.y + position.y, along.z + position.z, along.w + position.w};
    if (probe_touches_zone(0x10, 0)) // (nudged along the new hit's normal)
        entity_->at_point_ = feeler_.contact_pos_ + kCeilingNudge * feeler_.surface_dir_;
    else
        entity_->at_point_ = end;
    const maths::Vec4 entry_velocity = entity_->vel_; // (not original: for note_path)
    entity_->vel_.flatten_onto_plane(normal);
    collision_events::note_path(this, collision_events::RouteWay::kCeiling, entry_velocity); // (not original)
    return true;
}

// 0x005051B0
bool SkaterPhysicsModule::keep_off_ceiling_at_wall()
{
    // A feeler up the skater's height from 18 out along +0x38C.
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& wall = wall_normal_;
    const double x = static_cast<double>(wall.x) * kWallOffset;
    const float y = wall.y * kWallOffset;
    const float z = wall.z * kWallOffset;
    const float w = wall.w * kWallOffset;
    const maths::Vec4& position = object->at_point_;
    maths::Vec4& start = probe_start_;
    maths::Vec4& end = probe_end_;
    start.z = z + position.z;
    start.w = w + position.w;
    start.x = static_cast<float>(x + position.x);
    start.y = y + position.y;
    end = start;
    end.y = static_cast<float>(static_cast<double>(tuning_float_lookup(kEyeLevelStat, object)) + end.y);
    if (!probe_touches_zone(0x10, 0))
        return false;
    const maths::Vec4 normal = feeler_.surface_dir_;
    if (normal.y > kCeilingSteepest)
        return false;
    const maths::Vec4& point = feeler_.contact_pos_;
    maths::Vec4 along = {point.x - end.x, point.y - end.y, point.z - end.z, point.w - end.w};
    along.project_onto(normal);
    start = entity_->at_point_;
    const maths::Vec4& p = entity_->at_point_;
    end = {along.x + p.x, along.y + p.y, along.z + p.z, along.w + p.w};
    maths::Vec4& position2 = entity_->at_point_;
    if (probe_touches_zone(0x10, 0)) {
        const maths::Vec4& hit = feeler_.contact_pos_;
        const maths::Vec4& n = feeler_.surface_dir_;
        position2 = {n.x + hit.x, n.y + hit.y, n.z + hit.z, n.w + hit.w};
    } else {
        position2 = end;
    }
    const maths::Vec4 entry_velocity = entity_->vel_; // (not original: for note_path)
    entity_->vel_.flatten_onto_plane(normal);
    collision_events::note_path(this, collision_events::RouteWay::kCeiling, entry_velocity); // (not original)
    return true;
}

// 0x00506E50
void SkaterPhysicsModule::check_wall_ahead()
{
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& position = object->at_point_;
    const maths::Vec4& old = object->old_pos_;
    if (position.x == old.x && position.y == old.y && position.z == old.z && position.w == old.w)
        return;
    // A feeler Skater_First_Forward_Collision_Length along the move, from Skater_First_Forward_Collision_Height up
    // the skater at its last position to that height at its new one.
    maths::Vec4 move = {position.x - old.x, position.y - old.y, position.z - old.z, position.w - old.w};
    move.make_unit();
    object = object_of(this);
    const double height = tuning_float_lookup(kForwardHeight, object);
    const maths::Vec4& up = core::at(object->basis_frame_.row, 1);
    const maths::Vec4 lift = {static_cast<float>(height * up.x), static_cast<float>(height * up.y),
                              static_cast<float>(height * up.z), static_cast<float>(height * up.w)};
    const maths::Vec4& last = entity_->old_pos_;
    maths::Vec4& start = probe_start_;
    start.z = lift.z + last.z;
    start.w = lift.w + last.w;
    start.x = lift.x + last.x;
    start.y = lift.y + last.y;
    const double length = tuning_float_lookup(kForwardLength, object);
    const maths::Vec4& now = entity_->at_point_;
    auto ax = static_cast<float>(move.x * length), ay = static_cast<float>(move.y * length),
         az = static_cast<float>(move.z * length);
    const double aw = length * move.w;
    const double x = static_cast<double>(lift.x) + now.x; // (left unrounded)
    const float y = lift.y + now.y;
    const float z = lift.z + now.z;
    const float w = lift.w + now.w;
    maths::Vec4& end = probe_end_;
    end.z = z + az;
    end.w = static_cast<float>(w + aw);
    end.x = static_cast<float>(x + ax);
    end.y = y + ay;
    if (!probe_touches_zone(0x10, 0))
        return;
    const maths::Vec4 normal = feeler_.surface_dir_;
    if (hit_skatable_) {
        // Skatable and not flat against the ground: pushed out of it a little.
        const maths::Vec4& ground = floor_normal_;
        const double into = static_cast<double>(normal.z) * ground.z + static_cast<double>(normal.y) * ground.y +
                            static_cast<double>(normal.x) * ground.x;
        if (!(std::fabs(into) < kWallFlat)) {
            const maths::Vec4 out = {normal.x * kWallPushOut, normal.y * kWallPushOut, normal.z * kWallPushOut,
                                     normal.w * kWallPushOut};
            entity_->at_point_ = feeler_.contact_pos_;
            entity_->at_point_ += out;
            collision_events::note_path(this, collision_events::RouteWay::kWallAheadClimb,
                                        entity_->vel_); // (not original)
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
    const maths::Vec4 entry_velocity = entity_->vel_; // (not original: for note_path)
    if (wall_push_off()) {
        collision_events::note_path(this, collision_events::RouteWay::kWallAheadPlant,
                                    entry_velocity); // (not original)
        return;
    }
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& position = object->at_point_;
    const maths::Vec4& old = object->old_pos_;
    // The move (rounded), and its direction.
    const maths::Vec4 move = {position.x - old.x, position.y - old.y, position.z - old.z, 0.0f};
    maths::Vec4 direction = {move.x, move.y, move.z, position.w - old.w};
    direction.make_unit();
    object = object_of(this);
    const double height = tuning_float_lookup(kForwardHeight, object);
    const maths::Vec4& up = core::at(object->basis_frame_.row, 1);
    const maths::Vec4 lift = {static_cast<float>(height * up.x), static_cast<float>(height * up.y),
                              static_cast<float>(height * up.z), static_cast<float>(height * up.w)};
    float turn = 0.0f;
    const float angle = deflect_from_wall(normal, &turn, 1.0f);

    // Slowed the more head-on the wall is (unless within Wall_Bounce_Dont_Slow_Angle); fast enough, a flail.
    const double dont_slow = static_cast<double>(script::fetch_float(kDontSlowAngle)) * kRadiansPerDegree;
    const double off = std::fabs(angle);
    if (off > dont_slow) {
        maths::Vec4& velocity = entity_->vel_;
        auto speed = static_cast<float>(length_unrounded(velocity));
        const double keep = 1.0f - (off - dont_slow) / (kHalfPi - dont_slow);
        velocity.x = static_cast<float>(keep * velocity.x);
        velocity.y = static_cast<float>(keep * velocity.y);
        velocity.z = static_cast<float>(keep * velocity.z);
        velocity.w = static_cast<float>(keep * velocity.w);
        if (script::fetch_float(kDontFlailSpeed) < speed) {
            const uint32_t left_turn = angle < 0.0f ? 1 : 0;
            const bool same =
                (state_->flag(PoseFlag::kFlipped).on ^ left_turn) == 0; // (flag +0x28 against the turn's side)
            skater_object(this)->post_own_event(same ? kFlailRight : kFlailLeft, nullptr);
            if (!on_vehicle_) {
                // (The original passes the surface byte in a word whose upper bytes are the angle's.)
                auto angle_bits = std::bit_cast<uint32_t>(angle);
                const uint32_t surface = (angle_bits & 0xFFFFFF00u) | feeler_.hit_.terrain;
                auto impact = static_cast<float>(speed / stat_value(object_of(this), kMaxMaxSpeedStat));
                static_cast<objects::RiderSfxPlayer*>(sound_)->play_bonk(impact, static_cast<int>(surface));
            } else {
                script::launch_script(kVehicleLandSound, nullptr, skater_object(this), false, nullptr);
            }
        }
    }

    // Back out of the wall: the hit point less the lift, 6 out along the normal.
    object = object_of(this);
    maths::Vec4& p = object->at_point_;
    const maths::Vec4& hit = feeler_.contact_pos_;
    p.z = hit.z - lift.z;
    p.w = hit.w - lift.w;
    p.x = hit.x - lift.x;
    p.y = hit.y - lift.y;
    const double nx = static_cast<double>(normal->x) * kBounceBack;
    const double ny = static_cast<double>(normal->y) * kBounceBack;
    const float nz = normal->z * kBounceBack;
    const float nw = normal->w * kBounceBack;
    p.x = static_cast<float>(nx + p.x);
    p.y = static_cast<float>(ny + p.y);
    p.z = nz + p.z;
    p.w = nw + p.w;
    collision_events::note_path(this, collision_events::RouteWay::kWallAhead, entry_velocity); // (not original)

    // Then a feeler from there (twice the lift up) along the velocity, as far as the skater moved plus
    // Skater_First_Forward_Collision_Length.
    object = object_of(this);
    auto moved =
        static_cast<float>(std::sqrt(static_cast<double>(move.x) * move.x + static_cast<double>(move.z) * move.z +
                                     static_cast<double>(move.y) * move.y));
    maths::Vec4 heading = object->vel_;
    heading.make_unit();
    const maths::Vec4 travel = {heading.x * moved, heading.y * moved, heading.z * moved, heading.w * moved};
    maths::Vec4& start = probe_start_;
    const maths::Vec4& here = object->at_point_;
    start.z = lift.z + here.z;
    start.w = lift.w + here.w;
    start.x = lift.x + here.x;
    start.y = lift.y + here.y;
    start.x = lift.x + start.x;
    start.y = lift.y + start.y;
    start.z = lift.z + start.z;
    start.w = lift.w + start.w;
    const double length = tuning_float_lookup(kForwardLength, object);
    const maths::Vec4& now = entity_->at_point_;
    auto ax = static_cast<float>(heading.x * length), ay = static_cast<float>(heading.y * length),
         az = static_cast<float>(heading.z * length);
    const double aw = length * heading.w;
    const double bx = static_cast<double>(lift.x) + now.x;
    const double by = static_cast<double>(lift.y) + now.y;
    const float bz = lift.z + now.z;
    const float bw = lift.w + now.w;
    const maths::Vec4 base = {static_cast<float>(bx + travel.x), static_cast<float>(by + travel.y), bz + travel.z,
                              bw + travel.w};
    maths::Vec4& end = probe_end_;
    end.x = static_cast<float>(static_cast<double>(base.x) + ax);
    end.y = static_cast<float>(static_cast<double>(base.y) + ay);
    end.z = base.z + az;
    end.w = static_cast<float>(base.w + aw);
    if (!probe_touches_zone(0x10, 0) || !entity_->is_local_skater_)
        return;
    // Into a corner: turned back the other way, half as fast; fast enough, a flail.
    const double back = static_cast<double>(kPi) - turn;
    auto back_angle = static_cast<float>(back);
    turn_velocity(entity_->vel_, back);
    entity_->basis_frame_.yaw_locally(back_angle);
    display_matrix_ = entity_->basis_frame_;
    entity_->vel_ *= 0.5f;
    collision_events::note_path(this, collision_events::RouteWay::kWallAheadCorner, entry_velocity); // (not original)
    if (script::fetch_float(kDontFlailSpeed) < moved)
        skater_object(this)->post_own_event(kFlailLeft, nullptr);
}

} // namespace skater
