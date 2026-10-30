// Self-tests for the functions the whole-exe byte scan found unwritten (no Ghidra function, or swallowed by one
// with a wrong extent): CEvent's MarkRead / EventController, CStruct::GetScript, CSkater::GetDisplayMatrix, the
// region's deleting destructor, the projection batches' and the evaluator's destructors, the AddPair jump and the
// file tables' static constructor. Original against ours, every callee a recording stand-in.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/file/pip.h"
#include "game/gfx/material.h"
#include "game/gfx/pc_texture_projection.h"
#include "game/obj/event.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/eval.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

uint32_t g_found = 0; // which names the stand-in Search / GetInteger find (bit per name & 31)

bool Found(uint32_t name) { return (g_found >> (name & 31)) & 1; }

void InstallStandIns(StandIns& s)
{
    s.At({0x00462B40, AddressOf(&Obj::CTracker::LogRead)}, 12, [](Regs& r) {
        Record({0x2B40, r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    using GetInt = bool (Script::CStruct::*)(uint32_t, int*, int);
    s.At({0x00476630, AddressOf(static_cast<GetInt>(&Script::CStruct::GetInteger))}, 12, [](Regs& r) {
        Record({0x6630, r.ecx, r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2]});
        if (!Found(r.args[0] ^ r.ecx))
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = r.ecx * 3 + 1;
        return 1u;
    });
    s.At({0x004764B0, AddressOf(&Script::CStruct::Search)}, 12, [](Regs& r) {
        Record({0x64B0, r.ecx, r.args[0], r.args[1]});
        if (!Found(r.args[0]))
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[2]) = r.args[0] * 7;
        return 1u;
    });
    s.At({0x00477FC0, AddressOf(&Script::CStruct::AddPair)}, 12, [](Regs& r) {
        Record({0x7FC0, r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0046D1C0, AddressOf(&Script::CExpressionEvaluator::Clear)}, 0, [](Regs& r) {
        Record({0xD1C0, r.ecx});
        return 0u;
    });
    s.At({0x004D2BD0, AddressOf(&Nx::sMaterial::DestroyBody)}, 0, [](Regs& r) {
        Record({0x2BD0, r.ecx});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, r.ecx, r.args[0]});
        return 0u;
    });
}

uint32_t g_rng = 1;
uint32_t Next()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

bool Same(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what, int variant)
{
    if (expected == actual)
        return true;
    Log("  byte scan leftovers: %s differs (variant %d, trace sizes %u / %u)", what, variant,
        static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
    for (size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        if (expected[i] != actual[i]) {
            Log("  entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            break;
        }
    }
    return false;
}

void TestByteScanLeftovers()
{
    uint32_t savedTracker = GameVariable<uint32_t>(0x006F0FC0);
    uint32_t savedManager = GameVariable<uint32_t>(0x007D1800);
    auto* standIns = new StandIns();
    InstallStandIns(*standIns);
    bool same = true;
    int reads = 0, controllers = 0, noData = 0, scripts = 0, asserts = 0, freed = 0;
    for (int variant = 0; variant < 2000 && same; ++variant) {
        g_rng = 0x9E3779B9u * (variant + 1);
        g_found = Next();
        GameVariable<uint32_t>(0x006F0FC0) = Next() | 0x10;
        GameVariable<uint32_t>(0x007D1800) = Next() | 0x20;
        uint32_t how = Next(), script = Next(), name = Next();
        int flagsSeed = static_cast<int>(Next());
        int assert = static_cast<int>(Next() % 3);

        // MarkRead: the tracker told, then flag 1 (the other flags kept).
        Obj::CEvent events[2] = {};
        for (Obj::CEvent& e : events)
            e.m_flags = static_cast<uint32_t>(flagsSeed);
        g_trace.clear();
        Original<void(__thiscall*)(Obj::CEvent*, uint32_t, uint32_t)>(0x0045F980)(&events[0], how, script);
        Record({events[0].m_flags});
        std::vector<uint32_t> expected = g_trace;
        g_trace.clear();
        events[1].MarkRead(how, script);
        Record({events[1].m_flags});
        // (the stand-in records the event's address: ours ran on the other one)
        for (size_t i = 0; i < expected.size(); ++i)
            if (expected[i] == reinterpret_cast<uint32_t>(&events[0]))
                expected[i] = reinterpret_cast<uint32_t>(&events[1]);
        same = same && Same(expected, g_trace, "CEvent::MarkRead (0x0045F980)", variant);
        reads += !(flagsSeed & 1);

        // EventController: 0 without data, else GetInteger's answer ("controller", not asserted).
        Obj::CEvent event = {};
        event.mp_data = Next() % 4 ? reinterpret_cast<Script::CStruct*>(Next() | 0x100) : nullptr;
        g_trace.clear();
        Record({static_cast<uint32_t>(Original<int(__cdecl*)(Obj::CEvent*)>(0x0045F9E0)(&event))});
        expected = g_trace;
        g_trace.clear();
        Record({static_cast<uint32_t>(Obj::EventController(&event))});
        same = same && Same(expected, g_trace, "EventController (0x0045F9E0)", variant);
        noData += !event.mp_data;
        controllers += event.mp_data && Found(0xB30D9965 ^ reinterpret_cast<uint32_t>(event.mp_data));

        // GetScript: Search for a script component; the name and code into `out` only when found.
        auto* structure = reinterpret_cast<Script::CStruct*>(Next() | 0x200);
        Script::SStructScript outs[2];
        for (Script::SStructScript& o : outs) {
            o.mNameChecksum = 0xAAAAAAAA;
            o.mpScriptTokens = reinterpret_cast<const uint8_t*>(0xBBBBBBBB);
        }
        g_trace.clear();
        Record({Original<bool(__thiscall*)(Script::CStruct*, uint32_t, Script::SStructScript*, int)>(0x00476900)(
                    structure, name, &outs[0], assert)
                    ? 1u
                    : 0u,
                outs[0].mNameChecksum, reinterpret_cast<uint32_t>(outs[0].mpScriptTokens)});
        expected = g_trace;
        g_trace.clear();
        Record({structure->GetScript(name, &outs[1], assert) ? 1u : 0u, outs[1].mNameChecksum,
                reinterpret_cast<uint32_t>(outs[1].mpScriptTokens)});
        same = same && Same(expected, g_trace, "CStruct::GetScript (0x00476900)", variant);
        scripts += Found(name);
        asserts += !Found(name) && assert;

        // CSkater::GetDisplayMatrix: the core physics' +0x18.
        alignas(16) uint8_t skater[0x300] = {};
        *reinterpret_cast<uint32_t*>(skater + 0x280) = Next();
        uint32_t original = reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*)>(0x00580030)(skater));
        uint32_t ours = reinterpret_cast<uint32_t>(reinterpret_cast<Obj::CSkater*>(skater)->GetDisplayMatrix());
        same = same && Same({original}, {ours}, "CSkater::GetDisplayMatrix (0x00580030)", variant);

        // The region's deleting destructor: the vtable set back, freed through the manager with flags & 1.
        alignas(16) uint8_t regions[2][sizeof(Mem::Region)];
        unsigned int flags = Next();
        for (int k = 0; k < 2; ++k) {
            std::memset(regions[k], 0xCD, sizeof(regions[k]));
            g_trace.clear();
            uint32_t result =
                k == 0 ? reinterpret_cast<uint32_t>(
                             Original<void*(__thiscall*)(void*, unsigned int)>(0x005BC6D0)(regions[k], flags))
                       : reinterpret_cast<uint32_t>(reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(
                             ReplacementFunction(0x005BC6D0))(regions[k], flags));
            Record({result - reinterpret_cast<uint32_t>(regions[k]), *reinterpret_cast<uint32_t*>(regions[k])});
            for (uint32_t& word : g_trace)
                if (word == reinterpret_cast<uint32_t>(regions[k]))
                    word = 0x12345678;
            if (k == 0)
                expected = g_trace;
        }
        same = same && Same(expected, g_trace, "Region's deleting destructor (0x005BC6D0)", variant);
        freed += flags & 1;

        // The jumps: AddPair (0x00478470), the evaluator's destructor (0x0046D3C0), the batch's destructor
        // (0x004C73D0, the material at +0x0C).
        uint32_t object = Next() | 0x300;
        float x = static_cast<float>(static_cast<int>(Next() % 2000) - 1000), y = static_cast<float>(Next() % 99);
        g_trace.clear();
        Original<void(__thiscall*)(uint32_t, uint32_t, float, float)>(0x00478470)(object, name, x, y);
        Original<void(__thiscall*)(uint32_t)>(0x0046D3C0)(object);
        Original<void(__thiscall*)(uint32_t)>(0x004C73D0)(object);
        expected = g_trace;
        g_trace.clear();
        reinterpret_cast<Script::CStruct*>(object)->AddPair(name, x, y);
        reinterpret_cast<Script::CExpressionEvaluator*>(object)->Clear();
        reinterpret_cast<Nx::sProjectionBatch*>(object)->Destroy();
        same = same && Same(expected, g_trace, "the jumps 0x00478470 / 0x0046D3C0 / 0x004C73D0", variant);
    }
    delete standIns;
    GameVariable<uint32_t>(0x006F0FC0) = savedTracker;
    GameVariable<uint32_t>(0x007D1800) = savedManager;

    // The file tables' static constructor: both tables emptied, nothing else touched; returns this.
    constexpr uint32_t kFrom = 0x007CF190, kTo = 0x007CFFB0;
    std::vector<uint8_t> saved(reinterpret_cast<uint8_t*>(kFrom), reinterpret_cast<uint8_t*>(kTo));
    std::vector<uint8_t> after[2];
    uint32_t results[2];
    for (int k = 0; k < 2; ++k) {
        for (uint32_t a = kFrom; a < kTo; ++a)
            *reinterpret_cast<uint8_t*>(a) = static_cast<uint8_t>(a * 37 + 11);
        auto* tables = reinterpret_cast<Pip::FileTables*>(0x007CFFA8);
        results[k] = reinterpret_cast<uint32_t>(
            k == 0 ? Original<Pip::FileTables*(__thiscall*)(Pip::FileTables*)>(0x005B70F0)(tables) : tables->Construct());
        after[k].assign(reinterpret_cast<uint8_t*>(kFrom), reinterpret_cast<uint8_t*>(kTo));
    }
    std::memcpy(reinterpret_cast<void*>(kFrom), saved.data(), saved.size());
    bool tables = after[0] == after[1] && results[0] == results[1];
    if (!tables)
        Log("  byte scan leftovers: the file tables' constructor (0x005B70F0) differs");

    bool covered = reads > 300 && controllers > 200 && noData > 200 && scripts > 300 && asserts > 200 && freed > 300;
    if (!covered)
        Log("  byte scan leftovers coverage: reads %d, controllers %d, no data %d, scripts %d, asserts %d, freed %d",
            reads, controllers, noData, scripts, asserts, freed);
    Check(same && tables && covered, "the byte scan's leftovers (MarkRead, EventController, GetScript, "
                                     "GetDisplayMatrix, Region's destructor, three jumps, the file tables)");
}

} // namespace

SELFTEST(Early, TestByteScanLeftovers, TestStandIns::RemoveStandIns);
