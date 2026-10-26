// Self-tests for the PC weather (src/game/gfx/pc_weather.cpp, RFX-20): the original and ours over stand-in scenes - a
// box, collision casts answered from a made-up ground -, a recording stand-in device, the engine's constructor /
// destructor, the render states, the random numbers and the texture lookups stood in.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/pc_weather.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "game/gfx/texture.h"
#include "game/mth/random.h"
#include "game/nx/feeler.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPcWeather;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

uint32_t g_rnd;
float* g_box;
uint8_t* g_textures[2];
int g_ground; // the made-up ground's kind

void TestPcWeather()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x00786EBC, 8}, {0x00786CC4, 0xC},
                       {0x00764330, 0x2000}, {0x00700F20, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(0xD3D00000);
    com.On(0xD3D00000, kDrawPrimitiveUP, [](Regs& r) {
        uint32_t stride = r.args[4], count = r.args[2];
        uint32_t vertices = r.args[1] == D3DPT_LINELIST ? 2 * count : r.args[1] == D3DPT_TRIANGLELIST ? 3 * count : count;
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), static_cast<int>(stride * vertices))});

        return 0u;
    });
    s.At({0x004A14C0}, 0, [](Regs& r) {
        Record({0x14C0, Relocate(r.ecx)});
        return r.ecx;
    });
    StandInThis(s, {0x004A1080}, 0);
    s.At({0x004D9CF0, AddressOf(&Nx::BlendModeFromName)}, 0, [](Regs& r) {
        Record({0x9CF0, r.args[0]});
        return (r.args[0] * 2654435761u) & 0xFFFFFF;
    });
    s.At({0x004DA760, AddressOf(&Nx::SetBlendMode)}, 0, [](Regs& r) {
        Record({0xA760, r.args[0]});
        return 0u;
    });
    s.At({0x004DAB60, AddressOf(&Nx::SetState)}, 0, [](Regs& r) {
        Record({0xAB60, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004DAA80, AddressOf(&Nx::SetPixelShader)}, 0, [](Regs& r) {
        Record({0xAA80, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004D9DB0, AddressOf(&Nx::SetVertexFormat)}, 0, [](Regs& r) {
        Record({0x9DB0, r.args[0]});
        return 0u;
    });
    s.At({0x004E0380, AddressOf(&Nx::sTexture::Set)}, 4, [](Regs& r) {
        Record({0x0380, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        g_rnd = g_rnd * 1103515245u + 12345u;
        return (g_rnd >> 8) % r.args[0];
    });
    s.At({0x00417760, AddressOf(static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct))}, 0, [](Regs& r) {
        std::memset(reinterpret_cast<void*>(r.ecx), 0, sizeof(Nx::CFeeler));
        return r.ecx;
    });
    s.At({0x00417190, AddressOf(&Nx::CFeeler::SetStart)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x7190, v[0], v[1], v[2], v[3]});
        std::memcpy(reinterpret_cast<void*>(r.ecx), v, 16);
        return 0u;
    });
    s.At({0x004171B0, AddressOf(&Nx::CFeeler::SetEnd)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x71B0, v[0], v[1], v[2], v[3]});
        return 0u;
    });
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) { // a made-up ground
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.ecx);
        float x = feeler->m_line.start.x, z = feeler->m_line.start.z;
        int ix = static_cast<int>(std::floor(x / 128.0f)), iz = static_cast<int>(std::floor(z / 128.0f));
        if (g_ground == 1 && ((ix + iz) & 3) == 0)
            return 0u; // (holes)
        if (g_ground == 2 && (ix < 2 || iz > 9))
            return 0u;
        int last = static_cast<int>(g_box[4]) / 128 + 1;
        if (g_ground == 3 && (iz & 3) == 1 && ix != last)
            return 0u; // (rows with ground in their last column only)
        // (and a row whose ground the highest-neighbour pass leaves as one column: no ground counts as highest)
        if (g_ground == 3 && (iz & 3) == 3 && ix != last - 4 && ix != last - 3)
            return 0u;
        if (g_ground == 3 && (iz & 3) == 0 && (ix < last - 4 || ix > last - 2))
            return 0u;
        feeler->m_point.y = static_cast<float>(((ix * 7 + iz * 3) % 5) * 37.5 + (ix & 1) * 3.0 + 0.25 * iz);
        return 1u;
    });
    s.At({0x0048C940}, 0, [](Regs&) { return reinterpret_cast<uint32_t>(g_box); });
    s.At({0x0049F540}, 4, [](Regs& r) {
        Record({0xF540, Relocate(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(r.args[0] == 0x45C7EB0F ? g_textures[0] : g_textures[1]);
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 16; ++round) {
        ok &= CompareTwoRuns("pc weather", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x8E0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            g_rnd = 0x3333 + round;
            g_ground = round % 4;
            Lcg random(0x8E00 + round);
            std::vector<uint32_t> trace;
            auto* manager = reinterpret_cast<Mem::Manager*>(Bump(sizeof(Mem::Manager)));
            std::memset(manager, 0, sizeof(Mem::Manager));
            manager->m_topDownHeap = reinterpret_cast<Mem::Heap*>(Bump(16));
            GameVariable<Mem::Manager*>(0x007D1800) = manager;
            // the scene's box (about 10-24 cells each way), the textures
            g_box = round == 4 ? nullptr : reinterpret_cast<float*>(Bump(0x20));
            if (g_box) {
                g_box[0] = RandomFloat(random, -600, 300);
                g_box[2] = RandomFloat(random, -600, 300);
                g_box[4] = g_box[0] + RandomFloat(random, 1000, 2600);
                g_box[6] = g_box[2] + RandomFloat(random, 1000, 1900);
                g_box[1] = g_box[3] = g_box[5] = g_box[7] = 0;
            }
            for (int k = 0; k < 2; ++k) {
                g_textures[k] = round % 5 == k + 1 ? nullptr : reinterpret_cast<uint8_t*>(Bump(0x20));
                if (g_textures[k]) {
                    auto* key = reinterpret_cast<uint32_t*>(Bump(4));
                    *key = 0x7E070000 + k;
                    g_textures[k][8] = static_cast<uint8_t>(round & 1);
                    *reinterpret_cast<uint32_t*>(g_textures[k] + 0x10) = round & 1 ? reinterpret_cast<uint32_t>(key) : *key;
                }
            }
            GameVariable<uint32_t>(0x00700F20) = 0xD1C70000;
            auto* weather = reinterpret_cast<CPcWeather*>(Bump(sizeof(CPcWeather)));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004CC560)(weather)));
            trace.insert(trace.end(), {weather->rainBlend, weather->splashBlend, weather->snowBlend,
                                       std::bit_cast<uint32_t>(weather->rainDue), std::bit_cast<uint32_t>(weather->splashDue),
                                       ArenaRelocate(reinterpret_cast<uint32_t>(weather->columns))});
            Fn<void(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x004CC4D0)(weather, 0xB1E0 + round, round & 1 ? 0x40 : 0);
            Fn<void(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x004CC500)(weather, 0xB2E0 + round, 0x20);
            Fn<void(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x004CC530)(weather, 0xB3E0 + round, 0x10);
            if (round & 2)
                weather->rainBlend = 0x5500000A; // (mode 10: drawn as 9)
            weather->rainHeight = RandomFloat(random, 300, 2000);
            weather->rainFrames = round % 4 == 1 ? 32 : 4 + static_cast<int>(random.Below(60));
            weather->rainLength = RandomFloat(random, 20, 150);
            weather->rainRate = round % 4 == 1 ? RandomFloat(random, 31.5f, 40) : RandomFloat(random, 2, 90);
            weather->snowHeight = RandomFloat(random, 200, 700);
            weather->snowFrames = 4 + static_cast<int>(random.Below(200));
            weather->snowSize = RandomFloat(random, 1, 8);
            weather->snowRate = RandomFloat(random, 2, 60);
            weather->splashRate = RandomFloat(random, 0.2f, 1.5f);
            weather->splashLife = 2 + static_cast<int>(random.Below(10));
            weather->splashSize = RandomFloat(random, 4, 30);
            for (int k = 0; k < 4; ++k) {
                weather->rainTop[k] = static_cast<uint8_t>(random.Next());
                weather->rainBottom[k] = static_cast<uint8_t>(random.Next());
                weather->splashColour[k] = static_cast<uint8_t>(random.Next());
                weather->snowColour[k] = static_cast<uint8_t>(random.Next());
            }
            weather->raining = (round & 1) != 0;
            Fn<void(__thiscall*)(void*)>(original, 0x004CD1A0)(weather);
            trace.push_back(weather->active);
            if (weather->active) {
                // the camera inside the grid (now and then outside it)
                float cx = weather->originX + RandomFloat(random, -200, 128.0f * weather->width + 200);
                float cz = weather->originZ + RandomFloat(random, -200, 128.0f * weather->depth + 200);
                if (round == 7)
                    cz = weather->originZ - 128.0f * 40;
                weather->cameraX = cx;
                weather->cameraZ = cz;
                auto* camera = &GameVariable<float>(0x00786CC4);
                camera[0] = cx + RandomFloat(random, -300, 300);
                camera[1] = 100;
                camera[2] = cz + RandomFloat(random, -300, 300);
                for (int frame = 0; frame < (weather->raining ? 40 : 12); ++frame) {
                    Fn<void(__thiscall*)(void*, int)>(original, 0x004CBDB0)(weather, 3);
                    if (frame % 4 == 3) { // (each kind over drops of its own: the tables cover only its frames)
                        if (weather->raining) {
                            Fn<void(__thiscall*)(void*, float, float)>(original, 0x004CBFE0)(weather, cx, cz);
                            Fn<void(__thiscall*)(void*, float, float)>(original, 0x004CCC00)(weather, cx, cz);
                        } else {
                            Fn<void(__thiscall*)(void*, float, float)>(original, 0x004CC630)(weather, cx, cz);
                        }
                    }
                    Fn<void(__thiscall*)(void*)>(original, 0x004CD120)(weather);
                }
            }
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004CC600)(weather, round & 1)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc weather: grids made, drops started, rain / snow / splashes drawn, falling, destroyed");
}

} // namespace

SELFTEST(Early, TestPcWeather, RemoveStandIns);
