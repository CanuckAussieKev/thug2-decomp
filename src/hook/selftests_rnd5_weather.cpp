// Self-tests for the engine's weather and 2D windows (src/game/gfx/weather.cpp, window2d.cpp; RND5-2): the original and
// ours over dirty stand-in objects with recording vtables, the script structure's getters answering as scripted
// (given or not, out of range), Dbg::Printf and the memory manager recorded; the object and the globals compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/weather.h"
#include "game/gfx/window2d.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/rnd5_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace Rnd5Tests;

RecordingVtable g_vtable;

// Answers for a getter: given (a value from a range that crosses the clamps) or not.
void Answer(Lcg& random)
{
    static const uint32_t values[] = {0u, 1u, 40u, 0xFEu, 0xFFu, 0x100u, 0xFFFFFFFFu, 0x80000000u, 0x3F800000u,
                                      0x12345678u, 0xC2F60000u};
    g_answers.insert(g_answers.end(), {random.Below(3) ? 1u : 0u, random.Below(3) ? random.Pick(values) : random.Next()});
}

// The weather: constructed over dirty memory, every setter (given values or not), the virtual stubs, the
// dispatchers, destroyed.
void TestEngineWeather()
{
    GameMemory memory({{0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    StandInScriptStructure(s);
    g_vtable.Make(s, {1, 0, 1, 0, 2, 2, 2});
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    const uint32_t setters[] = {0x004A10B0, 0x004A10D0, 0x004A1110, 0x004A1130, 0x004A1190, 0x004A11B0,
                                0x004A1240, 0x004A1260, 0x004A1280, 0x004A12A0, 0x004A12F0, 0x004A1350,
                                0x004A1370, 0x004A13B0, 0x004A13D0, 0x004A1430, 0x004A1450, 0x004A14A0,
                                0x004A14B0};
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("engine weather", reported, [&](bool original) {
            g_trace.clear();
            g_answers.clear();
            ResetArena(0xEE);
            Lcg random(0x3EA0 + round);
            GameVariable<uint32_t>(0x007D1800) = Bump(0x10);
            auto* weather = reinterpret_cast<uint8_t*>(Bump(0x60));
            for (int i = 0; i < 0x60; ++i)
                weather[i] = static_cast<uint8_t>(random.Next());
            Record({Relocate(At<uint32_t(__thiscall*)(void*)>(original, 0x004A14C0)(weather))});
            for (int i = 0; i < 0x60; i += 4)
                Record({*reinterpret_cast<uint32_t*>(weather + i)});
            *reinterpret_cast<uint32_t**>(weather) = g_vtable.slots;
            auto* params = reinterpret_cast<void*>(Bump(0x10));
            for (int step = 0; step < 30; ++step) {
                g_answers.clear();
                for (int k = 0; k < 2; ++k)
                    Answer(random);
                uint32_t setter = random.Pick(setters);
                Record({setter, At<uint32_t(__thiscall*)(void*, void*, void*)>(original, setter)(weather, params,
                                                                                               nullptr) & 0xFF});
                for (int i = 0; i < 0x60; i += 4)
                    Record({Relocate(*reinterpret_cast<uint32_t*>(weather + i))});
            }
            // The stubs (called on the weather directly) and the dispatchers (through the recording vtable).
            At<void(__thiscall*)(void*)>(original, 0x004A1020)(weather);
            At<void(__thiscall*)(void*, int)>(original, 0x004A1030)(weather, 5);
            At<void(__thiscall*)(void*)>(original, 0x004A1040)(weather);
            At<void(__thiscall*)(void*, int, int)>(original, 0x004A1050)(weather, 1, 2);
            At<void(__thiscall*)(void*, int, int)>(original, 0x004A1060)(weather, 1, 2);
            At<void(__thiscall*)(void*, int, int)>(original, 0x004A1070)(weather, 1, 2);
            At<void(__thiscall*)(void*)>(original, 0x004A1090)(weather);
            At<void(__thiscall*)(void*)>(original, 0x004A10A0)(weather);
            At<void(__thiscall*)(void*)>(original, 0x004A1080)(weather);
            Record({*reinterpret_cast<uint32_t*>(weather)});
            *reinterpret_cast<uint32_t**>(weather) = g_vtable.slots;
            Record({Relocate(At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A1550)(
                weather, static_cast<int>(random.Below(4))))});
            Record({*reinterpret_cast<uint32_t*>(weather)});
            return g_trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine weather: defaults, the script setters (defaults, clamps, blend modes passed on), stubs, "
              "dispatchers, destroyed");
}

// The 2D windows: made, placed, sized, taken from and put back on the free list, destroyed.
void TestWindow2D()
{
    GameMemory memory({{0x007D1800, 4}, {0x007013D0, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    StandInScriptStructure(s);
    g_vtable.Make(s, {1, 0});
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 30; ++round) {
        ok &= CompareTwoRuns("2D windows", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            Lcg random(0x2D20 + round);
            GameVariable<uint32_t>(0x007D1800) = Bump(0x10);
            GameVariable<uint32_t>(0x007013D0) = 0;
            std::vector<uint8_t*> windows;
            for (int i = 0; i < 5; ++i) {
                auto* window = reinterpret_cast<uint8_t*>(Bump(0x18));
                Record({Relocate(At<uint32_t(__thiscall*)(void*, int, int, int, int)>(original, 0x004A1580)(
                    window, static_cast<int>(random.Next()), static_cast<int>(random.Next()),
                    static_cast<int>(random.Below(640)), -static_cast<int>(random.Below(480))))});
                for (int k = 0; k < 0x18; k += 4) // (the whole window, before it goes on the list)
                    Record({Relocate(*reinterpret_cast<uint32_t*>(window + k))});
                *reinterpret_cast<uint32_t**>(window) = g_vtable.slots;
                At<void(__cdecl*)(void*)>(original, 0x004A1670)(window);
                windows.push_back(window);
            }
            for (int step = 0; step < 20; ++step) {
                uint8_t* window = windows[random.Below(5)];
                int a = static_cast<int>(random.Next()), b = static_cast<int>(random.Next());
                switch (random.Below(5)) {
                case 0:
                    At<void(__thiscall*)(void*, int, int)>(original, 0x004A15C0)(window, a, b);
                    break;
                case 1:
                    At<void(__thiscall*)(void*, int, int)>(original, 0x004A15E0)(window, a, b);
                    break;
                case 2:
                    if (GameVariable<uint32_t>(0x007013D0))
                        Record({Relocate(At<uint32_t(__cdecl*)(int, int, int, int)>(original, 0x004A1620)(
                            a, b, static_cast<int>(random.Next()), static_cast<int>(random.Next())))});
                    break;
                case 3:
                    At<void(__cdecl*)(void*)>(original, 0x004A1670)(window);
                    break;
                default:
                    At<void(__thiscall*)(void*)>(original, 0x004A1600)(window);
                    break;
                }
                Record({Relocate(GameVariable<uint32_t>(0x007013D0))});
            }
            uint8_t* window = windows[random.Below(5)];
            At<void(__thiscall*)(void*)>(original, 0x004A15B0)(window);
            Record({*reinterpret_cast<uint32_t*>(window)});
            *reinterpret_cast<uint32_t**>(window) = g_vtable.slots;
            Record({Relocate(At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A1690)(
                window, static_cast<int>(random.Below(4))))});
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "2D windows: made, placed and sized (the engine told), the free list, destroyed");
}

} // namespace

SELFTEST(Early, TestEngineWeather, RemoveStandIns);
SELFTEST(Early, TestWindow2D, RemoveStandIns);
