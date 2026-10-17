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
#include "game/core/dump_text.h"
#include "game/file/file.h"
#include "game/script/cscript.h"
#include "game/script/skate_cfuncs.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
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
    s.At({0x0045BF90}, 0, [](Regs& r) {
        Record({0xBF90, r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2] & 0xFF});
        *reinterpret_cast<uint32_t*>(r.args[1]) = 0x12340005; // a 16-bit count: only the low half is read
        return reinterpret_cast<uint32_t>(g_fakeNodes);
    });
    s.At({0x0048FE40}, 0, [](Regs& r) { Record({0xFE40, r.args[0], r.args[1], r.args[2], r.args[3]}); return 0u; });
    s.At({0x00491F10}, 0, [](Regs&) { Record({0x1F10}); return 0u; });
    s.At({0x0045BE70}, 0, [](Regs& r) { Record({0xBE70, r.args[0]}); return r.args[0] & 0x101; });
    s.At({0x004753C0, AddressOf(&Script::DeleteSpawnedScripts)}, 0, [](Regs&) { Record({0x53C0}); return 0u; });
    s.At({0x005A5960}, 0, [](Regs& r) {
        Record({0x5960, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x0045C210}, 0, [](Regs&) { Record({0xC210}); return 0u; });
    s.At({0x004A0950}, 0, [](Regs& r) { Record({0x0950, r.args[0]}); return r.args[0] == 1 ? 0x77u : 0u; });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0xFFFFFFFFu;
    });
    s.At({0x00576E80}, 0, [](Regs& r) { Record({0x6E80, r.args[0]}); return 0u; });
    s.At({0x00576E90}, 0, [](Regs& r) { Record({0x6E90, r.args[0]}); return 0u; });
    s.At({0x00576EA0}, 0, [](Regs&) { Record({0x6EA0}); return reinterpret_cast<uint32_t>(g_viewer); });
    s.At({0x00576D40}, 0, [](Regs& r) { Record({0x6D40, r.ecx}); return 0u; });
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

} // namespace

SELFTEST(Early, TestSkateCFuncs, RemoveStandIns);
