// Self-tests for the PC lights and the primitive-list states (src/game/gfx/pc_lights.cpp, primitive_states.cpp,
// RFX-5): the original and ours on stand-in scene lights and model lights, stand-in dynamic lights (their falloff a
// real function), a recording stand-in device and render-state calls; the renderer's light constants compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/pc_lights.h"
#include "game/gfx/primitive_states.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPcModelLights;

constexpr uint32_t kDevice = 0xD3D00000;

// Dynamic lights: +4 a position, +0x14 a type, +0x20 an intensity, +0x24 a colour; their falloff (virtual [2]).
struct DynamicLight {
    void** vtable;
    float position[4];
    int type;
    uint8_t pad18[8];
    float intensity;
    uint8_t color[4];
};
float __fastcall FakeFalloff(DynamicLight* light, void*, float distance)
{
    Record({0xFA11, std::bit_cast<uint32_t>(distance), static_cast<uint32_t>(light->type)});
    return distance * 0.0123f + light->intensity * 0.25f;
}
void* g_lightVtable[4] = {nullptr, nullptr, reinterpret_cast<void*>(&FakeFalloff), nullptr};
DynamicLight g_dynamic[6];
uint32_t g_dynamicCount;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// The scene's lights, the model lights' and the dynamic lights (seeded), and 31 lights' worth of constants.
struct SceneLights {
    uint32_t colors[32];
    float intensities[32];
    Mth::Vector directions[32];
};
SceneLights g_scene;

void FillScene(Lcg& random)
{
    for (int i = 0; i < 32; ++i) {
        g_scene.colors[i] = random.Next() | (random.Next() << 24);
        g_scene.intensities[i] = RandomFloat(random, 0, 2);
        g_scene.directions[i] = {RandomFloat(random, -1, 1), RandomFloat(random, -1, 1), RandomFloat(random, -1, 1),
                                 RandomFloat(random, -1, 1)};
    }
    GameVariable<uint32_t*>(0x0067DCA0) = g_scene.colors;
    GameVariable<float*>(0x006FAAEC) = g_scene.intensities;
    GameVariable<Mth::Vector*>(0x0067DC9C) = g_scene.directions;
    GameVariable<uint32_t>(0x0067DC98) = random.Next() | (random.Next() << 24);
    GameVariable<float>(0x0067DC94) = RandomFloat(random, 0.25f, 3);
    for (DynamicLight& d : g_dynamic) {
        d.vtable = g_lightVtable;
        for (float& p : d.position)
            p = RandomFloat(random, -20, 20);
        d.type = random.Pick({0, 1, 1, 2, 3});
        d.intensity = RandomFloat(random, 0, 3);
        for (uint8_t& c : d.color)
            c = static_cast<uint8_t>(random.Next());
    }
}

void StandInLightCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x0048EF90}, 0, [](Regs& r) { // the dynamic lights near a position
        const auto* p = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xEF90, p[0], p[1], p[2], r.args[1]});
        auto** out = reinterpret_cast<DynamicLight**>(r.args[2]);
        for (uint32_t i = 0; i < g_dynamicCount; ++i)
            out[i] = &g_dynamic[i];
        return g_dynamicCount;
    });
}

// The constants: the ambient colour, and 32 lights (0x20 bytes each).
const std::pair<uint32_t, uint32_t> kConstants = {0x00786F38, 0x10 + 0x20 * 32};

