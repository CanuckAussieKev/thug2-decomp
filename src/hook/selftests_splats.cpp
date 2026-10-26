// Self-tests for the fog, texture splats, shatters and the splats' geometry helpers (src/game/gfx/pc_fog.cpp,
// texture_splats.cpp, shatter.cpp, RFX-8): the original and ours against a recording stand-in device, stand-in
// tables of splat sets and shatters (built by hand: no chained items), a stand-in vertex stack; the engine,
// material and lighting calls stood in.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/gfx/mesh.h"
#include "game/gfx/pc_fog.h"
#include "game/gfx/render_state.h"
#include "game/gfx/shatter.h"
#include "game/gfx/texture_splats.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

constexpr uint32_t kDevice = 0xD3D00000;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}
uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

// The fog: on / off from either state, with the loading screen up and not; distances with and without distance
// clipping; colours with alpha 0, small, 128 and more, the fog off and on.
void TestPcFog()
{
    GameMemory memory({{0x007D69E0, 4}, {0x00786D04, 1}, {0x00787338, 0x10}, {0x007D643A, 1}, {0x00786F0C, 12}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    UseStandardRelocation(com);
    bool ok = true, reported = false;
    for (int round = 0; round < 16; ++round) {
        ok &= CompareTwoRuns("fog", reported, [&](bool original) {
            g_trace.clear();
            memory.Fill(0xF06 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint8_t>(0x00786D04) = round & 1;
            GameVariable<uint32_t>(0x00787338) = round & 2 ? (round & 4 ? 1u : 2u) : 0u;
            GameVariable<uint8_t>(0x007D643A) = (round >> 3) & 1;
            Lcg random(0xF060 + round);
            std::vector<uint32_t> trace;
            Fn<void(__cdecl*)(int)>(original, 0x004B8E40)(round & 4 ? 0x101 : 0x100);
            trace.push_back(GameVariable<uint32_t>(0x00787338));
            Fn<void(__cdecl*)(int)>(original, 0x004B8E40)(0x101);
            Fn<void(__cdecl*)(float)>(original, 0x004B8E80)(RandomFloat(random, 0, 5000));
            GameVariable<uint32_t>(0x00787338) = round & 8 ? 1 : 0;
            uint32_t alphas[4] = {0, 1 + random.Below(127), 128, 129 + random.Below(127)};
            Fn<void(__cdecl*)(uint32_t)>(original, 0x004B8EE0)((random.Next() & 0xFFFFFF) | (alphas[round % 4] << 24));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "splats: the fog's on / off, distance, colour");
}

// The geometry helpers: random segments (also parallel, touching, sharing points, NaN), random triangles about the
// square (also degenerate, NaN, around it), and triangles split until small enough.
struct VertexStack {
    uint32_t count;
    uint32_t size;
};
std::vector<uint8_t> g_stack;
void TestSplatGeometry()
{
    GameMemory memory({{0x006FC4A0, 8}, {0x007052C0, 0x300}, {0x0067DDF0, 4}});
    StandIns& s = *new StandIns();
    s.At({0x00490D80}, 4, [](Regs& r) { // pop
        auto* stack = reinterpret_cast<VertexStack*>(r.ecx);
        --stack->count;
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_stack.data() + stack->count * stack->size, stack->size);
        return 0u;
    });
    s.At({0x00490D50}, 4, [](Regs& r) { // push
        auto* stack = reinterpret_cast<VertexStack*>(r.ecx);
        size_t at = static_cast<size_t>(stack->count) * stack->size;
        if (g_stack.size() < at + stack->size)
            g_stack.resize(at + stack->size);
        std::memcpy(g_stack.data() + at, reinterpret_cast<const void*>(r.args[0]), stack->size);
        ++stack->count;
        return 0u;
    });
    bool ok = true, reported = false;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (int round = 0; round < 6; ++round) {
        ok &= CompareTwoRuns("splat geometry", reported, [&](bool original) {
            memory.Fill(0x6E0 + round);
            Lcg random(0x6E00 + round);
            std::vector<uint32_t> trace;
            for (int k = 0; k < 4000; ++k) {
                float p[8];
                for (float& f : p)
                    f = static_cast<float>(random.Below(9)) * 0.5f - 2.0f; // (a grid: parallel / touching often)
                if (k % 50 == 7)
                    p[random.Below(8)] = nan;
                if (k % 3 != 1)
                    for (float& f : p)
                        f = RandomFloat(random, -2, 2);
                trace.push_back(Fn<uint32_t(__cdecl*)(float, float, float, float, float, float, float, float)>(
                                    original, 0x004B7FA0)(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]) &
                                0xFF);
            }
            for (int k = 0; k < 600; ++k) {
                float p[6];
                float spread = k < 200 ? 1.5f : k < 400 ? 4.0f : 12.0f;
                for (float& f : p)
                    f = RandomFloat(random, -spread, spread);
                if (k % 40 == 3)
                    p[random.Below(6)] = nan;
                if (k % 25 == 5) // (around the square: a big triangle holding it)
                    p[0] = -9, p[1] = -9, p[2] = 9, p[3] = -9, p[4] = 0, p[5] = 9;
                trace.push_back(Fn<uint32_t(__cdecl*)(float, float, float, float, float, float)>(original, 0x004B88D0)(
                                    p[0], p[1], p[2], p[3], p[4], p[5]) &
                                0xFF);
            }
            // splitting: a vertex of 0x20 bytes (position; colour at +0x0C or none; uvs at +0x14 or none)
            auto* stack = &GameVariable<VertexStack>(0x006FC4A0);
            stack->size = 0x20;
            stack->count = 0;
            g_stack.clear();
            GameVariable<float>(0x0067DDF0) = 82944.0f;
            Nx::sSplitFormat format = {};
            format.colorOffset = round & 1 ? 0x0C : 0;
            format.uvOffset = round & 2 ? 0x14 : 0;
            for (int t = 0; t < 30; ++t) {
                for (int v = 0; v < 3; ++v) {
                    uint8_t vertex[0x20];
                    auto* f = reinterpret_cast<float*>(vertex);
                    for (int k = 0; k < 8; ++k)
                        f[k] = RandomFloat(random, -40, 40);
                    for (int k = 0; k < 4; ++k)
                        vertex[0x0C + k] = static_cast<uint8_t>(random.Next());
                    Fn<void(__thiscall*)(void*, void*)>(true, 0x00490D50)(stack, vertex);
                }
            }
            std::vector<uint8_t> out(0x20 * 3 * 16384);
            uint8_t* cursor = out.data();
            int splits = 0;
            while (splits < 20000 &&
                   (Fn<uint32_t(__cdecl*)(uint8_t**, const void*)>(original, 0x004B8220)(&cursor, &format) & 0xFF))
                ++splits;
            trace.push_back(static_cast<uint32_t>(splits));
            trace.push_back(static_cast<uint32_t>(cursor - out.data()));
            trace.push_back(HashBytes(out.data(), cursor - out.data()));
            memory.Dump(trace);
            return trace;
        });
    }
    delete &s;
    Check(ok, "splats: segments, triangles against the screen square, splitting triangles");
}

// A table built by hand: `count` values at addresses whose low bits pick different buckets (no chained items).
Lst::HashTable* MakeTable(std::vector<uint8_t*>& values, int count, uint32_t size)
{
    auto* table = reinterpret_cast<Lst::HashTable*>(Bump(sizeof(Lst::HashTable)));
    std::memset(table, 0, sizeof(Lst::HashTable));
    table->m_bits = 4;
    table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(16 * sizeof(Lst::HashTable::Item)));
    std::memset(table->m_buckets, 0, 16 * sizeof(Lst::HashTable::Item));
    for (int i = 0; i < count; ++i) {
        uint8_t* value = reinterpret_cast<uint8_t*>(Bump(size + 64)) + 4 * i; // (bucket 4i + the arena's low bits)
        values.push_back(value);
        uint32_t key = reinterpret_cast<uint32_t>(value);
        table->m_buckets[key & 15] = {key, value, nullptr};
        ++table->m_size;
    }
    return table;
}

void* g_shatterVtable[1];

// Splat sets: lit, drawn (the correction's equal depths, empty sets), killed in a box and all; shatters: made (with
// and without a material of their own), drawn, destroyed, all killed.
void TestTextureSplatsAndShatters()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x00786D0C, 4}, {0x007004B0, 12},
                       {0x00786EBC, 8}, {0x007CA964, 4}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    StandInMemory(s);
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    com.On(kDevice, kGetSamplerState, [](Regs& r) {
        *reinterpret_cast<DWORD*>(r.args[3]) = 0x3A3;
        return 0u;
    });
    com.On(kDevice, kDrawPrimitiveUP, [](Regs& r) {
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), r.args[2] * r.args[4])});
        return 0u;
    });
    s.At({0x0048F620}, 0, [](Regs& r) { // the lights' colours for points
        const auto* points = reinterpret_cast<const uint32_t*>(r.args[2]);
        Record({0xF620, Relocate(r.args[0]), points[0], points[1], points[2], points[4], points[9], r.args[3],
                r.args[4]});
        auto* colors = reinterpret_cast<uint32_t*>(r.args[1]);
        for (int k = 0; k < 3; ++k)
            colors[k] = colors[k] * 2654435761u + k;
        return 0u;
    });
    StandInThis(s, {0x004D2BD0}, 0);
    StandInThis(s, {0x004D1F00}, 0);
    StandInThis(s, {0x004D1A90}, 0);
    StandInThis(s, {0x004912C0}, 1);
    StandInThis(s, {0x00490E80}, 0);
    s.At({0x004DAA80, AddressOf(&Nx::SetPixelShader)}, 0, [](Regs& r) {
        Record({0xAA80, r.args[0], Relocate(r.args[1])});
        return 0u;
    });
    s.At({0x004D9DB0, AddressOf(&Nx::SetVertexFormat)}, 0, [](Regs& r) {
        Record({0x9DB0, r.args[0]});
        return 0u;
    });
    g_shatterVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xDE57, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    bool ok = true, reported = false;
    for (int round = 0; round < 6; ++round) {
        ok &= CompareTwoRuns("splats and shatters", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5A1 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<FakeCom*>(0x00786D0C) = device;
            Lcg random(0x5A10 + round);
            std::vector<uint32_t> trace;
            std::vector<uint8_t*> sets;
            int count = round == 0 ? 0 : 1 + static_cast<int>(random.Below(3));
            GameVariable<Lst::HashTable*>(0x007004B0) = MakeTable(sets, count, sizeof(Nx::sTextureSplats));
            for (uint8_t* bytes : sets) {
                auto* set = reinterpret_cast<Nx::sTextureSplats*>(bytes);
                for (uint32_t& a : set->alive)
                    a = random.Below(5) ? 1 + random.Below(3) : 0;
                for (int i = 0; i < 0x100; ++i) {
                    set->correctionSteps[i] = 1 + static_cast<int>(random.Below(6));
                    set->correctionA[i] = RandomFloat(random, 1, 50);
                    set->correctionB[i] = random.Below(6) ? RandomFloat(random, 1, 50) : set->correctionA[i];
                    for (uint32_t& c : set->baseColors[i])
                        c = random.Next();
                }
                for (auto& uv : set->uvs)
                    uv[0] = RandomFloat(random, 0, 1), uv[1] = RandomFloat(random, 0, 1);
                set->last = random.Below(4) ? static_cast<int>(random.Below(0x100)) : -1;
                if (bytes == sets.back() && round & 1)
                    set->last = 0; // (one triangle)
                set->material = random.Below(3) ? reinterpret_cast<uint8_t*>(Bump(0x110)) : nullptr;
                auto* v = reinterpret_cast<float*>(set->vertices);
                for (int i = 0; i < 0x100 * 3 * 6; ++i)
                    v[i] = RandomFloat(random, -100, 100);
                Fn<void(__cdecl*)(void*)>(original, 0x004B87B0)(set);
                Fn<void(__cdecl*)(void*, int)>(original, 0x004B7F30)(set, static_cast<int>(random.Below(0x100)));
            }
            Fn<void(__cdecl*)()>(original, 0x004B9380)();
            float box[8] = {-60, -50, -40, 0, 60, 45, 30, 0};
            if (round == 3)
                box[0] = std::numeric_limits<float>::quiet_NaN();
            Fn<void(__cdecl*)(const float*)>(original, 0x004B92A0)(box);
            for (uint8_t* bytes : sets)
                trace.push_back(HashBytes(bytes, 0x400));
            Fn<void(__cdecl*)()>(original, 0x004B9380)();
            Fn<void(__cdecl*)()>(original, 0x004B9200)();
            // shatters
            auto* mesh = reinterpret_cast<Nx::sMesh*>(Bump(sizeof(Nx::sMesh)));
            std::memset(mesh, 0, sizeof(Nx::sMesh));
            mesh->stride = static_cast<uint8_t>(0x18 + 4 * random.Below(3));
            mesh->material = reinterpret_cast<uint8_t*>(Bump(0x110));
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x38) = 0x142;
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x44) = 0x5AD0;
            std::vector<uint8_t*> shatters;
            int shatterCount = static_cast<int>(random.Below(4));
            GameVariable<Lst::HashTable*>(0x007004B8) = MakeTable(shatters, shatterCount, 0x40);
            for (size_t i = 0; i < shatters.size(); ++i) {
                auto* shatter = reinterpret_cast<Nx::CPcShatter*>(shatters[i]);
                int triangles = 1 + static_cast<int>(random.Below(40));
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, int, void*, uint32_t)>(
                    original, 0x004B8120)(shatter, triangles, mesh, (i & 1) ? 0x100 : 1)));
                shatter->triangleCount = static_cast<uint16_t>(triangles);
                for (int k = 0; k < 3 * mesh->stride * triangles; ++k)
                    shatter->vertices[k] = static_cast<uint8_t>(random.Next());
                Fn<void(__cdecl*)(void*)>(original, 0x004B80B0)(shatter);
                if (i == 1) {
                    trace.push_back(ArenaRelocate(
                        Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004B9A30)(shatter, 2)));
                    Fn<void*(__thiscall*)(void*, int, void*, uint32_t)>(original, 0x004B8120)(shatter, 3, mesh, 0);
                }
                shatter->vtable = g_shatterVtable;
            }
            Fn<void(__cdecl*)()>(original, 0x004B9550)();
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "splats: lit, drawn, killed in a box and all; shatters made, drawn, destroyed, killed");
}

} // namespace

SELFTEST(Early, TestPcFog, RemoveStandIns);
SELFTEST(Early, TestSplatGeometry, RemoveStandIns);
SELFTEST(Early, TestTextureSplatsAndShatters, RemoveStandIns);
