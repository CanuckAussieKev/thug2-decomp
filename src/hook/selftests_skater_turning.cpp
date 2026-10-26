// Self-tests for the skater core physics component's turns (core_physics_turning.cpp): original vs ours on a
// stand-in component, every callee into game code replaced by a recording stand-in for the length of the test (the
// math functions pointed at ours).

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/nx/feeler.h"
#include "game/obj/control_pad.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/core_physics.h"
#include "game/skater/manual.h"
#include "game/skater/skater.h"
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

float NotANumber()
{
    uint32_t bits = 0x7FC00000u;
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::vector<StandIn> TurningStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, -1, -1, 0, CodeAddress(&Tmr::GameClockElapsed)}, // the clock
        {0x00478F00, 0, 1, Ret::Float, false, 0, -1, -1, 0,
         CodeAddress(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
        {0x00478F60, 0, 1, Ret::Int, false, 0, -1, -1, 0,
         CodeAddress(static_cast<int (*)(uint32_t)>(&Script::GetInteger))},
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0, CodeAddress(&Obj::GetPhysicsFloat)},  // PhysicsFloat
        {0x00580790, 4, 1, Ret::Double, true, 0, -1, -1, 0, CodeAddress(&Obj::CSkater::GetScaledPhysics)},  // StatValue
        {0x0046B920, 0, 0, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Obj::CControlButton::GetPressedTime)},     // TimeSincePress
        {0x004FE420, 0, 0, Ret::Bool, true, 0, -1, -1, 0},    // (may turn on the slope)
        {0x00584CC0, 4, 1, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Obj::CSkaterCareer::GetCheat)},    // (a cheat on)
        {0x005843A0, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CSkaterCareer::SetGlobalFlag)},
        {0x00579CC0, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CBalance::Update)},    // (the balance meter)
        {0x004F8F20, 0, 0, Ret::Double, true, 0, -1, -1, 0},  // (how far off balance)
        {0x00564BE0, 4, 1, Ret::None, true, 0, -1, -1, 0},    // (the rotation shown)
        {0x004172F0, 8, 2, Ret::Bool, true, 3, -1, -1, 16, CodeAddress(&Nx::CFeeler::GetCollision), nullptr, 0, 0,
         0x50}, // (the hit's flags, distance, point and normal filled in)
    };
}

using Method = void (CorePhysicsComponent::*)();

struct Case {
    const char* name;
    uint32_t original;
    Method ours;
};

