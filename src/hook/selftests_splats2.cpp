// Self-tests for the shatters' flight and making, and the texture splats' making (src/game/gfx/shatter.cpp,
// texture_splats.cpp, RFX-9; pc_texture_projection.cpp, RFX-14): the original and ours over stand-in geoms (meshes with real vertices and strips), the
// engine's shatter step, the vertex stack, the game's rand(), D3DX and the material / mesh constructors stood in;
// the shatters' and sets' contents compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include "game/core/dbg.h"
#include "game/gfx/mesh.h"
#include "game/gfx/pc_texture_projection.h"
#include "game/gfx/pixel_shaders.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "game/gfx/shatter.h"
#include "game/gfx/texture_splats.h"
#include "game/lst/hashtable.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPcShatter;
using Nx::sMesh;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}
uint32_t HashFloats(const void* p, int count) { return HashBytes(static_cast<const uint8_t*>(p), 4 * count); }

uint32_t g_rand; // the stand-in rand()'s state
std::vector<uint8_t> g_stack;

void StandInCommon(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00623BA3}, 0, [](Regs&) {
        g_rand = g_rand * 1103515245u + 12345u;
        return (g_rand >> 16) & 0x7FFF;
    });
    s.At({0x00490D80}, 4, [](Regs& r) { // the vertex stack: pop
        auto* stack = reinterpret_cast<uint32_t*>(r.ecx);
        stack[0] -= stack[1];
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_stack.data() + stack[0], stack[1]);
        return 0u;
    });
    s.At({0x00490D50}, 4, [](Regs& r) { // push
        auto* stack = reinterpret_cast<uint32_t*>(r.ecx);
        if (g_stack.size() < stack[0] + stack[1])
            g_stack.resize(stack[0] + stack[1] + 0x1000);
        std::memcpy(g_stack.data() + stack[0], reinterpret_cast<const void*>(r.args[0]), stack[1]);
        stack[0] += stack[1];
        return 0u;
    });
    s.At({0x00490D20}, 0, [](Regs& r) { // cleared
        Record({0x0D20});
        auto* stack = reinterpret_cast<uint32_t*>(r.ecx);
        stack[0] = stack[1] = 0;
        return 0u;
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) { // D3DXMatrixMultiply(out, a, b)
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, HashFloats(a, 16), HashFloats(b, 16)});
        float out[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double sum = 0;
                for (int k = 0; k < 4; ++k)
                    sum += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
                out[i * 4 + j] = static_cast<float>(sum);
            }
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, sizeof(out));
        return r.args[0];
    });
    s.At({0x00462740, AddressOf(&Lst::HashTable::PutItem)}, 8, [](Regs& r) {
        Record({0x2740, Relocate(r.ecx), Relocate(*reinterpret_cast<uint32_t*>(r.args[0])), Relocate(r.args[1])});
        return 1u;
    });
    s.At({0x004D4090}, 16, [](Regs& r) { // the vertex buffer's Lock: its data
        Record({0x4090, Relocate(r.ecx), r.args[3]});
        *reinterpret_cast<uint8_t**>(r.args[2]) = reinterpret_cast<Nx::sVertexBuffer*>(r.ecx)->data;
        return 0u;
    });
    s.At({0x004D42F0}, 16, [](Regs& r) { // the index buffer's
        Record({0x42F0, Relocate(r.ecx), r.args[3]});
        *reinterpret_cast<uint16_t**>(r.args[2]) = reinterpret_cast<Nx::sIndexBuffer*>(r.ecx)->indices;
        return 0u;
    });
    StandInThis(s, {0x004D40E0}, 0);
    StandInThis(s, {0x004D4300}, 0);
}

