// Self-tests for the skater core physics component's wall plant and wall ride start (core_physics_wallplant.cpp):
// original vs ours on a stand-in component, every callee into game code replaced by a recording stand-in for the
// length of the test (the math functions pointed at ours).

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/trigger_component.h"
#include "game/obj/walk_hang.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/skater/tagging.h"
#include "game/skater/trick_component.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/selftests_skater_physics_harness.h"

using namespace SkaterPhysicsTest;
using Skater::CorePhysicsComponent;

namespace {

CorePhysicsComponent* Ours(uint8_t* component) { return reinterpret_cast<CorePhysicsComponent*>(component); }

uint32_t g_array; // the Wallplant_Trick array the stand-in returns

std::vector<StandIn> WallplantStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, -1, -1, 0, CodeAddress(&Tmr::GameClockElapsed)}, // the clock
        {0x00478F00, 0, 1, Ret::Float, false, 0, -1, -1, 0,
         CodeAddress(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
        {0x00478F60, 0, 1, Ret::Int, false, 0, -1, -1, 0,
         CodeAddress(static_cast<int (*)(uint32_t)>(&Script::GetInteger))},
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0, CodeAddress(&Obj::GetPhysicsFloat)}, // PhysicsFloat
        {0x004171D0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::SetLine)},
        {0x004170F0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::SetIgnore), nullptr, 0, 3},
        {0x004172F0, 8, 2, Ret::Bool, true, 3, -1, -1, 9, CodeAddress(&Nx::CFeeler::GetCollision), nullptr, 0, 0,
         0x74, 0, 8},
        {0x00417210, 0, 0, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::HitObjectAlive)},
        {0x00417240, 0, 0, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::GetHitObject)},
        {0x004607C0, 8, 2, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Obj::CObject::GetIntegerTag)},
        {0x00420250, 0, 1, Ret::Int, false, 0, -1, -1, 0, CodeAddress(&Tmr::GameClockTimeSince)},   // (the time since)
        {0x00479070, 0, 1, Ret::Fixed, false, 0, -1, -1, 0,
         CodeAddress(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray)), &g_array},
        {0x0046CC30, 4, 1, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Script::CArray::GetElement)},
        {0x0051FDA0, 16, 4, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Obj::CTrickComponent::CheckTrigger)},  // (a trick pressed)
        {0x00401C30, 0, 1, Ret::Int, false, 0, -1, -1, 0, CodeAddress(&Dbg::Printf)},
        {0x0051DDF0, 12, 3, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Obj::CTaggingControlComponent::SlapSticker)},
        {0x00434BF0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CTriggerComponent::CheckFeeler)},   // a trigger check
        {0x00427CB0, 0, 0, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Obj::CMovableContactComponent::IsRiding)},   // (riding)
        {0x00427C70, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CMovableContactComponent::StopRiding)},
        {0x0043EB80, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Walk::MovableContact::StartRiding)},
        {0x0042BBF0, 4, 1, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Mth::Vector::SetLength), nullptr, 1, 0, -1, 0, 4}, // SetLength (the vector recorded)
        {0x004609F0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CObject::SelfEvent)},
        {0x00522370, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CTrickComponent::PassToTrickObjects)},
        {0x004FDED0, 4, 1, Ret::None, true, 0, -1, -1, 0},   // SetState
        {0x0051ECF0, 4, 1, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Obj::CTrickComponent::IsButtonDown)},   // (a button pressed)
        {0x0051F6B0, 12, 3, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Obj::CTrickComponent::FindPress)},  // (... within a time)
        {0x005232A0, 4, 1, Ret::None, true, 1, -1, -1, 0, CodeAddress(&Obj::CTrickComponent::SetTrickObjects)},
        {0x0043A930, 4, 1, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::Assign)},    // (a feeler copied)
        {0x00427CD0, 4, 1, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Obj::CMovableContactComponent::StartRiding)},   // (a ride started)
    };
}

