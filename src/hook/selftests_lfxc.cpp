// Self-tests for 0x0059C900-0x0059F610 (park editor memory, the memory dump's text buffer, the skate game's script
// commands): each original against ours on identical stand-in state; every game function they call is stood in
// for by recording code (over the game's function and our version of it alike), so they're compared call by call.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/alloc.h"
#include "game/core/dump_text.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/ed/park.h"
#include "game/file/file.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/node_prefixes.h"
#include "game/script/nodes.h"
#include "game/script/skate_cfuncs.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/sys/viewer.h"
#include "game/gfx/viewport.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CComponent;
using Script::CStruct;
using Script::ESymbolType;

// A script structure built by hand (no memory manager needed): components chained in the order added.
struct TestParams {
    alignas(16) uint8_t storage[8];
    CComponent components[12];
    int count = 0;
    TestParams() { std::memset(storage, 0, sizeof(storage)); std::memset(components, 0, sizeof(components)); }
    CStruct* Struct() { return reinterpret_cast<CStruct*>(storage); }
    TestParams& Add(ESymbolType type, uint32_t name, uint32_t raw)
    {
        CComponent& c = components[count];
        c.typeAndFlag = static_cast<uint8_t>(type << 1);
        c.name = name;
        c.raw = raw;
        if (count)
            components[count - 1].next = &c;
        else
            reinterpret_cast<CComponent**>(storage)[1] = &c;
        ++count;
        return *this;
    }
    TestParams& Checksum(uint32_t name, uint32_t value) { return Add(Script::ESYMBOLTYPE_NAME, name, value); }
    TestParams& Flag(uint32_t flag) { return Add(Script::ESYMBOLTYPE_NAME, 0, flag); }
    TestParams& Integer(uint32_t name, int value) { return Add(Script::ESYMBOLTYPE_INTEGER, name, static_cast<uint32_t>(value)); }
    TestParams& Float(uint32_t name, float value)
    {
        uint32_t raw;
        std::memcpy(&raw, &value, 4);
        return Add(Script::ESYMBOLTYPE_FLOAT, name, raw);
    }
    TestParams& String(uint32_t name, const char* value) { return Add(Script::ESYMBOLTYPE_STRING, name, reinterpret_cast<uint32_t>(value)); }
    TestParams& Structure(uint32_t name, CStruct* value) { return Add(Script::ESYMBOLTYPE_STRUCTUREPOINTER, name, reinterpret_cast<uint32_t>(value)); }
};

uint32_t g_now;
uint32_t g_casArtist;
void* g_viewer;
uint32_t g_spawned;
uint32_t g_fakeNodes[4];
alignas(16) uint8_t g_script[0x100];
alignas(16) uint8_t g_controller[0x20];
alignas(16) uint8_t g_input[0x20];
// The calling script's address, made position-independent.
uint32_t Rel(uint32_t value)
{
    auto script = reinterpret_cast<uint32_t>(g_script);
    if (value >= script && value < script + sizeof(g_script))
        return 0x5C000000u + (value - script);
    return value;
}

