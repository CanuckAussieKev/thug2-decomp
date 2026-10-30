// Self-test for the skater's gap component (src/game/skater/gap.cpp, 0x00511BB0-0x00512E21): the original and ours on
// the same stand-in skater, gaps started, checked against every physics state and ended in every way, every call
// they make recorded.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/pool.h"
#include "game/core/string.h"
#include "game/obj/movie_manager.h"
#include "game/obj/objman.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skater/gap.h"
#include "game/skater/skater.h"
#include "game/skater/trick_lists.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;

// Arena layout
constexpr uint32_t kComponent = 0x0000, kObject = 0x0100, kCore = 0x0400, kCoreState = 0x0500, kBalance = 0x0700,
                   kControl = 0x0780, kState = 0x0800, kWalk = 0x0980, kScore = 0x0A00, kCamera = 0x0B00,
                   kVehicle = 0x0C00, kVehiclePhysics = 0x0D00, kTrick = 0x0E00, kScript = 0x1000,
                   kParams = 0x1400, kArrays = 0x1C00, kStructs = 0x2000, kHeap = 0x4000, kHeapEnd = 0xF000;

struct Answers {
    uint32_t calloc, heap, nextStruct;
    int counts[4];
    bool linked, allGaps, killOldest;
    int perfect;
    uint32_t linkCount;
};
Answers g_a;
void* g_entryVtable[2]; // the entries' slot 0 (the scalar deleting destructor): recorded, the node unlinked

// A fake script array at `offset`: `count` elements (one: in place).
Script::CArray* MakeArray(uint32_t offset, std::initializer_list<uint32_t> elements)
{
    std::memset(At(offset), 0, 0x40);
    F<uint32_t>(offset + 4) = static_cast<uint32_t>(elements.size());
    if (elements.size() == 1) {
        F<uint32_t>(offset + 8) = *elements.begin();
    } else {
        F<uint32_t>(offset + 8) = Addr(offset + 0x10);
        uint32_t i = 0;
        for (uint32_t e : elements)
            F<uint32_t>(offset + 0x10 + 4 * i++) = e;
    }
    return reinterpret_cast<Script::CArray*>(At(offset));
}

uint32_t Allocate(uint32_t size)
{
    if (g_a.heap + size > kHeapEnd) { // (a loop that never ends: as the original would, but not in the arena)
        Log("  skater gap test: the stand-in heap ran out");
        g_a.heap = kHeap;
    }
    uint32_t at = g_a.heap;
    g_a.heap += (size + 15) & ~15u;
    std::memset(At(at), 0xCD, size);
    return Addr(at);
}

