// Self-tests for the skater core physics component's setup, reset, timers and speed code (core_physics_state.cpp,
// core_physics_speed.cpp): original vs ours on a stand-in component, every callee into game code replaced by a
// recording stand-in for the length of the test.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
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

uint32_t g_pointerResult;  // what a Ret::Fixed stand-in returns (a struct, the score...)

const void* RunScriptAddress()
{
    return CodeAddress(
        static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript));
}

// The stand-ins for the timers' and the component's callees.
std::vector<StandIn> StateStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, -1, -1, 0, CodeAddress(&Tmr::GameClockElapsed)},  // the clock
        {0x00478F00, 0, 1, Ret::Float, false, 0, -1, -1, 0,
         CodeAddress(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
        {0x00478F60, 0, 1, Ret::Int, false, 0, -1, -1, 0,
         CodeAddress(static_cast<int (*)(uint32_t)>(&Script::GetInteger))},
        {0x00401C30, 0, 4, Ret::Int, false, 0, -1, -1, 0, CodeAddress(&Dbg::Printf)},
        {0x0046B920, 0, 0, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Obj::CControlButton::GetPressedTime)},   // TimeSincePress
        {0x00475790, 0, 5, Ret::None, false, 8, -1, -1, 0, RunScriptAddress()},
        {0x004FC3C0, 0, 0, Ret::Bool, true, 0, -1, -1, 0},  // (a throw)
        {0x004FC420, 0, 0, Ret::Bool, true, 0, -1, -1, 0},  // (walking)
        {0x0045DB90, 4, 1, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Obj::CCompositeObject::GetComponent)},
        {0x004208D0, 0, 0, Ret::None, true, 0, -1, -1, 0},  // (the model's rotations reset)
        {0x005114F0, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00580480, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, nullptr, &g_pointerResult}, // (the score)
        {0x00409030, 0, 0, Ret::Fixed, false, 0, -1, -1, 0, CodeAddress(&Script::NewStruct), &g_pointerResult},
        {0x00476410, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Script::CStruct::Init), &g_pointerResult},
        {0x00477D40, 8, 2, Ret::None, true, 0, -1, -1, 0,
         CodeAddress(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))},
        {0x00477130, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Script::CStruct::Clear)},
        {0x00409070, 0, 1, Ret::None, false, 0, -1, -1, 0, CodeAddress(&Script::DeleteStruct)},
        {0x004FF470, 0, 0, Ret::Double, true, 0, -1, -1, 0},  // the gravity
        {0x00477C60, 8, 2, Ret::None, true, 0, -1, -1, 0,
         CodeAddress(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat)), nullptr, 2},
        {0x0043A930, 4, 1, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::Assign)},   // (a feeler copied)
        {0x0044E4F0, 0, 0, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&GameNet::Manager::InNetGame)},
        {0x004FDED0, 4, 1, Ret::None, true, 0, -1, -1, 0},  // SetState
        {0x00416DD0, 0, 0, Ret::None, true, 0, -1, -1, 0},  // (a smart pointer released)
        {0x005BABA0, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Mem::Manager::Free)},
        {0x004FC500, 0, 0, Ret::None, true, 0, -1, -1, 0},  // (the destructor body)
        {0x0045D730, 0, 0, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Obj::CBaseComponent::Construct)},
        {0x00417760, 0, 0, Ret::Int, true, 0, -1, -1, 0,
         CodeAddress(static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct))},
        {0x0040AB50, 0, 0, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Nx::CCollCache::Construct)},
        {0x00401CC0, 0, 1, Ret::Fixed, false, 0, -1, -1, 0,
         CodeAddress(static_cast<void* (*)(unsigned int)>(&Mem::Calloc)), &g_pointerResult},
        // (speed)
        {0x00580790, 4, 1, Ret::Double, true, 0, -1, -1, 0, CodeAddress(&Obj::CSkater::GetScaledPhysics)},  // StatValue
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0, CodeAddress(&Obj::GetPhysicsFloat)},  // PhysicsFloat
        {0x0042BBF0, 4, 1, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Mth::Vector::SetLength), nullptr, 1}, // SetLength (a float)
        {0x004FE1F0, 4, 1, Ret::None, true, 0, -1, -1, 0, nullptr, nullptr, 1}, // the air friction (a float)
        {0x004FE420, 0, 0, Ret::Bool, true, 0, -1, -1, 0},
        {0x004FE6A0, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00501B00, 0, 0, Ret::Bool, true, 0, -1, -1, 0},
        {0x004FE730, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00427CD0, 4, 1, Ret::Bool, true, 0, -1, -1, 0},
    };
}

