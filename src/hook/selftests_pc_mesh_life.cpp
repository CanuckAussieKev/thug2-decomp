// Self-tests for the PC meshes' life cycle (src/game/gfx/pc_mesh_life.cpp, RFX-27): the original and ours over meshes
// constructed, given vertex buffers (own and arena shares), coloured per vertex, strips with long degenerate runs
// cleaned, destroyed (owning their buffers or sharing them, billboards, the device's stream one of them), and the
// buffers let go of and made again around a device reset with stand-in scenes - a recording stand-in device and
// buffers, the C runtime's heap and the billboards stood in.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_scene.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::sIndexBuffer;
using Nx::sMesh;
using Nx::sVertexBuffer;

constexpr uint32_t kDevice = 0xD3D00000;

uint8_t* g_card;
uint32_t g_lockResult;
FakeCom* g_stream;
uint8_t* g_sceneList; // the stand-in scene list's first node

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// A strip with runs of repeated indices (degenerate triangles) of every length.
void FillStrip(uint16_t* strip, uint32_t count, Lcg& random)
{
    if (random.Below(2)) { // (a few indices: degenerates of every pattern)
        for (uint32_t i = 0; i < count; ++i)
            strip[i] = static_cast<uint16_t>(random.Below(random.Below(2) ? 3 : 6));
        return;
    }
    uint32_t i = 0;
    while (i < count) {
        if (i > 1 && random.Below(3) == 0) {
            uint32_t run = random.Below(10);
            for (uint32_t k = 0; k < run && i < count; ++k, ++i)
                strip[i] = strip[i - 1 - (random.Below(4) == 0 ? 1 : 0)];
        } else {
            strip[i++] = static_cast<uint16_t>(random.Below(500));
        }
    }
}

// The engine's scene list: nodes of (+4 the scene, +0xC the next), the last pointing at a head whose +4 is -1; each
// scene's mesh list (+0x280) with an arena buffer (+0x7C) or none.
uint8_t* MakeSceneList(Lcg& random, FakeCom* arena)
{
    auto* head = reinterpret_cast<uint8_t*>(Bump(0x10));
    std::memset(head, 0, 0x10);
    *reinterpret_cast<int*>(head + 4) = -1;
    uint8_t* next = head;
    int count = static_cast<int>(random.Below(3));
    for (int i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<uint8_t*>(Bump(0x10));
        std::memset(node, 0, 0x10);
        if (random.Below(4)) {
            auto* scene = reinterpret_cast<uint8_t*>(Bump(0x284));
            std::memset(scene, 0, 0x284);
            if (random.Below(4)) {
                auto* meshes = reinterpret_cast<Nx::sScene*>(Bump(sizeof(Nx::sScene)));
                std::memset(meshes, 0, sizeof(Nx::sScene));
                meshes->comObject = random.Below(3) ? arena : nullptr;
                meshes->unknown80 = 0x1000 + random.Below(64) * 32;
                *reinterpret_cast<Nx::sScene**>(scene + 0x280) = meshes;
            }
            *reinterpret_cast<uint8_t**>(node + 4) = scene;
        }
        *reinterpret_cast<uint8_t**>(node + 0xC) = next;
        next = node;
    }
    return next == head ? nullptr : next;
}

