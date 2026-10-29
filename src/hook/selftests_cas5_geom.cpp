// Self-tests for the engine's geom (src/game/gfx/engine_geom.cpp): the base's stubs, its own members on geoms in an
// arena with a recording vtable, and the slot dispatchers - original and ours, the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_geom.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CGeom;

void* g_geomVtable[51];
void* g_dispatchVtable[51];
uint32_t g_vertexCount;
uint32_t g_cloneResult;

void StandInGeomCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0x55u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, r.ecx});
        return 0u;
    });
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Record({0xBAE0, r.args[0]});
        return Bump(r.args[0] + 4);
    });
    for (int slot = 0; slot < 51; ++slot) {
        uint16_t pop = slot == 1 || slot == 2 ? 8 : slot == 3 ? 12 : slot == 7 || slot == 17 || slot == 38 ? 4 : 0;
        g_geomVtable[slot] = s.Thunk(pop, [slot](Regs& r) {
            Record({0xF00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            for (int i = 0; i < (slot == 1 || slot == 2 ? 2 : slot == 3 ? 3 : slot == 7 || slot == 17 || slot == 38 ? 1 : 0); ++i)
                Record({Relocate(r.args[i])});
            if (slot == 36)
                return g_vertexCount;
            if (slot == 38) {
                for (uint32_t i = 0; i < g_vertexCount; ++i)
                    reinterpret_cast<uint32_t*>(r.args[0])[i] = 0xC0100000u + i;
            }
            if (slot == 1 || slot == 2)
                return g_cloneResult;
            return 0x77u + static_cast<uint32_t>(slot);
        });
        g_dispatchVtable[slot] = s.Thunk(8, [slot](Regs& r) {
            Record({0xD00u + static_cast<uint32_t>(slot), Relocate(r.ecx), r.args[0], r.args[1]});
            return 0x99u + static_cast<uint32_t>(slot);
        });
    }
}

// The stubs: each called with its arguments; the message and the result recorded (and the statics' guards).
void TestEngineGeomStubs()
{
    GameMemory& memory = *new GameMemory({{0x006FAA08, 0xC4}});
    StandIns& s = *new StandIns();
    StandInGeomCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 4; ++round) {
        ok &= CompareTwoRuns("geom stubs", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x6E00 + round);
            if (round & 1) { // the guards set already
                GameVariable<uint32_t>(0x006FAA28) |= 1;
                GameVariable<uint32_t>(0x006FAA3C) |= 1;
            } else {
                GameVariable<uint32_t>(0x006FAA28) &= ~1u;
                GameVariable<uint32_t>(0x006FAA3C) &= ~1u;
                GameVariable<uint32_t>(0x006FAA80) &= ~1u;
                GameVariable<uint32_t>(0x006FAAC8) &= ~1u;
            }
            std::vector<uint32_t> trace;
            auto* geom = reinterpret_cast<uint8_t*>(Bump(0x40));
            auto* out = reinterpret_cast<uint32_t*>(Bump(0x10));
            auto low = [](uint32_t value) { return value & 0xFF; };
            trace.push_back(low(Fn<uint32_t(__thiscall*)(void*, int, int, int)>(original, 0x0048DA90)(geom, 1, 2, 3)));
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048DAB0)(geom));
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DAC0)(geom, 1);
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x0048E040)(geom, out)));
            trace.push_back(out[0]);
            Fn<void(__thiscall*)(void*)>(original, 0x0048DAD0)(geom);
            trace.push_back(low(Fn<uint32_t(__thiscall*)(void*, int, int, int)>(original, 0x0048DAE0)(geom, 1, 2, 3)));
            trace.push_back(
                ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, void*, int, int)>(original, 0x0048E070)(geom, out + 1, 2, 3)));
            trace.push_back(out[1]);
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DB00)(geom, 1);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048DB10)(geom));
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DB20)(geom, 1);
            trace.push_back(low(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048DB30)(geom)));
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048DB40)(geom));
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DC30)(geom, 1);
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x0048E0A0)(geom, out)));
            for (int i = 0; i < 4; ++i)
                trace.push_back(out[i]);
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DBB0)(geom, 1);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048E0D0)(geom));
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DBC0)(geom, 1);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048E1C0)(geom));
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DBD0)(geom, 1);
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DBE0)(geom, 1);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048E1F0)(geom));
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DBF0)(geom, 1);
            std::memset(out, 0xAB, 16);
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x0048E100)(geom, out)));
            for (int i = 0; i < 4; ++i)
                trace.push_back(out[i]);
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DC00)(geom, 1);
            trace.push_back(
                low(Fn<uint32_t(__thiscall*)(void*, int, int, int, int, int)>(original, 0x0048DC10)(geom, 1, 2, 3, 4, 5)));
            trace.push_back(low(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x0048DC40)(geom, 1)));
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048DC60)(geom));
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DC70)(geom, 1);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048DC80)(geom));
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048DC90)(geom));
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DCA0)(geom, 1);
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DCB0)(geom, 1);
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DCC0)(geom, 1);
            Fn<void(__thiscall*)(void*, int, int, int, int, int, int, int, int)>(original, 0x0048DCE0)(geom, 1, 2, 3, 4, 5,
                                                                                                     6, 7, 8);
            Fn<void(__thiscall*)(void*, int)>(original, 0x0048DCF0)(geom, 1);
            trace.push_back(low(Fn<uint32_t(__thiscall*)(void*, int, int, int, int)>(original, 0x0048DD10)(geom, 1, 2, 3, 4)));
            Fn<void(__thiscall*)(void*, int, int)>(original, 0x0048DD00)(geom, 1, 2);
            trace.push_back(low(Fn<uint32_t(__thiscall*)(void*, int, int, int)>(original, 0x0048DD30)(geom, 1, 2, 3)));
            trace.push_back(low(Fn<uint32_t(__thiscall*)(void*, int, int)>(original, 0x0048DD50)(geom, 1, 2)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "geom: the base's platform stubs (messages, results, the statics)");
}