// A float near `around`: below, at, above, or not a number.
float Around(float around, float spread)
{
    switch (Next() % 6) {
    case 0:
        return around;
    case 1:
        return around - spread;
    case 2:
        return around + spread;
    case 3: {
        uint32_t nan = 0x7FC00000u;
        float f;
        std::memcpy(&f, &nan, 4);
        return f;
    }
    default:
        return around + RandomFloat(spread * 2.0f);
    }
}

using Method = void (CorePhysicsComponent::*)();

struct Case {
    const char* name;
    uint32_t original;
    Method ours;
};

// The timers and the speed code: each run on random state around its thresholds.
void TestSkaterPhysicsTimers()
{
    g_rng = 1;
    StandIns standIns(StateStandIns());
    int savedOffset = GameVariable<int>(0x00696D60); // (the clock's offset: 0, so times can meet exactly)
    GameVariable<int>(0x00696D60) = 0;
    static const Case cases[] = {
        {"UpdateFreeRevert", 0x00500710, &CorePhysicsComponent::UpdateFreeRevert},
        {"UpdateFocusButton", 0x005007A0, &CorePhysicsComponent::UpdateFocusButton},
        {"UpdateProjectiles", 0x00500820, &CorePhysicsComponent::UpdateProjectiles},
        {"UpdateModelRotation", 0x00500900, &CorePhysicsComponent::UpdateModelRotation},
        {"UpdateSpecialTime", 0x00500BC0, &CorePhysicsComponent::UpdateSpecialTime},
        {"Reset", 0x00506BE0, &CorePhysicsComponent::Reset},
        {"LimitSpeed", 0x00501850, &CorePhysicsComponent::LimitSpeed},
        {"Brake", 0x005019E0, &CorePhysicsComponent::Brake},
        {"ApplyGroundFriction", 0x00506DF0, &CorePhysicsComponent::ApplyGroundFriction},
        {"StartRidingHitObject", 0x005021D0, &CorePhysicsComponent::StartRidingHitObject},
    };
    constexpr int kCases = sizeof(cases) / sizeof(cases[0]);
    int deep[kCases] = {};
    bool same = true;
    for (int variant = 0; variant < 8000 && same; ++variant) {
        int which = variant % kCases;
        g_rng = 0x9E3779B9u * (variant + 1);
        FillArena();
        uint8_t* c = g_physicsArena + kComponent;
        g_pointerResult = (Next() % 4) ? ArenaAddress(kScratch) : 0;
        for (uint32_t flag = 0x20; flag < 0xF8; flag += 8)
            ArenaAt<uint8_t>(kFlags + flag) = RandomBool(30);
        ArenaAt<int>(kFlags + 0x18) = static_cast<int>(Next() % 3);
        switch (which) {
        case 0:
            ArenaAt<int>(kComponent + 0x318) = static_cast<int>(Next() % 3);
            ArenaAt<int>(kComponent + 0x31C) = PickInt();
            break;
        case 1:
            c[0x14C1] = RandomBool(50);
            ArenaAt<uint8_t>(kInput + 0xE0) = RandomBool(60);
            break;
        case 2:
            c[0x14C8] = RandomBool(10);
            c[0x2F4] = RandomBool(90);
            ArenaAt<uint8_t>(kFlags + 0x70) = RandomBool(10);
            ArenaAt<uint8_t>(kProjectile + 0x1C) = RandomBool(10);
            ArenaAt<int>(kProjectile + 0x18) = RandomBool(10);
            ArenaAt<float>(kProjectile + 0x20) = RandomBool(85) ? 0.0f : Around(0.0f, 1.0f);
            ArenaAt<int>(kComponent + 0x14C4) = RandomBool(60) ? 0 : static_cast<int>(Next() % 3);
            ArenaAt<int>(kComponent + 0x2F8) = static_cast<int>(Next() % 3) - 1;
            break;
        case 3:
            c[0x2DB] = RandomBool(85);
            c[0x2DC] = RandomBool(50);
            ArenaAt<int>(kFlags + 0x18) = RandomBool(75) ? 1 : static_cast<int>(Next() % 3);
            ArenaAt<float>(kComponent + 0x2E8) = Around(1.0f, 0.5f);
            ArenaAt<float>(kComponent + 0x2E0) = (Next() % 2) ? Around(21.0f, 1.0f) : Around(1.0f, 1.0f);
            if (RandomBool(20)) {
                // (21 - 0.99999994: 20.00000006 unrounded - over 20 - but 20 as a float)
                ArenaAt<float>(kComponent + 0x2E0) = 21.0f;
                ArenaAt<uint32_t>(kComponent + 0x2E8) = 0x3F7FFFFFu;
            }
            break;
        case 4:
            c[0x301] = RandomBool(85);
            ArenaAt<float>(kScratch) = Around(0.0f, 0.5f);
            ArenaAt<float>(kComponent + 0x8C) = Around(0.25f, 0.25f);
            break;
        case 5:
            ArenaAt<uint8_t>(kObject + 0x110) = RandomBool(50);
            ArenaAt<uint8_t*>(kRide + 0x18) = RandomBool(50) ? g_physicsArena + kRider : nullptr;
            ArenaAt<uint32_t>(kComponent + 0x350) = RandomBool(30) ? 0xC0000000u : Next();
            break;
        case 6: {
            ArenaAt<float>(kComponent + 0x350) = RandomBool(40) ? 0.0f : Around(0.0f, 0.25f);
            if (RandomBool(20))
                ArenaAt<uint32_t>(kComponent + 0x350) = RandomBool(50) ? 0xBF800000u : 0xC0000000u;
            ArenaAt<float>(kComponent + 0x320) = RandomBool(40) ? 0.0f : Around(0.0f, 0.25f);
            ArenaAt<float>(kComponent + 0x8C) = Around(0.25f, 0.25f);
            if (RandomBool(25)) // (a timer that ends exactly)
                ArenaAt<float>(kComponent + 0x350) = ArenaAt<float>(kComponent + 0x8C);
            if (RandomBool(25))
                ArenaAt<float>(kComponent + 0x320) = ArenaAt<float>(kComponent + 0x8C);
            float* velocity = &ArenaAt<float>(kObject + 0xAC);
            for (int i = 0; i < 3; ++i)
                velocity[i] = PickFloat();
            ArenaAt<float>(kComponent + 0x354) = PickFloat();
            ArenaAt<float>(kComponent + 0x358) = PickFloat();
            break;
        }
        case 7: {
            float* velocity = &ArenaAt<float>(kObject + 0xAC);
            for (int i = 0; i < 3; ++i)
                velocity[i] = (Next() % 3) ? RandomFloat(0.2f) : PickFloat();
            ArenaAt<float>(kComponent + 0x8C) = Around(0.05f, 0.05f);
            if (RandomBool(20)) // (stopped: 0 left, and nothing to brake when the brake is 0)
                velocity[0] = velocity[1] = velocity[2] = 0.0f;
            break;
        }
        case 8:
            c[0x2D9] = RandomBool(30);
            ArenaAt<uint8_t>(kFlags + 0x70) = RandomBool(30);
            ArenaAt<float>(kComponent + 0x314) = (Next() % 2) ? PickFloat() : Around(1.0f, 1.0f);
            break;
        case 9:
            ArenaAt<uint8_t>(kFlags + 0x88) = RandomBool(30);
            break;
        }
        SaveStart();
        uint32_t seed = Next() | 1;
        const Case& test = cases[which];
        std::vector<uint32_t> expected = RunOnArena(seed, [&](uint8_t* component) {
            Original<void(__thiscall*)(void*)>(test.original)(component);
            return 0u;
        });
        std::vector<uint32_t> actual = RunOnArena(seed, [&](uint8_t* component) {
            (Ours(component)->*test.ours)();
            return 0u;
        });
        same = Compare(expected, actual, test.name, variant);
        // (a call past the first checks)
        static const uint32_t deepCalls[kCases] = {0x00401C30, 0x00475790, 0x00475790, 0x005114F0, 0x00475790,
                                                   0x005BABA0, 0x004FE1F0, 0x0042BBF0, 0x004FE730, 0x00427CD0};
        deep[which] += Called(actual, deepCalls[which]);
    }
    GameVariable<int>(0x00696D60) = savedOffset;
    bool covered = true;
    for (int i = 0; i < kCases; ++i) {
        if (deep[i] < 40) {
            Log("  physics timers coverage: %s reached its deep call %d times", cases[i].name, deep[i]);
            covered = false;
        }
    }
    Check(same && covered, "skater physics: timers, reset and speed (10 functions)");
}