// A mesh of `vertexCount` vertices (stride `stride`, colour at +0xC, normals at +0x1D's offset) and a strip.
sMesh* MakeMesh(Lcg& random, uint32_t stride, int vertexCount, int indexCount, float spread)
{
    auto* mesh = reinterpret_cast<sMesh*>(Bump(sizeof(sMesh)));
    std::memset(mesh, 0, sizeof(sMesh));
    mesh->stride = static_cast<uint8_t>(stride);
    mesh->unknown1C[0] = 0x0C; // the colour
    mesh->unknown1C[1] = random.Below(2) ? 0x10 : 0; // the normal
    mesh->unknown1C[2] = 0x1C; // the texture coordinates
    mesh->flags = random.Below(5) ? 0 : 2;
    for (float& c : mesh->centre)
        c = RandomFloat(random, -5, 5);
    auto* vb = reinterpret_cast<Nx::sVertexBuffer*>(Bump(0x40));
    std::memset(vb, 0, 0x40);
    vb->data = reinterpret_cast<uint8_t*>(Bump(stride * vertexCount + 16));
    for (int v = 0; v < vertexCount; ++v) {
        auto* f = reinterpret_cast<float*>(vb->data + stride * v);
        for (uint32_t k = 0; k < stride / 4; ++k)
            f[k] = RandomFloat(random, -spread, spread);
        *reinterpret_cast<uint32_t*>(vb->data + stride * v + 0xC) = random.Next();
    }
    mesh->vertexBuffers[0] = vb;
    auto* ib = reinterpret_cast<Nx::sIndexBuffer*>(Bump(0x20));
    std::memset(ib, 0, 0x20);
    ib->indices = reinterpret_cast<uint16_t*>(Bump(2 * indexCount + 16));
    for (int i = 0; i < indexCount; ++i)
        ib->indices[i] = static_cast<uint16_t>(random.Below(4) ? random.Below(vertexCount) : (i ? ib->indices[i - 1] : 0));
    mesh->indexBuffers[0] = ib;
    mesh->indexCounts[0] = static_cast<uint16_t>(indexCount);
    return mesh;
}

// The engine's shatter: its arrays (0x004912C0) and its step (0x004913D0: the centre moved by the velocity).
void StandInShatterBase(StandIns& s)
{
    s.At({0x004912C0}, 4, [](Regs& r) {
        Record({0x12C0, Relocate(r.ecx), r.args[0]});
        auto* shatter = reinterpret_cast<CPcShatter*>(r.ecx);
        uint32_t count = r.args[0];
        shatter->centres = reinterpret_cast<Mth::Vector*>(Bump(16 * count));
        shatter->velocities = reinterpret_cast<Mth::Vector*>(Bump(16 * count));
        shatter->rotations = reinterpret_cast<Mth::Matrix*>(Bump(64 * count));
        shatter->triangleCount = static_cast<uint16_t>(count);
        for (uint32_t i = 0; i < count; ++i)
            shatter->centres[i].w = 0.25f * static_cast<float>(i);
        return r.ecx;
    });
    s.At({0x004913D0}, 8, [](Regs& r) {
        auto* shatter = reinterpret_cast<CPcShatter*>(r.ecx);
        int i = static_cast<int>(r.args[0]);
        float step = std::bit_cast<float>(r.args[1]);
        Record({0x13D0, static_cast<uint32_t>(i), r.args[1]});
        Mth::Vector& c = shatter->centres[i];
        const Mth::Vector& v = shatter->velocities[i];
        c.x += v.x * step;
        c.y += v.y * step;
        c.z += v.z * step;
        return 0u;
    });
}