// The model lights over random overrides (none, some, all), the ambient and the lights from either source, with no
// radius, no dynamic lights and several (one exactly at the position: no normalising); their members; the scene
// light functions; the defaults.
void TestPcLights()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, kConstants, {0x0067DC94, 0x10}, {0x006FAAE0, 8},
                       {0x006FAAEC, 4}, {0x0070166C, 0x14}});
    StandIns& s = *new StandIns();
    StandInLightCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 12; ++round) {
        ok &= CompareTwoRuns("lights", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x1167 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x7E11 + round);
            FillScene(random);
            GameVariable<uint32_t>(0x006FAAE0) = 2 + random.Below(3); // lights
            GameVariable<uint32_t>(0x006FAAE4) = 3 + random.Below(3); // model light slots
            std::vector<uint32_t> trace;
            auto* lights = reinterpret_cast<CPcModelLights*>(
                Fn<uint32_t(__cdecl*)()>(original, 0x004B6B10)());
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(lights)));
            // members
            uint32_t ambient = random.Next();
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, const uint32_t*)>(original, 0x004B6160)(lights, &ambient) &
                            0xFF);
            uint32_t out = 0;
            trace.push_back(ArenaRelocate(
                Fn<uint32_t(__thiscall*)(void*, uint32_t*)>(original, 0x004B6170)(lights, &out)));
            trace.push_back(out);
            for (int i = 0; i < 3; ++i) {
                Mth::Vector d = {RandomFloat(random, -1, 1), RandomFloat(random, -1, 1), RandomFloat(random, -1, 1),
                                 0.5f};
                trace.push_back(
                    Fn<uint32_t(__thiscall*)(void*, int, const void*)>(original, 0x004B6780)(lights, i, &d) & 0xFF);
                uint32_t c = random.Next();
                trace.push_back(
                    Fn<uint32_t(__thiscall*)(void*, int, const uint32_t*)>(original, 0x004B61B0)(lights, i, &c) & 0xFF);
                trace.push_back(ArenaRelocate(
                    Fn<uint32_t(__thiscall*)(void*, uint32_t*, int)>(original, 0x004B61D0)(lights, &out, i)));
                trace.push_back(out);
            }
            uint32_t pattern = round == 0 ? 0 : round == 1 ? 0xFFFFFFFF : random.Next();
            Fn<void(__thiscall*)(void*, int)>(original, 0x004B61F0)(lights, pattern & 1 ? 0x101 : 0x100);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004B6240)(lights));
            for (int i = 0; i < 4; ++i) {
                Fn<void(__thiscall*)(void*, int, int)>(original, 0x004B6210)(lights, i,
                                                                            (pattern >> (i + 1)) & 1 ? 0x201 : 0x200);
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x004B6250)(lights, i));
                const auto* d = Fn<const uint32_t*(__thiscall*)(void*, int)>(original, 0x004B6180)(lights, i);
                trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(d)));
                trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(d), 16));
            }
            trace.push_back(lights->overrides);
            lights->scale = RandomFloat(random, 0.5f, 2);
            for (uint32_t i = 0; i < 6; ++i)
                lights->intensities[i % 2] = RandomFloat(random, 0, 2);
            // the update
            Mth::Vector position = {RandomFloat(random, -20, 20), RandomFloat(random, -20, 20),
                                    RandomFloat(random, -20, 20), 1};
            g_dynamicCount = round % 4 == 2 ? 0 : 1 + random.Below(6);
            if (round == 5)
                std::memcpy(g_dynamic[0].position, &position, 12); // (at the position itself)
            uint32_t radius = round % 4 == 3 ? 0 : std::bit_cast<uint32_t>(RandomFloat(random, 1, 50));
            Fn<void(__thiscall*)(void*, const void*, uint32_t)>(original, 0x004B63B0)(lights, &position, radius);
            trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(kConstants.first), kConstants.second));
            // the ambient override off after the lights' (only its bit goes)
            Fn<void(__thiscall*)(void*, int)>(original, 0x004B61F0)(lights, 0);
            trace.push_back(lights->overrides);
            // the scene's lights
            trace.push_back(Fn<uint32_t(__cdecl*)()>(original, 0x004B67E0)() & 0xFF);
            trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t*)>(original, 0x004B6850)(&out)));
            trace.push_back(out);
            for (int i = 0; i < 3; ++i) {
                trace.push_back(Fn<uint32_t(__cdecl*)(int)>(original, 0x004B6860)(i) & 0xFF);
                trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t*, int)>(original, 0x004B68E0)(&out, i)));
                trace.push_back(out);
                trace.push_back(Fn<uint32_t(__cdecl*)(int)>(original, 0x004B6A70)(i) & 0xFF);
                trace.push_back(Fn<uint32_t(__cdecl*)(int)>(original, 0x004B6AB0)(i + 3));
            }
            if (round & 1)
                Fn<void(__cdecl*)()>(original, 0x004B6900)();
            else
                Fn<void(__cdecl*)()>(original, 0x004B6940)();
            if (round % 3 == 0) // (straight to the destructor: DeleteModelLights' virtual call reaches the original)
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004B67B0)(
                    lights, round == 3 ? 2u : 1u)));
            else
                trace.push_back(Fn<uint32_t(__cdecl*)(void*)>(original, 0x004B6920)(round == 4 ? nullptr : lights) &
                                0xFF);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "lights: the model lights' members and update, the scene lights, the defaults");
}

// The primitive-list states and the device calls (both results of setting the render target and depth).
void TestPrimitiveStates()
{
    GameMemory memory({{0x007D69E0, 4}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    UseStandardRelocation(com);
    s.At({0x004DAB60, AddressOf(&Nx::SetState)}, 0, [](Regs& r) {
        Record({0xAB60, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004D9DB0, AddressOf(&Nx::SetVertexFormat)}, 0, [](Regs& r) {
        Record({0x9DB0, r.args[0]});
        return 0u;
    });
    s.At({0x004DA730, AddressOf(&Nx::SetTexture)}, 0, [](Regs& r) {
        Record({0xA730, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004DA760, AddressOf(&Nx::SetBlendMode)}, 0, [](Regs& r) {
        Record({0xA760, r.args[0]});
        return 0u;
    });
    s.At({0x004DAA80, AddressOf(&Nx::SetPixelShader)}, 0, [](Regs& r) {
        Record({0xAA80, r.args[0], r.args[1]});
        return 0u;
    });
    uint32_t results[2];
    com.On(kDevice, kSetRenderTarget, [&](Regs&) { return results[0]; });
    com.On(kDevice, kSetDepthStencilSurface, [&](Regs&) { return results[1]; });
    bool ok = true, reported = false;
    for (int round = 0; round < 4; ++round) {
        ok &= CompareTwoRuns("primitive states", reported, [&](bool original) {
            g_trace.clear();
            GameVariable<FakeCom*>(0x007D69E0) = device;
            std::vector<uint32_t> trace;
            Fn<void(__cdecl*)()>(original, 0x004B60B0)();
            Fn<void(__cdecl*)()>(original, 0x004B5FF0)();
            Fn<void(__cdecl*)(uint32_t, uint32_t, uint32_t)>(original, 0x004B6030)(round, 0x10 + round, 0x77 + round);
            float m[16] = {};
            Fn<void(__cdecl*)(uint32_t, const void*)>(original, 0x004B6050)(0x100 + round, m);
            results[0] = (round & 1) ? 0x80004005 : 0;
            results[1] = (round & 2) ? 0x80004005 : 0;
            trace.push_back(Fn<uint32_t(__cdecl*)(void*, void*)>(original, 0x004B6070)(
                reinterpret_cast<void*>(0x5F500000), reinterpret_cast<void*>(0x5F510000)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        });
    }
    delete &s;
    Check(ok, "lights: the primitive-list states and the device calls");
}

} // namespace

SELFTEST(Early, TestPcLights, RemoveStandIns);
SELFTEST(Early, TestPrimitiveStates, RemoveStandIns);
