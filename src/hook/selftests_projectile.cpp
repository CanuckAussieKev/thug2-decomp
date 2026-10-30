// Self-tests for the projectile component (projectile_component.cpp): original vs ours on a stand-in component, skater
// and trigger component in the arena (skater_core_testkit.h), every callee into game code stood in for; the feeler on
// the stack constructed for real.

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/obj/projectile_component.h"
#include "game/obj/smtptr.h"
#include "game/obj/trigger_component.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Obj::CProjectileComponent;

constexpr uint32_t kProjectile = kFree;          // the component (0x28 bytes)
constexpr uint32_t kSkaterObject = kFree + 0x40; // the local skater (+0x1C0 its five slots)
constexpr uint32_t kTrigger2 = kFree + 0x300;    // its trigger component
int g_localSkaterCalls;

void TestProjectileComponent()
{
    StandIns standIns({
        {0x00401CC0, 0, 1, Ret::Int, false, 0, {Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kProjectile)) : 0; }},
        {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}},
        {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}},
        {0x00566520, 0, 0, Ret::Int, true, 0, {Ours(&Mdl::Skate::GetLocalSkater)}, [](Frame&, Result& r) {
             // (InitFromStructure's always finds one: the original reads the slots without a check)
             r.eax = g_localSkaterCalls++ == 0 || Next() % 5 ? reinterpret_cast<uint32_t>(ArenaPtr(kSkaterObject)) : 0;
         }},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kTrigger2)); }},
        {0x004172F0, 8, 2, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::GetCollision)}, [](Frame& f, Result&) {
             const float* line = reinterpret_cast<const float*>(f.ecx);
             for (int i = 0; i < 8; ++i)
                 RecordFloat(line[i]);
             if (Next() % 3 == 0) // (an object hit: its smart pointer let go at the end)
                 *reinterpret_cast<void**>(f.ecx + 0x98) = ArenaPtr(kFree + 0x400);
         }},
        {0x00434BF0, 8, 2, Ret::Bool, true, 0, {Ours(&Obj::CTriggerComponent::CheckFeeler)}, [](Frame& f, Result& r) {
             Record(reinterpret_cast<const uint32_t*>(f.args[0])[0]);
             r.eax = Next() % 6 == 0;
         }},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
    });
    bool same = true;
    int rested = 0, set = 0, found = 0, none = 0;
    for (int variant = 0; variant < 3000 && same; ++variant) {
        g_rng = 0x9E3779B1u * (variant + 1);
        FillArena();
        // the object: named as one of the slots (or none), somewhere
        uint32_t names[5] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555};
        if (Next() % 3 == 0) // (two slots of one name: the first wins)
            names[Next() % 5] = names[Next() % 5];
        for (int i = 0; i < 5; ++i)
            ArenaAt<uint32_t>(kSkaterObject + 0x1C0 + i * 0x18) = names[i];
        uint32_t pick = Next() % 7;
        ArenaAt<uint32_t>(kObject + 0x14) = pick < 5 ? names[pick] : 0x12345678u;
        ArenaAt<Mth::Vector>(kObject + 0x4C) = {RandomFloat(500.0f), RandomFloat(100.0f), RandomFloat(500.0f),
                                                Next() % 2 ? 1.0f : RandomFloat(2.0f)};
        ArenaAt<uint8_t*>(kProjectile + 0x0C) = ArenaPtr(kObject);
        ArenaAt<int>(kProjectile + 0x20) = static_cast<int>(Next() % 3) - (Next() % 4 == 0 ? 3 : 0);
        ArenaAt<void*>(kProjectile + 0x24) = ArenaPtr(kTrigger2);
        SaveStart();
        uint32_t seed = Next() | 1;
        auto runAll = [](bool original) {
            uint8_t* p = ArenaPtr(kProjectile);
            g_localSkaterCalls = 0;
            uint32_t made = original ? Original<uint32_t(__cdecl*)()>(0x004F3890)()
                                     : reinterpret_cast<uint32_t>(CProjectileComponent::Create());
            Record(made);
            if (made) {
                // (Create's fields kept, the rest as set up)
                ArenaAt<uint8_t*>(kProjectile + 0x0C) = ArenaPtr(kObject);
                ArenaAt<void*>(kProjectile + 0x24) = ArenaPtr(kTrigger2);
            }
            auto* ours = reinterpret_cast<CProjectileComponent*>(p);
            if (original) {
                Original<void(__thiscall*)(void*, void*)>(0x004F3800)(p, nullptr);
                Original<void(__thiscall*)(void*)>(0x004F3860)(p);
            } else {
                ours->InitFromStructure(nullptr);
                ours->Finalize();
            }
            for (int i = -1; i < 5; ++i)
                Record(ArenaAt<uint8_t>(kSkaterObject + 0x1C4 + i * 0x18));
            Record(reinterpret_cast<uint32_t>(ArenaAt<void*>(kProjectile + 0x24)));
            if (ArenaAt<void*>(kProjectile + 0x24) == nullptr)
                ArenaAt<void*>(kProjectile + 0x24) = ArenaPtr(kTrigger2); // (the original would crash)
            for (int frame = 0; frame < 4; ++frame) {
                if (original)
                    Original<void(__thiscall*)(void*)>(0x004F3A90)(p);
                else
                    ours->Update();
            }
            if (original)
                Original<void(__thiscall*)(void*)>(0x004F3910)(p);
            else
                ours->CheckTriggers();
            if (original)
                Original<void*(__thiscall*)(void*, unsigned int)>(0x004F37D0)(p, Next() % 2);
            else
                ours->Destroy(Next() % 2);
            return 0u;
        };
        auto expected = RunOnArena(seed, [&](uint8_t*) { return runAll(true); });
        auto actual = RunOnArena(seed, [&](uint8_t*) { return runAll(false); });
        same = Compare(expected, actual, "projectile component", variant);
        int feelers = 0;
        for (size_t i = 1; i < actual.size() - kArenaSize / 4; ++i)
            feelers += actual[i] == 0x004172F0;
        rested += feelers < 5 * 8;
        set += Called(actual, 0x00434BF0);
        int slot = ArenaAt<int>(kProjectile + 0x1C);
        found += slot >= 0;
        none += slot < 0;
    }
    bool covered = rested > 100 && set > 300 && found > 300 && none > 100;
    if (!covered)
        Log("  projectile coverage: rested %d, triggers asked %d, slot found %d / none %d", rested, set, found, none);
    Check(same && covered, "obj: the projectile component (create, init, finalize, update, triggers, destroy)");
}

} // namespace

SELFTEST(Early, TestProjectileComponent);
