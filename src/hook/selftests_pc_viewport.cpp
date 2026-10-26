// Self-tests for the PC viewport (src/game/gfx/pc_viewport.cpp, RFX-19): the original and ours over random points
// and matrices, a recording stand-in device, D3DX's transform, the render states and occlusion queries stood in.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/gfx/pc_viewport.h"
#include "game/gfx/render_state.h"
#include "game/sys/platform_d3d.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

uint32_t g_queryResult;

void TestPcViewport()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x00786BD0, 0x80}, {0x0070137C, 4},
                       {0x007642A0, 0x90}, {0x00701384, 0x10}, {0x0067E550, 0x20}, {0x00786AB5, 1}, {0x007CA94C, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(0xD3D00000);
    com.On(0xD3D00000, kDrawPrimitiveUP, [](Regs& r) {
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), 4 * r.args[4])});
        return 0u;
    });
    s.At({0x004A0DB0}, 8, [](Regs& r) {
        Record({0x0DB0, Relocate(r.ecx), r.args[0], r.args[1]});
        return r.ecx;
    });
    StandInThis(s, {0x004A0E60}, 0);
    s.At({0x005BF833}, 12, [](Regs& r) { // D3DXVec4Transform(out, v, m)
        const auto* v = reinterpret_cast<const float*>(r.args[1]);
        const auto* m = reinterpret_cast<const float*>(r.args[2]);
        float out[4];
        for (int i = 0; i < 4; ++i) {
            double sum = 0;
            for (int j = 0; j < 4; ++j)
                sum += static_cast<double>(v[j]) * m[j * 4 + i];
            out[i] = static_cast<float>(sum);
        }
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, sizeof(out));
        return r.args[0];
    });
    s.At({0x004DA760, AddressOf(&Nx::SetBlendMode)}, 0, [](Regs& r) {
        Record({0xA760, r.args[0]});
        return 0u;
    });
    s.At({0x004DA730, AddressOf(&Nx::SetTexture)}, 0, [](Regs& r) {
        Record({0xA730, r.args[0], r.args[1]});
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
    s.At({0x004DAB60, AddressOf(&Nx::SetState)}, 0, [](Regs& r) {
        Record({0xAB60, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x005F4840, AddressOf(&Platform::BeginOcclusionQuery)}, 0, [](Regs&) {
        Record({0x4840});
        return 0u;
    });
    s.At({0x005F5650, AddressOf(&Platform::EndOcclusionQuery)}, 0, [](Regs& r) {
        Record({0x5650, r.args[0]});
        return 0u;
    });
    s.At({0x005F4C80, AddressOf(&Platform::GetOcclusionResult)}, 0, [](Regs& r) {
        Record({0x4C80, r.args[0]});
        *reinterpret_cast<DWORD*>(r.args[1]) = 0x1234;
        return g_queryResult;
    });
    s.At({0x004A0F80}, 0, [](Regs&) {
        Record({0x0F80});
        return 0u;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns("pc viewport", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x7F0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<bool>(0x00786AB5) = round == 5;
            Lcg random(0x7F00 + round);
            std::vector<uint32_t> trace;
            auto* m = &GameVariable<float>(0x00786BD0);
            for (int k = 0; k < 32; ++k)
                m[k] = RandomFloat(random, -1, 1);
            if (round == 3)
                m[16 + 3] = m[16 + 7] = m[16 + 11] = m[16 + 15] = 0.0f; // (w = 0)
            if (round == 4)
                m[2] = m[6] = m[10] = m[14] = 0.0f; // (view depth 0)
            GameVariable<float>(0x0070137C) = RandomFloat(random, 0, 0.5f);
            if (round == 8)
                g_failAllocations = {true};
            auto* viewport = reinterpret_cast<Nx::CPcViewport*>(
                Fn<uint32_t(__cdecl*)(uint32_t, uint32_t)>(original, 0x004CB860)(0xA1 + round, 0xB2 + round));
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(viewport)));
            if (!viewport)
                viewport = reinterpret_cast<Nx::CPcViewport*>(Bump(sizeof(Nx::CPcViewport)));
            for (int k = 0; k < 6; ++k) {
                Mth::Vector point = {RandomFloat(random, -20, 20), RandomFloat(random, -20, 20),
                                     RandomFloat(random, -20, 20), RandomFloat(random, -2, 2)};
                float x = 9, y = 9;
                int depth = 0x5555;
                float result = Fn<float(__thiscall*)(void*, const Mth::Vector*, float*, float*, int*)>(original, 0x004CB940)(
                    viewport, &point, &x, &y, &depth);
                trace.insert(trace.end(), {std::bit_cast<uint32_t>(result), std::bit_cast<uint32_t>(x),
                                           std::bit_cast<uint32_t>(y), static_cast<uint32_t>(depth)});
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, const Mth::Vector*, int, uint8_t)>(original, 0x004CBC00)(
                                    viewport, &point, 7, static_cast<uint8_t>(k == 2 ? 0 : 1 + k)) & 0xFF);
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, float, float, float, float, uint32_t, uint8_t)>(
                                    original, 0x004CBA90)(viewport, x, y, x + 10, y + 10,
                                                          k == 3 ? 0x80000005u : static_cast<uint32_t>(random.Below(1000)),
                                                          static_cast<uint8_t>(k == 4 ? 0 : 9 + k)) & 0xFF);
            }
            g_queryResult = round % 3 == 0 ? S_OK : round % 3 == 1 ? S_FALSE : 0x88760868u;
            GameVariable<uint32_t>(0x00701390) = random.Below(16);
            trace.push_back(Fn<uint32_t(__cdecl*)()>(original, 0x004CBD40)());
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004CB910)(viewport, round & 1)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc viewports: made, points onto the screen, occlusion queries drawn, their pixels, destroyed");
}

} // namespace

SELFTEST(Early, TestPcViewport, RemoveStandIns);
