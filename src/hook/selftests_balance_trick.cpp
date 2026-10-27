// Self-tests for the skater's balance trick component (balance_trick.cpp): original vs ours on a stand-in component in
// skater_core_testkit's arena, every callee stood in for - the four balances' methods (manual.cpp), the structures'
// getters (values chosen per variant), the skater's scaled values, the memory manager.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/score.h"
#include "game/skater/balance_trick.h"
#include "game/skater/manual.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Obj::CBalance;
using Obj::CSkaterBalanceTrickComponent;

constexpr uint32_t kBT = kFree;                   // the component (0x188 bytes)
constexpr uint32_t kParamsArea = kFree + 0x200;   // the parameters (a stand-in structure)
constexpr uint32_t kOwnParams = kFree + 0x240;    // DoBalanceTrick's own structure (from the stand-in manager)
constexpr uint32_t kHeap = kFree + 0x280;         // the stand-in manager's memory (bump)
constexpr uint32_t kScoreArea = kFree + 0x380;
constexpr uint32_t kStructs = kFree + 0x400;      // structures handed out by the getters

uint32_t g_bump;
uint32_t g_types[2];
bool g_has[16];
float g_floats[6];

std::vector<StandIn> BalanceStandIns()
{
    auto self = [](Frame& f, Result& r) { r.eax = f.ecx; };
    return {
        {0x00401CC0, 0, 1, Ret::Int, false, 0, {Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kBT)) : 0; }},
        {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}},
        {0x005BAFC0, 12, 3, Ret::Int, true, 0, {Ours(&Mem::Manager::Allocate)}, [](Frame& f, Result& r) {
             r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kHeap + g_bump));
             g_bump += (f.args[0] + 15) & ~15u;
         }},
        {0x004E1950, 0, 0, Ret::Int, false, 0, {Ours(Mem::EnsureManager)}},
        {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame& f, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(f.args[0] == 0xD262802F ? kScoreArea : kFree + 0x3C0)); }},
        {0x00563370, 8, 2, Ret::None, true, 1, {Ours(&Mdl::Score::SetBalanceMeter)}},
        {0x00563390, 8, 2, Ret::None, true, 1, {Ours(&Mdl::Score::SetManualMeter)}},
        // the balances
        {0x00579950, 0, 0, Ret::Int, true, 0, {Ours(&CBalance::Construct)}, self},
        {0x00579850, 0, 0, Ret::None, true, 0, {Ours(&CBalance::DestroyBody)}},
        {0x00579860, 4, 1, Ret::None, true, 0, {Ours(&CBalance::Init)}},
        {0x005798A0, 0, 0, Ret::None, true, 0, {Ours(&CBalance::Reset)}},
        {0x005798F0, 0, 0, Ret::None, true, 0, {Ours(&CBalance::ClearBestTime)}},
        {0x00579900, 0, 0, Ret::None, true, 0, {Ours(&CBalance::HideDisplay)}},
        {0x00579910, 0, 0, Ret::None, true, 0, {Ours(&CBalance::ShowDisplay)}},
        {0x00579920, 0, 0, Ret::None, true, 0, {Ours(&CBalance::KeepBestTime)}},
        {0x00579940, 0, 0, Ret::None, true, 0, {Ours(&CBalance::ShowMeter)}},
        {0x005799D0, 0, 0, Ret::None, true, 0, {Ours(&CBalance::Stop)}},
        {0x00579A00, 20, 5, Ret::None, true, 0x18, {Ours(&CBalance::Start)}},
        // scripts
        {0x0046F060, 0, 3, Ret::None, false, 0, {Ours(Script::RegisterMemberFunctionTable)}},
        {0x005807F0, 8, 2, Ret::Float, true, 0, {Ours(&Obj::CSkater::GetScaledParam)}},
        {0x005805B0, 4, 1, Ret::Float, true, 0, {Ours(&Obj::CSkater::GetScaledValue)}},
        {0x00479020, 0, 1, Ret::Int, false, 0,
         {Ours(static_cast<Script::CStruct* (*)(uint32_t)>(&Script::GetStructure))},
         [](Frame& f, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kStructs + (f.args[0] & 0x3C))); }},
        {0x00476410, 0, 0, Ret::Int, true, 0, {Ours(&Script::CStruct::Init)}, self},
        {0x00477090, 0, 0, Ret::None, true, 0, {Ours(&Script::CStruct::Clear)}},
        {0x00478540, 4, 1, Ret::None, true, 0, {Ours(&Script::CStruct::AppendStructureIfAny)}},
        {0x004766D0, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(&Script::CStruct::GetFloat))},
         [](Frame& f, Result& r) {
             int i = static_cast<int>(f.args[0] % 6);
             r.eax = g_has[i];
             if (r.eax)
                 *reinterpret_cast<float*>(f.args[1]) = g_floats[i];
         }},
        {0x00476630, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(&Script::CStruct::GetInteger))},
         [](Frame& f, Result& r) {
             r.eax = g_has[6 + (f.args[0] & 1)];
             if (r.eax)
                 *reinterpret_cast<int*>(f.args[1]) = 3 + static_cast<int>(f.args[0] & 7);
         }},
        {0x00476950, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
         [](Frame& f, Result& r) {
             bool type = f.args[0] == 0x7321A8D6;
             r.eax = g_has[8 + (f.args[0] & 1)];
             if (r.eax)
                 *reinterpret_cast<uint32_t*>(f.args[1]) = type ? g_types[0] : f.args[0] ^ 0x5A5A;
         }},
        {0x00476B40, 4, 1, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))}},
        {0x004767C0, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CStruct**, int)>(&Script::CStruct::GetStructure))},
         [](Frame& f, Result& r) {
             r.eax = g_has[10 + (f.args[0] & 3)];
             if (r.eax)
                 *reinterpret_cast<uint32_t*>(f.args[1]) =
                     reinterpret_cast<uint32_t>(ArenaPtr(kStructs + 0x40 + (f.args[0] & 0x1C)));
         }},
        {0x004738E0, 0, 0, Ret::Int, true, 0, {Ours(&Script::CScript::Construct)}, self},
        {0x00419BF0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CAnimBlenderComponent::FirstChannel47E450)},
         [](Frame& f, Result&) {
             const float* v = reinterpret_cast<const float*>(f.args[0]);
             for (int i = 0; i < 5; ++i)
                 RecordFloat(v[i]);
         }},
    };
}