void TestSkaterPhysicsWallplant()
{
    g_rng = 17;
    StandIns standIns(WallplantStandIns());
    OurMath math;
    int savedOffset = GameVariable<int>(0x00696D60);
    GameVariable<int>(0x00696D60) = 0;
    bool same = true;
    int planted = 0, late = 0, rode = 0;
    for (int variant = 0; variant < 30000 && same; ++variant) {
        int which = variant % 5; // 0, 1: wall plants; 2-4: the wall ride
        if (which > 2)
            which = 2;
        g_rng = 0x2C1B3C6Du * (variant + 1);
        FillArena();
        g_fixedOut = false;
        uint8_t* c = g_physicsArena + kComponent;
        for (uint32_t flag = 0x20; flag < 0xF8; flag += 8)
            ArenaAt<uint8_t>(kFlags + flag) = RandomBool(50);
        c[0x2ED] = RandomBool(10);
        c[0x2DB] = RandomBool(10);
        c[0x22A] = RandomBool(90);
        ArenaAt<uint8_t>(kState + 0x24) = RandomBool(10);
        ArenaAt<uint8_t>(kState + 0x26) = RandomBool(15);
        ArenaAt<uint16_t>(kComponent + 0xF8) = static_cast<uint16_t>(Next() & 0x1F);
        ArenaAt<float>(kObject + 0x80) = RandomBool(80) ? 0.5f + RandomFloat(0.5f) : RandomFloat(0.5f);
        ArenaAt<float>(kObject + 0xB0) = RandomBool(20) ? 0.0f : RandomFloat(300.0f);
        ArenaAt<float>(kComponent + 0x134) = RandomFloat(0.3f);
        ArenaAt<float>(kFlags + 0x11C) = RandomFloat(300.0f);
        ArenaAt<uint8_t>(kInput + 0x1A0) = RandomBool(50);
        // The trick list: up to three names.
        g_array = ArenaAddress(kScratch);
        ArenaAt<uint32_t>(kScratch + 4) = Next() % 4;
        if (RandomBool(30)) { // (full-mantissa vectors)
            for (uint32_t i = 0x4C; i < 0xBC; i += 4)
                ArenaAt<uint32_t>(kObject + i) = (Next() & 0x80000000u) | 0x3F000000u | (Next() & 0x00FFFFFF);
        }
        if (RandomBool(10)) // (stopped)
            std::memset(g_physicsArena + kObject + 0xAC, 0, 12);
        if (which == 2 && RandomBool(70)) {
            // (a wall ride that may well start: long after the last, along a wall of normal about x, upright)
            ArenaAt<int>(kComponent + 0x39C) = -1000000;
            ArenaAt<int>(kComponent + 0x3A0) = -1000000;
            ArenaAt<float>(kComponent + 0x130) = 1.0f;
            ArenaAt<float>(kComponent + 0x134) = RandomFloat(0.2f);
            ArenaAt<float>(kComponent + 0x138) = RandomFloat(0.2f);
            ArenaAt<float>(kObject + 0xAC) = RandomFloat(1.0f);
            ArenaAt<float>(kObject + 0xB4) = RandomFloat(300.0f);
            ArenaAt<float>(kObject + 0x80) = 0.99f;
        }
        switch (Next() % 8) { // (values at the thresholds)
        case 0:
            ArenaAt<float>(kObject + 0x80) = 0.1f;
            break;
        case 1:
            ArenaAt<int>(kComponent + 0x4E0) = 0;
            ArenaAt<int>(kComponent + 0x39C) = 0;
            ArenaAt<int>(kComponent + 0x3A0) = 0;
            break;
        case 2: {
            static const float heights[] = {0.0f, 1.0f, 7.0f, 200.0f};
            ArenaAt<float>(kFlags + 0x11C) = heights[Next() % 4];
            break;
        }
        case 3:
            ArenaAt<float>(kObject + 0xAC) = 0.01f;
            ArenaAt<float>(kObject + 0xB0) = 0.0f;
            ArenaAt<float>(kObject + 0xB4) = 0.0f;
            break;
        case 4: // (moving along z into a wall along x: square on, and no horizontal speed left after)
        case 5:
            ArenaAt<float>(kComponent + 0x130) = 1.0f;
            ArenaAt<float>(kComponent + 0x138) = 0.0f;
            ArenaAt<float>(kObject + 0xAC) = 0.0f;
            if (Next() % 2)
                ArenaAt<float>(kObject + 0xB4) = 0.0f;
            break;
        case 6: // (a horizontal speed of exactly 0.0001 left after bouncing off a wall along z)
            ArenaAt<float>(kComponent + 0x130) = 0.0f;
            ArenaAt<float>(kComponent + 0x138) = 1.0f;
            ArenaAt<float>(kObject + 0xAC) = 0.0001f;
            ArenaAt<float>(kObject + 0xB4) = 0.0f;
            ArenaAt<float>(kObject + 0x80) = 0.9f;
            ArenaAt<uint16_t>(kComponent + 0xF8) |= 8;
            ArenaAt<int>(kComponent + 0x4E0) = -1000000;
            ArenaAt<float>(kFlags + 0x11C) = 300.0f;
            break;
        default:
            break;
        }
        bool lateArg = which == 0 && RandomBool(40);
        bool checkOnly = which == 0 && RandomBool(15);
        bool walking = which == 1;
        SaveStart();
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected, actual;
        if (which < 2) {
            expected = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(Original<bool(__thiscall*)(void*, bool, bool, bool)>(0x00507500)(
                    component, lateArg, checkOnly, walking));
            });
            actual = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(Ours(component)->WallPlant(lateArg, checkOnly, walking));
            });
            planted += Called(actual, 0x00522370);
            late += Called(actual, 0x00401C30);
        } else {
            expected = RunOnArena(seed, [](uint8_t* component) {
                return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x00508140)(component));
            });
            actual = RunOnArena(seed, [](uint8_t* component) {
                return static_cast<uint32_t>(Ours(component)->StartWallRide());
            });
            rode += Called(actual, 0x004FDED0);
        }
        same = Compare(expected, actual, which < 2 ? "WallPlant" : "StartWallRide", variant);
    }
    GameVariable<int>(0x00696D60) = savedOffset;
    g_fixedOut = false;
    bool covered = planted > 100 && late > 50 && rode > 50;
    if (!covered)
        Log("  physics wallplant coverage: planted %d, late %d, wall rides %d", planted, late, rode);
    Check(same && covered, "skater physics: WallPlant, StartWallRide");
}

} // namespace

SELFTEST(Early, TestSkaterPhysicsWallplant);