void TestPcMeshLife()
{
    GameMemory memory({{0x007D1800, 4}, {0x007D69E0, 4}, {0x007865E4, 8}, {0x00775B80, 0x458}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* vertexBuffer = com.Buffer(0x7B000000);
    FakeCom* arenaBuffer = com.Buffer(0x7A000000);
    FakeCom* newArena = com.Buffer(0x7C000000);
    FakeCom* indexBuffer = com.Buffer(0x1B000000);
    com.On(kDevice, 26, [vertexBuffer, newArena](Regs& r) { // CreateVertexBuffer
        *reinterpret_cast<FakeCom**>(r.args[5]) = r.args[1] >= 0x1000 ? newArena : vertexBuffer;
        return 0u;
    });
    com.On(kDevice, 27, [indexBuffer](Regs& r) { // CreateIndexBuffer
        *reinterpret_cast<FakeCom**>(r.args[5]) = indexBuffer;
        return 0u;
    });
    com.On(kDevice, 101, [](Regs& r) { // GetStreamSource
        *reinterpret_cast<FakeCom**>(r.args[2]) = g_stream;
        *reinterpret_cast<uint32_t*>(r.args[3]) = 0;
        *reinterpret_cast<uint32_t*>(r.args[4]) = 0x20;
        return 0u;
    });
    for (uint32_t tag : {0x7B000000u, 0x7A000000u, 0x7C000000u, 0x1B000000u}) {
        com.On(tag, 11, [](Regs& r) { // Lock
            *reinterpret_cast<uint8_t**>(r.args[3]) = g_card;
            return g_lockResult;
        });
        com.On(tag, 12, [](Regs&) { // Unlock: what was written
            Record({HashBytes(g_card, 0x400)});
            return 0u;
        });
        com.On(tag, 2, [](Regs&) { return 0x33u; }); // Release: the count left
    }
    s.At({0x00624031}, 0, [](Regs& r) { // malloc
        Record({0x4031, r.args[0]});
        return Bump(r.args[0] ? r.args[0] : 4);
    });
    s.At({0x0062411B}, 0, [](Regs& r) { // free
        Record({0x411B, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x004CE9F0, AddressOf(&Nx::sBillboardManager::RemoveMesh)}, 4, [](Regs& r) {
        Record({0xE9F0, r.ecx, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x0048C7F0}, 0, [](Regs&) { return reinterpret_cast<uint32_t>(g_sceneList); });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 20; ++round) {
        ok &= CompareTwoRuns("pc mesh life", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x4BE0 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x007865E4) = 0;
            GameVariable<uint32_t>(0x007865E8) = 0;
            Lcg random(0x4BE00 + round);
            g_card = reinterpret_cast<uint8_t*>(Bump(0x400));
            std::memset(g_card, 0, 0x400);
            g_lockResult = random.Below(5) == 0 ? 0x8876086Cu : 0u;
            std::vector<uint32_t> trace;
            // constructed
            void* at = reinterpret_cast<void*>(Bump(sizeof(sMesh)));
            auto* mesh = Fn<sMesh*(__thiscall*)(void*)>(original, 0x004D4BE0)(at);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(mesh)));
            DumpArena(trace);
            // a vertex buffer of its own or an arena's share
            mesh->stride = static_cast<uint8_t>(0x10 + 4 * random.Below(5));
            mesh->vertexCount = static_cast<uint16_t>(1 + random.Below(12));
            mesh->bufferUsage = random.Below(2) ? 0x208 : 0;
            mesh->bufferFlags = random.Below(4) == 0 ? 1 : 0;
            mesh->unknown1C[0] = static_cast<uint8_t>(4 * random.Below(3));
            uint32_t bytes = mesh->stride * mesh->vertexCount;
            auto* vertices = reinterpret_cast<uint8_t*>(Bump(bytes));
            for (uint32_t i = 0; i < bytes; ++i)
                vertices[i] = static_cast<uint8_t>(random.Next());
            Nx::sVertexArena arena{arenaBuffer, 0, 0x40 * random.Below(4), nullptr};
            bool shared = random.Below(3) == 0;
            if (!(mesh->bufferFlags & 1)) {
                Fn<void(__thiscall*)(void*, const void*, uint32_t, void*)>(original, 0x004D5C30)(mesh, vertices, bytes,
                                                                                               shared ? &arena : nullptr);
                trace.push_back(arena.size);
            } else {
                mesh->vertexBuffers[0] = Fn<sVertexBuffer*(__cdecl*)(uint32_t, uint32_t, uint32_t)>(original, 0x004D5760)(
                    bytes, 0, 1);
            }
            if (mesh->vertexBuffers[0]->flags & 4)
                mesh->vertexBuffers[0]->buffer = arenaBuffer;
            // its colours per vertex
            auto* material = reinterpret_cast<Nx::sMaterial*>(Bump(sizeof(Nx::sMaterial)));
            std::memset(material, 0, sizeof(Nx::sMaterial));
            material->animColors = reinterpret_cast<uint32_t*>(Bump(16));
            for (int i = 0; i < 4; ++i)
                material->animColors[i] = random.Next() * 2654435761u;
            mesh->material = reinterpret_cast<uint8_t*>(material);
            if (random.Below(4)) {
                mesh->perVertexBytes = reinterpret_cast<uint8_t*>(Bump(16));
                for (int i = 0; i < 16; ++i)
                    mesh->perVertexBytes[i] = static_cast<uint8_t>(random.Below(5));
            }
            if (mesh->vertexBuffers[0]->data)
                trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004D4CA0)(mesh));
            // its position, its sphere
            float position[4] = {9, 9, 9, 9};
            if (random.Below(2)) {
                mesh->transform = reinterpret_cast<float*>(Bump(64));
                for (int i = 0; i < 16; ++i)
                    mesh->transform[i] = RandomFloat(random, -50, 50);
            }
            Fn<void(__thiscall*)(void*, float*)>(original, 0x004D4D30)(mesh, position);
            for (float v : position)
                trace.push_back(*reinterpret_cast<uint32_t*>(&v));
            float centre[3] = {RandomFloat(random, -9, 9), RandomFloat(random, -9, 9), RandomFloat(random, -9, 9)};
            Fn<void(__thiscall*)(void*, const float*, float, int, int)>(original, 0x004D5900)(mesh, centre, 4.5f, 7, 8);
            trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(mesh->centre), 16));
            // its strip cleaned
            uint32_t count = 3 + random.Below(60);
            void* ibAt = reinterpret_cast<void*>(Bump(sizeof(sIndexBuffer)));
            auto* ib = Fn<sIndexBuffer*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x004D4270)(
                ibAt, 2 * (count + 32), 0, D3DFMT_INDEX16);
            FillStrip(ib->indices, count, random);
            mesh->indexBuffers[0] = ib;
            mesh->indexCounts[0] = static_cast<uint16_t>(count);
            Fn<void(__thiscall*)(void*)>(original, 0x004D57D0)(mesh);
            trace.push_back(mesh->indexCounts[0]);
            if (random.Below(2)) {
                void* ib2 = reinterpret_cast<void*>(Bump(sizeof(sIndexBuffer)));
                mesh->indexBuffers[1 + random.Below(7)] =
                    Fn<sIndexBuffer*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x004D4270)(ib2, 8, 0, 0x65);
            }
            // more buffers: extra ones, a last one
            mesh->vertexBufferCount = static_cast<uint8_t>(1 + random.Below(3));
            for (uint32_t i = 0; i < mesh->vertexBufferCount; ++i) {
                if (i > 0 && random.Below(2))
                    mesh->vertexBuffers[i] = Fn<sVertexBuffer*(__cdecl*)(uint32_t, uint32_t, uint32_t)>(original, 0x004D5760)(
                        0x40, 0, 0);
                if (random.Below(3) == 0)
                    mesh->extraBuffers[i] = Fn<sVertexBuffer*(__cdecl*)(uint32_t, uint32_t, uint32_t)>(original, 0x004D5760)(
                        0x20, 0, 0);
            }
            if (random.Below(2))
                mesh->lastBuffer = Fn<sVertexBuffer*(__cdecl*)(uint32_t, uint32_t, uint32_t)>(original, 0x004D5760)(0x20, 0, 0);
            // a device reset
            g_sceneList = MakeSceneList(random, arenaBuffer);
            Fn<void(__cdecl*)()>(original, 0x004D67F0)();
            for (sIndexBuffer* buffer : mesh->indexBuffers)
                trace.push_back(buffer ? Relocate(reinterpret_cast<uint32_t>(buffer->buffer)) : 0xDEADu);
            g_lockResult = random.Below(4) == 0 ? 0x8876086Cu : 0u;
            Fn<void(__cdecl*)()>(original, 0x004D68C0)();
            // a buffer of each destroyed on its own
            sVertexBuffer* lone = Fn<sVertexBuffer*(__cdecl*)(uint32_t, uint32_t, uint32_t)>(original, 0x004D5760)(0x30, 0, 0);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004D6720)(lone));
            void* ibAt3 = reinterpret_cast<void*>(Bump(sizeof(sIndexBuffer)));
            auto* ib3 = Fn<sIndexBuffer*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x004D4270)(ibAt3, 6, 0, 0x65);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004D6790)(ib3));
            // the mesh destroyed
            g_stream = random.Below(2) ? vertexBuffer : random.Below(2) ? arenaBuffer : nullptr;
            if (random.Below(3) == 0)
                mesh->flags |= 0x10;
            if (random.Below(4) == 0)
                mesh->flags |= 1;
            if (random.Below(2))
                mesh->billboard = reinterpret_cast<Nx::sBillboard*>(Bump(sizeof(Nx::sBillboard)));
            mesh->unknown48 = random.Below(2) ? reinterpret_cast<void*>(Bump(8)) : nullptr;
            mesh->positions = random.Below(2) ? reinterpret_cast<float*>(Bump(12)) : nullptr;
            mesh->triangles = random.Below(2) ? reinterpret_cast<uint16_t*>(Bump(6)) : nullptr;
            mesh->perVertexData = random.Below(2) ? reinterpret_cast<uint8_t*>(Bump(0x4C)) : nullptr;
            Fn<void(__thiscall*)(void*)>(original, 0x004D69E0)(mesh);
            trace.push_back(GameVariable<uint32_t>(0x007865E4) ? 1u : 0u);
            trace.push_back(GameVariable<uint32_t>(0x007865E8) ? 1u : 0u);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc mesh life: constructed, buffers made / coloured / strips cleaned, reset, destroyed");
}

} // namespace

SELFTEST(Early, TestPcMeshLife, RemoveStandIns);