// The geom's own members on geoms with a recording vtable, and the dispatchers.
void TestEngineGeom()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInGeomCalls(s);
    g_relocate = ArenaRelocate;
    alignas(16) static uint8_t manager[0x600];
    static const uint32_t dispatchers[] = {
        0x0048E4B0, 0x0048D060, 0x0048DE30, 0x0048E4C0, 0x0048DE40, 0x0048DE50, 0x0048DE60, 0x0048DE70, 0x0048DE80,
        0x0048DEB0, 0x0048DEC0, 0x0048DED0, 0x0048DEE0, 0x0048DEF0, 0x0048DF00, 0x0048DF20, 0x0048DF30, 0x0048DF40,
        0x0048DF50, 0x0048DF60, 0x0048DF70, 0x0048DF80, 0x0048DF90, 0x0048DFA0, 0x0048DFB0, 0x0048DFC0, 0x0048DFE0,
        0x0048DFD0, 0x0048DFF0, 0x0048E000, 0x0048E010, 0x0048E020, 0x0048E030};
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("geom", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            std::memset(manager, 0, sizeof(manager));
            *reinterpret_cast<uint32_t*>(manager + 0x11C) = 0x7E110000;
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint8_t*>(0x007D1800) = manager;
            Lcg random(0x6F00 + round);
            std::vector<uint32_t> trace;
            auto* geom = reinterpret_cast<CGeom*>(Bump(0x20));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048DD60)(geom)));
            trace.push_back(reinterpret_cast<uint32_t>(geom->m_vtable));
            geom->m_vtable = g_geomVtable;
            for (int step = 0; step < 12; ++step) {
                g_trace.clear();
                switch (random.Below(9)) {
                case 0:
                case 1: { // cloned (for a model)
                    auto* clone = reinterpret_cast<CGeom*>(Bump(0x20));
                    g_cloneResult = random.Below(4) ? reinterpret_cast<uint32_t>(clone) : 0;
                    int instance = static_cast<int>(random.Below(3) ? random.Below(2) : random.Next() & ~0xFFu);
                    if (random.Below(3) == 0)
                        geom->mp_collTriData = nullptr;
                    else if (random.Below(2))
                        geom->mp_collTriData = reinterpret_cast<void*>(0x7C000000 + step);
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, int, uint32_t)>(
                        original, random.Below(2) ? 0x0048DD80 : 0x0048DDC0)(geom, instance, 0x7D000000)));
                    break;
                }
                case 2:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, int, int, int)>(original, 0x0048DDF0)(
                        geom, 1, 2, static_cast<int>(random.Next())));
                    break;
                case 3:
                    trace.push_back(
                        ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0048DE10)(geom, 0x7B000000)));
                    trace.push_back(
                        ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0048DE90)(geom, 0x7B100000)));
                    break;
                case 4:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0048DF10)(geom, random.Next());
                    break;
                case 5: { // the colours (copied once)
                    g_vertexCount = random.Below(6);
                    auto* out = reinterpret_cast<uint32_t*>(Bump(0x20));
                    if (random.Below(4) == 0 && geom->mp_renderColours)
                        geom->mp_renderColours = nullptr;
                    Fn<void(__thiscall*)(void*, void*)>(original, 0x0048E150)(geom, out);
                    break;
                }
                case 6: // destroyed (and made again)
                    if (random.Below(2))
                        Fn<void(__thiscall*)(void*)>(original, 0x0048E130)(geom);
                    else
                        trace.push_back(ArenaRelocate(
                            Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x0048E220)(geom, random.Below(4))));
                    trace.push_back(reinterpret_cast<uint32_t>(geom->m_vtable));
                    geom = reinterpret_cast<CGeom*>(Bump(0x20));
                    Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048DD60)(geom);
                    geom->m_vtable = g_geomVtable;
                    break;
                default: { // a dispatcher
                    uint32_t at = dispatchers[random.Below(sizeof(dispatchers) / 4)];
                    auto* object = reinterpret_cast<void***>(Bump(0x10));
                    *object = g_dispatchVtable;
                    trace.push_back(at);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, at)(
                        object, random.Next(), random.Next()));
                    break;
                }
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "geom: made, cloned, instanced, colours (copied once), destroyed, the slot dispatchers");
}

} // namespace

SELFTEST(Early, TestEngineGeomStubs, RemoveStandIns);
SELFTEST(Early, TestEngineGeom, RemoveStandIns);
