// Self-tests for a scene mesh's in-place operations (appmain batch 7, src/game/gfx/mesh.cpp): the original and ours
// on identical stand-in meshes with stand-in vertex / index buffers and allocation; the meshes, buffers and every
// call compared.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/mesh.h"
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

// Everything in one block (pointers into it made relative).
struct Arena {
    alignas(16) Nx::sMesh meshes[4];
    Nx::sVertexBuffer vertexBuffers[16];
    Nx::sIndexBuffer indexBuffers[24];
    uint8_t vertexData[16][0x200];
    uint16_t indexData[24][64];
    uint8_t heap[0x4000];
    uint8_t locked[2][0x100];
};
alignas(16) Arena g_arena;
uint32_t g_heapUsed;
int g_nextVertexBuffer, g_nextIndexBuffer;

uint32_t ArenaRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_arena);
    if (word >= base && word < base + sizeof(g_arena))
        return 0xA0000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}
uint32_t Bump(uint32_t size)
{
    uint32_t at = reinterpret_cast<uint32_t>(g_arena.heap) + g_heapUsed;
    g_heapUsed = (g_heapUsed + size + 15) & ~15u;
    if (g_heapUsed > sizeof(g_arena.heap))
        g_heapUsed = 0; // (wraps: only the test's own data)
    return at;
}

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// A mesh: 4-6 vertices of 12-24 bytes in 1-3 buffers, index buffers in some of the 8 slots, positions, triangles,
// the per-vertex arrays or not, a transform or not.
void BuildMesh(Lcg& random, Nx::sMesh& mesh, int number)
{
    std::memset(&mesh, 0, sizeof(mesh));
    mesh.flags = 0x40 | (random.Below(2) << 9);
    mesh.stride = static_cast<uint8_t>(12 + 4 * random.Below(4));
    mesh.vertexCount = static_cast<uint16_t>(4 + random.Below(3));
    mesh.vertexBufferCount = static_cast<uint8_t>(1 + random.Below(3));
    for (int i = 0; i < 3; ++i)
        mesh.centre[i] = RandomFloat(random, -50, 50);
    mesh.radius = RandomFloat(random, 1, 9);
    for (int b = 0; b < mesh.vertexBufferCount; ++b) {
        int index = number * 3 + b;
        Nx::sVertexBuffer& buffer = g_arena.vertexBuffers[index];
        buffer.size = mesh.vertexCount * mesh.stride;
        buffer.data = g_arena.vertexData[index];
        for (uint32_t k = 0; k < buffer.size / 4; ++k)
            reinterpret_cast<float*>(buffer.data)[k] = RandomFloat(random, -100, 100);
        mesh.vertexBuffers[b] = &buffer;
    }
    for (int k = 0; k < 8; ++k) {
        if (random.Below(3) == 0 && k != 0)
            continue;
        int index = number * 6 + (k % 6);
        Nx::sIndexBuffer& buffer = g_arena.indexBuffers[index];
        mesh.indexCounts[k] = static_cast<uint16_t>(3 + random.Below(20));
        buffer.size = mesh.indexCounts[k] * 2;
        buffer.indices = g_arena.indexData[index];
        buffer.buffer = reinterpret_cast<void*>(0x1B000000 + index);
        for (int i = 0; i < mesh.indexCounts[k]; ++i)
            buffer.indices[i] = static_cast<uint16_t>(random.Below(mesh.vertexCount));
        mesh.indexBuffers[k] = &buffer;
    }
    mesh.positionCount = random.Below(4);
    mesh.positions = reinterpret_cast<float*>(Bump(48));
    for (int i = 0; i < 12; ++i)
        mesh.positions[i] = RandomFloat(random, -30, 30);
    mesh.triangleCount = 1 + random.Below(3);
    mesh.triangles = reinterpret_cast<uint16_t*>(Bump(32));
    if (random.Below(2)) {
        mesh.perVertexData = reinterpret_cast<uint8_t*>(Bump(mesh.vertexCount * 0x4C));
        mesh.perVertexData[0] = 0x4C;
    }
    if (random.Below(2)) {
        mesh.perVertexBytes = reinterpret_cast<uint8_t*>(Bump(mesh.vertexCount));
        mesh.perVertexBytes[0] = 0x8C;
    }
    if (random.Below(2)) {
        mesh.transform = reinterpret_cast<float*>(Bump(64));
        for (int i = 0; i < 16; ++i)
            mesh.transform[i] = RandomFloat(random, -20, 20);
    }
    mesh.unknownA4 = random.Next();
    mesh.unknownA8 = random.Next();
}