// Every function the commands call, stood in for (the game's address and ours).
void StandInCalls(StandIns& s)
{
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) { Record({0x7135}); return g_now; });
    s.At({0x004901D0}, 0, [](Regs& r) { Record({0x01D0, r.args[0], r.args[1]}); return 0u; });
    s.At({0x0045BF90, AddressOf(static_cast<uint32_t* (*)(uint32_t, uint16_t*)>(&SkateScript::GetPrefixedNodes))}, 0, [](Regs& r) {
        // (the original passes a third argument, exact-or-prefix, that 0x0045BF90 never reads - ours doesn't pass it)
        Record({0xBF90, r.args[0], *reinterpret_cast<uint32_t*>(r.args[1])});
        *reinterpret_cast<uint32_t*>(r.args[1]) = 0x12340005; // a 16-bit count: only the low half is read
        return reinterpret_cast<uint32_t>(g_fakeNodes);
    });
    s.At({0x0048FE40}, 0, [](Regs& r) { Record({0xFE40, r.args[0], r.args[1], r.args[2], r.args[3]}); return 0u; });
    s.At({0x00491F10}, 0, [](Regs&) { Record({0x1F10}); return 0u; });
    s.At({0x0045BE70, AddressOf(&SkateScript::NodeExists)}, 0, [](Regs& r) { Record({0xBE70, r.args[0]}); return r.args[0] & 0x101; });
    s.At({0x004753C0, AddressOf(&Script::DeleteSpawnedScripts)}, 0, [](Regs&) { Record({0x53C0}); return 0u; });
    s.At({0x005A5960}, 0, [](Regs& r) {
        Record({0x5960, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x0045C210, AddressOf(&SkateScript::GetLastNodeArray)}, 0, [](Regs&) { Record({0xC210}); return 0u; });
    s.At({0x004A0950, AddressOf(Nx::GetCamera)}, 0, [](Regs& r) { Record({0x0950, r.args[0]}); return r.args[0] == 1 ? 0x77u : 0u; });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0xFFFFFFFFu;
    });
    s.At({0x00576E80, AddressOf(&Mdl::SetMovementVelocity)}, 0, [](Regs& r) { Record({0x6E80, r.args[0]}); return 0u; });
    s.At({0x00576E90, AddressOf(&Mdl::SetRotateVelocity)}, 0, [](Regs& r) { Record({0x6E90, r.args[0]}); return 0u; });
    s.At({0x00576EA0, AddressOf(&Mdl::GetViewer)}, 0, [](Regs&) { Record({0x6EA0}); return reinterpret_cast<uint32_t>(g_viewer); });
    s.At({0x00576D40, AddressOf(&Mdl::Viewer::RemoveViewerObject)}, 0, [](Regs& r) { Record({0x6D40, r.ecx}); return 0u; });
    s.At({0x004790C0, AddressOf(static_cast<int (*)(const char*)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x90C0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_casArtist;
    });
    using SpawnFn = Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t, bool, bool, bool, bool);
    s.At({0x00475420, AddressOf(static_cast<SpawnFn>(&Script::SpawnScript))}, 0, [](Regs& r) {
        Record({0x5420, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
        return g_spawned;
    });
    s.At({0x00472F90, AddressOf(&Script::CScript::SetSpawnerFile)}, 4, [](Regs& r) {
        Record({0x2F90, r.ecx, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00472F40, AddressOf(&Script::CScript::UnblockInterruptedScript)}, 0, [](Regs& r) {
        Record({0x2F40, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00473FA0, AddressOf(&Script::KillSpawnedScriptsThatReferTo)}, 0, [](Regs& r) {
        Record({0x3FA0, r.args[0]});
        return 0u;
    });
    s.At({0x00474040, AddressOf(&Script::KillSpawnedScriptsWithId)}, 0, [](Regs& r) {
        Record({0x4040, r.args[0]});
        return 0u;
    });
    s.At({0x00472D80, AddressOf(&Script::PauseSpawnedScripts)}, 0, [](Regs& r) {
        Record({0x2D80, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00490890}, 0, [](Regs& r) {
        Record({0x0890, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF, r.args[2] & 0xFF});
        return 0u;
    });
    s.At({0x00490770}, 0, [](Regs& r) { Record({0x0770, r.args[0]}); return 0u; });
    s.At({0x004908C0}, 0, [](Regs&) { Record({0x08C0}); return 0u; });
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x005B9F60, AddressOf(&File::Write)}, 0, [](Regs& r) {
        Record({0x9F60, HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), r.args[1] * r.args[2]), r.args[1],
                r.args[2], r.args[3]});
        return r.args[2];
    });
}

using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);

void TestSkateCFuncs()
{
    // the globals: the level name .. the Omnigon (0x007CE698-0x007CEBCC, with the node array name, the debounce
    // times and the dump buffer), the symbol table (empty)
    std::vector<Saved> saved = {{0x007CE698, 0x534, {}}, {0x006F3624, 4, {}}};
    SaveAll(saved);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;

    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        std::memset(reinterpret_cast<void*>(0x007CE698), 0, 0x534);
        GameVariable<uint32_t>(0x007CEBB8) = 0x0BADF11E; // the dump file
        GameVariable<uint32_t>(0x007CE7B0) = 0x007CE7B8;  // the dump position
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) {
            std::memset(g_script, 0, sizeof(g_script));
            reinterpret_cast<int*>(g_script)[0xB8 / 4] = 42; // m_node
            auto* script = reinterpret_cast<Script::CScript*>(g_script);
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };

        // ControllerDebounce: the clock's top bit set (unsigned), fractions of a millisecond, no button, a non-button
        g_now = 0x80001234;
        {
            TestParams a; a.Checksum(0, 0x7323E97C); call(0x0059EDF0, CFuncs::CFunc_ControllerDebounce, a);
            TestParams b; b.Float(0x906B67BA, 0.2537f).Checksum(0, 0x26B0C991); call(0x0059EDF0, CFuncs::CFunc_ControllerDebounce, b);
            TestParams c; c.Float(0x906B67BA, 3.0f); call(0x0059EDF0, CFuncs::CFunc_ControllerDebounce, c);
            TestParams d; d.Checksum(0, 0x12345678).Float(0x906B67BA, 0.0015f); call(0x0059EDF0, CFuncs::CFunc_ControllerDebounce, d);
            g_now = 0x00001000;
            TestParams e; e.Checksum(0, 0x767A45D7).Float(0x906B67BA, 1234.5678f); call(0x0059EDF0, CFuncs::CFunc_ControllerDebounce, e);
            for (int i = 0; i < 22; ++i)
                Record({CFuncs::DebounceTimes()[i]});
        }

        // ControllerPressed: every button (and an unknown one) over input patterns, with some buttons debounced
        {
            const uint32_t buttons[] = {0xBC6B118F, 0xE3006FC4, 0x85981897, 0x4B358AEB, 0xB7231A95, 0xA50950C5,
                                        0xD8847EFA, 0x786B8B68, 0x7323E97C, 0x20689278, 0x321C9756, 0x2B489A86,
                                        0x26B0C991, 0xBFB9982B, 0xF2F1F64E, 0x6BF8A7F4, 0x767A45D7, 0xBD30325B,
                                        0x9D2D8850, 0x11111111};
            g_now = 5000;
            uint32_t* debounce = CFuncs::DebounceTimes();
            for (int i = 0; i < 22; ++i)
                debounce[i] = i % 5 == 2 ? 5001u : (i % 3 == 0 ? 5000u : 4000u);
            reinterpret_cast<void**>(g_controller)[0x18 / 4] = g_input;
            uint32_t mask = 0x2468ACE1;
            using PressedFn = bool(__cdecl*)(const void*, uint32_t);
            for (int pattern = 0; pattern < 24; ++pattern) {
                mask = mask * 1103515245u + 12345u;
                for (int k = 0; k < 0x20; ++k)
                    g_input[k] = static_cast<uint8_t>(((mask >> (k % 32)) & 1) ? (k * 3 + 1) : 0);
                if (pattern == 0)
                    std::memset(g_input, 1, sizeof(g_input));
                for (uint32_t button : buttons) {
                    bool pressed = original ? Original<PressedFn>(0x0059EE80)(g_controller, button)
                                            : CFuncs::ControllerPressed(g_controller, button);
                    Record({0xEE80, button, pressed ? 1u : 0u});
                }
            }
            bool none = original ? Original<PressedFn>(0x0059EE80)(nullptr, 0x7323E97C)
                                 : CFuncs::ControllerPressed(nullptr, 0x7323E97C);
            reinterpret_cast<void**>(g_controller)[0x18 / 4] = nullptr;
            bool noInput = original ? Original<PressedFn>(0x0059EE80)(g_controller, 0x7323E97C)
                                    : CFuncs::ControllerPressed(g_controller, 0x7323E97C);
            Record({0xEE81, none ? 1u : 0u, noInput ? 1u : 0u});
        }

        // FakeLights: by id; by prefix (a name; a string; the flag; a time); neither
        {
            TestParams a; a.Checksum(0x40C698AF, 0xABCD).Checksum(0x6C4E7971, 0x99); call(0x0059F0F0, CFuncs::CFunc_FakeLights, a);
            TestParams b; b.Checksum(0x6C4E7971, 0x5151).Integer(0x906B67BA, 7); call(0x0059F0F0, CFuncs::CFunc_FakeLights, b);
            TestParams c; c.String(0x6C4E7971, "TRG_Light").Flag(0x80804991); call(0x0059F0F0, CFuncs::CFunc_FakeLights, c);
            TestParams d; d.Integer(0x906B67BA, 9); call(0x0059F0F0, CFuncs::CFunc_FakeLights, d);
        }
        {
            TestParams none;
            call(0x0059F1B0, CFuncs::CFunc_UpdateFxLighting, none);
            TestParams a; a.Checksum(0, 0x101); call(0x0059F1C0, CFuncs::CFunc_NodeExists, a);
            TestParams b; b.Checksum(0, 0x200); call(0x0059F1C0, CFuncs::CFunc_NodeExists, b);
            call(0x0059F1C0, CFuncs::CFunc_NodeExists, none);
            TestParams c; c.String(0, "levels\\la\\la_scripts"); call(0x0059F1F0, CFuncs::CFunc_LoadNodeArray, c);
            call(0x0059F260, CFuncs::CFunc_ResetCamera, none);
            call(0x0059F290, CFuncs::CFunc_SetMovementVelocity, none);
            TestParams d; d.Float(0, 3.5f); call(0x0059F290, CFuncs::CFunc_SetMovementVelocity, d);
            call(0x0059F2C0, CFuncs::CFunc_SetRotateVelocity, none);
            TestParams e; e.Float(0, -7.25f); call(0x0059F2C0, CFuncs::CFunc_SetRotateVelocity, e);
            g_viewer = nullptr;
            call(0x0059F2F0, CFuncs::CFunc_ClearViewerObject, none);
            g_viewer = reinterpret_cast<void*>(0x00C0FFEE);
            call(0x0059F2F0, CFuncs::CFunc_ClearViewerObject, none);
            g_casArtist = 0;
            call(0x0059F310, CFuncs::CFunc_CasArtist, none);
            g_casArtist = 0x100;
            call(0x0059F310, CFuncs::CFunc_CasArtist, none);
        }
        // SpawnScript: everything given; only the name (the spawner file noted); the keep-spawner flag
        {
            TestParams inner; inner.Integer(0x1234, 5);
            TestParams a;
            a.Checksum(0, 0x5C417).Structure(0x7031F10C, inner.Struct()).Checksum(0x40C698AF, 0x1D)
                .Checksum(0x86068BD9, 0xCA11).Structure(0x0DD85BDA, inner.Struct()).Flag(0x20209C31)
                .Flag(0x23627FD7).Integer(0x2EBFF4BC, 3);
            g_spawned = 0x00ABC000;
            call(0x0059F330, CFuncs::CFunc_SpawnScript, a);
            TestParams b; b.Checksum(0, 0x5C418).Flag(0x23627FD7);
            call(0x0059F330, CFuncs::CFunc_SpawnScript, b);
            TestParams c; c.Checksum(0, 0x5C419).Flag(0x01917DF2).Flag(0x20209C31).Integer(0x2EBFF4BC, 0);
            call(0x0059F330, CFuncs::CFunc_SpawnScript, c);
        }
        {
            TestParams none;
            call(0x0059F440, CFuncs::CFunc_UnBlockInterruptedScript, none);
            TestParams a; a.Checksum(0x9F09D9E6, 0x10); // (an unrelated name)
            call(0x0059F450, CFuncs::CFunc_KillSpawnedScript, a);
            TestParams b; b.Checksum(0xA1DC81F9, 0x20).Checksum(0x40C698AF, 0x21); // Name and Id: Name wins
            call(0x0059F450, CFuncs::CFunc_KillSpawnedScript, b);
            TestParams c; c.Checksum(0x40C698AF, 0x31);
            call(0x0059F450, CFuncs::CFunc_KillSpawnedScript, c);
            call(0x0059F4C0, CFuncs::CFunc_PauseSpawnedScripts, none);
            call(0x0059F4D0, CFuncs::CFunc_UnPauseSpawnedScripts, none);
            call(0x0059F4E0, CFuncs::CFunc_DisplayLoadingScreen, none);
            TestParams d; d.String(0, "images\\load").Flag(0xB96E0BE5).Float(0, 2.5f);
            call(0x0059F4E0, CFuncs::CFunc_DisplayLoadingScreen, d);
            TestParams e; e.Flag(0xC3D43B9A);
            call(0x0059F4E0, CFuncs::CFunc_DisplayLoadingScreen, e);
            call(0x0059F550, CFuncs::CFunc_HideLoadingScreen, none);
            GameVariable<uint32_t>(0x007CEBC8) = 0x0A61;
            TestParams f; f.Checksum(0xAE94C183, 0x0A61);
            call(0x0059F560, CFuncs::CFunc_LastBroadcastedOmnigonWas, f);
            TestParams g; g.Checksum(0xAE94C183, 0x0A62);
            call(0x0059F560, CFuncs::CFunc_LastBroadcastedOmnigonWas, g);
            call(0x0059F590, CFuncs::CFunc_OmnigonAllowed, f);
            TestParams h; h.String(0, "Downhill Jam");
            call(0x0059F5B0, CFuncs::CFunc_SetLevelName, h);
            call(0x0059F5E0, CFuncs::CFunc_DumpScripts, none);
            TestParams i; i.Flag(0x2AB71EEF);
            call(0x0059F5E0, CFuncs::CFunc_DumpScripts, i);
            TestParams j; j.Flag(Crc::GenerateCRCFromString("just_stack"));
            call(0x0059F5E0, CFuncs::CFunc_DumpScripts, j);
        }

        // DumpText: lines (newlines become CR LF), nothing, then text that doesn't fit (a flush first)
        {
            using DumpFn = void(__cdecl*)(const char*);
            static char big[0x300];
            std::memset(big, 'x', sizeof(big) - 1);
            big[0x40] = '\n';
            static char huge[0x3F5]; // (into an empty buffer: written past its end, not flushed - to 0x007CEBAC)
            std::memset(huge, 'y', sizeof(huge) - 1);
            const char* texts[] = {huge, "Pool usage\n", "", "a\nb\n\nc", big, big, "\n", big};
            for (const char* text : texts) {
                if (original)
                    Original<DumpFn>(0x0059ED60)(text);
                else
                    Mem::DumpText(text);
                Record({0xED60, GameVariable<uint32_t>(0x007CE7B0)});
            }
        }

        traces[run] = g_trace;
        const uint32_t* globals = reinterpret_cast<const uint32_t*>(0x007CE698);
        traces[run].insert(traces[run].end(), globals, globals + 0x534 / 4);
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 700, "skate script commands: the original reached its calls");
    Compare(traces[0], traces[1], "skate script commands (0x0059EDF0-0x0059F5E0), ControllerPressed, DumpText");
}