// Shatters flying: random triangles about random centres, random rotations, with normals and without.
void TestShatterFlight()
{
    StandIns& s = *new StandIns();
    StandInShatterBase(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 6; ++round) {
        ok &= CompareTwoRuns("shatter flight", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            Lcg random(0x5F11 + round);
            uint32_t stride = 0x20 + 4 * random.Below(3);
            sMesh* mesh = reinterpret_cast<sMesh*>(Bump(sizeof(sMesh)));
            std::memset(mesh, 0, sizeof(sMesh));
            mesh->stride = static_cast<uint8_t>(stride);
            mesh->unknown1C[1] = round & 1 ? 0x10 : 0;
            auto* shatter = reinterpret_cast<CPcShatter*>(Bump(sizeof(CPcShatter)));
            std::memset(shatter, 0, sizeof(CPcShatter));
            int count = round == 0 ? 0 : 1 + static_cast<int>(random.Below(40));
            shatter->mesh = mesh;
            shatter->triangleCount = static_cast<uint16_t>(count);
            shatter->centres = reinterpret_cast<Mth::Vector*>(Bump(16 * count + 16));
            shatter->velocities = reinterpret_cast<Mth::Vector*>(Bump(16 * count + 16));
            shatter->rotations = reinterpret_cast<Mth::Matrix*>(Bump(64 * count + 64));
            shatter->vertices = reinterpret_cast<uint8_t*>(Bump(3 * stride * count + 16));
            for (int i = 0; i < count; ++i) {
                auto* c = &shatter->centres[i].x;
                for (int k = 0; k < 4; ++k)
                    c[k] = RandomFloat(random, -20, 20) * (round == 5 ? 1e6f : 1.0f);
                auto* v = &shatter->velocities[i].x;
                for (int k = 0; k < 4; ++k)
                    v[k] = RandomFloat(random, -3, 3);
                auto* m = reinterpret_cast<float*>(&shatter->rotations[i]);
                for (int k = 0; k < 16; ++k)
                    m[k] = RandomFloat(random, -1, 1) * (round == 5 ? 300.0f : 1.0f);
            }
            auto* f = reinterpret_cast<float*>(shatter->vertices);
            for (uint32_t k = 0; k < 3 * stride * count / 4; ++k)
                f[k] = RandomFloat(random, -25, 25) * (round == 5 ? 1e6f : 1.0f);
            if (round == 5) { // (a sum that cancels: the order of its terms shows)
                shatter->centres[0] = {0, 0, 0, 1};
                float* v0 = reinterpret_cast<float*>(shatter->vertices);
                v0[0] = 1.0f, v0[1] = 1e17f, v0[2] = -1e17f;
                auto* m = reinterpret_cast<float*>(&shatter->rotations[0]);
                for (int k = 0; k < 12; ++k)
                    m[k] = 1.0f;
            }
            for (int step = 0; step < 3; ++step)
                Fn<void(__cdecl*)(void*, float)>(original, 0x004B95E0)(shatter, 0.016f * (step + 1));
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "splats: shatters' flight (vertices and normals turned and moved)");
}

// Geoms shattered: meshes with fewer than three indices, degenerate strips, triangles big enough to split (the limit
// as loaded and smaller), a mesh with nothing to shatter.
void TestShatterGeom()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FC4A0, 8}, {0x007052C0, 0x300}, {0x0067DDF0, 8},
                       {0x007004B8, 4}, {0x007004CC, 0x14}});
    StandIns& s = *new StandIns();
    StandInCommon(s);
    StandInShatterBase(s);
    s.At({0x00404C60, AddressOf(static_cast<Mth::Matrix& (*)(Mth::Matrix&, const Mth::Vector&, float)>(
                          &Mth::CreateRotateMatrix))},
         0, [](Regs& r) {
             const auto* axis = reinterpret_cast<const uint32_t*>(r.args[1]);
             Record({0x4C60, axis[0], axis[1], axis[2], axis[3], r.args[2]});
             auto* m = reinterpret_cast<float*>(r.args[0]);
             for (int i = 0; i < 16; ++i)
                 m[i] = std::bit_cast<float>(axis[i % 3]) * 0.5f + (i % 5 == 0 ? 1.0f : 0.0f);
             return r.args[0];
         });
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 6; ++round) {
        ok &= CompareTwoRuns("shatter geom", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5E0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            g_rand = 0x1234 + round;
            g_stack.clear();
            Lcg random(0x5E00 + round);
            GameVariable<uint32_t>(0x006FC4A0) = 0;
            GameVariable<float>(0x0067DDF0) = round & 1 ? 82944.0f : 40.0f;
            GameVariable<float>(0x0067DDF4) = RandomFloat(random, 0.5f, 2);
            for (int k = 0; k < 4; ++k)
                GameVariable<float>(0x007004D0 + 4 * k) = RandomFloat(random, -2, 2);
            GameVariable<float>(0x007004CC) = RandomFloat(random, 0, 3);
            GameVariable<uint32_t>(0x007004B8) = 0x7AB1E000;
            int meshCount = 1 + static_cast<int>(random.Below(3));
            auto** meshes = reinterpret_cast<sMesh**>(Bump(4 * meshCount));
            for (int m = 0; m < meshCount; ++m) {
                int indexCount = m == 0 && round == 2 ? 2 : m == 0 && round == 3 ? 3 : 3 + static_cast<int>(random.Below(12));
                meshes[m] = MakeMesh(random, 0x24, 8, indexCount, round & 2 ? 20.0f : 4.0f);
                if (indexCount == 3) { // (one triangle, not degenerate)
                    uint16_t* indices = meshes[m]->indexBuffers[0]->indices;
                    indices[0] = 0, indices[1] = 1, indices[2] = 2;
                }
            }
            uint8_t* geom = reinterpret_cast<uint8_t*>(Bump(0x64));
            *reinterpret_cast<sMesh***>(geom + 0x3C) = meshes;
            *reinterpret_cast<uint32_t*>(geom + 0x40) = meshCount;
            Fn<void(__cdecl*)(void*)>(original, 0x004BA7A0)(geom);
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "splats: geoms shattered (strips counted and split, velocities, turns)");
}

