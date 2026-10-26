// Self-tests for the PC gamma (src/game/gfx/pc_gamma.cpp, RFX-22): the original and ours over random gammas (out of
// range too), a recording stand-in device.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <vector>

#include "game/gfx/pc_gamma.h"
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

void TestPcGamma()
{
    GameMemory memory({{0x0067E9C8, 12}, {0x00781BF8, 0x600}, {0x007D69E0, 4}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(0xD3D00000);
    com.On(0xD3D00000, 21, [](Regs& r) { // SetGammaRamp
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), 0x600)});
        return 0u;
    });
    UseStandardRelocation(com);
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns("pc gamma", reported, [&](bool original) {
            g_trace.clear();
            memory.Fill(0x6A0 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            Lcg random(0x6A00 + round);
            std::vector<uint32_t> trace;
            float g[3];
            for (float& v : g)
                v = round % 4 == 0 ? RandomFloat(random, -0.5f, 1.5f) : RandomFloat(random, 0, 1);
            if (round == 5) // (not a NaN: the original's pow then takes the CRT's error path, not set up here)
                g[1] = 1.0001f, g[0] = -0.0001f;
            if (round == 6)
                g[0] = 0.0f, g[2] = 1.0f;
            Fn<void(__cdecl*)(float, float, float)>(original, 0x004D07B0)(g[0], g[1], g[2]);
            float back[3] = {9, 9, 9};
            Fn<void(__cdecl*)(float*, float*, float*)>(original, 0x004D06A0)(&back[0], &back[1], &back[2]);
            for (float v : back)
                trace.push_back(std::bit_cast<uint32_t>(v));
            for (int k = 0; k < 3; ++k)
                GameVariable<float>(0x0067E9C8 + 4 * k) = RandomFloat(random, 0.4f, 2.6f);
            Fn<void(__cdecl*)()>(original, 0x004D06F0)();
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc gamma: set (clamped), read back, the ramp built");
}

} // namespace

SELFTEST(Early, TestPcGamma, RemoveStandIns);
