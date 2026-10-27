// Self-tests for the skater core physics component's life and state functions (core_physics_life.cpp): original vs
// ours on a stand-in component, every callee into game code stood in for (skater_core_testkit.h).

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "game/core/dbg.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/obj/control_pad.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/physics_control.h"
#include "game/skater/skater.h"
#include "game/skater/skater_state.h"
#include "game/skater/trick_component.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace Skater {
void __cdecl TrackingPrint(int index, const Mth::Vector* a, const Mth::Vector* b);
}

namespace {

using namespace SkaterCoreKit;
using Skater::CorePhysicsComponent;

std::vector<StandIn> LifeStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00580400, 0, 3, Ret::Float, false, 0, {Ours(&Obj::GetPhysicsFloat)}},
        {0x00478FB0, 0, 1, Ret::Int, false, 0,
         {Ours(static_cast<uint32_t (*)(const char*)>(&Script::GetSymbolValue))}},
        {0x0051FB90, 12, 3, Ret::Bool, true, 6, {Ours(&Obj::CTrickComponent::CheckReleasePress)}},
        {0x005196A0, 4, 1, Ret::None, true, 1, {Ours(&Obj::CSkaterStateComponent::SetTerrain)}},
        {0x00519660, 4, 1, Ret::None, true, 0, {Ours(&Obj::CSkaterStateComponent::SetState)}},
        {0x00478F60, 0, 1, Ret::Bool, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}},
        {0x00401C30, 0, 14, Ret::Int, false, 0, {Ours(&Dbg::Printf)}},
        {0x0040AB40, 0, 0, Ret::None, true, 0, {Ours(&Nx::CCollCache::Destruct)}},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
        {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame&, Result& r) { r.eax = 0x51000000u + (Next() & 0xFFFF); }},
        {0x00460A20, 12, 3, Ret::None, true, 0, {Ours(&Obj::CObject::BroadcastEvent)}},
        {0x005232A0, 4, 1, Ret::None, true, 1, {Ours(&Obj::CTrickComponent::SetTrickObjects)}},
        {0x0046B7E0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CControlButton::ClearTrigger)}},
    };
}

void FillLife()
{
    FillArena();
    // The state component's flags (+0x20 + 8 each, their times after), its state, the old state, the lip position.
    for (uint32_t i = 0; i < 24; ++i) {
        ArenaAt<uint8_t>(kFlags + 0x20 + 8 * i) = RandomBool(50);
        ArenaAt<int>(kFlags + 0x24 + 8 * i) = PickInt();
    }
    ArenaAt<int>(kFlags + 0x18) = static_cast<int>(Next() % 6);
    ArenaAt<int>(kComponent + 0x94) = static_cast<int>(Next() % 6);
    ArenaAt<uint8_t>(kComponent + 0x4F0) = RandomBool(50);
    ArenaAt<uint8_t>(kComponent + 0x34C) = static_cast<uint8_t>(Next() % 3);
    ArenaAt<uint8_t>(kObject + 0x110) = static_cast<uint8_t>(Next() % 3);
    ArenaAt<uint8_t>(kFlags + 0x28) = static_cast<uint8_t>(Next() % 3);
    ArenaAt<uint32_t>(kComponent + 0x234) = Next() % 4 * 100;
    ArenaAt<uint32_t>(kComponent + 0x238) = Next() % 4 * 100;
    switch (Next() % 4) {
    case 0:
        for (uint32_t i = 0; i < 3; ++i)
            ArenaAt<float>(kComponent + 0x258 + 4 * i) = 0.0f;
        break;
    case 1: // only z set
        ArenaAt<float>(kComponent + 0x258) = 0.0f;
        ArenaAt<float>(kComponent + 0x25C) = 0.0f;
        break;
    default:
        break;
    }
    // A vector shorter or longer than the limit (2.5): scaled.
    float scale = Next() % 2 ? 0.3f : 1.0f;
    for (uint32_t i = 0; i < 4; ++i)
        ArenaAt<float>(kFree + 4 * i) *= scale;
    if (Next() % 4 == 0) // (exactly as long as the limit)
        ArenaAt<Mth::Vector>(kFree) = Mth::Vector(1.5f, 2.0f, 0.0f, 3.0f);
    // The physics control's state pointer, its driving byte and mode; the movable contact's stop time.
    ArenaAt<uint8_t*>(kTriggerState + 0x2C) = ArenaPtr(kFlags);
    ArenaAt<uint8_t>(kFlags + 0x118) = RandomBool(50);
    ArenaAt<int>(kFlags + 0x1C) = static_cast<int>(Next() % 3);
    ArenaAt<int>(kRide + 0x70) = PickInt();
}