// --- the park editor's memory budget (0x0059E100-0x0059ED5F) -------------------------------------------------

// Stand-in memory: a bump allocator's arena (the pools and regions Init makes), the manager, two heaps and their
// regions, the park heap and region, the script's pools, the park manager and its map.
alignas(16) uint8_t g_parkArena[0x40000 + 16000 * 12];
uint32_t g_parkArenaUsed;
alignas(16) uint8_t g_fakeManager[sizeof(Mem::Manager)];
alignas(16) uint8_t g_fakeHeaps[3][sizeof(Mem::Heap)]; // main (bottom-up), top-down, park
alignas(16) uint8_t g_fakeRegions[3][sizeof(Mem::Region)];
alignas(16) uint8_t g_fakePools[3][sizeof(Mem::Pool)]; // components, vectors, arrays
alignas(16) uint8_t g_fakeParkManager[0x20];
alignas(16) uint8_t g_fakeParkMap[0x40];
int g_regionAvailable[3];
uint32_t g_largestFree;
int g_themeArg;
int g_valueShift; // varies the script's figures between scenarios
alignas(16) uint8_t g_structs_lfxc[8][16]; // the structures GetMemoryValue makes
uint32_t g_nextStruct_lfxc;
void* g_fakeRegionVtable[1]; // slot 0: the scalar deleting destructor (stood in)

