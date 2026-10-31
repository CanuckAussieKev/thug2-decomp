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

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kMaxMaxSpeedStat = 0xCC5F87AA;   // Skater_Max_Max_Speed_Stat
constexpr uint32_t kMaxSpeedStat = 0x2EACDDB3;      // Skater_Max_Speed_Stat
constexpr uint32_t kHeavyAirFriction = 0x850EB87A;  // Physics_Heavy_Air_Friction
constexpr uint32_t kCoastDrag = 0x78F80EC4;   // Physics_Rolling_Friction
constexpr uint32_t kBrakeAcceleration = 0xE5D73F6F; // Physics_Brake_Acceleration

// The fields used here.
constexpr uint32_t kRollingFrictionNow = 0x314;
constexpr uint32_t kRollingFrictionTimer = 0x320;
constexpr uint32_t kBraking = 0x324;
constexpr uint32_t kSpeedOverrideTimer = 0x350; // (-1 and -2: no end)
constexpr uint32_t kSpeedOverrideMax = 0x354;
constexpr uint32_t kSpeedOverrideMaxMax = 0x358;
constexpr uint32_t kFrictionOn = 0x2D9;

// The length of (x, y, z) as the original works it out: (x * x + y * y) + z * z, rounded to a float.
float speed_of(const maths::Vec4& v)
{
    double x = v.x, y = v.y, z = v.z;
    return static_cast<float>(std::sqrt(x * x + y * y + z * z));
}

// 0x004FE1F0 (not rewritten yet): the air friction `friction` applied to the velocity.
void apply_air_friction(void* component, float friction)
{
    static_cast<SkaterPhysicsModule*>(component)->apply_friction(friction);
}

} // namespace

// 0x00501850
void SkaterPhysicsModule::clamp_skater_speed()
{
    void* object = object_of(this);
    maths::Vec4& velocity = vector_at(object, kVelocity);
    // (The horizontal speed: y out of it while it's measured and limited.)
    float upward = velocity.y;
    velocity.y = 0.0f;
    float speed = speed_of(velocity);
    float max_max = static_cast<float>(stat_value(object, kMaxMaxSpeedStat));
    float max = static_cast<float>(stat_value(object, kMaxSpeedStat));
    // Not original: THAW 0x005CA8F0 - on the bike, faster for a while after vert ground (thaw_bmx_physics.cpp).
    bool heavy_friction = true;
    if (thaw_bmx::live_now(this))
        heavy_friction = thaw_bmx::vert_speed_allowance(this, &max_max);
    float& timer = get_at<float>(this, kSpeedOverrideTimer);
    if (timer != 0.0f) {
        uint32_t bits = get_at<uint32_t>(this, kSpeedOverrideTimer);
        if (bits != 0xBF800000u && bits != 0xC0000000u) {
            double left = static_cast<double>(timer) - get_at<float>(this, kFrameLength);
            timer = static_cast<float>(left);
            if (left < 0.0)
                get_at<uint32_t>(this, kSpeedOverrideTimer) = 0;
        }
        max_max = get_at<float>(this, kSpeedOverrideMaxMax);
        max = get_at<float>(this, kSpeedOverrideMax);
    }
    if (speed > max_max) {
        speed = max_max;
        set_length(vector_at(object_of(this), kVelocity), max_max);
    }
    object = object_of(this);
    if (speed > max && heavy_friction)
        apply_air_friction(this, script::fetch_float(kHeavyAirFriction));
    vector_at(object_of(this), kVelocity).y = upward;

    float& rolling = get_at<float>(this, kRollingFrictionTimer);
    if (rolling != 0.0f) {
        double left = static_cast<double>(rolling) - get_at<float>(this, kFrameLength);
        rolling = static_cast<float>(left);
        if (left <= 0.0) {
            get_at<uint32_t>(this, kRollingFrictionTimer) = 0;
            get_at<float>(this, kRollingFrictionNow) = tuning_float_lookup(kCoastDrag, object);
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
    get_at<uint8_t>(this, kBraking) = 1;
    if (may_turn_on_slope()) { // (turning on the slope instead)
        get_at<uint8_t>(this, kBraking) = 0;
        return;
    }
    void* object = object_of(this);
    float speed = speed_of(vector_at(object, kVelocity));
    double slowing = static_cast<double>(tuning_float_lookup(kBrakeAcceleration, object)) * get_at<float>(this, kFrameLength);
    if (slowing + slowing > speed) {
        // Less than two frames' worth left: stopped.
        vector_at(object_of(this), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        return;
    }
    maths::Vec4 slow = vector_at(object_of(this), kVelocity);
    set_length(slow, static_cast<float>(-(static_cast<double>(tuning_float_lookup(kBrakeAcceleration, object)) *
                                        get_at<float>(this, kFrameLength))));
    maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    velocity.x = slow.x + velocity.x;
    velocity.y = slow.y + velocity.y;
    velocity.z = slow.z + velocity.z;
    velocity.w = slow.w + velocity.w;
}

// 0x00506DF0
void SkaterPhysicsModule::apply_ground_friction()
{
    void* object = object_of(this);
    // (Not original: THAW 0x005D46C0 - on the bike the friction always applies.)
    if (!get_at<uint8_t>(this, kFrictionOn) && !flags_of(this)[0x70] &&
        get_at<float>(this, kRollingFrictionNow) == tuning_float_lookup(kCoastDrag, object) && !thaw_bmx::live_now(this))
        return;
    apply_stance_friction(); // (the friction: crouched or standing)
    if (turn_down_slope()) // (turned down the slope)
        return;
    apply_wheel_drag(); // (the rolling friction)
}

// 0x005021D0
void SkaterPhysicsModule::start_riding_hit_object()
{
    if (flags_of(this)[0x88])
        return;
    void* ride = get_at<void*>(this, kRide);
    if (!static_cast<objects::MovingSurfaceModule*>(ride)->start_riding(&get_at<render::ProbeRay>(this, kFeeler)))
        return;
    void* ride_object = *get_at<void**>(get_at<void*>(this, kRide), 0x18); // (the smart pointer's object)
    const maths::Vec4& ride_velocity = vector_at(ride_object, kVelocity);
    maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    velocity.x -= ride_velocity.x;
    velocity.y -= ride_velocity.y;
    velocity.z -= ride_velocity.z;
    velocity.w -= ride_velocity.w;
}

} // namespace Skater