// MoveTo, TurnY (0-4 turns), Clone (shared and copied), AddVertexBuffers and MakeBillboard (types 1, 2 with
// axes upright and not, and the allocation's own types 0 / 3 for the others) on random meshes; everything compared.
void TestMeshOperations()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    g_relocate = ArenaRelocate;
    uint32_t billboardType = 0;
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [&](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        uint32_t at = Bump(r.args[0]);
        std::memset(reinterpret_cast<void*>(at), 0, r.args[0]);
        if (r.args[0] == 0x24)
            *reinterpret_cast<uint32_t*>(at) = billboardType; // (what an allocation may hold)
        return at;
    });
    s.At({0x00624031}, 0, [](Regs& r) { // malloc
        Record({0x4031, r.args[0]});
        uint32_t at = Bump(r.args[0]);
        std::memset(reinterpret_cast<void*>(at), 0xCD, r.args[0]);
        return at;
    });
    s.At({0x004D5760}, 0, [](Regs& r) { // a new vertex buffer
        Record({0x5760, r.args[0], r.args[1], r.args[2]});
        Nx::sVertexBuffer& buffer = g_arena.vertexBuffers[12 + g_nextVertexBuffer % 4];
        buffer.size = r.args[0];
        buffer.data = g_arena.vertexData[12 + g_nextVertexBuffer++ % 4];
        std::memset(buffer.data, 0x11, sizeof(g_arena.vertexData[0]));
        return reinterpret_cast<uint32_t>(&buffer);
    });
    s.At({0x004D40E0}, 0, [](Regs& r) {
        auto* buffer = reinterpret_cast<Nx::sVertexBuffer*>(r.ecx);
        Record({0x40E0, ArenaRelocate(r.ecx), buffer->lockOffset, buffer->lockSize, buffer->lockFlags});
        return 0u;
    });
    s.At({0x004D4BE0}, 0, [](Regs& r) {
        Record({0x4BE0, ArenaRelocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x4B, sizeof(Nx::sMesh));
        return r.ecx;
    });
    void* lockThunk = s.Thunk(20, [](Regs& r) { // IDirect3DIndexBuffer9::Lock
        Record({0x1B0C, r.args[0], r.args[1], r.args[2], r.args[4]});
        *reinterpret_cast<uint8_t**>(r.args[3]) = g_arena.locked[r.args[0] & 1];
        return (r.args[0] & 0xF) == 7 ? static_cast<uint32_t>(E_FAIL) : 0u;
    });
    void* unlockThunk = s.Thunk(4, [](Regs& r) { Record({0x1B0D, r.args[0]}); return 0u; });
    static void* indexVtable[14];
    indexVtable[11] = lockThunk;
    indexVtable[12] = unlockThunk;
    static void* indexObjects[24];
    for (int i = 0; i < 24; ++i)
        indexObjects[i] = indexVtable;
    s.At({0x004D4270}, 12, [](Regs& r) { // an index buffer's constructor
        Record({0x4270, ArenaRelocate(r.ecx), r.args[0], r.args[1], r.args[2]});
        auto* buffer = reinterpret_cast<Nx::sIndexBuffer*>(r.ecx);
        int index = 18 + g_nextIndexBuffer++ % 6;
        buffer->size = r.args[0];
        buffer->buffer = &indexObjects[index];
        buffer->indices = g_arena.indexData[index];
        return r.ecx;
    });
    static const float kAxes[][4] = {{0, 1, 0, 0}, {0.3f, 0.2f, 0.9f, 0}, {0.001f, -1, 0.002f, 0}, {1, 0, 0, 0},
                                     {0, 0.99f, 0, 0}, {0, 0.9900001f, 0, 0}};
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            std::memset(&g_arena, 0, sizeof(g_arena));
            g_heapUsed = 0;
            g_nextVertexBuffer = g_nextIndexBuffer = 0;
            GameVariable<bool>(0x007CC979) = true;
            GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
            Lcg random(0x3E500000 + seed);
            for (int m = 0; m < 3; ++m) {
                BuildMesh(random, g_arena.meshes[m], m);
                for (Nx::sIndexBuffer*& buffer : g_arena.meshes[m].indexBuffers) {
                    if (buffer)
                        buffer->buffer = &indexObjects[buffer - g_arena.indexBuffers];
                }
            }
            Originals guard(original, {0x004D6D40, 0x004D6E00, 0x004D6FB0, 0x004D71F0, 0x004D7580, 0x004023F0});
            Nx::sMesh& a = g_arena.meshes[0];
            Nx::sMesh& b = g_arena.meshes[1];
            Nx::sMesh& c = g_arena.meshes[2];
            Mth::Vector position;
            position.x = RandomFloat(random, -40, 40);
            position.y = RandomFloat(random, -40, 40);
            position.z = RandomFloat(random, -40, 40);
            position.w = seed & 1 ? 1.0f : RandomFloat(random, -2, 2); // (w copied too)
            Fn<void(__thiscall*)(void*, const Mth::Vector*)>(original, 0x004D6E00)(&a, &position);
            for (int turns = -1; turns <= 4; ++turns)
                Fn<void(__thiscall*)(void*, int)>(original, 0x004D6FB0)(turns == 4 ? &a : &b, turns);
            auto* copy = Fn<Nx::sMesh*(__thiscall*)(void*, bool)>(original, 0x004D71F0)(&c, seed & 1);
            Record({ArenaRelocate(reinterpret_cast<uint32_t>(copy))});
            Fn<void(__thiscall*)(void*, int)>(original, 0x004D6D40)(&b, static_cast<int>(random.Below(3)));
            // A billboard of the third mesh (a quad: four vertices).
            c.vertexCount = 4;
            uint32_t type = 1 + seed % 3; // 1, 2, 3 (3: the allocation's type)
            billboardType = seed == 6 ? 0 : 3;
            Mth::Vector pivot;
            pivot.x = RandomFloat(random, -5, 5);
            pivot.y = RandomFloat(random, -5, 5);
            pivot.z = RandomFloat(random, -5, 5);
            pivot.w = 1.0f;
            const float* axisValues = kAxes[seed % 6];
            Mth::Vector axis;
            axis.x = axisValues[0];
            axis.y = axisValues[1];
            axis.z = axisValues[2];
            axis.w = 0.0f;
            Fn<void(__thiscall*)(void*, int, const Mth::Vector*, const Mth::Vector*)>(original, 0x004D7580)(
                &c, static_cast<int>(type), &pivot, &axis);
            std::vector<uint32_t> trace = g_trace;
            for (uint32_t i = 0; i < sizeof(g_arena) / 4; ++i)
                trace.push_back(ArenaRelocate(reinterpret_cast<const uint32_t*>(&g_arena)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: a mesh moved, turned, cloned, given buffers, made a billboard");
    }
    g_relocate = nullptr;
    delete &s;
}

} // namespace

SELFTEST(Early, TestMeshOperations, RemoveStandIns);