Mem::Heap* FakeHeap(int i) { return reinterpret_cast<Mem::Heap*>(g_fakeHeaps[i]); }
Mem::Region* FakeRegion(int i) { return reinterpret_cast<Mem::Region*>(g_fakeRegions[i]); }
Mem::Pool* FakePool(int i) { return reinterpret_cast<Mem::Pool*>(g_fakePools[i]); }

// Addresses of the stand-in objects, made position-independent.
uint32_t ParkRel(uint32_t value)
{
    struct Range {
        const void* base;
        uint32_t size;
        uint32_t tag;
    };
    const Range ranges[] = {{g_parkArena, sizeof(g_parkArena), 0xA0000000u},
                            {g_fakeManager, sizeof(g_fakeManager), 0xA1000000u},
                            {g_fakeHeaps, sizeof(g_fakeHeaps), 0xA2000000u},
                            {g_fakeRegions, sizeof(g_fakeRegions), 0xA3000000u},
                            {g_fakePools, sizeof(g_fakePools), 0xA4000000u},
                            {g_fakeParkManager, sizeof(g_fakeParkManager), 0xA5000000u},
                            {g_fakeParkMap, sizeof(g_fakeParkMap), 0xA6000000u},
                            {g_structs_lfxc, sizeof(g_structs_lfxc), 0xA7000000u}};
    for (const Range& r : ranges) {
        auto base = reinterpret_cast<uint32_t>(r.base);
        if (value >= base && value < base + r.size)
            return r.tag + (value - base);
    }
    return value;
}