void StandIn(StandIns& s)
{
    StandInStructGetters(s);
    // (the component's allocation: TestLifecycle's answer, else the heap)
    Rec(s, {0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 1, 0x1CC0,
        [](Regs& r) { return g_a.calloc ? g_a.calloc : Allocate(r.args[0]); }, false, true);
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        reinterpret_cast<uint32_t*>(r.ecx)[2] = 0;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    Rec(s, {0x005BABA0, AddressOf(&Mem::Manager::Free)}, 1, 0xABA0);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs& r) {
        switch (r.args[0]) {
        case 0x5BD63B29: return Addr(kCore);
        case 0x589E5FB3: return Addr(kBalance);
        case 0xD9151D4D: return Addr(kControl);
        case 0x726E85AA: return Addr(kWalk);
        case 0xE47F1B79: return Addr(kVehiclePhysics);
        case 0x270F56E1: return Addr(kTrick);
        default: return 0u;
        }
    });
    Rec(s, {0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 1, 0x19F0, [](Regs&) { return Addr(kVehicle); });
    // The entries' constructors: the node set up (data = itself, unlinked), the recording vtable, an array.
    for (int k = 0; k < 2; ++k) {
        g_entryVtable[k] = RecThunk(s, 1, 0xDE10 + k, [](Regs& r) {
            auto* node = reinterpret_cast<Mem::ListNode*>(r.ecx);
            node->prev->next = node->next;
            node->next->prev = node->prev;
            return r.ecx;
        });
    }
    auto construct = [](int k) {
        return [k](Regs& r) {
            Record({0x8740u + k, Rel(r.ecx)});
            auto* node = reinterpret_cast<Mem::ListNode*>(r.ecx);
            node->vtable = reinterpret_cast<void**>(&g_entryVtable[k]);
            node->data = node;
            node->priority = 0;
            node->next = node->prev = node;
            if (k == 0) { // (as 0x00578740: the trick fields cleared, an empty array)
                for (uint32_t at : {0x20u, 0x24u, 0x30u, 0x38u, 0x3Cu})
                    F<uint32_t>(reinterpret_cast<void*>(r.ecx), at) = 0;
                F<uint8_t>(reinterpret_cast<void*>(r.ecx), 0x34) = 0;
                F<uint32_t>(reinterpret_cast<void*>(r.ecx), 0x60) =
                    reinterpret_cast<uint32_t>(MakeArray(kArrays + 0x100 + 0x40 * (g_a.nextStruct++ % 8), {}));
            }
            return r.ecx;
        };
    };
    s.At({0x00578740, AddressOf(&Obj::TrickListEntry::Construct)}, 0, construct(0));
    s.At({0x005787F0, AddressOf(&Obj::TrickNameEntry::Construct)}, 0, construct(1));
    s.At({0x00401ED0, AddressOf(static_cast<Str::String& (Str::String::*)(const char*)>(&Str::String::operator=))}, 4,
         [](Regs& r) {
             Record({0x1ED0, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
             return r.ecx;
         });
    Rec(s, {0x00580880, AddressOf(&Obj::CSkater::GetSkaterCamera)}, 0, 0x0880, [](Regs&) { return Addr(kCamera); });
    s.At({0x00472E60, AddressOf(&Crc::GenerateCRCFromString)}, 0, [](Regs& r) {
        uint32_t h = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0x2E60, h});
        return h;
    });
    auto count = [](int i) { return [i](Regs&) { return static_cast<uint32_t>(g_a.counts[i]); }; };
    Rec(s, {0x00562620}, 3, 0x2620, count(0));
    Rec(s, {0x005625A0}, 3, 0x25A0, count(1));
    Rec(s, {0x00562510}, 4, 0x2510, count(2));
    Rec(s, {0x005624C0, AddressOf(&Mdl::Score::CountTrickAfterTaps)}, 4, 0x24C0, count(3));
    Rec(s, {0x0049F4C0}, 2, 0xF4C0);
    s.At({0x00563990}, 12, [](Regs& r) {
        Record({0x3990, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2]});
        return 0u;
    });
    Rec(s, {0x00478A60, AddressOf(&Script::CopyArray)}, 2, 0x8A60, nullptr, false, true);
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        auto* a = reinterpret_cast<uint32_t*>(r.ecx);
        Record({0xCC30, Rel(r.ecx), r.args[0]});
        return a[1] == 1 ? a[2] : reinterpret_cast<uint32_t*>(a[2])[r.args[0]];
    });
    // The flags' script integers: 0x111 require ground, 0x222 require lip, 0x333 cancel wall, 0x444 cancel rail.
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        switch (r.args[0]) {
        case 0x111: return 0x00010000u;
        case 0x222: return 0x00100000u;
        case 0x333: return 0x00000008u;
        case 0x444: return 0x00000004u;
        default: return 0u;
        }
    });
    Rec(s, {0x0045C520, AddressOf(static_cast<bool (*)(uint32_t, uint32_t)>(&SkateScript::IsLinkedTo))}, 2, 0xC520,
        [](Regs&) { return g_a.linked ? 1u : 0u; }, false, true);
    Rec(s, {0x0045C3A0, AddressOf(static_cast<uint32_t (*)(uint32_t)>(&SkateScript::GetLinkCount))}, 1, 0xC3A0,
        [](Regs&) { return g_a.linkCount; }, false, true);
    Rec(s, {0x0045C460, AddressOf(static_cast<Script::CStruct* (*)(uint32_t, int)>(&SkateScript::GetLinkedNode))}, 2,
        0xC460, [](Regs& r) { return Addr(kParams + 0x600 + 0x20 * (r.args[1] & 7)); }, false, true);
    // The perfect check: its results structure gets Perfect.
    s.At({0x004752F0, AddressOf(&Script::RunScriptOnce)}, 0, [](Regs& r) {
        Record({0x52F0, r.args[0], Rel(r.args[1]), Rel(r.args[2]), Rel(r.args[3])});
        SetComponent(reinterpret_cast<void*>(r.args[3]), 0x1C39F1B9, static_cast<uint32_t>(g_a.perfect));
        return 0u;
    });
    auto structAt = [](Regs&) { return Allocate(0x20); };
    s.At({0x00409030}, 0, [structAt](Regs& r) {
        Record({0xA3D0, Rel(Addr(0x1F00))});
        return structAt(r);
    });
    Rec(s, {0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, 0xA3D0, structAt);
    Rec(s, {0x00476410, AddressOf(&Script::CStruct::Init)}, 0, 0x6410, [](Regs& r) { return r.ecx; });
    Rec(s, {0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, 0x7130);
    Rec(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1, 0x9070, nullptr, false, true);
    s.At({0x006234FE}, 0, StandInSprintf);
    // The gap checklist and the career.
    Rec(s, {0x005656C0, AddressOf(&Mdl::Skate::GetGapChecklist)}, 0, 0x56C0, [](Regs&) { return Addr(kParams + 0x700); });
    s.At({0x00578BC0, AddressOf(&Game::CGapChecklist::SetGapPositions)}, 36, [](Regs& r) {
        Record({0x8BC0, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        for (int i = 1; i <= 8; ++i)
            Record({r.args[i]});
        return 0u;
    });
    s.At({0x00578C70, AddressOf(&Game::CGapChecklist::GotGap)}, 4, [](Regs& r) {
        Record({0x8C70, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    Rec(s, {0x00578CF0, AddressOf(&Game::CGapChecklist::ClearPending)}, 0, 0x8CF0);
    Rec(s, {0x005796C0, AddressOf(&Game::CGapChecklist::AwardPendingGaps)}, 0, 0x96C0);
    Rec(s, {0x00584BC0, AddressOf(&Obj::CSkaterCareer::AllGapsGot)}, 0, 0x4BC0, [](Regs&) { return g_a.allGaps ? 1u : 0u; });
    Rec(s, {0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, Script::CStruct*, uint32_t, Script::CStruct*,
                                                                    int, uint32_t, bool, bool, bool, bool)>(
                            &Script::SpawnScript))},
        10, 0x5420, nullptr, false, true);
    Rec(s, {0x00461480, AddressOf(&Obj::CObject::SpawnAndRunScript)}, 5, 0x1480);
    s.At({0x00401BE0, AddressOf(&Crc::ExtendCRCWithString)}, 0, [](Regs& r) {
        Record({0x1BE0, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return r.args[0] ^ 0x5CC55000;
    });
    // (killOldest: the oldest gap deleted - as a script run by the event could)
    Rec(s, {0x00462F00, AddressOf(&Obj::CTracker::LaunchEvent)}, 6, 0x2F00, [](Regs&) {
        auto* head = reinterpret_cast<Mem::ListNode*>(At(kComponent + 0x18));
        if (g_a.killOldest && head->prev != head && head->prev != head->next) {
            Mem::ListNode* oldest = head->prev;
            reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(oldest->vtable[0])(oldest, 1);
        }
        return 0u;
    });
    Rec(s, {0x0057A2F0, AddressOf(&Obj::ListHead::DeleteAllNodes)}, 0, 0xA2F0);
}

void ResetWorld(uint32_t seed)
{
    FillArena(seed);
    g_a = {};
    g_a.heap = kHeap;
    g_structs.clear();
    F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
    F<uint32_t>(kCore + 0x1528) = Addr(kCoreState);
    F<uint32_t>(kControl + 0x2C) = Addr(kState);
    F<uint32_t>(kScript + 0xB8) = 0x40 + seed;
    F<uint8_t>(kObject + 0xBC) = 1;
}

using Fn0 = void(__thiscall*)(void*);
using FnP = void(__thiscall*)(void*, void*);
using FnPS = void(__thiscall*)(void*, void*, void*);

// The component: made, constructed, finalized; the state flags in every state; gaps started (each trick kind, flag
// kind, duplicates, skipped), checked by updates, ended (every outcome), the gap tricks, the member functions; the
// lists cleared, the node and head destructors, the component destroyed; the two checklist helpers.
void TestSkaterGap()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory globals({{0x006F3614, 16}, {0x007CE5D8, 4}, {0x007CE478, 4}});
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            auto& pools = GameVariable<Mem::PoolPair>(0x006F3614);
            pools.current = 0;
            pools.pools[0] = reinterpret_cast<Mem::Pool*>(At(0x1F00));
            pools.pools[1] = nullptr;
            GameVariable<uint32_t>(0x007CE5D8) = Addr(kParams + 0x780);
            GameVariable<uint32_t>(0x007CE478) = Addr(kParams + 0x7C0);
            F<uint32_t>(kParams + 0x7C0 + 0x20) = Addr(kParams + 0x7E0); // the career
            Originals guard(original, {0x00512C60, 0x005122A0, 0x00511F10, 0x005122F0, 0x00511BB0, 0x00511BC0,
                                       0x00512320, 0x00512CC0, 0x00511C80, 0x00511F60, 0x00512030, 0x00512050,
                                       0x00512140, 0x005121A0, 0x00512360, 0x00512770, 0x00511E50, 0x00511E90,
                                       0x00512240, 0x00511E70, 0x00511ED0, 0x00512270, 0x00511C10, 0x00511C30});
            std::vector<uint32_t> trace;
            // Create / Construct (and out of memory).
            g_a.calloc = 0;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x00512C60))()));
            g_a.calloc = Addr(kComponent);
            std::memset(At(kComponent), 0xCD, 0x58);
            // (Create with memory: Calloc must return 0 for none - the heap gives memory otherwise)
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x00512C60))()));
            g_a.calloc = 0;
            for (uint32_t i = 0; i < 0x58; i += 4)
                trace.push_back(Rel(F<uint32_t>(kComponent + i)));
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            reinterpret_cast<FnP>(Code(original, 0x00511BB0))(c, At(kParams));
            reinterpret_cast<Fn0>(Code(original, 0x00511BC0))(c);
            F<uint32_t>(kComponent + 0x44) = Addr(kScore);
            Lcg random(seed * 101);

            // The state flags in every state.
            for (int k = 0; k < 40; ++k) {
                F<uint8_t>(kState + 0x118) = k < 4 ? 1 : 0;
                F<uint32_t>(kVehiclePhysics + 0x114) = k & 1 ? 0u : 3u;
                F<int>(kState + 0x1C) = k >= 4 && k < 10 ? 1 : 0;
                F<int>(kWalk + 0x18) = k - 5;
                F<int>(kCoreState + 0x18) = (k - 10) / 4 - (k == 39 ? 10 : 0);
                F<uint8_t>(kCoreState + 0x90) = (k & 1) ? 1 : 0;
                F<uint32_t>(kBalance + 0x18) = (k & 4) ? 5u : 0u;
                F<uint8_t>(kBalance + 0x1C) = (k % 7 == 3) ? 1 : 0;
                uint32_t cancel = 0xDEAD, require = 0xBEEF;
                reinterpret_cast<void(__thiscall*)(void*, uint32_t*, uint32_t*)>(Code(original, 0x00511C80))(c, &cancel,
                                                                                                            &require);
                trace.push_back(cancel);
                trace.push_back(require);
            }

            // Skating from here on (a state cancels gaps only through their flags), or walking.
            auto skate = [&](int state, bool walking = false) {
                F<uint8_t>(kState + 0x118) = 0;
                F<int>(kState + 0x1C) = walking ? 1 : 0;
                F<int>(kWalk + 0x18) = 0;
                F<int>(kCoreState + 0x18) = state;
                F<uint8_t>(kCoreState + 0x90) = 0;
                F<uint32_t>(kBalance + 0x18) = 0;
                F<uint8_t>(kBalance + 0x1C) = 0;
            };
            auto memberFunction = [&](uint32_t name, uint32_t params) {
                trace.push_back(reinterpret_cast<int(__thiscall*)(void*, uint32_t, void*, void*)>(Code(original, 0x00512CC0))(
                    c, name, At(params), At(kScript)));
            };
            auto update = [&]() { reinterpret_cast<Fn0>(Code(original, 0x00512320))(c); };
            auto setNode = [&](uint32_t n) { F<uint32_t>(kScript + 0xB8) = n; };
            constexpr uint32_t kStartGap = 0x65B9EC39, kEndGap = 0xE5399FB2, kStartGapTrick = 0xFC4F2009,
                               kCheckGapTricks = 0x42F80E5E, kClearGapTricks = 0x0196772B,
                               kDuplicateTrigger = 0x4562E4A6;
            static const char* const kTexts[] = {"Kickflip", "Big Air", "Ollie North"};
            auto text = [](int i) { return reinterpret_cast<uint32_t>(kTexts[i]); };
            // The gaps' parameters: P0 keycombo + spin + Perfect + taps, flags (an array: require ground and lip,
            // cancel wall), combined (require rail); P1 TrickText, spin, one flag (cancel rail); P2 a tricks array,
            // 0xE3ED4ADE; P3 no trickscript, GapID 0; P4 the same GapID as P0; P5 trickscript only, combined per
            // round; P6 a gap trick (P1's GapID); P7 for CheckGapTricks / ClearGapTricks; P8 CarGap; P9 a gap trick
            // like P0's.
            uint32_t p[10];
            for (int i = 0; i < 10; ++i)
                p[i] = i < 8 ? kParams + 0x80 * static_cast<uint32_t>(i) : kStructs + 0x100 * static_cast<uint32_t>(i);
            uint32_t ids[] = {0x1D000000 + seed, 0x2D000000, 0x3D000000, 0, 0x1D000000 + seed, 0x5D000000,
                              0x2D000000,        0,          0x8D000000, 0x1D000000 + seed};
            for (int i = 0; i < 10; ++i)
                if (i != 7)
                    SetComponent(At(p[i]), 0x3B442E26, ids[i]);
            for (int i : {0, 1, 2, 4, 5, 8})
                SetComponent(At(p[i]), 0xA26994E6, 0x5C000000 + static_cast<uint32_t>(i));
            SetComponent(At(p[0]), 0x95E16467, 0x00C0B000 + seed);
            SetComponent(At(p[0]), 0xEDF5DB70, static_cast<uint32_t>(seed & 1 ? 540 : -360));
            SetComponent(At(p[0]), 0x1C39F1B9, 0);
            SetComponent(At(p[0]), 0xA4BEE6A1, 2);
            SetComponent(At(p[0]), 0xF4FABE45, reinterpret_cast<uint32_t>(MakeArray(kArrays, {0x111, 0x222, 0x333})));
            SetComponent(At(p[0]), 0x2760DE9E, 0x00040000);
            SetComponent(At(p[1]), 0x3EAFA520, text(seed % 3));
            SetComponent(At(p[1]), 0xEDF5DB70, 179 + seed);
            SetComponent(At(p[1]), 0xF4FABE45, 0x444);
            SetComponent(At(p[1]), 0xA4BEE6A1, 3);
            SetComponent(At(p[2]), 0x1E26FD3E, reinterpret_cast<uint32_t>(MakeArray(kArrays + 0x80, {7, 8})));
            SetComponent(At(p[2]), 0xE3ED4ADE, 0);
            SetComponent(At(p[4]), 0x95E16467, 0x00C0B100);
            SetComponent(At(p[6]), 0x3EAFA520, text(0));
            SetComponent(At(p[6]), 0xA26994E6, 0x6C000000);
            SetComponent(At(p[8]), 0xC442E33D, 0);
            SetComponent(At(p[9]), 0xA26994E6, 0x5C000000);
            F<uint32_t>(kScore + 0x5C) = 0x77 + seed;
            // (the gap check setting: Off for odd seeds)
            F<uint32_t>(kParams + 0x780 + 0x40) = seed & 1 ? 0xD443A2BCu : 0u;

            // EndGap's parameters for gap `i` in round `round`: Require_Perfect, text, score, GapScript (permanent,
            // NetEnabled), Continue (a P3 gap: another GapID - one of its own would be ended again forever, as in the
            // original).
            auto endGap = [&](int round, int i) {
                uint32_t q = kStructs + 0x1000 + 0x100 * static_cast<uint32_t>(round) + 0x20 * static_cast<uint32_t>(i);
                SetComponent(At(q), 0x3B442E26, ids[i]);
                if ((i + round) % 3 == 1)
                    SetComponent(At(q), 0x687E2C25, 0);
                if (i != 2)
                    SetComponent(At(q), 0xC4745838, text((i + static_cast<int>(seed) + round) % 3));
                if (i % 2 == 0 || i == 5)
                    SetComponent(At(q), 0xCD66C8AE, 100 * static_cast<uint32_t>(i + 1));
                if (i != 4) {
                    SetComponent(At(q), 0x7C9E51AB, 0x6A000000 + static_cast<uint32_t>(i));
                    if (i & 1)
                        SetComponent(At(q), 0x23627FD7, 0);
                    if (i & 2)
                        SetComponent(At(q), 0x20209C31, 0);
                }
                if (i == 2)
                    SetComponent(At(q), 0xEC1CD520, Addr(p[3]));
                g_a.perfect = (static_cast<int>(seed) + i + round) % 3;
                g_a.linked = ((seed + static_cast<uint32_t>(round)) & 1) != 0;
                F<uint8_t>(kObject + 0xBC) = i == 5 ? 0 : 1;
                setNode(0x40 + seed);
                memberFunction(kEndGap, q);
            };
            auto startAll = [&](std::initializer_list<int> which) {
                setNode(0x40 + seed);
                for (int i : which)
                    memberFunction(i == 6 || i == 9 ? kStartGapTrick : kStartGap, p[i]);
            };

            // Started skipped (+0x70).
            skate(0);
            F<uint8_t>(kCoreState + 0x70) = 1;
            memberFunction(kStartGap, p[0]);
            F<uint8_t>(kCoreState + 0x70) = 0;

            // Round 0: started (driving for some seeds: checks matter even when Off), updates in four states,
            // ended.
            SetComponent(At(p[5]), 0x2760DE9E, seed & 1 ? 0x00020000u : 0x00040002u);
            F<uint8_t>(kState + 0x118) = seed % 3 == 0 ? 1 : 0;
            g_a.counts[0] = 3;
            g_a.counts[1] = 1;
            startAll({0, 1, 2, 3, 4, 5, 6, 8});
            for (int state : {0, 4, 1, 3})
                skate(state), update();
            trace.push_back(F<uint32_t>(kComponent + 0x40));
            g_a.counts[2] = seed & 2 ? 9 : 3;
            g_a.counts[3] = seed & 4 ? 9 : 1;
            for (int i : {0, 1, 2, 3, 4, 5})
                endGap(0, i);
            memberFunction(kCheckGapTricks, p[7]);

            // Round 1: started in the air (walking for even seeds: CarGap's walking flag), no updates: the gaps
            // with requirements left fail; the counts unchanged (not done).
            SetComponent(At(p[5]), 0x2760DE9E, 0x00010000);
            skate(1, (seed & 1) == 0);
            startAll({1, 0, 2, 5, 8, 6, 3});
            skate(1);
            g_a.counts[2] = 3;
            g_a.counts[3] = 1;
            for (int i : {5, 1, 0, 2, 3, 8})
                endGap(1, i);
            F<uint8_t>(kCoreState + 0x70) = 1; // (EndGap skipped)
            endGap(1, 0);
            F<uint8_t>(kCoreState + 0x70) = 0;

            // Round 2: the gap tricks cancelled with their gap: [another node's, a done one, P0's own, a duplicate].
            SetComponent(At(p[5]), 0x2760DE9E, 0);
            skate(0);
            startAll({9, 0});
            setNode(0x40 + seed);
            memberFunction(kStartGapTrick, p[9]);
            F<uint8_t>(reinterpret_cast<void*>(F<uint32_t>(kComponent + 0x38)), 0x2C) = 1; // (the newest done)
            setNode(0x99);
            memberFunction(kStartGapTrick, p[9]);
            memberFunction(kStartGap, p[0]); // (another node: P0's gap at the first node stays)
            for (int state : {2, 0})
                skate(state), update();
            startAll({5, 1});
            skate(3), update();
            endGap(2, 5);
            endGap(2, 1);
            memberFunction(kCheckGapTricks, p[7]);

            // Round 3: an event while a gap ends kills the other gap of the GapID (the walk begins again).
            skate(0);
            setNode(0x41);
            memberFunction(kStartGap, p[5]);
            setNode(0x42);
            memberFunction(kStartGap, p[5]);
            g_a.killOldest = true;
            endGap(3, 5);
            g_a.killOldest = false;

            SetComponent(At(p[7]), 0x97975226, 0);
            if (seed & 1)
                update(); // (another frame: none added this one)
            memberFunction(kClearGapTricks, p[7]);
            memberFunction(kStartGapTrick, p[6]);
            memberFunction(kClearGapTricks, p[0]);
            // DuplicateTrigger: no node / links without a TriggerScript / two with.
            for (int k = 0; k < 3; ++k) {
                F<uint32_t>(kScript + 0xB8) = k == 0 ? 0 : 0x50 + seed;
                g_a.linkCount = k == 0 ? 2 : 3;
                for (uint32_t at : {0x620u, 0x640u}) {
                    SetComponent(At(kParams + at), 0x2CA8A299, 0x7A000000 + at);
                    if (k < 2)
                        g_structs[Addr(kParams + at)].erase(0x2CA8A299);
                }
                memberFunction(kDuplicateTrigger, p[7]);
            }
            memberFunction(0x12345678, p[7]);
            // The lists' remains, then everything cleared, the destructors.
            for (uint32_t i = 0x18; i < 0x40; i += 4)
                trace.push_back(Rel(F<uint32_t>(kComponent + i)));
            reinterpret_cast<Fn0>(Code(original, 0x00512030))(c);
            for (uint32_t node : {0x00511E50u, 0x00511E70u}) {
                F<uint32_t>(0x3800 + 0x0C) = Addr(0x3800);
                F<uint32_t>(0x3800 + 0x10) = Addr(0x3800);
                reinterpret_cast<Fn0>(Code(original, node))(At(0x3800));
                trace.push_back(Rel(F<uint32_t>(0x3800)));
            }
            for (uint32_t destroy : {0x00511E90u, 0x00511ED0u, 0x00512240u, 0x00512270u}) {
                for (unsigned flags : {0u, 1u}) {
                    F<uint32_t>(0x3800 + 0x0C) = Addr(0x3820);
                    F<uint32_t>(0x3800 + 0x10) = Addr(0x3820);
                    F<uint32_t>(0x3820 + 0x0C) = Addr(0x3800);
                    F<uint32_t>(0x3820 + 0x10) = Addr(0x3800);
                    trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, destroy))(
                        At(0x3800), flags)));
                }
            }
            reinterpret_cast<Fn0>(Code(original, 0x00511F10))(c);
            for (unsigned flags : {0u, 1u})
                trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x005122F0))(
                    c, flags)));
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x00511C10))();
            for (bool all : {false, true}) {
                g_a.allGaps = all;
                reinterpret_cast<void(__cdecl*)()>(Code(original, 0x00511C30))();
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater gap component: made, state flags, gaps started / checked / ended, gap tricks");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterGap, RemoveStandIns);
