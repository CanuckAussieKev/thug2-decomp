// Self-tests for the script system's start-up, 0x005A5660-0x005A5F6F (script_startup.cpp): loading and unloading
// .qb files by name, the start-up .qb files listed in qdir.txt, the class pools and AllocatePools - each original
// against ours, the game functions they call stood in for by recording code (allocations from one arena per run).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/pip.h"
#include "game/script/load.h"
#include "game/script/names.h"
#include "game/script/qb.h"
#include "game/script/script_startup.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

constexpr unsigned int kArenaSize = 0x50000;
ArenaPair<kArenaSize> g_startupStorage;
uint8_t (*const g_startupArenas)[kArenaSize] = g_startupStorage.Get();
uint8_t* g_arena;
uint32_t g_arenaUsed;

alignas(16) uint8_t g_manager[0x600];
const char* g_qdirText;
int g_qdirSize;
uint32_t g_symbol;

uint32_t Relocate(uint32_t word) { return ArenaWord(g_startupArenas, kArenaSize, word); }

uint32_t Bump(uint32_t size)
{
    uint32_t at = (g_arenaUsed + 15) & ~15u;
    g_arenaUsed = at + size;
    return reinterpret_cast<uint32_t>(g_arena + at);
}

void StandInCalls(StandIns& s)
{
    s.At({0x0046EDE0, AddressOf(&Script::LoadQB)}, 0, [](Regs& r) {
        Record({0xEDE0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0046ED90, AddressOf(&Script::LoadQBData)}, 0, [](Regs& r) {
        Record({0xED90, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3], r.args[4]});
        return 0u;
    });
    s.At({0x0046EEC0, AddressOf(&Script::UnloadQB)}, 0, [](Regs& r) { Record({0xEEC0, r.args[0]}); return 0u; });
    using GetArrayFn = Script::CArray* (*)(uint32_t);
    s.At({0x00479070, AddressOf(static_cast<GetArrayFn>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return 0x0A770000u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, r.ecx == reinterpret_cast<uint32_t>(g_manager) ? 1u : r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, r.ecx == reinterpret_cast<uint32_t>(g_manager) ? 1u : r.ecx});
        return 0u;
    });
    s.At({0x005B75A0, AddressOf(&Pip::LoadPre)}, 0, [](Regs& r) {
        Record({0x75A0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x005B7C00, AddressOf(&Pip::Load)}, 0, [](Regs& r) {
        Record({0x7C00, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF, r.args[2], r.args[3], r.args[4]});
        return reinterpret_cast<uint32_t>(g_qdirText);
    });
    using SizeFn = int (*)(const char*);
    s.At({0x005B72E0, AddressOf(static_cast<SizeFn>(&Pip::GetFileSize))}, 0, [](Regs& r) {
        Record({0x72E0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return static_cast<uint32_t>(g_qdirSize);
    });
    s.At({0x00476300, AddressOf(&Script::BeginPermanentStrings)}, 0, [](Regs&) { Record({0x6300}); return 0u; });
    s.At({0x00476310, AddressOf(&Script::EndPermanentStrings)}, 0, [](Regs&) { Record({0x6310}); return 0u; });
    using UnloadFn = void (*)(const char*);
    s.At({0x005B7270, AddressOf(static_cast<UnloadFn>(&Pip::Unload))}, 0, [](Regs& r) {
        Record({0x7270, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x005B7130, AddressOf(&Pip::UnloadPre)}, 0, [](Regs& r) {
        Record({0x7130, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 1u;
    });
    s.At({0x00478CC0, AddressOf(&Script::FindSymbol)}, 0, [](Regs& r) { Record({0x8CC0, r.args[0]}); return g_symbol; });
    s.At({0x004711D0, AddressOf(&Script::CleanUpAndRemoveSymbol)}, 0, [](Regs& r) {
        Record({0x11D0, r.args[0]});
        return 0u;
    });
    // allocations: from the run's arena
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return Bump(r.args[0]);
    });
    s.At({0x004015D0, AddressOf(&Mem::New)}, 0, [](Regs& r) {
        Record({0xAFC0, r.args[0], 1, 0}); // (Mem::New: the manager's Allocate(size, 1, null))
        return Bump(r.args[0]);
    });
    s.At({0x0046F520, AddressOf(&Script::CreateScriptPools)}, 0, [](Regs&) { Record({0xF520}); return 0u; });
    s.At({0x0046EFD0, AddressOf(&Script::RegisterCFunctions)}, 0, [](Regs& r) {
        Record({0xEFD0, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046F020, AddressOf(&Script::RegisterMemberFunctions)}, 0, [](Regs& r) {
        Record({0xF020, r.args[0], r.args[1]});
        return 0u;
    });
}

using Creator = void(__cdecl*)(int, const char*);
struct PoolCreator {
    uint32_t address;
    Creator ours;
    uint32_t pools;
};
const PoolCreator kCreators[] = {
    {0x005A5B40, Script::CreateAnimCachePredictorPool, 0x006F92B8},
    {0x005A5BD0, Script::CreateBlendChannelPool, 0x006F92C8},
    {0x005A5C50, Script::CreateBonedAnimControllerPool, 0x00701430},
    {0x005A5CD0, Script::CreateAnimLookupPool, 0x006FA7F4},
    {0x005A5D60, Script::CreateSpawnedPolyDetailsPool, 0x00700500},
    {0x005A5DF0, Script::CreateEventReceiverListPool, 0x006F0FC8},
};

void RecordPools()
{
    for (const PoolCreator& creator : kCreators)
        for (int i = 0; i < 4; ++i)
            Record({Relocate(reinterpret_cast<const uint32_t*>(creator.pools)[i])});
}

void TestScriptStartup()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}, {0x007CC979, 1, {}}};
    for (const PoolCreator& creator : kCreators)
        saved.push_back({creator.pools, 16, {}});
    SaveAll(saved);
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    // The reference build's "qb_scripts.pre" (THUG2.exe has "qb_scripts.prx" there): ours names the .pre
    standIns->Bytes(0x006576A5, {'e'});

    // qdir.txt: paths with and without data\, CR LF and LF, empty lines, a last line without its end
    static const char kQdir[] = "c:\\game\\data\\scripts\\first.qb\r\nscripts\\second.qb\n\r\n\nxdata\\data\\third.qb\r\n"
                                "game\\levels\\last.qb";
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_arena = g_startupArenas[run];
        g_arenaUsed = 0;
        std::memset(g_arena, 0xCD, kArenaSize);
        std::memset(g_manager, 0, sizeof(g_manager));
        reinterpret_cast<uint32_t*>(g_manager)[0x110 / 4] = 0x70D00000; // the top-down heap
        reinterpret_cast<uint32_t*>(g_manager)[0x124 / 4] = 0x5C100000; // the script heap
        GameVariable<void*>(0x007D1800) = g_manager;
        GameVariable<bool>(0x007CC979) = true;
        for (const PoolCreator& creator : kCreators)
            std::memset(reinterpret_cast<void*>(creator.pools), 0, 16);

        // the counts
        Record({original ? Original<uint32_t (*)()>(0x005A5660)() : Script::GetNumCFunctions(),
                original ? Original<uint32_t (*)()>(0x005A5670)() : Script::GetNumMemberFunctions()});
        // load / unload by name
        for (int unused : {0, 1}) {
            if (original)
                Original<void (*)(const char*, int)>(0x005A5960)("levels\\x.qb", unused);
            else
                Script::LoadQBFile("levels\\x.qb", unused);
            const uint8_t data[4] = {};
            if (original)
                Original<void (*)(const char*, const uint8_t*, int, int)>(0x005A5990)("y.qb", data, unused, 7 - unused);
            else
                Script::LoadQBFileData("y.qb", data, unused, 7 - unused);
        }
        if (original)
            Original<void (*)(uint32_t)>(0x005A59B0)(0x12345678);
        else
            Script::UnloadQBFile(0x12345678);
        // the start-up files: the whole text, a size ending inside a line, no text; the symbol there or not
        for (int variant = 0; variant < 3; ++variant) {
            g_qdirText = kQdir;
            g_qdirSize = variant == 0 ? static_cast<int>(sizeof(kQdir) - 1) : variant == 1 ? 40 : 0;
            g_symbol = variant == 1 ? 0 : 0x5B000000u + variant;
            if (variant == 2) {
                if (original)
                    Original<void (*)()>(0x005A5B20)();
                else
                    Script::LoadAllStartupQBFiles();
            } else if (original) {
                Original<void (*)()>(0x005A59D0)();
            } else {
                Script::LoadStartupQBFiles();
            }
        }
        // the pool creators: each pair's current pool (0 or 1)
        for (const PoolCreator& creator : kCreators) {
            for (uint32_t current : {0u, 1u}) {
                reinterpret_cast<uint32_t*>(creator.pools)[0] = current;
                if (original)
                    Original<Creator>(creator.address)(3 + static_cast<int>(current), "pool name");
                else
                    creator.ours(3 + static_cast<int>(current), "pool name");
            }
        }
        RecordPools();
        // AllocatePools
        for (const PoolCreator& creator : kCreators)
            std::memset(reinterpret_cast<void*>(creator.pools), 0, 16);
        reinterpret_cast<uint32_t*>(kCreators[3].pools)[0] = 1;
        if (original)
            Original<void (*)()>(0x005A5E70)();
        else
            Script::AllocatePools();
        RecordPools();

        for (uint32_t& word : g_trace)
            word = Relocate(word);
        for (uint32_t i = 0; i < g_arenaUsed / 4; ++i)
            g_trace.push_back(ArenaWordOrHigh(g_startupArenas, kArenaSize, reinterpret_cast<uint32_t*>(g_arena)[i]));
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 1000, "script start-up: the original reached its calls");
    Compare(traces[0], traces[1], "script start-up (0x005A5660-0x005A5F6F)");
}

} // namespace

SELFTEST(Early, TestScriptStartup, RemoveStandIns);
