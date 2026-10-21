// Self-tests for the skater core physics component's walls and ceilings (core_physics_walls.cpp) and its grind trick
// (core_physics_state.cpp): original vs ours on a stand-in component, every callee into game code replaced by a
// recording stand-in for the length of the test (the math functions pointed at ours).

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/selftests_skater_physics_harness.h"

using namespace SkaterPhysicsTest;
using Skater::CorePhysicsComponent;

namespace {

CorePhysicsComponent* Ours(uint8_t* component) { return reinterpret_cast<CorePhysicsComponent*>(component); }

uint32_t g_pointer; // what Ret::Fixed stand-ins return

const void* RunScriptAddress()
{
    return CodeAddress(
        static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript));
}

std::vector<StandIn> WallStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, -1, -1, 0}, // the clock
        {0x00478F00, 0, 1, Ret::Float, false, 0, -1, -1, 0,
         CodeAddress(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
        {0x00478F60, 0, 1, Ret::Int, false, 0, -1, -1, 0,
         CodeAddress(static_cast<int (*)(uint32_t)>(&Script::GetInteger))},
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0}, // PhysicsFloat
        {0x00580790, 4, 1, Ret::Double, true, 0, -1, -1, 0}, // StatValue
        {0x0046B920, 0, 0, Ret::Int, true, 0, -1, -1, 0},    // TimeSincePress
        {0x004171D0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::SetLine)},
        {0x004170F0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::SetIgnore), nullptr, 0, 3},
        {0x004172F0, 8, 2, Ret::Bool, true, 3, -1, -1, 9, CodeAddress(&Nx::CFeeler::GetCollision), nullptr, 0, 0,
         0x74, 0, 8}, // (the line recorded; the hit's distance, point and normal filled in)
        {0x0051ECF0, 4, 1, Ret::Bool, true, 0, -1, -1, 0},   // (a button pressed)
        {0x0051F6B0, 12, 3, Ret::Bool, true, 0, -1, -1, 0},  // (... within a time)
        {0x0051DDF0, 12, 3, Ret::Bool, true, 0, -1, -1, 0},  // (a trick allowed)
        {0x00434BF0, 8, 2, Ret::None, true, 0, -1, -1, 0},   // a trigger check
        {0x004609F0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CObject::SelfEvent)},
        {0x00522370, 4, 1, Ret::None, true, 0, -1, -1, 0},
        {0x004FF670, 0, 0, Ret::Bool, true, 0, -1, -1, 0},
        {0x00518DD0, 8, 2, Ret::None, true, 0, -1, -1, 0, nullptr, nullptr, 1},
        {0x00475790, 0, 5, Ret::None, false, 8, -1, -1, 0, RunScriptAddress()},
        {0x0042BBF0, 4, 1, Ret::Int, true, 0, -1, -1, 0, nullptr, nullptr, 1}, // SetLength
        // (the grind trick)
        {0x00479070, 0, 1, Ret::Fixed, false, 0, -1, -1, 0,
         CodeAddress(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray)), &g_pointer},
        {0x0046CC30, 4, 1, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Script::CArray::GetElement), &g_pointer},
        {0x0045DB90, 4, 1, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Obj::CCompositeObject::GetComponent),
         &g_pointer},
        {0x00511570, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x004747A0, 12, 3, Ret::None, true, 0, -1, -1, 0,
         CodeAddress(static_cast<void (Script::CScript::*)(uint32_t, Script::CStruct*, Obj::CObject*)>(
             &Script::CScript::SetScript))},
        {0x004750B0, 0, 0, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Script::CScript::Update)},
        {0x005BA3D0, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Mem::Pool::Allocate), &g_pointer},
        {0x005BAFC0, 12, 3, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Mem::Manager::Allocate), &g_pointer},
        {0x004738E0, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Script::CScript::Construct), &g_pointer},
    };
}

using Method = void (CorePhysicsComponent::*)();
using Test = bool (CorePhysicsComponent::*)();

struct Case {
    const char* name;
    uint32_t original;
};

