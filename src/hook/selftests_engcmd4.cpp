// Self-tests for the engine's script commands 0x00447FF0-0x00448840 (the memory manager's commands, the pool-usage
// dump, DumpHeaps): each original against ours; the manager's methods, the debug output, the dump text and the
// other callees are stood in for by recording code (printed lines recorded as text hashes).

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/dump_text.h"
#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/gfx/engine_glue.h"
#include "game/script/cfuncs_engine.h"
#include "game/script/struct.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace CFuncTests;
using Script::CStruct;

uint32_t CrcOf(const char* name) { return Crc::GenerateCRCFromString(name); }

alignas(16) uint8_t g_manager[sizeof(Mem::Manager)];
alignas(16) uint8_t g_heaps[3][sizeof(Mem::Allocator)];
alignas(16) uint8_t g_regions[3][sizeof(Mem::Region)];
uint32_t g_heapCount;
uint32_t g_namedResult;
int g_soundFree;
alignas(16) uint8_t g_pools[14][sizeof(Mem::Pool)];

uint32_t Formatted(Regs& r, int first)
{
    char text[512];
    std::vsnprintf(text, sizeof(text), reinterpret_cast<const char*>(r.args[first]),
                   reinterpret_cast<va_list>(&r.args[first + 1]));
    return Hash(text);
}

