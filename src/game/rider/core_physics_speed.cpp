// The skater's speed: the speed limits, braking, the friction on the ground, a ride's velocity.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/movable_contact.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/skater_state.h"
#include "engine/objects/ride_tracker.h"

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kMaxMaxSpeedStat = 0xCC5F87AA;   // Skater_Max_Max_Speed_Stat
constexpr uint32_t kMaxSpeedStat = 0x2EACDDB3;      // Skater_Max_Speed_Stat
constexpr uint32_t kHeavyAirFriction = 0x850EB87A;  // Physics_Heavy_Air_Friction
constexpr uint32_t kCoastDrag = 0x78F80EC4;         // Physics_Rolling_Friction
constexpr uint32_t kBrakeAcceleration = 0xE5D73F6F; // Physics_Brake_Acceleration

// The length of (x, y, z) as the original works it out: (x * x + y * y) + z * z, rounded to a float.
float speed_of(const maths::Vec4& v)
{
    const double x = v.x;
    const double y = v.y;
    const double z = v.z;
    return static_cast<float>(std::sqrt((x * x) + (y * y) + (z * z)));
}


} // namespace

// 0x00501850
void SkaterPhysicsModule::clamp_skater_speed()
{
    objects::GameEntity* object = object_of(this);
    maths::Vec4& velocity = object->vel();
    // (The horizontal speed: y out of it while it's measured and limited.)
    const float upward = velocity.y;
    velocity.y = 0.0f;
    float speed = speed_of(velocity);
    auto max_max = static_cast<float>(stat_value(object, kMaxMaxSpeedStat));
    auto max = static_cast<float>(stat_value(object, kMaxSpeedStat));
    // Not original: THAW 0x005CA8F0 - on the bike, faster for a while after vert ground (thaw_bmx_physics.cpp).
    bool heavy_friction = true;
    if (thaw_bmx::live_now(this))
        heavy_friction = thaw_bmx::vert_speed_allowance(this, &max_max);
    float& timer = limits_time_;
    if (timer != 0.0f) {
        if (timer != -1.0f && timer != -kTimerForLevel) { // (-1: for good; -2: for the level)
            const double left = static_cast<double>(timer) - frame_length_;
            timer = static_cast<float>(left);
            if (left < 0.0)
                limits_time_ = 0.0f;
        }
        max_max = limits_max_max_;
        max = limits_max_;
    }
    if (speed > max_max) {
        speed = max_max;
        set_length(entity()->vel(), max_max);
    }
    object = object_of(this);
    if (speed > max && heavy_friction)
        apply_friction((script::fetch_float(kHeavyAirFriction)));
    entity()->vel().y = upward;

    float& rolling = special_friction_time_;
    if (rolling != 0.0f) {
        const double left = static_cast<double>(rolling) - frame_length_;
        rolling = static_cast<float>(left);
        if (left <= 0.0) {
            special_friction_time_ = 0.0f;
            rolling_friction_ = tuning_float_lookup(kCoastDrag, object);
        }
    }
}

// 0x005019E0
void SkaterPhysicsModule::get_brake()
{
    if (thaw_bmx::live_now(this)) { // (not original: THAW's bike brakes, thaw_bmx_physics.cpp)
        thaw_bmx::get_brake(this);
        return;
    }
    braking_ = 1;
    if (may_turn_on_slope()) { // (turning on the slope instead)
        braking_ = 0;
        return;
    }
    objects::GameEntity* object = object_of(this);
    const float speed = speed_of(object->vel());
    const double slowing = static_cast<double>(tuning_float_lookup(kBrakeAcceleration, object)) * frame_length_;
    if (slowing + slowing > speed) {
        // Less than two frames' worth left: stopped.
        entity()->vel() = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
        return;
    }
    maths::Vec4 slow = entity()->vel();
    set_length(slow, static_cast<float>(
                         -(static_cast<double>(tuning_float_lookup(kBrakeAcceleration, object)) * frame_length_)));
    maths::Vec4& velocity = entity()->vel();
    velocity.x = slow.x + velocity.x;
    velocity.y = slow.y + velocity.y;
    velocity.z = slow.z + velocity.z;
    velocity.w = slow.w + velocity.w;
}

// 0x00506DF0
void SkaterPhysicsModule::apply_ground_friction()
{
    objects::GameEntity* object = object_of(this);
    // (Not original: THAW 0x005D46C0 - on the bike the friction always applies.)
    if (!auto_kick_ && !state_->flag(PoseFlag::kBailing).on &&
        rolling_friction_ == tuning_float_lookup(kCoastDrag, object) && !thaw_bmx::live_now(this))
        return;
    apply_stance_friction(); // (the friction: crouched or standing)
    if (turn_down_slope())   // (turned down the slope)
        return;
    apply_wheel_drag(); // (the rolling friction)
}

// 0x005021D0
void SkaterPhysicsModule::start_riding_hit_object()
{
    if (state_->flag(PoseFlag::kSkitching).on)
        return;
    objects::MovingSurfaceModule* ride = ride_;
    if (!ride->start_riding(&feeler_))
        return;
    const objects::GameEntity* ride_object = ride_->ride()->entity().get_pull();
    const maths::Vec4& ride_velocity = ride_object->vel();
    maths::Vec4& velocity = entity()->vel();
    velocity.x -= ride_velocity.x;
    velocity.y -= ride_velocity.y;
    velocity.z -= ride_velocity.z;
    velocity.w -= ride_velocity.w;
}

} // namespace skater