void TestSkaterPhysicsWalls()
{
    g_rng = 13;
    StandIns standIns(WallStandIns());
    OurMath math;
    int savedOffset = GameVariable<int>(0x00696D60);
    GameVariable<int>(0x00696D60) = 0;
    uint8_t savedPools[16];
    std::memcpy(savedPools, &GameVariable<uint8_t>(0x006F3548), sizeof(savedPools));
    bool savedReady = GameVariable<bool>(0x007CC979);
    GameVariable<bool>(0x007CC979) = true;
    static const Case cases[] = {
        {"VertWallplant", 0x00503710},       {"LeanPushOffWall", 0x00503B30},  {"KeepOffCeiling", 0x00504EE0},
        {"KeepOffCeilingAtWall", 0x005051B0}, {"CheckWallAhead", 0x00506E50},   {"BounceOffWallAhead", 0x005059D0},
        {"StartGrindTrick", 0x005004B0},
    };
    constexpr int kCases = sizeof(cases) / sizeof(cases[0]);
    int deep[kCases] = {};
    bool same = true;
    for (int variant = 0; variant < 36000 && same; ++variant) {
        int which = variant % (kCases + 5);
        if (which >= kCases) // (the wall plant's checks are many: tried more often)
            which = 0;
        g_rng = 0x5851F42Du * (variant + 1);
        FillArena();
        g_fixedOut = false;
        g_pointer = ArenaAddress(kScratch);
        uint8_t* c = g_physicsArena + kComponent;
        for (uint32_t flag = 0x20; flag < 0xF8; flag += 8)
            ArenaAt<uint8_t>(kFlags + flag) = RandomBool(60);
        ArenaAt<uint16_t>(kComponent + 0xF8) = static_cast<uint16_t>(Next() & 0x1F);
        ArenaAt<float>(kComponent + 0x8C) = (Next() % 2) ? 1.0f / 60.0f : RandomFloat(0.5f);
        if (RandomBool(30)) { // (full-mantissa vectors: the roundings show)
            for (uint32_t i = 0x4C; i < 0xBC; i += 4)
                ArenaAt<uint32_t>(kObject + i) = (Next() & 0x80000000u) | 0x3F000000u | (Next() & 0x00FFFFFF);
        }
        std::vector<uint32_t> expected, actual;
        uint32_t seed = Next() | 1;
        switch (which) {
        case 0:
            c[0x2ED] = RandomBool(10);
            c[0x4E8] = RandomBool(10);
            ArenaAt<float>(kSkater + 0x13A4) = RandomBool(80) ? 0.0f : RandomFloat(10.0f);
            ArenaAt<float>(kFlags + 0x11C) = RandomBool(50) ? 60.0f + RandomFloat(10.0f) : 60.0f;
            ArenaAt<float>(kObject + 0xB0) = RandomFloat(300.0f);
            ArenaAt<float>(kComponent + 0x134) = RandomFloat(0.2f);
            ArenaAt<uint8_t>(kState + 0x26) = RandomBool(20);
            ArenaAt<uint8_t>(kFlags + 0x30) = RandomBool(85);
            ArenaAt<uint8_t>(kFlags + 0xD8) = RandomBool(85);
            if (RandomBool(50))
                ArenaAt<uint16_t>(kComponent + 0xF8) |= 8;
            if (RandomBool(15))
                ArenaAt<float>(kObject + 0xB0) = 0.0f;
            if (RandomBool(30)) {
                // (the hit at 2, 3, 1: the skater exactly 7 from it)
                g_fixedOut = true;
                ArenaAt<float>(kObject + 0x4C) = 2.0f;
                ArenaAt<float>(kObject + 0x50) = 3.0f;
                ArenaAt<float>(kObject + 0x54) = 8.0f;
            }
            break;
        case 1:
            for (uint32_t b : {0x60u, 0x80u})
                ArenaAt<uint8_t>(kInput + b) = RandomBool(50);
            for (uint32_t i = 0xAC; i < 0xB8; i += 4)
                ArenaAt<float>(kObject + i) = RandomFloat(RandomBool(50) ? 0.2f : 1.0f);
            if (RandomBool(20))
                std::memset(g_physicsArena + kObject + 0xAC, 0, 12);
            break;
        case 2:
        case 3:
            ArenaAt<int>(kComponent + 0x4E0) = PickInt();
            if (RandomBool(30)) // (a ceiling: a normal pointing down)
                ArenaAt<float>(kComponent + 0x134) = RandomBool(50) ? -0.1f : RandomFloat(1.0f) - 1.0f;
            break;
        case 4:
            c[0x228] = RandomBool(60);
            ArenaAt<uint8_t>(kState + 0x26) = RandomBool(20);
            if (RandomBool(10)) // (not moving)
                std::memcpy(g_physicsArena + kObject + 0x5C, g_physicsArena + kObject + 0x4C, 16);
            if (RandomBool(5)) // (only w moved)
                std::memcpy(g_physicsArena + kObject + 0x5C, g_physicsArena + kObject + 0x4C, 12);
            if (RandomBool(30)) {
                // (the hit's normal 2, 3, 1: exactly 0.01 into a ground normal of 0, 0, 0.01)
                g_fixedOut = true;
                ArenaAt<float>(kComponent + 0x68) = 0.0f;
                ArenaAt<float>(kComponent + 0x6C) = 0.0f;
                ArenaAt<float>(kComponent + 0x70) = 0.01f;
            }
            break;
        case 5:
            c[0x2ED] = RandomBool(20);
            ArenaAt<uint8_t>(kObject + 0xBC) = RandomBool(70);
            for (uint32_t i = 0xAC; i < 0xB8; i += 4)
                ArenaAt<float>(kObject + i) = RandomFloat(300.0f);
            if (RandomBool(15))
                std::memset(g_physicsArena + kObject + 0xAC, 0, 12);
            ArenaAt<uint8_t>(kFlags + 0x28) = static_cast<uint8_t>(Next() % 3);
            break;
        case 6:
            c[0x302] = RandomBool(20);
            ArenaAt<uint32_t>(kObject + 0x10) = RandomBool(70) ? ArenaAddress(kScratch + 0x100) : 0;
            // (the script pools: the current one's pool there or not)
            GameVariable<int>(0x006F3548) = static_cast<int>(Next() % 2);
            GameVariable<uint32_t>(0x006F3550) = RandomBool(50) ? ArenaAddress(kScratch + 0x300) : 0;
            GameVariable<uint32_t>(0x006F3554) = RandomBool(50) ? ArenaAddress(kScratch + 0x340) : 0;
            break;
        }
        SaveStart();
        const Case& test = cases[which];
        const Mth::Vector* normal = &ArenaAt<Mth::Vector>(kScratch + 0x200);
        uint32_t railType = static_cast<uint32_t>(Next() % 12);
        uint32_t buttons = Next() % 16;
        auto runOriginal = [&](uint8_t* component) -> uint32_t {
            switch (which) {
            case 0:
            case 2:
            case 3:
                return Original<bool(__thiscall*)(void*)>(test.original)(component);
            case 5:
                Original<void(__thiscall*)(void*, const void*)>(test.original)(component, normal);
                return 0;
            case 6:
                Original<void(__thiscall*)(void*, uint32_t, bool, bool, bool, bool)>(test.original)(
                    component, railType, buttons & 1, (buttons & 2) != 0, (buttons & 4) != 0, (buttons & 8) != 0);
                return 0;
            default:
                Original<void(__thiscall*)(void*)>(test.original)(component);
                return 0;
            }
        };
        auto runOurs = [&](uint8_t* component) -> uint32_t {
            CorePhysicsComponent* ours = Ours(component);
            switch (which) {
            case 0:
                return ours->VertWallplant();
            case 1:
                ours->LeanPushOffWall();
                return 0;
            case 2:
                return ours->KeepOffCeiling();
            case 3:
                return ours->KeepOffCeilingAtWall();
            case 4:
                ours->CheckWallAhead();
                return 0;
            case 5:
                ours->BounceOffWallAhead(normal);
                return 0;
            default:
                ours->StartGrindTrick(railType, buttons & 1, (buttons & 2) != 0, (buttons & 4) != 0, (buttons & 8) != 0);
                return 0;
            }
        };
        expected = RunOnArena(seed, runOriginal);
        actual = RunOnArena(seed, runOurs);
        static const uint32_t deepCalls[kCases] = {0x00522370, 0x004172F0, 0x004172F0, 0x004172F0,
                                                   0x00434BF0, 0x004172F0, 0x004750B0};
        deep[which] += Called(actual, deepCalls[which]);
        same = Compare(expected, actual, test.name, variant);
    }
    GameVariable<int>(0x00696D60) = savedOffset;
    std::memcpy(&GameVariable<uint8_t>(0x006F3548), savedPools, sizeof(savedPools));
    GameVariable<bool>(0x007CC979) = savedReady;
    g_fixedOut = false;
    bool covered = true;
    for (int i = 0; i < kCases; ++i) {
        if (deep[i] < 60) {
            Log("  physics walls coverage: %s reached its deep call %d times", cases[i].name, deep[i]);
            covered = false;
        }
    }
    Check(same && covered, "skater physics: walls, ceilings, the grind trick (7 functions)");
}

} // namespace

SELFTEST(Early, TestSkaterPhysicsWalls);
