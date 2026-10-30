// Self-tests for the shadows (appmain batch 3, src/game/gfx/shadow.cpp): the original and ours against a recording
// stand-in device, stand-in D3DX maths and stand-in models, on a stand-in caster table; traces and memory compared.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/shadow.h"
#include "game/lst/hashtable.h"
#include "game/gfx/model.h"
#include "game/gfx/geom.h"
#include "game/gfx/mesh.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

constexpr uint32_t kDevice = 0xD3D00000;

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
uint32_t HashFloats(const void* p, int count) { return HashBytes(static_cast<const uint8_t*>(p), 4 * count); }

// The D3DX functions the shadows use, stood in: each records its inputs and computes something simple (a real
// matrix product in double precision; a transform x*m0+m12...).
void StandInD3dx(StandIns& s)
{
    s.At({0x005BFA8C}, 12, [](Regs& r) { // MatrixMultiply(out, a, b)
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, HashFloats(a, 16), HashFloats(b, 16)});
        float out[16];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                double sum = 0;
                for (int k = 0; k < 4; ++k)
                    sum += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
                out[i * 4 + j] = static_cast<float>(sum);
            }
        }
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, sizeof(out));
        return r.args[0];
    });
    s.At({0x005BFF98}, 12, [](Regs& r) { // MatrixInverse(out, determinant, m)
        Record({0xFF98, r.args[1], HashFloats(reinterpret_cast<const float*>(r.args[2]), 16)});
        std::memset(reinterpret_cast<void*>(r.args[0]), 0x11, 64);
        return r.args[0];
    });
    s.At({0x005BF2F5}, 12, [](Regs& r) { // Vec3TransformCoord(out, v, m)
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* v = reinterpret_cast<const float*>(r.args[1]);
        const auto* m = reinterpret_cast<const float*>(r.args[2]);
        Record({0xF2F5, HashFloats(v, 3), HashFloats(m, 16)});
        out[0] = v[0] * m[0] + m[12];
        out[1] = v[1] * m[5] + m[13];
        out[2] = v[2] * m[10] + m[14];
        return r.args[0];
    });
    s.At({0x005C0985}, 16, [](Regs& r) { // MatrixLookAt(out, eye, at, up)
        Record({0x0985, HashFloats(reinterpret_cast<const float*>(r.args[1]), 3),
                HashFloats(reinterpret_cast<const float*>(r.args[2]), 3),
                HashFloats(reinterpret_cast<const float*>(r.args[3]), 3)});
        std::memset(reinterpret_cast<void*>(r.args[0]), 0x22, 64);
        return r.args[0];
    });
    s.At({0x005C0B39}, 20, [](Regs& r) { // MatrixOrtho(out, w, h, near, far)
        Record({0x0B39, r.args[1], r.args[2], r.args[3], r.args[4]});
        std::memset(reinterpret_cast<void*>(r.args[0]), 0x33, 64);
        return r.args[0];
    });
}

// Projection and clipping: positions moved by the stand-in transform to around 0..1 (so every clip bit and both
// sides of the facing test happen), triangles over them (also degenerate, also repeated indices).
void TestShadowProjection()
{
    GameMemory memory({{0x0079B640, 0x1C * 300}, {0x00796640, 0x3C * 400}});
    StandIns& s = *new StandIns();
    StandInD3dx(s);
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Fill(0x5AD00000 + seed);
            Lcg random(0x7A1A0000 + seed);
            std::vector<uint32_t> trace;
            for (int round = 0; round < 7; ++round) {
                uint32_t count = round == 0 ? 0 : 1 + random.Below(299);
                std::vector<float> positions(3 * 300);
                for (float& f : positions)
                    f = RandomFloat(random, -0.6f, 1.6f);
                if (round == 5)
                    positions[0] = -0.0f, positions[1] = 1.0f, positions[2] = 0.0f;
                if (round == 6) { // (0,0), (1,0), (1,-1e-4): facing exactly the limit; then (1,-2e-4) beyond it
                    count = 4;
                    const float kLimit = std::bit_cast<float>(0xB8D1B717u);
                    const float crafted[12] = {0, 0, 0.5f, 1, 0, 0.5f, 1, kLimit, 0.5f, 1, 2 * kLimit, 0.5f};
                    std::memcpy(positions.data(), crafted, sizeof(crafted));
                }
                float matrix[16] = {};
                matrix[0] = RandomFloat(random, -1.2f, 1.2f);
                matrix[5] = RandomFloat(random, -1.2f, 1.2f);
                matrix[10] = RandomFloat(random, -1.2f, 1.2f);
                matrix[12] = RandomFloat(random, -0.2f, 0.4f);
                matrix[13] = RandomFloat(random, -0.2f, 0.4f);
                matrix[14] = RandomFloat(random, -0.2f, 0.4f);
                if (round == 6) {
                    matrix[0] = matrix[5] = matrix[10] = 1.0f;
                    matrix[12] = matrix[13] = matrix[14] = 0.0f;
                }
                Fn<void(__cdecl*)(const float*, uint32_t, const void*)>(original, 0x004DA0F0)(positions.data(), count,
                                                                                            matrix);
                uint32_t triangles = random.Below(130);
                if (round == 1)
                    triangles = 3;
                if (round == 6)
                    triangles = 2;
                std::vector<uint16_t> indices(3 * 130 + 3);
                for (uint16_t& index : indices)
                    index = static_cast<uint16_t>(count ? random.Below(count) : 0);
                if (round == 6) {
                    const uint16_t crafted[6] = {0, 1, 2, 0, 1, 3};
                    std::memcpy(indices.data(), crafted, sizeof(crafted));
                }
                trace.push_back(static_cast<uint32_t>(
                    Fn<int(__cdecl*)(const uint16_t*, uint32_t)>(original, 0x004DA190)(indices.data(), triangles)));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: shadow vertex projection and triangle clipping");
    }
    delete &s;
}

