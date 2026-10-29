// Self-tests for the engine's light manager (src/game/gfx/light_manager.cpp): original and ours on the manager's
// globals and dynamic lights in an arena (the original dynamic light vtable), the PC light calls and the memory
// manager stood in; the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/pc_lights.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CDynamicLightBase;

float Value(Lcg& random)
{
    switch (random.Below(10)) {
    case 0:
        return std::numeric_limits<float>::quiet_NaN();
    case 1:
        return 0.5f;
    case 2:
        return 0.0f;
    default:
        return static_cast<float>(static_cast<int>(random.Below(3000)) - 1000) / 1000.0f;
    }
}

void StandInLightManagerCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x004B67E0, AddressOf(&Nx::UpdateAmbientLight)}, 0, [](Regs&) {
        Record({0x67E0, GameVariable<uint32_t>(0x0067DC98)});
        return 0x101u;
    });
    s.At({0x004B6850, AddressOf(&Nx::GetAmbientLightColor)}, 0, [](Regs& r) {
        Record({0x6850, Relocate(r.args[0])});
        *reinterpret_cast<uint32_t*>(r.args[0]) = 0x11223344;
        return r.args[0];
    });
    s.At({0x004B6860, AddressOf(&Nx::UpdateLightColor)}, 0, [](Regs& r) {
        Record({0x6860, r.args[0]});
        return 0x201u;
    });
    s.At({0x004B68E0, AddressOf(&Nx::GetLightColor)}, 0, [](Regs& r) {
        Record({0x68E0, Relocate(r.args[0]), r.args[1]});
        *reinterpret_cast<uint32_t*>(r.args[0]) = 0x55667700 + r.args[1];
        return r.args[0];
    });
    s.At({0x004B6900, AddressOf(&Nx::UpdateAllLights)}, 0, [](Regs&) {
        Record({0x6900});
        return 0u;
    });
    s.At({0x004B6A70, AddressOf(&Nx::UpdateLightDirection)}, 0, [](Regs& r) {
        Record({0x6A70, r.args[0]});
        return r.args[0] & 1 ? 0x300u : 0x301u;
    });
}

CDynamicLightBase* MakeLight(Lcg& random)
{
    auto* light = reinterpret_cast<CDynamicLightBase*>(Bump(sizeof(CDynamicLightBase)));
    light->vtable = reinterpret_cast<void**>(0x00649290);
    for (int i = 0; i < 3; ++i) // (coarse: ties)
        light->pos[i] = static_cast<float>(static_cast<int>(random.Below(4)) * 5 - 10);
    light->pos[3] = 1.0f;
    light->radius = random.Below(8) ? static_cast<float>(random.Below(30)) : std::numeric_limits<float>::quiet_NaN();
    light->inverseRadius = random.Below(6) ? 1.0f / (1.0f + static_cast<float>(random.Below(5))) : 0.0f;
    light->intensity = random.Below(5) ? Value(random) + 0.5f : 0.0f;
    light->owner = random.Below(5) ? random.Below(6) : 0;
    return light;
}