// Splats made: rays straight down (the random turn and a given one) and sideways, with an up point; sectors without a
// geom, meshes flagged out, faces against the ray and along it, big triangles split; the set new and found.
void TestTextureSplatCreate()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FC4A0, 8}, {0x007052C0, 0x300}, {0x0067DDF0, 4},
                       {0x007004B0, 4}});
    StandIns& s = *new StandIns();
    StandInCommon(s);
    s.At({0x005BF2F5}, 12, [](Regs& r) { // D3DXVec3TransformCoord: a fixed affine map into about -1.5..1.5
        const auto* v = reinterpret_cast<const float*>(r.args[1]);
        const auto* m = reinterpret_cast<const float*>(r.args[2]);
        auto* out = reinterpret_cast<float*>(r.args[0]);
        out[0] = static_cast<float>(v[0] * 0.1 + m[0] * 0.01);
        out[1] = static_cast<float>(v[1] * 0.1 - m[5] * 0.01);
        out[2] = static_cast<float>(v[2] * 0.2 + m[10] * 0.01);
        return r.args[0];
    });
    s.At({0x005BEF2D}, 8, [](Regs& r) { // D3DXVec3Normalize
        const auto* v = reinterpret_cast<const float*>(r.args[1]);
        auto* out = reinterpret_cast<float*>(r.args[0]);
        Record({0xEF2D, HashFloats(v, 3)});
        double length = std::sqrt(static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1] +
                                  static_cast<double>(v[2]) * v[2]);
        double inverse = length > 0 ? 1.0 / length : 0.0;
        float n[3] = {static_cast<float>(v[0] * inverse), static_cast<float>(v[1] * inverse),
                      static_cast<float>(v[2] * inverse)};
        std::memcpy(out, n, 12);
        return r.args[0];
    });
    s.At({0x005C0985}, 16, [](Regs& r) { // MatrixLookAt(out, eye, at, up)
        Record({0x0985, HashFloats(reinterpret_cast<const float*>(r.args[1]), 4),
                HashFloats(reinterpret_cast<const float*>(r.args[2]), 4),
                HashFloats(reinterpret_cast<const float*>(r.args[3]), 4)});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = (i % 5 == 0) ? 1.0f : 0.125f * (i % 3);
        return r.args[0];
    });
    s.At({0x005C0B39}, 20, [](Regs& r) { // MatrixOrtho(out, w, h, near, far)
        Record({0x0B39, r.args[1], r.args[2], r.args[3], r.args[4]});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = (i % 5 == 0) ? 0.5f : 0.0f;
        return r.args[0];
    });
    StandInThis(s, {0x004D1A90}, 0);
    s.At({0x004D4BE0}, 0, [](Regs& r) {
        Record({0x4BE0, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0xD8);
        return r.ecx;
    });
    StandInThis(s, {0x004D69E0}, 0);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 16; ++round) {
        ok &= CompareTwoRuns("splat create", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0);
            memory.Fill(0x5C0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            g_rand = 0x777 + round;
            g_stack.clear();
            GameVariable<uint32_t>(0x006FC4A0) = 0;
            Lcg random(0x5C00 + round);
            // the table: empty, or (odd rounds) holding a set for the texture already
            auto* table = reinterpret_cast<Lst::HashTable*>(Bump(sizeof(Lst::HashTable)));
            std::memset(table, 0, sizeof(Lst::HashTable));
            table->m_bits = 4;
            table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(16 * sizeof(Lst::HashTable::Item)));
            std::memset(table->m_buckets, 0, 16 * sizeof(Lst::HashTable::Item));
            GameVariable<Lst::HashTable*>(0x007004B0) = table;
            uint32_t textureKey = round == 10 ? 0 : 0x7E700000 + round;
            if (round & 1) {
                auto* set = reinterpret_cast<Nx::sTextureSplats*>(Bump(sizeof(Nx::sTextureSplats)));
                for (uint32_t& a : set->alive)
                    a = random.Below(3) ? 0 : random.Next();
                for (int& st : set->correctionSteps)
                    st = static_cast<int>(random.Below(5000));
                set->material = reinterpret_cast<uint8_t*>(Bump(0x110));
                *reinterpret_cast<uint32_t*>(set->material + 0x30) = textureKey;
                uint32_t key = reinterpret_cast<uint32_t>(set);
                table->m_buckets[key & 15] = {key, set, nullptr};
            }
            // the texture holder (through a pointer in some rounds)
            uint8_t* holder = reinterpret_cast<uint8_t*>(Bump(0x20));
            if (round % 3 == 2) {
                auto* pointer = reinterpret_cast<uint32_t*>(Bump(4));
                *pointer = textureKey;
                *reinterpret_cast<uint32_t**>(holder + 0x10) = pointer;
                holder[8] = 1;
            } else {
                *reinterpret_cast<uint32_t*>(holder + 0x10) = textureKey;
                holder[8] = 2;
            }
            auto* params = reinterpret_cast<Nx::sSplatParams*>(Bump(sizeof(Nx::sSplatParams)));
            std::memset(params, 0, sizeof(Nx::sSplatParams));
            for (int k = 0; k < 3; ++k) {
                params->start[k] = RandomFloat(random, -3, 3);
                params->end[k] = params->start[k] + RandomFloat(random, -1, 1);
            }
            if (round == 11) { // (a ray just steeper than half)
                params->start[0] = params->start[1] = params->start[2] = 0.0f;
                params->end[0] = 1.0f, params->end[1] = 0.66f, params->end[2] = 0.5f;
            } else if (round % 4 < 2)
                params->end[1] = params->start[1] - RandomFloat(random, 3, 10); // (steep)
            params->start[3] = params->end[3] = 1.0f;
            params->size = RandomFloat(random, 1, 8);
            params->depth = RandomFloat(random, 1, 10);
            params->angle = round % 4 == 1 ? RandomFloat(random, 0, 6) : 0.0f;
            params->coneAngle = RandomFloat(random, 0.2f, 1.4f);
            params->lifetime = round == 3 ? -1.0f : RandomFloat(random, 0.5f, 9);
            params->texture = holder;
            if (round >= 6 && round != 11) {
                auto* up = reinterpret_cast<float*>(Bump(16));
                for (int k = 0; k < 4; ++k)
                    up[k] = RandomFloat(random, -2, 2);
                params->up = up;
            }
            // sectors: a null geom among them, meshes in each
            int sectorCount = 1 + static_cast<int>(random.Below(3));
            auto** sectors = reinterpret_cast<void**>(Bump(4 * (sectorCount + 1)));
            for (int i = 0; i < sectorCount; ++i) {
                uint8_t* sector = reinterpret_cast<uint8_t*>(Bump(0x20));
                if (i == 1) {
                    *reinterpret_cast<void**>(sector + 0x14) = nullptr;
                } else {
                    int meshCount = 1 + static_cast<int>(random.Below(3));
                    auto** meshes = reinterpret_cast<sMesh**>(Bump(4 * meshCount));
                    for (int m = 0; m < meshCount; ++m)
                        meshes[m] = MakeMesh(random, 0x24, 10, 3 + static_cast<int>(random.Below(14)),
                                             round & 4 ? 20.0f : 6.0f);
                    uint8_t* geom = reinterpret_cast<uint8_t*>(Bump(0x64));
                    *reinterpret_cast<sMesh***>(geom + 0x3C) = meshes;
                    *reinterpret_cast<uint32_t*>(geom + 0x40) = meshCount;
                    *reinterpret_cast<uint8_t**>(sector + 0x14) = geom;
                }
                sectors[i] = sector;
            }
            sectors[sectorCount] = nullptr;
            uint32_t result = ArenaRelocate(Fn<uint32_t(__cdecl*)(void*, int, void*)>(original, 0x004B9C30)(
                round == 9 ? sectors + sectorCount : sectors, 0x55, params));
            std::vector<uint32_t> trace = g_trace;
            trace.push_back(result);
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "splats: splats made on sectors (projection, faces, splitting, the set new and found)");
}

