// The skater's core physics component: the frame's update (the state's own update and what runs around it).

#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/skater/collision_events.h"
#include "game/skater/core_fields.h"
#include "game/skater/core_physics.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace CoreFields;

namespace {

constexpr uint32_t kLandedThisFrame = 0x254;   // set when the air's update lands the skater on the ground
constexpr uint32_t kFrameFlag255 = 0x255;      // the timed flags' +0x78 when the frame began
constexpr uint32_t kFrameBeganInLip = 0x256;
constexpr uint32_t kGroundStart = 0x338;       // the position the ground's update starts from
constexpr uint32_t kAirFromJump = 0x4F0;       // in the air with +0x30 set and +0x78 not (told by event when it changes)
constexpr uint32_t kAirTimer = 0x14CC;         // (cleared whenever the skater is out of the air)

constexpr uint32_t kEnteredAir = 0xF225FE69;   // the events sent when +0x4F0 changes (names unknown)
constexpr uint32_t kLeftAir = 0x5E27200A;

void Call(uint32_t address, void* component) { GameFunction<void(__thiscall*)(void*)>(address)(component); }

} // namespace

// 0x00510830
void CorePhysicsComponent::Update()
{
    At<float>(this, kFrameLength) = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    CollisionEvents::Checkpoint(this, "update-start"); // (not original)
    At<uint8_t>(this, kLandedThisFrame) = 0;
    At<uint8_t>(this, kFrameBeganInLip) = StateOf(this) == kLip;
    At<uint8_t>(this, kFrameFlag255) = Flag(this, 0x78);
    if (!Flag(this, 0x20) && InputOf(this)[0x1C0])
        SetFlag(this, 0x20, 1);
    LimitSpeed();
    SetFlag(this, 0x98, 0);
    SetFlag(this, 0xA0, 0);
    Call(0x00500670, this); // (a box round the skater at +0x4F4, published at 0x00691974 for the frame)
    UpdateModelRotation();

    switch (StateOf(this)) {
    case kGround:
        VectorAt(this, kGroundStart) = VectorAt(ObjectOf(this), kPosition);
        DoGroundPhysics();
        TryStartSkitch();
        break;
    case kAir:
        DoAirPhysics();
        if (StateOf(this) == kGround)
            At<uint8_t>(this, kLandedThisFrame) = 1;
        break;
    case kWallRide:
        DoWallRidePhysics();
        break;
    case kLip:
        DoLipPhysics();
        break;
    case kRail:
        DoRailPhysics();
        break;
    case kWallplant:
        DoWallplantPhysics();
        break;
    default:
        break;
    }

    CollisionEvents::Checkpoint(this, "state-physics"); // (not original)
    Call(0x004FF200, this); // (not rewritten yet)
    CollisionEvents::Checkpoint(this, "after-4FF200"); // (not original)
    TryGrind(false); // (inlined in the original)
    CollisionEvents::Checkpoint(this, "try-grind"); // (not original)
    UpdateFreeRevert();
    CollisionEvents::Checkpoint(this, "free-revert"); // (not original)
    UpdateFocusButton();
    UpdateProjectiles();
    UpdateSpecialTime();
    CollisionEvents::Checkpoint(this, "update-rest"); // (not original)

    uint8_t inAir = StateOf(this) == kAir && Flag(this, 0x30) && !Flag(this, 0x78);
    uint8_t& airFromJump = At<uint8_t>(this, kAirFromJump);
    if (airFromJump != inAir) {
        airFromJump = !airFromJump;
        static_cast<Obj::CObject*>(ObjectOf(this))->BroadcastEvent(airFromJump ? kEnteredAir : kLeftAir, nullptr, 0.0f);
    }
    if (StateOf(this) != kAir)
        At<int>(this, kAirTimer) = 0;
    GameVariable<void*>(0x00691974) = nullptr;
    CollisionEvents::EndFrame(this); // (not original: [debug] collision_events)
}

} // namespace Skater

REPLACE(0x00510830, &Skater::CorePhysicsComponent::Update);