// The component made (Create / Construct), destroyed, a ground feeler kept, GetAirTimeLeft.
void TestSkaterPhysicsSetup()
{
    g_rng = 7;
    StandIns standIns(StateStandIns());
    bool same = true;
    int landing = 0;
    for (int variant = 0; variant < 2000 && same; ++variant) {
        int which = variant % 5;
        g_rng = 0x7F4A7C15u * (variant + 1);
        FillArena();
        g_pointerResult = (Next() % 3) ? ArenaAddress(kScratch) : 0;
        SaveStart();
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected, actual;
        switch (which) {
        case 0:
            // (the memory: the component itself, or none; each constructs it: the original's, ours)
            g_pointerResult = (Next() % 3) ? ArenaAddress(kComponent) : 0;
            expected = RunOnArena(seed, [](uint8_t*) { return Original<uint32_t(__cdecl*)()>(0x00505440)(); });
            actual = RunOnArena(seed, [](uint8_t*) {
                return reinterpret_cast<uint32_t>(CorePhysicsComponent::Create());
            });
            break;
        case 1:
            expected = RunOnArena(seed, [](uint8_t* component) {
                return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*)>(0x00500CA0)(component));
            });
            actual = RunOnArena(seed, [](uint8_t* component) {
                return reinterpret_cast<uint32_t>(Ours(component)->Construct());
            });
            break;
        case 2: {
            unsigned int flags = Next() % 4;
            expected = RunOnArena(seed, [flags](uint8_t* component) {
                return reinterpret_cast<uint32_t>(
                    Original<void*(__thiscall*)(void*, unsigned int)>(0x00500DB0)(component, flags));
            });
            actual = RunOnArena(seed, [flags](uint8_t* component) {
                return reinterpret_cast<uint32_t>(Ours(component)->Destroy(flags));
            });
            break;
        }
        case 3:
            expected = RunOnArena(seed, [](uint8_t* component) {
                Original<void(__thiscall*)(void*, void*)>(0x00506BA0)(component, g_physicsArena + kScratch);
                return 0u;
            });
            actual = RunOnArena(seed, [](uint8_t* component) {
                Ours(component)->SetLastGroundFeeler(g_physicsArena + kScratch);
                return 0u;
            });
            break;
        case 4: {
            ArenaAt<uint8_t*>(kScratch + 0x14) = g_physicsArena + kScratch + 0x100; // (the script's parameters)
            switch (Next() % 4) {
            case 0: // (at the take-off height, not moving up: -0 / gravity)
                ArenaAt<float>(kObject + 0xB0) = 0.0f;
                ArenaAt<float>(kFlags + 0x11C) = 0.0f;
                break;
            case 1: // (far up, a take-off height with bits the subtraction loses even unrounded)
                ArenaAt<float>(kObject + 0x50) = 1.0e10f + RandomFloat(1.0e9f);
                ArenaAt<uint32_t>(kFlags + 0x11C) = 0x3F000000u | (Next() & 0x7FFFFF);
                break;
            case 2:
                ArenaAt<uint32_t>(kObject + 0xB0) = 0x7FC00000u; // (not a number)
                break;
            default:
                break;
            }
            SaveStart();
            auto* script = reinterpret_cast<Script::CScript*>(g_physicsArena + kScratch);
            expected = RunOnArena(seed, [script](uint8_t* component) {
                return static_cast<uint32_t>(Original<int(__thiscall*)(void*, void*, void*)>(0x00500DE0)(
                    component, reinterpret_cast<void*>(0x1234), script));
            });
            actual = RunOnArena(seed, [script](uint8_t* component) {
                return static_cast<uint32_t>(
                    Ours(component)->GetAirTimeLeft(reinterpret_cast<Script::CStruct*>(0x1234), script));
            });
            landing += Called(actual, 0x00477C60);
            break;
        }
        }
        same = Compare(expected, actual, "physics setup", variant);
    }
    Check(same && landing > 100, "skater physics: Create, Construct, Destroy, SetLastGroundFeeler, GetAirTimeLeft");
}

} // namespace

SELFTEST(Early, TestSkaterPhysicsTimers);
SELFTEST(Early, TestSkaterPhysicsSetup);