constexpr uint32_t kProjectionDevice = 0xD3D00000;

// Texture projections: made, projected onto stand-in sectors (whole and split finer, onto new batches and ones made
// already, one nearly full), batches got when all ten are taken, drawn, destroyed; the batch's constructor.
void TestTextureProjections()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FC4A0, 8}, {0x0067DDF0, 4}, {0x007D69E0, 4},
                       {0x00786D0C, 4}, {0x00786EBC, 8}, {0x00705750, 8 + 10 * sizeof(Nx::sProjectionBatch)}});
    StandIns& s = *new StandIns();
    StandInCommon(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(kProjectionDevice);
    com.On(kProjectionDevice, kGetSamplerState, [](Regs& r) {
        *reinterpret_cast<DWORD*>(r.args[3]) = 0x3A3;
        return 0u;
    });
    com.On(kProjectionDevice, kDrawPrimitiveUP, [](Regs& r) {
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), r.args[2] * 3 * r.args[4]), r.args[2]});
        return 0u;
    });
    s.At({0x005BF2F5}, 12, [](Regs& r) { // D3DXVec3TransformCoord: an affine map from a few of the matrix's
        const auto* v = reinterpret_cast<const float*>(r.args[1]);
        const auto* m = reinterpret_cast<const float*>(r.args[2]);
        auto* out = reinterpret_cast<float*>(r.args[0]);
        out[0] = static_cast<float>(v[0] * m[0] + m[12]);
        out[1] = static_cast<float>(v[1] * m[5] + m[13]);
        out[2] = static_cast<float>(v[2] * m[10] + m[14]);
        return r.args[0];
    });
    StandInThis(s, {0x004D1A90}, 0);
    s.At({0x004D4BE0}, 0, [](Regs& r) {
        Record({0x4BE0, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0xD8);
        return r.ecx;
    });
    StandInThis(s, {0x004D69E0}, 0);
    StandInThis(s, {0x004D1F00}, 0);
    StandInThis(s, {0x0049FE20}, 0);
    StandInThis(s, {0x004A04A0}, 0);
    s.At({0x004DAA80, AddressOf(&Nx::SetPixelShader)}, 0, [](Regs& r) {
        Record({0xAA80, r.args[0], Relocate(r.args[1])});
        return 0u;
    });
    s.At({0x004D9DB0, AddressOf(&Nx::SetVertexFormat)}, 0, [](Regs& r) {
        Record({0x9DB0, r.args[0]});
        return 0u;
    });
    s.At({0x004DDC00, AddressOf(&Nx::MaterialPixelShader)}, 0, [](Regs& r) {
        Record({0xDC00, Relocate(r.args[0]), Relocate(r.args[1])});
        *reinterpret_cast<uint32_t*>(r.args[1]) = 0x5ADE0000 + (r.args[0] & 0xFFFF);
        return 0u;
    });
    s.At({0x004D9CF0, AddressOf(&Nx::BlendModeFromName)}, 0, [](Regs& r) {
        Record({0x9CF0, r.args[0]});
        return r.args[0] * 2654435761u;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 5)});
        return 0u;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 14; ++round) {
        ok &= CompareTwoRuns("texture projections", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x7E0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<FakeCom*>(0x00786D0C) = device;
            g_rand = 0x999 + round;
            g_stack.clear();
            GameVariable<uint32_t>(0x006FC4A0) = 0;
            Lcg random(0x7E00 + round);
            std::vector<uint32_t> trace;
            uint32_t textureKey = 0x7E700000 + round;
            // the frame's batches so far: none, a few, one for the texture (nearly full in rounds 1, 3 and 13), all ten
            auto* batches = &GameVariable<Nx::sProjectionBatch>(0x00705758);
            uint32_t made = round % 4 == 0 ? 0 : round == 6 ? 10 : 1 + random.Below(4);
            GameVariable<uint32_t>(0x00705750) = made;
            for (uint32_t i = 0; i < made; ++i) {
                batches[i].texture = 0x0BA70000 + i;
                batches[i].triangleCount = random.Below(60);
            }
            if (made && round & 1) {
                batches[made - 1].texture = textureKey;
                if (round == 1 || round == 3 || round == 13) // (nearly full: filled up by this projection)
                    batches[made - 1].triangleCount = round == 1 ? 500 : round == 3 ? 480 : 505;
            }
            if (round == 6) {
                trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t, uint32_t)>(original, 0x004C73E0)(
                    textureKey, 0xB1E0)));
            } else {
                auto* projection = reinterpret_cast<Nx::CPcTextureProjection*>(Bump(sizeof(Nx::CPcTextureProjection)));
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004C7E80)(projection)));
                uint8_t* holder = reinterpret_cast<uint8_t*>(Bump(0x20));
                if (round % 3 == 2) {
                    auto* pointer = reinterpret_cast<uint32_t*>(Bump(4));
                    *pointer = textureKey;
                    *reinterpret_cast<uint32_t**>(holder + 0x10) = pointer;
                    holder[8] = 1;
                } else {
                    *reinterpret_cast<uint32_t*>(holder + 0x10) = textureKey;
                    holder[8] = 2;
                }
                projection->texture = holder;
                projection->blendName = 0xB1E00000 + round;
                auto* m = reinterpret_cast<float*>(&projection->projection);
                for (int k = 0; k < 16; ++k)
                    m[k] = RandomFloat(random, -0.3f, 0.3f);
                m[0] = RandomFloat(random, 0.1f, 0.4f);
                m[5] = RandomFloat(random, 0.1f, 0.4f);
                m[10] = RandomFloat(random, 0.1f, 0.3f);
                projection->fineness = round & 2 ? 0 : 1 + random.Below(4);
                int sectorCount = 1 + static_cast<int>(random.Below(3));
                auto** sectors = reinterpret_cast<void**>(Bump(4 * (sectorCount + 1)));
                for (int i = 0; i < sectorCount; ++i) {
                    uint8_t* sector = reinterpret_cast<uint8_t*>(Bump(0x20));
                    if (i == 1) {
                        *reinterpret_cast<void**>(sector + 0x14) = nullptr;
                    } else {
                        int meshCount = 1 + static_cast<int>(random.Below(3));
                        auto** meshes = reinterpret_cast<sMesh**>(Bump(4 * meshCount));
                        for (int k = 0; k < meshCount; ++k) {
                            meshes[k] = MakeMesh(random, 0x24, 10, 3 + static_cast<int>(random.Below(14)),
                                                 round & 4 ? 8.0f : 4.0f);
                            for (int v = 0; v < 10; ++v) // (colours with every bit: Next() gives 24)
                                *reinterpret_cast<uint32_t*>(meshes[k]->vertexBuffers[0]->data + 0x24 * v + 0xC) =
                                    random.Next() << 8 | (random.Next() & 0xFF);
                        }
                        uint8_t* geom = reinterpret_cast<uint8_t*>(Bump(0x64));
                        *reinterpret_cast<sMesh***>(geom + 0x3C) = meshes;
                        *reinterpret_cast<uint32_t*>(geom + 0x40) = meshCount;
                        *reinterpret_cast<uint8_t**>(sector + 0x14) = geom;
                    }
                    sectors[i] = sector;
                }
                sectors[sectorCount] = nullptr;
                Fn<void(__thiscall*)(void*, void**, int)>(original, 0x004C7620)(
                    projection, round == 9 ? sectors + sectorCount : sectors, 0x55);
                trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t, uint32_t)>(original, 0x004C73E0)(
                        0x0E770000 + round, 0xB2E0)));
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004C7EA0)(
                    projection, round & 2 ? 1 : 0)));
            }
            Fn<void(__cdecl*)()>(original, 0x004C7520)();
            uint8_t* batch = reinterpret_cast<uint8_t*>(Bump(0x120));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004C7610)(batch)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "texture projections: made, projected (whole, split, full), batches got / drawn, destroyed");
}

} // namespace

SELFTEST(Early, TestShatterFlight, RemoveStandIns);
SELFTEST(Early, TestShatterGeom, RemoveStandIns);
SELFTEST(Early, TestTextureSplatCreate, RemoveStandIns);
SELFTEST(Early, TestTextureProjections, RemoveStandIns);
