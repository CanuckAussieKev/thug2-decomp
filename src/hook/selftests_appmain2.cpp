// Self-tests for the occlusion polygons and view culling (appmain batch 2, src/game/gfx/occlusion.cpp): the
// original and ours run the same random scenes - polygons added, enabled and disabled, occluders built from
// moving eyes over many frames, spheres tested - from identical memory, and the results and memory are compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "game/gfx/occlusion.h"
#include "game/mth/vector.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// Polygons: quads facing the eye area at random places and turns, some degenerate (all corners equal, three in
// a line), some disabled, checksums shared; frames with the eye moving (also exactly on a polygon's plane),
// enabling and disabling by checksum, spheres in front of / behind / beside polygons with random weights (also
// negative), with and without an object transform; RemoveAll part way through.
void TestOcclusion()
{
    GameMemory memory({{0x00787350, 0x10}, {0x00787360, 0x58 * 64}, {0x00792360, 0x5C * 8}, {0x007CA968, 4},
                       {0x007CA980, 0x40}});
    static const uint32_t kChecksums[] = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555};
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            std::vector<uint32_t> trace;
            memory.Zero();
            Lcg random(0x0CC10000 + seed);
            Originals guard(original, {0x004D8BB0, 0x004D8C10, 0x004D8C90, 0x004D8CC0, 0x004D8CE0, 0x004D8D70,
                                       0x004D8ED0, 0x004D9AC0, 0x004023F0});
            auto addPolys = [&](int count) {
                for (int n = 0; n < count; ++n) {
                    Mth::Vector corners[4];
                    float cx = RandomFloat(random, -60, 60), cy = RandomFloat(random, -20, 20);
                    float cz = RandomFloat(random, 20, 80), size = RandomFloat(random, 5, 40);
                    float turn = RandomFloat(random, -1.2f, 1.2f);
                    int kind = static_cast<int>(random.Below(12));
                    for (int c = 0; c < 4; ++c) {
                        float sx = (c == 1 || c == 2) ? size : -size;
                        float sy = (c >= 2) ? size : -size;
                        corners[c].x = cx + sx * std::cos(turn);
                        corners[c].y = cy + sy;
                        corners[c].z = cz + sx * std::sin(turn);
                        corners[c].w = RandomFloat(random, 0, 2);
                        if (kind == 0)
                            corners[c] = corners[0];
                        if (kind == 1 && c == 2)
                            corners[c] = corners[1];
                    }
                    if (kind == 2) // the other way round
                        std::swap(corners[1], corners[3]);
                    if (kind == 3) // not a number
                        corners[0].y = std::numeric_limits<float>::quiet_NaN();
                    if (kind == 4) { // far apart in size: differences that don't fit a float
                        corners[0].x = 0.1f + cx * 1e-6f;
                        corners[3].x = 1000000.5f + cx;
                    }
                    Fn<void(__cdecl*)(const Mth::Vector*, const Mth::Vector*, const Mth::Vector*, const Mth::Vector*,
                                      uint32_t)>(original, 0x004D8D70)(&corners[0], &corners[1], &corners[2],
                                                                       &corners[3], random.Pick(kChecksums));
                }
            };
            addPolys(14 + static_cast<int>(seed));
            float world[16] = {};
            for (int frame = 0; frame < 60; ++frame) {
                if (frame == 33 && seed == 2) {
                    Fn<void(__cdecl*)()>(original, 0x004D8CC0)();
                    addPolys(6);
                }
                for (int e = 0; e < 3; ++e) {
                    if (random.Below(2))
                        Fn<void(__cdecl*)(uint32_t, bool)>(original, 0x004D8C90)(random.Pick(kChecksums),
                                                                                 random.Below(4) != 0);
                }
                Mth::Vector eye;
                eye.x = RandomFloat(random, -30, 30);
                eye.y = RandomFloat(random, -10, 10);
                eye.z = RandomFloat(random, -40, 10);
                eye.w = 1.0f;
                if (frame % 17 == 5) { // on the first polygon's plane
                    const float* p0 = &GameVariable<float>(0x00787368);
                    eye.x = p0[0];
                    eye.y = p0[1];
                    eye.z = p0[2];
                }
                if (frame % 13 == 7) // behind them all
                    eye.z = 150.0f;
                Fn<void(__cdecl*)(const Mth::Vector*, int)>(original, 0x004D8ED0)(&eye, frame & 1);
                for (int s = 0; s < 40; ++s) {
                    Mth::Vector centre;
                    centre.x = RandomFloat(random, -70, 70);
                    centre.y = RandomFloat(random, -25, 25);
                    centre.z = RandomFloat(random, 0, 160);
                    centre.w = 1.0f;
                    if (s % 13 == 12) // (not a number: behind every plane)
                        centre.y = std::numeric_limits<float>::quiet_NaN();
                    float radius = random.Below(5) == 0 ? 0.0f : RandomFloat(random, 0.1f, 15.0f);
                    if (random.Below(4) == 0) {
                        for (float& f : world)
                            f = RandomFloat(random, -5, 5);
                        Fn<void(__cdecl*)(const float*)>(false, 0x004DB620)(world); // (ours: tested in batch 1)
                    } else {
                        GameVariable<const float*>(0x007CA968) = nullptr;
                    }
                    int weight = static_cast<int>(random.Below(7)) - 2;
                    bool hidden = Fn<bool(__cdecl*)(const Mth::Vector*, float, int)>(original, 0x004D9AC0)(&centre,
                                                                                                       radius, weight);
                    trace.push_back(hidden);
                }
                if (frame % 9 == 4)
                    Fn<void(__cdecl*)()>(original, 0x004D8C10)();
                if (frame % 11 == 6)
                    Fn<void(__cdecl*)()>(original, 0x004D8CE0)();
            }
            memory.Dump(trace);
            // Pointers to polygons: as offsets.
            for (uint32_t& word : trace) {
                if (word >= 0x00787360 && word < 0x00787360 + 0x58 * 64)
                    word = 0xB0000000 + (word - 0x00787360);
            }
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: occlusion polygons, occluders and the sphere test");
    }
}