void TestSkaterPhysicsTurning()
{
    g_rng = 11;
    StandIns standIns(TurningStandIns());
    OurMath math;
    int savedOffset = GameVariable<int>(0x00696D60);
    GameVariable<int>(0x00696D60) = 0;
    void* savedCareer = GameVariable<void*>(0x007CE478);
    uint8_t savedCheat = GameVariable<uint8_t>(0x007CE4FD);
    static const Case cases[] = {
        {"TurnForRailJump", 0x005002D0, &CorePhysicsComponent::TurnForRailJump},
        {"TurnOnGround", 0x00503560, &CorePhysicsComponent::TurnOnGround},
        {"NatasSpin", 0x00504730, &CorePhysicsComponent::NatasSpin},
        {"LeanInAir", 0x00504910, &CorePhysicsComponent::LeanInAir},
        {"RotateInAcidDrop", 0x00504A60, &CorePhysicsComponent::RotateInAcidDrop},
        {"RecoverInAir", 0x00504B40, &CorePhysicsComponent::RecoverInAir},
        {"KeepUpright", 0x00504DF0, &CorePhysicsComponent::KeepUpright},
        {"TurnDownSlope", 0x00501B00, nullptr},
        {"BounceOffWall", 0x005039F0, nullptr},
    };
    constexpr int kCases = sizeof(cases) / sizeof(cases[0]);
    int moved[kCases] = {};
    bool same = true;
    for (int variant = 0; variant < 27000 && same; ++variant) {
        int which = variant % kCases;
        g_rng = 0x3C6EF372u * (variant + 1);
        FillArena();
        uint8_t* c = g_physicsArena + kComponent;
        for (uint32_t flag = 0x20; flag < 0xF8; flag += 8)
            ArenaAt<uint8_t>(kFlags + flag) = RandomBool(25);
        // (a career object for the cheat check: +0x20)
        ArenaAt<uint32_t>(kScratch + 0x20) = ArenaAddress(kScratch + 0x100);
        GameVariable<void*>(0x007CE478) = g_physicsArena + kScratch;
        ArenaAt<float>(kComponent + 0x8C) = (Next() % 2) ? 1.0f / 60.0f : RandomFloat(0.5f);
        // Unit-ish rows with full mantissas sometimes.
        if (RandomBool(30)) {
            for (uint32_t i = 0x6C; i < 0xBC; i += 4)
                ArenaAt<uint32_t>(kObject + i) = (Next() & 0x80000000u) | 0x3E000000u | (Next() & 0x01FFFFFF);
        }
        switch (which) {
        case 0:
        case 1:
            for (uint32_t b : {0x40u, 0x60u, 0x80u, 0xA0u})
                ArenaAt<uint8_t>(kInput + b) = RandomBool(40);
            c[0x388] = RandomBool(30);
            for (uint32_t i = 0xAC; i < 0xB8; i += 4) // (around a speed of 10: the sharp turn ramps up below)
                ArenaAt<float>(kObject + i) = RandomFloat(RandomBool(50) ? 4.0f : 9.0f);
            break;
        case 2:
            c[0x1500] = RandomBool(50);
            ArenaAt<float>(kSkater + 0x13A4) = (Next() % 2) ? RandomFloat(720.0f) : RandomFloat(400.0f) + 340.0f;
            if (RandomBool(20)) { // (exactly a turn, not spinning: with a slop of 0.1, right at the snap)
                ArenaAt<float>(kSkater + 0x13A4) = RandomBool(50) ? 360.0f : -360.0f;
                ArenaAt<float>(kComponent + 0x8C) = 0.0f;
            }
            break;
        case 3:
            for (uint32_t b : {0x20u, 0x40u, 0x160u, 0x180u})
                ArenaAt<uint8_t>(kInput + b) = RandomBool(b < 0x100 ? 50 : 10);
            for (uint32_t f : {0x30u, 0x78u, 0xC0u})
                ArenaAt<uint8_t>(kFlags + f) = RandomBool(10);
            break;
        case 4:
            ArenaAt<uint8_t>(kFlags + 0x78) = RandomBool(80);
            ArenaAt<float>(kComponent + 0x48C) = (Next() % 2) ? 1.0f : RandomFloat(2.0f);
            ArenaAt<float>(kComponent + 0x488) = RandomFloat(1.5f);
            break;
        case 5:
            ArenaAt<uint8_t>(kFlags + 0x78) = RandomBool(10);
            ArenaAt<uint8_t*>(kRide + 0x18) = RandomBool(50) ? g_physicsArena + kRider : nullptr;
            ArenaAt<float>(kObject + 0x80) = (Next() % 2) ? RandomFloat(1.0f) : 0.9f;
            break;
        case 6:
            ArenaAt<float>(kObject + 0x90) = (Next() % 3) ? RandomFloat(1.0f) : 0.95f;
            if (RandomBool(5))
                ArenaAt<float>(kObject + 0x90) = NotANumber();
            break;
        case 8:
            if (RandomBool(30)) {
                // (the side row along x, the normal a hair against it: an angle that rounds to 90 degrees)
                for (uint32_t i = 0x6C; i < 0x7C; i += 4)
                    ArenaAt<float>(kObject + i) = i == 0x6C ? 1.0f : 0.0f;
                for (uint32_t i = 0; i < 16; i += 4)
                    ArenaAt<float>(kScratch + 0x200 + i) = 0.0f;
                ArenaAt<float>(kScratch + 0x200) = -1.0e-8f * static_cast<float>(1 + Next() % 10);
            }
            break;
        default:
            break;
        }
        SaveStart();
        uint32_t seed = Next() | 1;
        const Case& test = cases[which];
        std::vector<uint32_t> expected, actual;
        uint32_t displayBefore[16];
        std::memcpy(displayBefore, g_physicsArena + kComponent + 0x18, sizeof(displayBefore));
        if (which == 7) {
            expected = RunOnArena(seed, [](uint8_t* component) {
                return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x00501B00)(component));
            });
            actual = RunOnArena(seed, [](uint8_t* component) { return static_cast<uint32_t>(Ours(component)->TurnDownSlope()); });
        } else if (which == 8) {
            const Mth::Vector* normal = &ArenaAt<Mth::Vector>(kScratch + 0x200);
            float* turn = &ArenaAt<float>(kScratch + 0x210);
            float scale = (Next() % 2) ? 1.0f : RandomFloat(2.0f);
            expected = RunOnArena(seed, [=](uint8_t* component) {
                float r = Original<float(__thiscall*)(void*, const void*, float*, float)>(0x005039F0)(component, normal,
                                                                                                    turn, scale);
                uint32_t bits;
                std::memcpy(&bits, &r, 4);
                return bits;
            });
            actual = RunOnArena(seed, [=](uint8_t* component) {
                float r = Ours(component)->BounceOffWall(normal, turn, scale);
                uint32_t bits;
                std::memcpy(&bits, &r, 4);
                return bits;
            });
        } else {
            expected = RunOnArena(seed, [&](uint8_t* component) {
                Original<void(__thiscall*)(void*)>(test.original)(component);
                return 0u;
            });
            actual = RunOnArena(seed, [&](uint8_t* component) {
                (Ours(component)->*test.ours)();
                return 0u;
            });
        }
        // (moved: the display matrix changed)
        {
            bool changed = false;
            for (int i = 0; i < 16; ++i)
                changed |= actual[actual.size() - kArenaSize / 4 + 0x18 / 4 + i] != displayBefore[i];
            moved[which] += changed;
        }
        same = Compare(expected, actual, test.name, variant);
    }
    GameVariable<int>(0x00696D60) = savedOffset;
    GameVariable<void*>(0x007CE478) = savedCareer;
    GameVariable<uint8_t>(0x007CE4FD) = savedCheat;
    bool covered = true;
    for (int i = 0; i < kCases; ++i) {
        if (moved[i] < 100) {
            Log("  physics turning coverage: %s turned %d times", cases[i].name, moved[i]);
            covered = false;
        }
    }
    Check(same && covered, "skater physics: turning (9 functions)");
}

} // namespace

SELFTEST(Early, TestSkaterPhysicsTurning);