// The script's memory figures, by name (and index): fixed numbers, shifted per scenario.
int MemoryValue(const char* name, int index)
{
    struct Figure {
        const char* name;
        int value;
    };
    const Figure figures[] = {{"main_heap_pad", 0x20000},   {"anim_set_mem", 0x11000},
                              {"park_heap_pad", 0x1100},    {"theme_pad", 0x3000},
                              {"main_heap_base", 0x400000}, {"park_heap_base", 0x100000},
                              {"top_down_required", 0x80000}, {"component_use_base", 100},
                              {"vector_use_base", 50},      {"CArray_use_base", 25},
                              {"max_dma_pieces", 300},      {"floor_piece_size_park", 0x800},
                              {"floor_piece_size_main", 0x400}, {"park_heap_size", 0x2000}};
    for (const Figure& f : figures) {
        if (std::strcmp(f.name, name) == 0)
            return f.value + (index >= 0 ? index : 0) + g_valueShift * (f.value / 7 + 3);
    }
    return 12345;
}

void StandInParkMemory(StandIns& s)
{
    s.At({0x0059E5C0, AddressOf(&Ed::GetMemoryValue)}, 0, [](Regs& r) {
        auto* name = reinterpret_cast<const char*>(r.args[0]);
        Record({0xE5C0, Hash(name), r.args[1]});
        return static_cast<uint32_t>(MemoryValue(name, static_cast<int>(r.args[1])));
    });
    s.At({0x00595200, AddressOf(&Ed::CParkManager::sInstance)}, 0, [](Regs&) { return reinterpret_cast<uint32_t>(g_fakeParkManager); });
    s.At({0x005950B0, AddressOf(&Ed::CParkManager::GetShellSize)}, 0, [](Regs& r) { Record({0x50B0, ParkRel(r.ecx)}); return static_cast<uint32_t>(g_themeArg); });
    s.At({0x00592AB0, AddressOf(&Ed::CParkGenerator::GetNodeCounts)}, 4, [](Regs& r) {
        Record({0x2AB0, ParkRel(r.ecx), r.args[0]});
        auto* counts = reinterpret_cast<int*>(r.args[0]);
        for (int i = 0; i < 6; ++i)
            counts[i] = 7 + i * 13 + g_valueShift;
        return r.args[0];
    });
    s.At({0x005BC530, AddressOf(&Mem::Region::MemAvailable)}, 0, [](Regs& r) {
        Record({0xC530, ParkRel(r.ecx)});
        for (int i = 0; i < 3; ++i) {
            if (r.ecx == reinterpret_cast<uint32_t>(FakeRegion(i)))
                return static_cast<uint32_t>(g_regionAvailable[i]);
        }
        return static_cast<uint32_t>(g_regionAvailable[2] + 0x10); // (a region Init made)
    });
    s.At({0x005BA4F0, AddressOf(&Mem::Heap::LargestFree)}, 0, [](Regs& r) {
        Record({0xA4F0, ParkRel(r.ecx)});
        return g_largestFree;
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        static TestParams table;
        if (!table.count)
            table.Integer(0x1111, 4).Integer(0x2222, -3).Integer(0x3333, 0);
        return reinterpret_cast<uint32_t>(table.Struct());
    });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), ParkRel(r.args[1])});
        return 0xFFFFFFFFu;
    });
    // the memory manager
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        uint32_t size = (r.args[0] + 15) & ~15u;
        Record({0xAFC0, ParkRel(r.ecx), r.args[0], r.args[1], r.args[2]});
        if (g_parkArenaUsed + size > sizeof(g_parkArena))
            return 0u;
        uint8_t* block = g_parkArena + g_parkArenaUsed;
        g_parkArenaUsed += size;
        std::memset(block, 0xCD, size);
        return reinterpret_cast<uint32_t>(block);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, ParkRel(r.ecx), ParkRel(r.args[0])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, ParkRel(r.ecx), ParkRel(r.args[0])});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, ParkRel(r.ecx)});
        return 0u;
    });
    s.At({0x005BB4E0, AddressOf(&Mem::Manager::AddHeap)}, 12, [](Regs& r) {
        Record({0xB4E0, ParkRel(r.ecx), ParkRel(r.args[0]), r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
        return reinterpret_cast<uint32_t>(FakeHeap(2));
    });
    g_fakeRegionVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xDE57, ParkRel(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x005BABC0, AddressOf(&Mem::Manager::DeleteHeap)}, 4, [](Regs& r) {
        Record({0xABC0, ParkRel(r.ecx), ParkRel(r.args[0])});
        return 0u;
    });
}