// The view tests with a stand-in D3DXVec3Transform (a simple deterministic transform), random spheres around
// each plane's edge, the object transform on / off, the clip distance setting on / off.
void TestViewCulling()
{
    GameMemory memory({{0x00786BD0, 0x40}, {0x00786CA8, 8}, {0x00786E20, 0x10}, {0x007CA968, 4},
                       {0x007CA978, 4}, {0x007CA980, 0x40}, {0x007D643A, 1}});
    StandIns& s = *new StandIns();
    s.At({0x005BF19F}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* in = reinterpret_cast<const float*>(r.args[1]);
        Record({0x19F, Bits(in[0]), Bits(in[1]), Bits(in[2]), r.args[2]});
        const auto* m = reinterpret_cast<const float*>(r.args[2]);
        out[0] = in[0] * m[0] + m[12];
        out[1] = in[1] * m[5] + m[13];
        out[2] = in[2] * m[10] + m[14];
        out[3] = 1.0f;
        return r.args[0];
    });
    for (uint32_t seed = 1; seed <= 3; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Zero();
            Lcg random(0x51E00000 + seed);
            float* view = &GameVariable<float>(0x00786BD0);
            float world[16] = {};
            // Exact edges first (identity view): radius - z == near, radius == a + b, radius == c - d, and just
            // either side of each.
            std::memset(view, 0, 64);
            view[0] = view[5] = view[10] = 1.0f;
            GameVariable<float>(0x00786CA8) = -2.0f;
            GameVariable<float>(0x00786CAC) = 5.0f;
            GameVariable<float>(0x00786E20) = 0.5f;
            GameVariable<float>(0x00786E28) = 0.5f;
            GameVariable<float>(0x00786E24) = 0.25f;
            GameVariable<float>(0x00786E2C) = -0.25f;
            GameVariable<uint8_t>(0x007D643A) = 1;
            GameVariable<const float*>(0x007CA968) = nullptr;
            static const float kEdges[][4] = {
                {0, 0, 5, 3},  {0, 0, 5, 2.9999998f}, {0, 0, 5, 3.0000002f}, {2, 0, 4, 3},  {2, 0, 4, 2.9999998f},
                {-2, 0, 4, 3}, {0, 4, 4, 3},          {0, -4, 4, 3},          {0, 4, 8, 3},  {0, -4, 8, 3},
                {0, 0, 4, 1},  {3, 4, 0, 0},          {3, 4, 0, 1},           {0, 0, -1, 1}, {1, 1, 1, 1},
            };
            for (const float* e : kEdges) {
                Mth::Vector centre;
                centre.x = e[0];
                centre.y = e[1];
                centre.z = e[2];
                centre.w = 1.0f;
                float sphere[5] = {0, e[0], e[1], e[2], e[3]};
                Record({static_cast<uint32_t>(Fn<int(__cdecl*)(const Mth::Vector*, float)>(original, 0x004DB570)(
                            &centre, e[3])),
                        Fn<bool(__cdecl*)(const Mth::Vector*, float)>(original, 0x004DB790)(&centre, e[3]),
                        Fn<bool(__cdecl*)(const void*)>(original, 0x004DB6D0)(sphere)});
            }
            for (int step = 0; step < 3000; ++step) {
                if (step % 100 == 0) {
                    view[0] = RandomFloat(random, 0.5f, 2.0f);
                    view[5] = RandomFloat(random, 0.5f, 2.0f);
                    view[10] = random.Below(2) ? -1.0f : 1.0f;
                    view[12] = RandomFloat(random, -5, 5);
                    view[13] = RandomFloat(random, -5, 5);
                    view[14] = RandomFloat(random, -5, 5);
                    GameVariable<float>(0x00786CA8) = RandomFloat(random, -10, 1);
                    GameVariable<float>(0x00786CAC) = RandomFloat(random, 20, 200);
                    for (int i = 0; i < 4; ++i)
                        GameVariable<float>(0x00786E20 + 4 * i) = RandomFloat(random, -1, 1);
                    GameVariable<uint8_t>(0x007D643A) = static_cast<uint8_t>(random.Below(3));
                }
                if (random.Below(3) == 0) {
                    for (float& f : world)
                        f = RandomFloat(random, -20, 20);
                    Fn<void(__cdecl*)(const float*)>(false, 0x004DB620)(world);
                } else {
                    GameVariable<const float*>(0x007CA968) = nullptr;
                }
                float sphere[5] = {RandomFloat(random, -1, 1), RandomFloat(random, -100, 100),
                                   RandomFloat(random, -100, 100), RandomFloat(random, -150, 150), 0};
                sphere[4] = random.Below(6) == 0 ? 0.0f : RandomFloat(random, 0.1f, 60.0f);
                Mth::Vector centre;
                centre.x = sphere[1];
                centre.y = sphere[2];
                centre.z = sphere[3];
                centre.w = 1.0f;
                switch (random.Below(3)) {
                case 0:
                    Record({static_cast<uint32_t>(Fn<int(__cdecl*)(const Mth::Vector*, float)>(original, 0x004DB570)(
                        &centre, sphere[4]))});
                    break;
                case 1:
                    Record({Fn<bool(__cdecl*)(const Mth::Vector*, float)>(original, 0x004DB790)(&centre, sphere[4]),
                            GameVariable<uint32_t>(0x007CA978)});
                    break;
                default:
                    Record({Fn<bool(__cdecl*)(const void*)>(original, 0x004DB6D0)(sphere)});
                    break;
                }
            }
            std::vector<uint32_t> trace = g_trace;
            for (uint32_t& word : trace) // (the stack copies of the centre)
                word = OnStack(word) ? 0x57AC0000 : word;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the view culling tests");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestOcclusion);
SELFTEST(Early, TestViewCulling, RemoveStandIns);