// A caster table (Lst::HashTable, two bits) built in place: the casters in buckets and chained items, an empty
// value, a key used twice.
struct Table {
    alignas(4) uint8_t tableBytes[sizeof(Lst::HashTable)]; // (the class has a constructor: raw storage)
    Lst::HashTable& Hash() { return *reinterpret_cast<Lst::HashTable*>(tableBytes); }
    Lst::HashTable::Item buckets[4];
    Lst::HashTable::Item chained[4];
    Nx::ShadowCaster casters[5];
    uint8_t targets[5][0x20];
};
alignas(16) Table g_table;

void BuildTable(Lcg& random, bool emptyTable)
{
    std::memset(&g_table, 0, sizeof(g_table));
    g_table.Hash().m_bits = 2;
    g_table.Hash().m_buckets = g_table.buckets;
    for (int i = 0; i < 5; ++i) {
        Nx::ShadowCaster& c = g_table.casters[i];
        c.target = g_table.targets[i];
        *reinterpret_cast<uint32_t*>(g_table.targets[i] + 0x10) = 0x7E700000 + i; // the texture
        *reinterpret_cast<uint32_t*>(g_table.targets[i] + 0x18) = 0x5F500000 + i; // its surface
        c.model = (i == 1) ? nullptr : reinterpret_cast<void*>(0x30DE0000 + i); // (more casters follow it)
        float* m = c.view;
        for (int k = 0; k < 64; ++k)
            m[k] = (k % 16 % 5 == 0 ? 1.0f : 0.0f) + (k % 4 == 2 && k % 16 != 10 ? 0.0f : RandomFloat(random, -0.1f, 0.1f));
        // (nothing else into z: the receivers' z stays inside 0..1)
    }
    if (emptyTable)
        return;
    // Bucket 0: caster 0 (key 0x100) -> chained caster 1 (0x204, no model); bucket 1: caster 2 (0x101) -> an item
    // with no value (0x105) -> caster 3 (0x105 again); bucket 2 empty; bucket 3: caster 4 (0x3).
    g_table.buckets[0] = {0x100, &g_table.casters[0], &g_table.chained[0]};
    g_table.chained[0] = {0x204, &g_table.casters[1], nullptr};
    g_table.buckets[1] = {0x101, &g_table.casters[2], &g_table.chained[1]};
    g_table.chained[1] = {0x105, nullptr, &g_table.chained[2]};
    g_table.chained[2] = {0x105, &g_table.casters[3], nullptr};
    g_table.buckets[3] = {0x3, &g_table.casters[4], nullptr};
}

uint32_t TableRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_table);
    if (word >= base && word < base + sizeof(g_table))
        return 0x7AB00000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}

// Models with meshes: some active (+0x4C), each with its flags word.
struct Meshes {
    uint8_t geoms[8][0x50];
    uint8_t meshes[8][0x60];
    uint32_t flags[8];
};
alignas(16) Meshes g_meshes;