// The stand-in state of one scenario (the same for both runs).
void SetUpParkState(int scenario)
{
    std::memset(g_fakeManager, 0, sizeof(g_fakeManager));
    std::memset(g_fakeHeaps, 0, sizeof(g_fakeHeaps));
    std::memset(g_fakeRegions, 0, sizeof(g_fakeRegions));
    std::memset(g_fakePools, 0, sizeof(g_fakePools));
    auto* manager = reinterpret_cast<Mem::Manager*>(g_fakeManager);
    manager->m_bottomUpHeap = FakeHeap(0);
    manager->m_topDownHeap = FakeHeap(1);
    for (int i = 0; i < 3; ++i) {
        FakeHeap(i)->m_region = FakeRegion(i);
        FakeRegion(i)->m_vtable = g_fakeRegionVtable;
    }
    GameVariable<int>(0x006F2338) = 0;
    GameVariable<int>(0x006F3654) = 0;
    GameVariable<int>(0x006F22B4) = 0;
    // (main heap: free blocks; park heap: used and free)
    FakeHeap(0)->m_freeSize = 0x30000 + scenario * 0x9000;
    FakeHeap(2)->m_usedSize = 0x6000 + scenario * 0x100;
    FakeHeap(2)->m_freeSize = 0x800 + scenario * 0x40;
    const int mainAvailable[] = {0x200000, 0x10000, 0x60000, 0x3000, 0x90000, 0x48000};
    g_regionAvailable[0] = mainAvailable[scenario % 6];
    g_regionAvailable[1] = 0x50000 + (scenario % 3) * 0x40000; // top-down: under and over its requirement
    g_regionAvailable[2] = 0x30000 - scenario * 0x7000;
    const uint32_t largest[] = {0x7FFFFFFF, 0x1000, 0x40000, 0x18000, 0x10, 0x30000};
    g_largestFree = largest[scenario % 6];
    g_themeArg = scenario * 3 - 2;
    g_valueShift = scenario % 4 - 1;
    reinterpret_cast<void**>(g_fakeParkManager)[0] = g_fakeParkMap;
    reinterpret_cast<int*>(g_fakeParkMap)[0x20 / 4] = scenario % 3;
    // the script's pools: room for more or fewer items than the park has
    for (int i = 0; i < 3; ++i) {
        FakePool(i)->m_count = 1000 - i * 200 + scenario * 37;
        FakePool(i)->m_used = 400 + i * 150 - scenario * 21;
    }
    GameVariable<Mem::Pool*>(0x006F2338 + 8) = FakePool(0);
    GameVariable<Mem::Pool*>(0x006F3654 + 8) = FakePool(1);
    GameVariable<Mem::Pool*>(0x006F22B4 + 8) = FakePool(2);
    Ed::ParkHeap() = FakeHeap(2);
    Ed::ParkRegion() = FakeRegion(2);
    Ed::DmaPieces() = 40 + scenario * 47;
    Ed::CountsDirty() = scenario % 2 == 0;
    if (scenario == 10) {
        // the main heap's free memory exactly its pad (fragmented: the largest block is small)
        int pad = MemoryValue("main_heap_pad", -1) + (scenario % 3) * 0x8EF1C - 0x8EF1C;
        g_regionAvailable[0] = 2 * pad + MemoryValue("anim_set_mem", -1) - FakeHeap(0)->m_freeSize;
        g_largestFree = 0x10;
    } else if (scenario == 11 || scenario == 13) {
        // plenty free, more than a float holds exactly (the park heap the fullest); or just a little
        int pad = MemoryValue("main_heap_pad", -1) + (scenario % 3) * 0x8EF1C - 0x8EF1C;
        int target = scenario == 11 ? 0x1234567 : 100;
        g_regionAvailable[0] = target + MemoryValue("anim_set_mem", -1) + pad + MemoryValue("theme_pad", g_themeArg) -
                               FakeHeap(0)->m_freeSize;
        g_largestFree = 0x7FFFFFFF;
        g_regionAvailable[2] = 0x400;
    } else if (scenario == 12) {
        // the main heap's free memory 5 floor pieces (and a bit) over 10% of its base: room for 5, not 6
        int pad = MemoryValue("main_heap_pad", -1) + (scenario % 3) * 0x8EF1C - 0x8EF1C;
        int target = MemoryValue("main_heap_base", -1) / 10 + 5 * MemoryValue("floor_piece_size_main", -1) + 100;
        g_regionAvailable[0] = target + MemoryValue("anim_set_mem", -1) + pad + MemoryValue("theme_pad", g_themeArg) -
                               FakeHeap(0)->m_freeSize;
        g_largestFree = 0x7FFFFFFF;
        g_regionAvailable[2] = 0x40000;
        Ed::DmaPieces() = 40;
    }
}

void RecordParkGlobals()
{
    const auto* words = reinterpret_cast<const uint32_t*>(0x007CE5B4);
    for (uint32_t i = 0; i < (0x007CE698 - 0x007CE5B4) / 4; ++i)
        Record({ParkRel(words[i])});
}