constexpr uint32_t kNames[] = {0x78E669AB, 0x73727701, 0x6CC475B7, 0x7A0DCD4B, 0xEA6D0EFD,
                               0xE553A5B8, 0x7E92670C, 0x7E92670C, 0x7E92670C, 0x12345678};
constexpr uint32_t kTypes[] = {0xEF24413B, 0x0AC90769, 0x255ED86F, 0x8D10119D, 0x3506CE64, 0xA549B57B, 0x777, 0};

void TestBalanceTrick()
{
    StandIns standIns(BalanceStandIns());
    Mem::PoolPair savedStructs = GameVariable<Mem::PoolPair>(0x006F3614);
    Mem::PoolPair savedScripts = GameVariable<Mem::PoolPair>(0x006F3548);
    bool same = true;
    for (int variant = 0; variant < 8000 && same; ++variant) {
        g_rng = 0x3C6EF372u * (variant + 1);
        FillArena();
        auto* bt = reinterpret_cast<CSkaterBalanceTrickComponent*>(ArenaPtr(kBT));
        ArenaAt<uint8_t*>(kBT + 0x0C) = ArenaPtr(kObject);
        ArenaAt<uint32_t>(kBT + 0x18) = kTypes[Next() % 8];
        ArenaAt<uint8_t>(kBT + 0x1C) = RandomBool(50);
        ArenaAt<uint8_t*>(kBT + 0x20) = Next() % 2 ? ArenaPtr(kOwnParams) : nullptr;
        ArenaAt<uint8_t*>(kBT + 0x184) = ArenaPtr(kFree + 0x3C0);
        ArenaAt<uint8_t*>(kScoreArea + 0x18) = ArenaPtr(kScoreArea + 0x20);
        ArenaAt<uint8_t*>(kObject + 0x10) = Next() % 2 ? ArenaPtr(kFree + 0x3E0) : nullptr;
        g_types[0] = kTypes[Next() % 8];
        if (Next() % 4 == 0)
            g_types[0] = ArenaAt<uint32_t>(kBT + 0x18);
        for (bool& h : g_has)
            h = Next() % 4 != 0;
        for (float& v : g_floats)
            v = Next() % 3 ? RandomFloat(3.0f) : 0.0f;
        // a balance's lean / speed / time of either sign (or zero)
        for (uint32_t b : {0x24u, 0x7Cu, 0xD4u, 0x12Cu}) {
            ArenaAt<float>(kBT + b + 0x28) = Next() % 4 ? RandomFloat(2.0f) : 0.0f;
            ArenaAt<float>(kBT + b + 0x30) = Next() % 4 ? RandomFloat(40.0f) : (Next() % 2 ? 0.0f : -0.0f);
            ArenaAt<float>(kBT + b + 0x34) = Next() % 4 ? RandomFloat(9.0f) : 0.0f;
        }
        int op = static_cast<int>(Next() % 19);
        if (op == 3 && ArenaAt<uint32_t>(kBT + 0x18) == 0x777) // (AdjustBalance on an unknown type crashes the original)
            ArenaAt<uint32_t>(kBT + 0x18) = kTypes[Next() % 6];
        uint32_t arg = Next();
        SaveStart();
        uint32_t seed = Next() | 1;
        auto runOp = [&](bool o) -> uint32_t {
            g_bump = 0;
            Mem::PoolPair none = {};
            GameVariable<Mem::PoolPair>(0x006F3614) = none;
            GameVariable<Mem::PoolPair>(0x006F3548) = none;
            auto* params = reinterpret_cast<Script::CStruct*>(ArenaPtr(kParamsArea));
            if (op < 10) {
                return static_cast<uint32_t>(
                    o ? Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x004F9420)(bt, kNames[op], params,
                                                                                             nullptr)
                      : bt->CallMemberFunction(kNames[op], params, nullptr));
            }
            switch (op) {
            case 10: {
                uint32_t made = o ? Original<uint32_t(__cdecl*)()>(0x004F9170)()
                                  : reinterpret_cast<uint32_t>(CSkaterBalanceTrickComponent::Create());
                Record(made);
                if (made) {
                    ArenaAt<uint8_t*>(kBT + 0x0C) = ArenaPtr(kObject);
                    o ? Original<void(__thiscall*)(void*, void*)>(0x004F9040)(bt, nullptr) : bt->InitFromStructure(nullptr);
                    o ? Original<void(__thiscall*)(void*)>(0x004F9080)(bt) : bt->Finalize();
                }
                uint32_t flags = Next() % 2;
                return reinterpret_cast<uint32_t>(o ? Original<void*(__thiscall*)(void*, unsigned int)>(0x004F9010)(bt, flags)
                                                    : bt->Destroy(flags));
            }
            case 11:
                o ? Original<void(__cdecl*)()>(0x004F8CD0)() : CSkaterBalanceTrickComponent::RegisterMemberFunctions();
                return 1;
            case 12:
                return static_cast<uint32_t>(o ? Original<int(__thiscall*)(void*, void*, void*)>(0x004F8D70)(bt, params, nullptr)
                                               : bt->DoingBalanceTrick(params, nullptr));
            case 13: {
                static const uint32_t calls[4] = {0x004F8D80, 0x004F8DB0, 0x004F8DE0, 0x004F8EE0};
                uint32_t which = arg % 4;
                if (o) {
                    Original<void(__thiscall*)(void*)>(calls[which])(bt);
                } else {
                    switch (which) {
                    case 0: bt->ClearBestTimes(); break;
                    case 1: bt->KeepBestTimes(); break;
                    case 2: bt->Reset(); break;
                    default: bt->Stop(); break;
                    }
                }
                return which;
            }
            case 14: {
                auto* out = reinterpret_cast<uint32_t*>(ArenaPtr(kFree + 0x3F0));
                if (o)
                    Original<void(__thiscall*)(void*, int*, uint32_t*)>(0x004F8E20)(bt, reinterpret_cast<int*>(out), out + 1);
                else
                    bt->GetButtons(reinterpret_cast<int*>(out), out + 1);
                return 1;
            }
            case 15:
                return Bits(o ? static_cast<float>(Original<double(__thiscall*)(void*)>(0x004F8F20)(bt)) : bt->GetLean());
            case 16:
                return Bits(o ? static_cast<float>(Original<double(__thiscall*)(void*, uint32_t)>(0x004F90A0)(bt, arg))
                              : bt->GetPhysicsValue(arg));
            case 17:
                if (arg % 2) {
                    o ? Original<void(__thiscall*)(void*)>(0x004F91D0)(bt) : bt->ClearParams();
                } else {
                    auto* source = reinterpret_cast<Script::CStruct*>(ArenaPtr(kStructs + 0x80));
                    o ? Original<void(__thiscall*)(void*, void*)>(0x004F9370)(bt, source) : bt->SetParams(source);
                }
                return 1;
            default: {
                auto* wobble = reinterpret_cast<Script::CStruct*>(ArenaPtr(kStructs + 0x90));
                o ? Original<void(__thiscall*)(void*, void*)>(0x004F9200)(bt, wobble) : bt->SetWobble(wobble);
                return 1;
            }
            }
        };
        auto expected = RunOnArena(seed, [&](uint8_t*) { return runOp(true); });
        auto actual = RunOnArena(seed, [&](uint8_t*) { return runOp(false); });
        same = Compare(expected, actual, "balance trick", variant);
        if (!same)
            Log("  (op %d)", op);
    }
    GameVariable<Mem::PoolPair>(0x006F3614) = savedStructs;
    GameVariable<Mem::PoolPair>(0x006F3548) = savedScripts;
    Check(same, "skater: SkaterBalanceTrick (member functions, life, balances, lean, physics values, wobble)");
}

} // namespace

SELFTEST(Early, TestBalanceTrick);