void StandInModels(StandIns& s)
{
    s.At({0x00492E40, AddressOf(&Nx::CModel::GetNumGeoms)}, 0, [](Regs& r) {
        Record({0x2E40, TableRelocate(r.ecx)});
        return (r.ecx & 0xF) + 2;
    });
    s.At({0x00494640, AddressOf(&Nx::CModel::GetGeom)}, 4, [](Regs& r) {
        Record({0x4640, TableRelocate(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_meshes.geoms[(r.args[0] + (r.ecx & 3)) % 8]);
    });
    s.At({0x004D16B0, AddressOf(Nx::RenderInstance)}, 0, [](Regs& r) {
        auto index = static_cast<uint32_t>((r.args[0] - reinterpret_cast<uint32_t>(g_meshes.meshes)) / 0x60);
        Record({0x16B0, index, r.args[1], g_meshes.flags[index]});
        return 0u;
    });
    std::memset(&g_meshes, 0, sizeof(g_meshes));
    for (int i = 0; i < 8; ++i) {
        *reinterpret_cast<uint8_t**>(g_meshes.geoms[i] + 0x4C) = g_meshes.meshes[i];
        g_meshes.meshes[i][0x4C] = (i % 3) != 1;
        *reinterpret_cast<uint32_t**>(g_meshes.meshes[i] + 0x58) = &g_meshes.flags[i];
        g_meshes.flags[i] = 0x1000u * i + 0x81;
    }
}

GameMemory* ShadowMemory()
{
    return new GameMemory({{0x00786B90, 0xC0}, {0x00786CBC, 1}, {0x00786E30, 0x110}, {0x00787338, 0x10},
                           {0x007CA940, 0x40}, {0x007CA968, 4}, {0x007CA970, 4}, {0x007CA978, 8}, {0x007D1EC0, 1},
                           {0x00786AB8, 0xC}, {0x0067EF4C, 0x10}, {0x0079B640, 0x1C * 64}, {0x00796640, 0x3C * 64}});
}

// RenderShadowTextures and DrawShadows over the stand-in table, ShadowTextureMatrix with and without its first
// matrix, the render skipped, an empty table, the fog and depth bias on / off, receivers hidden / in view / flagged.
void TestShadowPasses()
{
    std::unique_ptr<GameMemory> memory(ShadowMemory());
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* renderTarget = com.Surface(0x5F000001);
    FakeCom* depth = com.Surface(0x5F000002);
    g_relocate = TableRelocate;
    StandInD3dx(s);
    StandInModels(s);
    bool targetsNull = false, depthNull = false;
    com.On(kDevice, kGetRenderTarget, [&](Regs& r) {
        *reinterpret_cast<FakeCom**>(r.args[2]) = targetsNull ? nullptr : renderTarget;
        return 0u;
    });
    com.On(kDevice, kGetDepthStencilSurface, [&](Regs& r) {
        *reinterpret_cast<FakeCom**>(r.args[1]) = targetsNull || depthNull ? nullptr : depth;
        return 0u;
    });
    com.On(kDevice, 45, [](Regs& r) { // GetTransform
        auto* m = reinterpret_cast<float*>(r.args[2]);
        for (int i = 0; i < 16; ++i)
            m[i] = static_cast<float>(i * r.args[1]) * 0.25f;
        return 0u;
    });
    com.On(kDevice, kGetSamplerState, [](Regs& r) {
        *reinterpret_cast<DWORD*>(r.args[3]) = 7;
        return 0u;
    });
    com.On(kDevice, kSetTransform, [](Regs& r) {
        Record({HashFloats(reinterpret_cast<const float*>(r.args[2]), 16)});
        return 0u;
    });
    com.On(kDevice, kDrawPrimitiveUP, [](Regs& r) {
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), r.args[2] * 0x3C)});
        return 0u;
    });
    int inView = 0;
    s.At({0x004DB790, AddressOf(&Nx::SphereInView)}, 0, [&](Regs& r) {
        Record({0xB790, TableRelocate(r.args[0]), r.args[1]});
        return static_cast<uint32_t>(++inView % 3 != 0);
    });
    // Receivers: flags, sphere, vertices, triangles.
    struct Receiver {
        uint8_t bytes[0xC0];
    };
    static Receiver receivers[6];
    static float positions[6][3 * 20];
    static uint16_t indices[6][3 * 12];
    void* receiverList[6];
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0x5AD50000 + seed);
            Lcg random(0x5AD60000 + seed);
            BuildTable(random, seed == 4);
            for (int i = 0; i < 8; ++i)
                g_meshes.flags[i] = 0x1000u * i + 0x81;
            GameVariable<Lst::HashTable*>(0x007CA970) = &g_table.Hash();
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<bool>(0x007D1EC0) = seed == 5;
            GameVariable<uint32_t>(0x00787338) = seed & 1;
            GameVariable<uint32_t>(0x00786E54) = seed == 2 ? 0xD : (seed == 3 ? 0 : 5);
            GameVariable<bool>(0x00786AB8) = (seed & 2) != 0;
            GameVariable<bool>(0x00786AC0) = (seed & 1) != 0;
            targetsNull = seed == 6;
            depthNull = seed == 3;
            GameVariable<uint32_t>(0x00786F34) = 0; // (no blend override)
            float* viewport = &GameVariable<float>(0x0067EF4C);
            viewport[0] = 0.0f, viewport[1] = 0.0f, viewport[2] = 1.0f, viewport[3] = 1.0f;
            inView = 0;
            for (int i = 0; i < 6; ++i) {
                std::memset(receivers[i].bytes, 0, sizeof(receivers[i].bytes));
                receivers[i].bytes[0] = (i == 2) ? 2 : 0;
                for (int k = 0; k < 3 * 20; ++k) // (z inside 0..1: none clipped by it)
                    positions[i][k] = k % 3 == 2 ? RandomFloat(random, 0.1f, 0.9f) : RandomFloat(random, -0.5f, 1.5f);
                for (uint16_t& index : indices[i])
                    index = static_cast<uint16_t>(random.Below(20));
                *reinterpret_cast<uint32_t*>(receivers[i].bytes + 0xAC) = 20;
                *reinterpret_cast<float**>(receivers[i].bytes + 0xB0) = positions[i];
                *reinterpret_cast<uint32_t*>(receivers[i].bytes + 0xB4) = 12;
                *reinterpret_cast<uint16_t**>(receivers[i].bytes + 0xB8) = indices[i];
                receiverList[i] = receivers[i].bytes;
            }
            Originals guard(original, {0x004DBBD0, 0x004DBF50, 0x004DB880, 0x004DA0F0, 0x004DA190, 0x004DA760,
                                       0x004DAB60, 0x004DA730, 0x004DAA80, 0x004DAA10, 0x004D9DE0, 0x004D9DB0,
                                       0x00588190});
            Fn<void(__cdecl*)()>(original, 0x004DBBD0)();
            Fn<void(__cdecl*)(const void*, void* const*, int)>(original, 0x004DBF50)(nullptr, receiverList, 6);
            float a[16], b[16], first[16], out[16];
            for (int i = 0; i < 16; ++i) {
                a[i] = RandomFloat(random, -1, 1);
                b[i] = RandomFloat(random, -1, 1);
                first[i] = RandomFloat(random, -1, 1);
            }
            Fn<void(__cdecl*)(const float*, const float*, float*, const float*)>(original, 0x004DB880)(
                a, b, out, seed & 1 ? first : nullptr);
            std::vector<uint32_t> trace = g_trace;
            trace.push_back(HashFloats(out, 16));
            trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(&g_table), sizeof(g_table)));
            trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(&g_meshes.flags), sizeof(g_meshes.flags)));
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: shadow textures and shadows drawn");
    }
    g_relocate = nullptr;
    delete &s;
}