void TestLightManager()
{
    GameMemory& memory = *new GameMemory({{0x0067DC88, 0x30}, {0x006FAAE0, 0x10}, {0x006FAF10, 0x70},
                                          {0x006FAAF0, 0x404}, {0x0067DCB8, 0x100}, {0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInLightManagerCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 120; ++round) {
        ok &= CompareTwoRuns("light manager", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x4600 + round);
            std::vector<uint32_t> trace;
            uint32_t count = 1 + random.Below(5);
            Fn<void(__cdecl*)(uint32_t, uint32_t)>(original, 0x0048EB70)(count, random.Below(9));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            auto& world = GameVariable<Nx::sWorldLights>(0x0067DC98);
            world.ambientLow = Value(random) * 2.0f;
            world.ambientHigh = Value(random) * 2.0f;
            for (uint32_t i = 0; i < count; ++i) {
                world.lightLow[i] = Value(random) * 2.0f;
                world.lightHigh[i] = Value(random) * 2.0f;
                world.colours[i] = random.Next();
            }
            GameVariable<float>(0x0067DC8C) = Value(random);
            GameVariable<float>(0x006FAAE8) = random.Below(2) ? 0.0f : Value(random);
            GameVariable<int>(0x0067DC88) = static_cast<int>(random.Below(5)) - 1;
            // The dynamic lights: 0-8 (with null slots in some rounds - not looked through by GetDynamicLightsNear).
            bool holes = round % 3 == 0;
            int lights = static_cast<int>(random.Below(9));
            auto** slots = &GameVariable<CDynamicLightBase*>(0x006FAAF0);
            for (int i = 0; i < lights; ++i)
                slots[i] = holes && random.Below(3) == 0 ? nullptr : MakeLight(random);
            GameVariable<int>(0x006FAEF0) = lights;
            if (random.Below(3) == 0) // (a slot past the end not empty)
                slots[lights] = MakeLight(random);
            auto* out = reinterpret_cast<uint32_t*>(Bump(16));
            for (int step = 0; step < 16; ++step) {
                g_trace.clear();
                int light = static_cast<int>(random.Below(count));
                switch (random.Below(15)) {
                case 0:
                    trace.push_back(Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x0048E8F0)(random.Next()));
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(void*)>(original, 0x0048E900)(out)));
                    break;
                case 1:
                    trace.push_back(Fn<uint32_t(__cdecl*)(int, uint32_t)>(original, 0x0048E930)(light, random.Next()));
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(void*, int)>(original, 0x0048E950)(out, light)));
                    break;
                case 2: {
                    float v[4] = {Value(random), Value(random), Value(random), Value(random)};
                    trace.push_back(Fn<uint32_t(__cdecl*)(int, const float*)>(original, 0x0048ED70)(light, v));
                    break;
                }
                case 3:
                    Fn<void(__cdecl*)(float, float)>(original, 0x0048E970)(Value(random), Value(random));
                    Fn<void(__cdecl*)(int, float, float)>(original, 0x0048E990)(light, Value(random), Value(random));
                    for (int low = 0; low < 2; ++low) {
                        trace.push_back(std::bit_cast<uint32_t>(Fn<float(__cdecl*)(int)>(original, 0x0048E9B0)(low * 0x101)));
                        trace.push_back(
                            std::bit_cast<uint32_t>(Fn<float(__cdecl*)(int, int)>(original, 0x0048E9D0)(light, low)));
                    }
                    break;
                case 4:
                    Fn<void(__cdecl*)()>(original, 0x0048EA00)();
                    break;
                case 5:
                    trace.push_back(Fn<uint32_t(__cdecl*)()>(original, 0x0048EA40)() & 0xFF);
                    break;
                case 6: {
                    float b = GameVariable<float>(0x0067DC8C);
                    float target = random.Below(4) ? b + static_cast<float>(static_cast<int>(random.Below(60)) - 30) / 1000.0f
                                                   : Value(random);
                    trace.push_back(Fn<uint32_t(__cdecl*)(float)>(original, 0x0048EDB0)(target) & 0xFF);
                    break;
                }
                case 7: {
                    auto* w = reinterpret_cast<Nx::sWorldLights*>(Bump(sizeof(Nx::sWorldLights)));
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048EAC0)(w)));
                    if (random.Below(2))
                        w->lightHigh = reinterpret_cast<float*>(Bump(4));
                    Fn<void(__thiscall*)(void*)>(original, 0x0048EB00)(random.Below(3) ? w : &world);
                    if (!world.lightLow) // (the world's freed: given new ones)
                        Fn<void(__cdecl*)(uint32_t, uint32_t)>(original, 0x0048EB70)(count, 3);
                    break;
                }
                case 8:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x0048EF50)(random.Below(6))));
                    break;
                case 9:
                case 10:
                    if (!holes) {
                        float position[4] = {static_cast<float>(static_cast<int>(random.Below(4)) * 5 - 10),
                                             static_cast<float>(static_cast<int>(random.Below(4)) * 5 - 10),
                                             static_cast<float>(static_cast<int>(random.Below(4)) * 5 - 10), 1.0f};
                        auto* found = reinterpret_cast<uint32_t*>(Bump(0x40));
                        trace.push_back(Fn<uint32_t(__cdecl*)(const float*, uint32_t, void*)>(original, 0x0048EF90)(
                            position, 1 + random.Below(5), found));
                    }
                    break;
                case 11:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(int)>(original, 0x0048FB80)(
                        static_cast<int>(random.Below(4) || !holes ? 1 : random.Below(3)))));
                    break;
                case 12:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(int)>(original, 0x0048F160)(static_cast<int>(random.Below(3)))));
                    break;
                case 13:
                    if (random.Below(3) == 0)
                        Fn<void(__cdecl*)()>(original, random.Below(2) ? 0x0048EF00 : 0x0048FB60)();
                    break;
                default: { // a dynamic light's slots
                    CDynamicLightBase* l = MakeLight(random);
                    float d = Value(random) * 20.0f;
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048E8C0)(l));
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, float)>(original, 0x0048EA90)(l, d));
                    trace.push_back(std::bit_cast<uint64_t>(Fn<double(__thiscall*)(void*, float)>(original, 0x0048EAB0)(l, d)) >> 32);
                    trace.push_back(static_cast<uint32_t>(
                        std::bit_cast<uint64_t>(Fn<double(__thiscall*)(void*, float)>(original, 0x0048EAB0)(l, d))));
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
    // The original's insertion leaves the list out of order: [5, 4, 3, 4] - then a tie with the last entry of the
    // full list is turned away (it would go in before the 3).
    ok &= CompareTwoRuns("light manager (a full list out of order)", reported, [&](bool original) {
        g_trace.clear();
        ResetArena(0xEE);
        memory.Zero();
        static const float xs[] = {3.0f, 5.0f, 4.0f, 4.0f, 4.0f};
        auto** slots = &GameVariable<CDynamicLightBase*>(0x006FAAF0);
        for (int i = 0; i < 5; ++i) {
            auto* light = reinterpret_cast<CDynamicLightBase*>(Bump(sizeof(CDynamicLightBase)));
            light->vtable = reinterpret_cast<void**>(0x00649290);
            light->pos[0] = xs[i];
            light->pos[1] = light->pos[2] = 0.0f;
            light->radius = 10.0f;
            light->inverseRadius = 1.0f;
            light->intensity = 1.0f;
            light->owner = 1;
            slots[i] = light;
        }
        GameVariable<int>(0x006FAEF0) = 5;
        float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        auto* found = reinterpret_cast<uint32_t*>(Bump(0x40));
        std::vector<uint32_t> trace;
        trace.push_back(Fn<uint32_t(__cdecl*)(const float*, uint32_t, void*)>(original, 0x0048EF90)(position, 4, found));
        memory.Dump(trace);
        DumpArena(trace);
        return trace;
    });
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "light manager: set up, colours, directions, modulation, the stack, the world brightness, dynamic lights "
              "made / found / near a point / freed");
}

} // namespace

SELFTEST(Early, TestLightManager, RemoveStandIns);