void StandInCalls(StandIns& s)
{
    using Manager = Mem::Manager;
    s.At({0x005BB0E0, AddressOf(&Manager::PushContext)}, 4, [](Regs& r) { Record({0xB0E0, r.ecx, r.args[0]}); return 0u; });
    s.At({0x005BB110, AddressOf(&Manager::PopContext)}, 0, [](Regs& r) { Record({0xB110, r.ecx}); return 0u; });
    s.At({0x005BB310, AddressOf(&Manager::GetHeap)}, 4, [](Regs& r) {
        Record({0xB310, r.ecx, r.args[0]});
        return r.args[0] & 1 ? 0u : 0x4EA70000u + (r.args[0] & 0xFFFF);
    });
    s.At({0x005BB9E0, AddressOf(&Manager::InitNamedHeap)}, 16, [](Regs& r) {
        Record({0xB9E0, r.ecx, r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2])), r.args[3]});
        return 0u;
    });
    s.At({0x005BAF70, AddressOf(&Manager::DeleteNamedHeap)}, 8, [](Regs& r) {
        Record({0xAF70, r.ecx, r.args[0], r.args[1]});
        return g_namedResult;
    });
    s.At({0x005BAF50, AddressOf(&Manager::GetNamedHeap)}, 8, [](Regs& r) {
        Record({0xAF50, r.ecx, r.args[0], r.args[1]});
        return g_namedResult & 0xFF ? 0x4EA80000u : 0u;
    });
    s.At({0x005BB160, AddressOf(&Manager::GetHeapName)}, 4, [](Regs& r) {
        Record({0xB160, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(r.args[0] & 2 ? "BottomUpHeap" : "SomeOtherHeap");
    });
    s.At({0x005BB150, AddressOf(&Manager::CurrentHeapName)}, 0, [](Regs& r) {
        Record({0xB150, r.ecx});
        return reinterpret_cast<uint32_t>("ScriptHeap");
    });
    s.At({0x005BAC10, AddressOf(&Manager::FirstHeap)}, 0, [](Regs& r) {
        Record({0xAC10, r.ecx});
        return g_heapCount ? reinterpret_cast<uint32_t>(g_heaps[0]) : 0u;
    });
    s.At({0x005BAC20, AddressOf(&Manager::NextHeap)}, 4, [](Regs& r) {
        Record({0xAC20, r.ecx});
        uint32_t index = (r.args[0] - reinterpret_cast<uint32_t>(g_heaps[0])) / sizeof(g_heaps[0]) + 1;
        return index < g_heapCount ? reinterpret_cast<uint32_t>(g_heaps[index]) : 0u;
    });
    s.At({0x005BC530, AddressOf(&Mem::Region::MemAvailable)}, 0, [](Regs& r) {
        uint32_t index = (r.ecx - reinterpret_cast<uint32_t>(g_regions[0])) / sizeof(g_regions[0]);
        Record({0xC530, index});
        // (it lowers the minimum, as the real one does: the dump must read the minimum first)
        reinterpret_cast<Mem::Region*>(r.ecx)->m_minAvailable = 1;
        return 0x1234567u * (index + 1) - 0x2000000u;
    });
    s.At({0x004B2270, AddressOf(&Nx::ToggleMemView)}, 0, [](Regs&) { Record({0x2270}); return 0u; });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        Record({0x1C30, Formatted(r, 0)});
        return 0xFFFFFFFFu;
    });
    s.At({0x0059ED60, AddressOf(&Mem::DumpText)}, 0, [](Regs& r) {
        Record({0xED60, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x005BC3B0, AddressOf(&Mem::PoolManager::GetPool)}, 0, [](Regs& r) {
        Record({0xC3B0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_pools[13]);
    });
    s.At({0x0047AE10}, 0, [](Regs& r) {
        Record({0xAE10, r.ecx});
        g_soundFree += 0x2F1;
        return static_cast<uint32_t>(g_soundFree);
    });
}

const uint32_t kPairs[13] = {0x006F3614, 0x006F2338, 0x006F362C, 0x006F3654, 0x006F3644, 0x006F22B4, 0x006F3548,
                             0x006F92B8, 0x006F92C8, 0x00701430, 0x006FA7F4, 0x00700500, 0x006F0FC8};

// The pool-usage dump through its register convention (BL, then a stack argument the caller pops).
void CallDumpPoolUsage(void* code, uint32_t toDumpFile, uint32_t print)
{
    __asm {
        push ebx
        mov ebx, toDumpFile
        push print
        call code
        add esp, 4
        pop ebx
    }
}

void TestEngineCommandsMemory()
{
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x007D1800, 4, {}}, {0x007CEFAC, 1, {}}, {0x006F85A8, 4, {}}};
    for (uint32_t pair : kPairs)
        saved.push_back({pair, 16, {}});
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_soundFree = 0x100000;
        std::memset(g_manager, 0, sizeof(g_manager));
        auto* manager = reinterpret_cast<Mem::Manager*>(g_manager);
        manager->m_bottomUpHeap = reinterpret_cast<Mem::Heap*>(0x4EA7B000);
        GameVariable<Mem::Manager*>(0x007D1800) = manager;
        GameVariable<uint32_t>(0x006F85A8) = 0x50DD0000;
        // the heaps: names, sizes (negative and odd ones for the K rounding), regions
        std::memset(g_heaps, 0, sizeof(g_heaps));
        std::memset(g_regions, 0, sizeof(g_regions));
        const char* names[3] = {"BottomUpHeap", "TopDownHeap", "Skater0"};
        for (int i = 0; i < 3; ++i) {
            auto* heap = reinterpret_cast<Mem::Allocator*>(g_heaps[i]);
            auto* region = reinterpret_cast<Mem::Region*>(g_regions[i]);
            heap->m_name = names[i];
            heap->m_usedSize = 0x123456 * (i + 1) + 1023;
            heap->m_freeSize = i == 1 ? -2047 : 0x7FF * i;
            heap->m_usedBlocks = 17 + i;
            heap->m_region = region;
            region->m_minAvailable = i == 2 ? -1025 : 0x40000 + 3 * i;
        }
        g_heapCount = 3;
        // the pools: each pair's current pool, numbers per pool
        std::memset(g_pools, 0, sizeof(g_pools));
        for (int i = 0; i < 14; ++i) {
            auto* pool = reinterpret_cast<Mem::Pool*>(g_pools[i]);
            pool->m_used = 3 * i + 1;
            pool->m_count = 100 + i;
            pool->m_peakUsed = 50 + 7 * i;
        }
        for (int i = 0; i < 13; ++i) {
            auto& pair = GameVariable<Mem::PoolPair>(kPairs[i]);
            pair.current = i & 1;
            pair.pools[i & 1] = reinterpret_cast<Mem::Pool*>(g_pools[i]);
            pair.pools[(i & 1) ^ 1] = reinterpret_cast<Mem::Pool*>(g_pools[13 - i]); // (the other one: not read)
        }
        auto call = [&](uint32_t address, CFunc ours, Params& params) { Call(original, address, ours, params); };
        auto callNull = [&](uint32_t address, CFunc ours) {
            bool result = original ? Original<CFunc>(address)(nullptr, nullptr) : ours(nullptr, nullptr);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;
        {
            Params a; a.String(0, "Skater geometry");
            call(0x00447FF0, CFuncs::CFunc_PushMemProfile, a);
            call(0x00448020, CFuncs::CFunc_PopMemProfile, none);
            Params b; b.Flag(0xD443A2BC);
            call(0x00448030, CFuncs::CFunc_SetMemThreadSafe, b);
            call(0x00448030, CFuncs::CFunc_SetMemThreadSafe, none);
            Params c; c.Integer(0, 4096).Flag(CrcOf("K"));
            call(0x00448060, CFuncs::CFunc_DumpFragments, c);
            call(0x00448090, CFuncs::CFunc_MemViewToggle, none);
            for (int extra : {0, 1, 0x80}) {
                GameVariable<uint8_t>(0x007CEFAC) = static_cast<uint8_t>(extra);
                call(0x004480A0, CFuncs::CFunc_GotExtraMemory, none);
            }
        }
        // MemPushContext: 0 (the bottom-up heap), another number, a heap name; MemPopContext
        {
            Params a; a.Integer(0, 0);
            call(0x004480B0, CFuncs::CFunc_MemPushContext, a);
            Params b; b.Integer(0, 2);
            call(0x004480B0, CFuncs::CFunc_MemPushContext, b);
            Params c; c.Checksum(0, 0x0FA33D9B);
            call(0x004480B0, CFuncs::CFunc_MemPushContext, c);
            Params d; d.Checksum(0, 0x0FA33D9C);
            call(0x004480B0, CFuncs::CFunc_MemPushContext, d);
            call(0x00448130, CFuncs::CFunc_MemPopContext, none);
        }
        // the named heaps: by string and by checksum, there or not
        {
            Params a; a.String(0xA1DC81F9, "LevelHeap").Integer(0x083FDB95, 0x200000);
            call(0x00448140, CFuncs::CFunc_MemInitHeap, a);
            for (uint32_t result : {0x101u, 0x200u}) {
                g_namedResult = result;
                Params b; b.String(0xA1DC81F9, "LevelHeap");
                call(0x004481A0, CFuncs::CFunc_MemDeleteHeap, b);
                call(0x00448200, CFuncs::CFunc_MemHeapExists, b);
                Params c; c.Checksum(0xA1DC81F9, 0xABCD1234);
                call(0x004481A0, CFuncs::CFunc_MemDeleteHeap, c);
                call(0x00448200, CFuncs::CFunc_MemHeapExists, c);
            }
        }
        // AnalyzeHeap / PrintMemInfo: the default, a heap given, a heap that isn't there, no params
        {
            Params a; a.Checksum(0, 0x8FDB68AF);
            Params b; b.Checksum(0, 0x8FDB68AD);
            call(0x00448270, CFuncs::CFunc_AnalyzeHeap, none);
            call(0x00448270, CFuncs::CFunc_AnalyzeHeap, a);
            call(0x00448270, CFuncs::CFunc_AnalyzeHeap, b);
            callNull(0x00448270, CFuncs::CFunc_AnalyzeHeap);
            call(0x004482C0, CFuncs::CFunc_PrintMemInfo, none);
            call(0x004482C0, CFuncs::CFunc_PrintMemInfo, a);
            callNull(0x004482C0, CFuncs::CFunc_PrintMemInfo);
        }
        // the pool-usage dump with each pair of flags
        for (uint32_t flags = 0; flags < 4; ++flags) {
            void* code = original ? OriginalFunction(0x00448330) : reinterpret_cast<void*>(&CFuncs::DumpPoolUsageStub);
            CallDumpPoolUsage(code, 0xFFFFFF00u | (flags & 1), flags >> 1);
        }
        // DumpHeaps: in K, in bytes, no params, no heaps
        {
            call(0x00448840, CFuncs::CFunc_DumpHeaps, none);
            Params a; a.Flag(0xD65B9AC7);
            call(0x00448840, CFuncs::CFunc_DumpHeaps, a);
            callNull(0x00448840, CFuncs::CFunc_DumpHeaps);
            g_heapCount = 0;
            call(0x00448840, CFuncs::CFunc_DumpHeaps, a);
        }
        for (int i = 0; i < 3; ++i)
            Record({static_cast<uint32_t>(reinterpret_cast<Mem::Region*>(g_regions[i])->m_minAvailable)});
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 300, "engine commands (memory): the original reached its calls");
    Compare(traces[0], traces[1], "engine commands (memory, 0x00447FF0-0x00448840)");
}

} // namespace

SELFTEST(Early, TestEngineCommandsMemory, RemoveStandIns);