// DrawShadowVolumes: meshes flagged or not, in view or not, two-sided stencil or not, from random caches.
void TestShadowVolumes()
{
    std::unique_ptr<GameMemory> memory(ShadowMemory());
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    UseStandardRelocation(com);
    static uint8_t meshes[10][0x20];
    void* list[10];
    int inView = 0;
    s.At({0x004DB790, AddressOf(&Nx::SphereInView)}, 0, [&](Regs& r) {
        Record({0xB790, static_cast<uint32_t>((r.args[0] - reinterpret_cast<uint32_t>(meshes)) / 0x20), r.args[1]});
        return static_cast<uint32_t>(++inView % 4 != 0);
    });
    s.At({0x004D5290, AddressOf(&Nx::sMesh::Render)}, 0, [&](Regs& r) {
        Record({0x5290, static_cast<uint32_t>((r.ecx - reinterpret_cast<uint32_t>(meshes)) / 0x20),
                GameVariable<uint32_t>(0x00786EC4)}); // (the pixel shader lock while drawn)
        return 0u;
    });
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0x501D0000 + seed);
            Lcg random(0x501E0000 + seed);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<bool>(0x007CA97C) = (seed & 1) != 0;
            for (uint32_t address = 0x00786E30; address < 0x00786EC8; address += 4)
                GameVariable<uint32_t>(address) = random.Below(4);
            GameVariable<uint32_t>(0x00786F34) = 0; // (no blend override)
            inView = 0;
            for (int i = 0; i < 10; ++i) {
                std::memset(meshes[i], 0, sizeof(meshes[i]));
                *reinterpret_cast<uint32_t*>(meshes[i]) = random.Pick({0x240u, 0x40u, 0x200u, 0x2C0u, 0u});
                *reinterpret_cast<float*>(meshes[i] + 0x10) = static_cast<float>(i);
                list[i] = meshes[i];
            }
            struct {
                uint8_t pad[0x1C];
                void** meshes;
                int count;
            } scene = {{}, list, seed == 3 ? 0 : 10};
            Originals guard(original, {0x004DB9D0, 0x004DA760, 0x004DAB60, 0x004DA730, 0x004DAA80, 0x004DAA10,
                                       0x004D9DE0});
            Fn<void(__cdecl*)(const void*)>(original, 0x004DB9D0)(&scene);
            std::vector<uint32_t> trace = g_trace;
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: shadow volumes");
    }
    g_relocate = nullptr;
    delete &s;
}