void TestCoreLife()
{
    StandIns standIns(LifeStandIns());
    ClockOffsetZero clock;
    using Run = std::function<uint32_t(uint8_t*)>;
    using C = CorePhysicsComponent;
    auto self = [](uint8_t* c) { return reinterpret_cast<C*>(c); };
    struct Case {
        const char* name;
        Run original;
        Run ours;
    };
    const std::vector<Case> cases = {
        {"ZeroIfShorterThan",
         [](uint8_t*) {
             return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, float)>(0x004FC370)(ArenaPtr(kFree), 2.5f));
         },
         [](uint8_t*) { return reinterpret_cast<uint32_t>(&ArenaAt<Mth::Vector>(kFree).ZeroIfShorterThan(2.5f)); }},
        {"ProjectileButtonPressed",
         [](uint8_t* c) { return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x004FC3C0)(c)); },
         [=](uint8_t* c) { return static_cast<uint32_t>(self(c)->ProjectileButtonPressed()); }},
        {"TimeSinceStop",
         [](uint8_t*) { return static_cast<uint32_t>(Original<int(__thiscall*)(void*)>(0x004FC400)(ArenaPtr(kRide))); },
         [](uint8_t*) {
             return static_cast<uint32_t>(reinterpret_cast<Obj::CMovableContactComponent*>(ArenaPtr(kRide))->TimeSinceStop());
         }},
        {"IsWalking",
         [](uint8_t*) {
             return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x004FC420)(ArenaPtr(kTriggerState)));
         },
         [](uint8_t*) {
             return static_cast<uint32_t>(
                 reinterpret_cast<Obj::CSkaterPhysicsControlComponent*>(ArenaPtr(kTriggerState))->IsWalking());
         }},
        {"IsSwitched", [](uint8_t* c) { return static_cast<uint32_t>(Original<int(__thiscall*)(void*)>(0x004FC440)(c)); },
         [=](uint8_t* c) { return static_cast<uint32_t>(self(c)->IsSwitched()); }},
        {"SetTerrain",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*, uint32_t)>(0x004FC470)(c, 0x1234A6u);
             return 0u;
         },
         [=](uint8_t* c) {
             self(c)->SetTerrain(0x1234A6u);
             return 0u;
         }},
        {"TrackingPrint",
         [](uint8_t*) {
             Original<void(__cdecl*)(int, void*, void*)>(0x004FC4A0)(7, ArenaPtr(kFree + 0x10), ArenaPtr(kFree + 0x20));
             return 0u;
         },
         [](uint8_t*) {
             Skater::TrackingPrint(7, &ArenaAt<Mth::Vector>(kFree + 0x10), &ArenaAt<Mth::Vector>(kFree + 0x20));
             return 0u;
         }},
        {"DestroyBody",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*)>(0x004FC500)(c);
             return 0u;
         },
         [=](uint8_t* c) {
             self(c)->DestroyBody();
             return 0u;
         }},
        {"InitFromStructure",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*, void*)>(0x004FC5C0)(c, nullptr);
             return 0u;
         },
         [=](uint8_t* c) {
             self(c)->InitFromStructure(nullptr);
             return 0u;
         }},
        {"Finalize",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*)>(0x004FC690)(c);
             return 0u;
         },
         [=](uint8_t* c) {
             self(c)->Finalize();
             return 0u;
         }},
        {"LeaveSkating",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*)>(0x004FDD50)(c);
             return 0u;
         },
         [=](uint8_t* c) {
             self(c)->LeaveSkating();
             return 0u;
         }},
        {"ClearFlags",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*)>(0x004FDE40)(c);
             return 0u;
         },
         [=](uint8_t* c) {
             self(c)->ClearFlags();
             return 0u;
         }},
        {"TurnAround",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*)>(0x004FE120)(c);
             return 0u;
         },
         [=](uint8_t* c) {
             self(c)->TurnAround();
             return 0u;
         }},
        {"ResetToAir",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*, void*)>(0x004FE1D0)(c, nullptr);
             return 0u;
         },
         [=](uint8_t* c) {
             self(c)->ResetToAir(nullptr);
             return 0u;
         }},
    };
    const int kCases = static_cast<int>(cases.size());
    bool same = true;
    for (int variant = 0; variant < (kCases + 6) * 150 && same; ++variant) {
        g_rng = 0x9E3779B9u * (variant + 3);
        FillLife();
        SaveStart();
        uint32_t seed = Next() | 1;
        int which = variant % (kCases + 6);
        std::vector<uint32_t> expected, actual;
        const char* name = "SetPhysicsState";
        if (which >= kCases) {
            int state = (variant / (kCases + 6)) % 6; // each new state from each old one
            expected = RunOnArena(seed, [=](uint8_t* c) {
                Original<void(__thiscall*)(void*, int)>(0x004FDED0)(c, state);
                return 0u;
            });
            actual = RunOnArena(seed, [=](uint8_t* c) {
                self(c)->SetPhysicsState(state);
                return 0u;
            });
        } else {
            name = cases[which].name;
            expected = RunOnArena(seed, cases[which].original);
            actual = RunOnArena(seed, cases[which].ours);
        }
        same = Compare(expected, actual, name, variant);
    }
    Check(same, "skater: the core physics' life and state (DestroyBody, InitFromStructure, Finalize, SetPhysicsState...)");
}

} // namespace

SELFTEST(Early, TestCoreLife);