// Every function of the park editor's memory budget: original against ours. GetMemoryValue on its own (the
// structures, the script run and its result stood in for); the rest with GetMemoryValue stood in for.
void TestParkMemory()
{
    std::vector<Saved> saved = {{0x007CE5B4, 0x007CE698 - 0x007CE5B4, {}}, {0x006F2338, 16, {}}, {0x006F3654, 16, {}},
                                {0x006F22B4, 16, {}}, {0x006F3614, 16, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}},
                                {0x006F3624, 4, {}}};
    SaveAll(saved);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<void*>(0x007D1800) = g_fakeManager;

    std::vector<uint32_t> traces[2];
    // 1. GetMemoryValue
    {
        auto* standIns = new StandIns;
        StandIns& s = *standIns;
        s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs&) {
            Record({0xA3D0});
            return reinterpret_cast<uint32_t>(g_structs_lfxc[g_nextStruct_lfxc++ & 7]);
        });
        s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) { Record({0x6410, ParkRel(r.ecx)}); return r.ecx; });
        s.At({0x00477090, 0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
            Record({0x7090, ParkRel(r.ecx)});
            return 0u;
        });
        s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
            Record({0x9070, ParkRel(r.args[0])});
            return 0u;
        });
        using AddChecksumFn = void (CStruct::*)(uint32_t, uint32_t);
        using AddIntegerFn = void (CStruct::*)(uint32_t, int);
        s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
            Record({0x7D40, ParkRel(r.ecx), r.args[0], r.args[1]});
            return 0u;
        });
        s.At({0x00477B80, AddressOf(static_cast<AddIntegerFn>(&CStruct::AddInteger))}, 8, [](Regs& r) {
            Record({0x7B80, ParkRel(r.ecx), r.args[0], r.args[1]});
            return 0u;
        });
        s.At({0x004752F0, AddressOf(&Script::RunScriptOnce)}, 0, [](Regs& r) {
            Record({0x52F0, r.args[0], ParkRel(r.args[1]), r.args[2], ParkRel(r.args[3])});
            return 0u;
        });
        using GetIntegerFn = bool (CStruct::*)(uint32_t, int*, int);
        s.At({0x00476CB0, AddressOf(static_cast<GetIntegerFn>(&CStruct::GetInteger))}, 12, [](Regs& r) {
            Record({0x6CB0, ParkRel(r.ecx), r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2] & 0xFF});
            *reinterpret_cast<int*>(r.args[1]) = 0x4321 + static_cast<int>(g_nextStruct_lfxc);
            return 1u;
        });
        reinterpret_cast<void**>(0x006F3614)[2] = g_structs_lfxc; // a struct pool (its allocate is stood in)
        GameVariable<int>(0x006F3614) = 0;
        using ValueFn = int(__cdecl*)(const char*, int);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            g_nextStruct_lfxc = 0;
            for (int index : {-1, 0, 5, -7}) {
                int value = original ? Original<ValueFn>(0x0059E5C0)("main_heap_pad", index)
                                     : Ed::GetMemoryValue("main_heap_pad", index);
                Record({0x5C0, static_cast<uint32_t>(value)});
            }
            traces[run] = g_trace;
        }
        RemoveStandIns();
        Compare(traces[0], traces[1], "park memory: GetMemoryValue");
        Check(traces[0].size() > 40, "park memory: GetMemoryValue's original reached its calls");
    }

    // 2. the rest
    auto* standIns = new StandIns;
    StandInParkMemory(*standIns);
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        std::memset(reinterpret_cast<void*>(0x007CE5B4), 0, 0x007CE698 - 0x007CE5B4);
        using VoidFn = void(__cdecl*)();
        using PieceFn = void(__cdecl*)(const void*);
        using TypeFn = void(__cdecl*)(uint32_t);
        using StatsFn = Ed::MemoryStats*(__cdecl*)(Ed::MemoryStats*);
        using FitFn = bool(__cdecl*)(int, int);
        using PoolFn = void(__cdecl*)(int, const char*);

        SetUpParkState(0);
        Record({ParkRel(reinterpret_cast<uint32_t>(original ? Original<Mem::Heap*(__cdecl*)()>(0x0059E100)() : Ed::GetParkHeap()))});
        Record({ParkRel(reinterpret_cast<uint32_t>(original ? Original<Mem::Heap*(__cdecl*)()>(0x0059E180)() : Ed::GetMainHeap()))});
        original ? Original<VoidFn>(0x0059E170)() : Ed::SetCountsDirty();
        Record({GameVariable<uint32_t>(0x007CE694)});

        // DMA pieces: flags 0 / bit 8 / bit 2 / both; types in the table, missing (1), negative, zero
        struct Piece {
            uint8_t unknown0[0xC];
            uint32_t flags;
            uint8_t unknown10[0x28];
            uint32_t type;
        } pieces[8] = {};
        const uint32_t flags[] = {0, 0x100, 0x4, 0x104, 0x4, 0x4, 0xFFFFFEFF, 0x100};
        const uint32_t types[] = {0x1111, 0x2222, 0x9999, 0x1111, 0x2222, 0x3333, 0x1111, 0x9999};
        for (int i = 0; i < 8; ++i) {
            pieces[i].flags = flags[i];
            pieces[i].type = types[i];
            original ? Original<PieceFn>(0x0059E190)(&pieces[i]) : Ed::AddPieceDma(&pieces[i]);
            Record({static_cast<uint32_t>(Ed::DmaPieces())});
        }
        for (int i = 0; i < 8; ++i) {
            original ? Original<PieceFn>(0x0059E1E0)(&pieces[i]) : Ed::RemovePieceDma(&pieces[i]);
            Record({static_cast<uint32_t>(Ed::DmaPieces())});
        }
        original ? Original<TypeFn>(0x0059E130)(0x9999) : Ed::RemoveDma(0x9999);
        original ? Original<TypeFn>(0x0059E130)(0x1111) : Ed::RemoveDma(0x1111);
        Record({static_cast<uint32_t>(Ed::DmaPieces())});

        // the markers (free memory changing between them)
        const uint32_t markers[] = {0x2D987043, 0xAFFF3067, 0x9BF91DBE, 0xCC63F828, 0xE113C765, 0x7DE18688,
                                    0x65B63305, 0x12345678, 0xAF6240B2, 0x2D987043, 0xFFFFFFFF};
        int step = 0;
        for (uint32_t marker : markers) {
            g_regionAvailable[0] = 0x100000 - step * 0x1234;
            FakeHeap(0)->m_freeSize = 0x2000 + step * 0x111;
            FakePool(0)->m_used = 300 + step;
            FakePool(1)->m_used = 200 + step * 2;
            Ed::DmaPieces() = 50 + step;
            ++step;
            original ? Original<TypeFn>(0x0059E390)(marker) : Ed::Marker(marker);
            RecordParkGlobals();
        }
        original ? Original<VoidFn>(0x0059E110)() : Ed::ClearStats();
        RecordParkGlobals();

        // the figures, in scenarios crossing each comparison; MainHeapHasRoom / PiecesFit around their limits
        for (int scenario = 0; scenario < 14; ++scenario) {
            SetUpParkState(scenario);
            Ed::MemoryStats out;
            std::memset(&out, 0xEE, sizeof(out));
            Ed::MemoryStats* result = original ? Original<StatsFn>(0x0059E730)(&out) : Ed::GetMemoryStats(&out);
            Record({result == &out ? 1u : 0u});
            const auto* words = reinterpret_cast<const uint32_t*>(&out);
            for (size_t i = 0; i < sizeof(out) / 4; ++i)
                Record({words[i]});
            RecordParkGlobals();
            for (int count : {0, 1, 3, 9, 40, -2}) {
                bool room = original ? Original<FitFn>(0x0059EAD0)(count, scenario + 1)
                                     : Ed::MainHeapHasRoom(count, scenario + 1);
                Record({0xEAD0, room ? 1u : 0u});
            }
            // (and at PiecesFit's limits: the DMA budget exactly used; the main heap just at / over 90%)
            int exactDma = MemoryValue("max_dma_pieces", -1) - Ed::DmaPieces();
            int mainSize = MemoryValue("floor_piece_size_main", -1);
            int overMain = (Ed::Stats().mainHeapFree - MemoryValue("main_heap_base", -1) / 10) / mainSize + 1;
            for (int last : {10, 13, 60, 400, 9, 10 + exactDma, 11 + exactDma, 10 + overMain, 9 + overMain}) {
                bool fit = original ? Original<FitFn>(0x0059EB30)(10, last) : Ed::PiecesFit(10, last);
                Record({0xEB30, fit ? 1u : 0u});
            }
        }

        // the pools on their own, then Init and ShutDown (both levels; a pool not made here kept)
        g_parkArenaUsed = 0;
        SetUpParkState(1);
        GameVariable<int>(0x007CE5B4) = 1;
        GameVariable<int>(0x007CE5C4) = 0;
        GameVariable<int>(0x007CE5F0) = 1;
        original ? Original<PoolFn>(0x0059E210)(30, "MapList") : Ed::CreateMapListNodePool(30, "MapList");
        original ? Original<PoolFn>(0x0059E290)(20, "Entries") : Ed::CreateClipboardEntryPool(20, "Entries");
        original ? Original<PoolFn>(0x0059E310)(10, "Boards") : Ed::CreateClipboardPool(10, "Boards");
        RecordParkGlobals();
        original ? Original<VoidFn>(0x0059E4E0)() : Ed::ShutDown();
        RecordParkGlobals();
        for (uint32_t level : {0xCD3682C1u, 0x7ECA21E5u}) {
            SetUpParkState(level & 3);
            GameVariable<int>(0x007CE5B4) = 0;
            GameVariable<int>(0x007CE5C4) = 1;
            GameVariable<int>(0x007CE5F0) = 0;
            original ? Original<TypeFn>(0x0059EC00)(level) : Ed::Init(level);
            RecordParkGlobals();
            GameVariable<uint8_t>(0x007CE5C8 + 1) = 0; // the clipboard entries' pool isn't ours to delete
            original ? Original<VoidFn>(0x0059E4E0)() : Ed::ShutDown();
            RecordParkGlobals();
        }
        // (the arena: the pools' and regions' contents)
        traces[run] = g_trace;
        for (uint32_t i = 0; i < g_parkArenaUsed / 4; ++i)
            traces[run].push_back(ParkRel(reinterpret_cast<uint32_t*>(g_parkArena)[i]));
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 2000, "park memory: the original reached its calls");
    Compare(traces[0], traces[1], "park memory (0x0059E100-0x0059EC00)");
}

} // namespace

SELFTEST(Early, TestSkateCFuncs, RemoveStandIns);
SELFTEST(Early, TestParkMemory, RemoveStandIns);