// AddShadowCaster (three casters, stand-in allocation and table); RemoveShadowCaster / AimShadowCaster for keys in buckets, chained, behind an empty value, missing; aimed
// straight down (the other up), along x only, from a point to itself.
void TestShadowCasters()
{
    GameMemory memory({{0x007CA970, 4}, {0x007D1800, 4}, {0x007CC979, 1}});
    StandIns& s = *new StandIns();
    StandInD3dx(s);
    g_relocate = TableRelocate;
    alignas(16) static uint8_t allocated[3][0x10C];
    int allocations = 0;
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [&](Regs& r) {
        Record({0xAFC0, r.ecx, r.args[0], r.args[1], r.args[2]});
        std::memset(allocated[allocations % 3], 0x5C, sizeof(allocated[0]));
        return reinterpret_cast<uint32_t>(allocated[allocations++ % 3]);
    });
    s.At({0x00462740, AddressOf(&Lst::HashTable::PutItem)}, 8, [&](Regs& r) {
        Record({0x2740, TableRelocate(r.ecx), *reinterpret_cast<uint32_t*>(r.args[0]),
                static_cast<uint32_t>(r.args[1] - reinterpret_cast<uint32_t>(allocated))});
        return 1u;
    });
    s.At({0x005B80D0, AddressOf(&Lst::HashTable::FlushItem)}, 4, [](Regs& r) {
        Record({0x80D0, TableRelocate(r.ecx), *reinterpret_cast<uint32_t*>(r.args[0])});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, r.ecx, TableRelocate(r.args[0])});
        return 0u;
    });
    static const uint32_t kKeys[] = {0x100, 0x204, 0x101, 0x105, 0x3, 0x7, 0x200};
    for (uint32_t seed = 1; seed <= 2; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0xCA570000 + seed);
            BuildTable(random, false);
            GameVariable<Lst::HashTable*>(0x007CA970) = &g_table.Hash();
            GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
            GameVariable<bool>(0x007CC979) = true;
            allocations = 0;
            for (uint32_t i = 0; i < 3; ++i)
                Fn<void(__cdecl*)(uint8_t*, void*, uint32_t)>(original, 0x004DDC90)(
                    reinterpret_cast<uint8_t*>(0x7A200000 + i), reinterpret_cast<void*>(0x30DE1000 + i), seed * 7 + i);
            for (uint32_t key : kKeys) {
                float position[3] = {1, 2, 3}, target[3] = {1, -5, 3};
                if (key == 0x101)
                    target[0] = 4;
                if (key == 0x3)
                    target[2] = -0.0f, position[2] = 0.0f;
                if (key == 0x105)
                    target[1] = 2;
                if (key == 0x204)
                    target[2] = 7; // (x the same, z not)
                Fn<void(__cdecl*)(uint32_t, const float*, const float*)>(original, 0x004DB100)(key, position, target);
            }
            for (uint32_t key : kKeys) {
                if (seed == 2 || key != 0x204)
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x004DB0A0)(key);
            }
            std::vector<uint32_t> trace = g_trace;
            trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(&g_table), sizeof(g_table)));
            trace.push_back(HashBytes(allocated[0], sizeof(allocated)));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: shadow casters added, removed and aimed");
    }
    g_relocate = nullptr;
    delete &s;
}

} // namespace

SELFTEST(Early, TestShadowProjection, RemoveStandIns);
SELFTEST(Early, TestShadowPasses, RemoveStandIns);
SELFTEST(Early, TestShadowVolumes, RemoveStandIns);
SELFTEST(Early, TestShadowCasters, RemoveStandIns);
