// The skater's speed: the speed limits, braking, the friction on the ground, a ride's velocity.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/movable_contact.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kMaxMaxSpeedStat = 0xCC5F87AA;   // Skater_Max_Max_Speed_Stat
constexpr uint32_t kMaxSpeedStat = 0x2EACDDB3;      // Skater_Max_Speed_Stat
constexpr uint32_t kHeavyAirFriction = 0x850EB87A;  // Physics_Heavy_Air_Friction
constexpr uint32_t kRollingFriction = 0x78F80EC4;   // Physics_Rolling_Friction
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
float SpeedOf(const Mth::Vector& v)
{
    double x = v.x, y = v.y, z = v.z;
    return static_cast<float>(std::sqrt(x * x + y * y + z * z));
}

// 0x004FE1F0 (not rewritten yet): the air friction `friction` applied to the velocity.
void ApplyAirFriction(void* component, float friction)
{
    GameFunction<void(__thiscall*)(void*, float)>(0x004FE1F0)(component, friction);
}

} // namespace

// 0x00501850
void CorePhysicsComponent::LimitSpeed()
{
    void* object = ObjectOf(this);
    Mth::Vector& velocity = VectorAt(object, kVelocity);
    // (The horizontal speed: y out of it while it's measured and limited.)
    float upward = velocity.y;
    velocity.y = 0.0f;
    float speed = SpeedOf(velocity);
    float maxMax = static_cast<float>(StatValue(object, kMaxMaxSpeedStat));
    float max = static_cast<float>(StatValue(object, kMaxSpeedStat));
    float& timer = At<float>(this, kSpeedOverrideTimer);
    if (timer != 0.0f) {
        uint32_t bits = At<uint32_t>(this, kSpeedOverrideTimer);
        if (bits != 0xBF800000u && bits != 0xC0000000u) {
            double left = static_cast<double>(timer) - At<float>(this, kFrameLength);
            timer = static_cast<float>(left);
            if (left < 0.0)
                At<uint32_t>(this, kSpeedOverrideTimer) = 0;
        }
        maxMax = At<float>(this, kSpeedOverrideMaxMax);
        max = At<float>(this, kSpeedOverrideMax);
    }
    if (speed > maxMax) {
        speed = maxMax;
        SetLength(VectorAt(ObjectOf(this), kVelocity), maxMax);
    }
    object = ObjectOf(this);
    if (speed > max)
        ApplyAirFriction(this, Script::GetFloat(kHeavyAirFriction));
    VectorAt(ObjectOf(this), kVelocity).y = upward;

    float& rolling = At<float>(this, kRollingFrictionTimer);
    if (rolling != 0.0f) {
        double left = static_cast<double>(rolling) - At<float>(this, kFrameLength);
        rolling = static_cast<float>(left);
        if (left <= 0.0) {
            At<uint32_t>(this, kRollingFrictionTimer) = 0;
            At<float>(this, kRollingFrictionNow) = PhysicsFloat(kRollingFriction, object);
        }
    }
}

// 0x005019E0
void CorePhysicsComponent::Brake()
{
    At<uint8_t>(this, kBraking) = 1;
    if (GameFunction<bool(__thiscall*)(void*)>(0x004FE420)(this)) { // (turning on the slope instead)
        At<uint8_t>(this, kBraking) = 0;
        return;
    }
    void* object = ObjectOf(this);
    float speed = SpeedOf(VectorAt(object, kVelocity));
    double slowing = static_cast<double>(PhysicsFloat(kBrakeAcceleration, object)) * At<float>(this, kFrameLength);
    if (slowing + slowing > speed) {
        // Less than two frames' worth left: stopped.
        VectorAt(ObjectOf(this), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        return;
    }
    Mth::Vector slow = VectorAt(ObjectOf(this), kVelocity);
    SetLength(slow, static_cast<float>(-(static_cast<double>(PhysicsFloat(kBrakeAcceleration, object)) *
                                        At<float>(this, kFrameLength))));
    Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
    velocity.x = slow.x + velocity.x;
    velocity.y = slow.y + velocity.y;
    velocity.z = slow.z + velocity.z;
    velocity.w = slow.w + velocity.w;
}

// 0x00506DF0
void CorePhysicsComponent::ApplyGroundFriction()
{
    void* object = ObjectOf(this);
    if (!At<uint8_t>(this, kFrictionOn) && !FlagsOf(this)[0x70] &&
        At<float>(this, kRollingFrictionNow) == PhysicsFloat(kRollingFriction, object))
        return;
    GameFunction<void(__thiscall*)(void*)>(0x004FE6A0)(this); // (the friction: crouched or standing)
    if (TurnDownSlope()) // (turned down the slope)
        return;
    GameFunction<void(__thiscall*)(void*)>(0x004FE730)(this); // (the rolling friction)
}

// 0x005021D0
void CorePhysicsComponent::StartRidingHitObject()
{
    if (FlagsOf(this)[0x88])
        return;
    void* ride = At<void*>(this, kRide);
    if (!static_cast<Obj::CMovableContactComponent*>(ride)->StartRiding(&At<Nx::CFeeler>(this, kFeeler)))
        return;
    void* rideObject = *At<void**>(At<void*>(this, kRide), 0x18); // (the smart pointer's object)
    const Mth::Vector& rideVelocity = VectorAt(rideObject, kVelocity);
    Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
    velocity.x -= rideVelocity.x;
    velocity.y -= rideVelocity.y;
    velocity.z -= rideVelocity.z;
    velocity.w -= rideVelocity.w;
}

} // namespace Skater

REPLACE(0x00501850, &Skater::CorePhysicsComponent::LimitSpeed);
REPLACE(0x005019E0, &Skater::CorePhysicsComponent::Brake);
REPLACE(0x00506DF0, &Skater::CorePhysicsComponent::ApplyGroundFriction);
REPLACE(0x005021D0, &Skater::CorePhysicsComponent::StartRidingHitObject);
