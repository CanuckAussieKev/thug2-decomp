// Self-tests for the PC meshes' loading (src/game/gfx/pc_mesh_load.cpp, RFX-29): the original and ours over random
// mesh records - index buffers of every level, up to three vertex buffers (some with bone-scaled positions, the skin
// scaling on and off), vertex formats (none: unpacked for skinning), per-vertex colours, levels of detail, shaders by
// index or a chunk - from memory and through a stand-in file; and the collision data from a strip.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/file/file.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_scene.h"
#include "game/gfx/pixel_shaders.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::sMesh;

constexpr uint32_t kDevice = 0xD3D00000;

std::vector<uint8_t> g_stream;
size_t g_position;
uint8_t* g_card;
Nx::sMaterial* g_material;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

struct Writer {
    std::vector<uint8_t>& out;
    void Bytes(const void* p, size_t n) { out.insert(out.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n); }
    void U8(uint8_t v) { out.push_back(v); }
    void U16(uint16_t v) { Bytes(&v, 2); }
    void U32(uint32_t v) { Bytes(&v, 4); }
    void F32(float v) { Bytes(&v, 4); }
};

void WriteMesh(Writer& w, Lcg& random, bool skinned, bool lightWeights)
{
    uint16_t vertices = static_cast<uint16_t>(2 + random.Below(10));
    for (int i = 0; i < 8; ++i) {
        uint16_t count = i == 0 || random.Below(3) == 0 ? static_cast<uint16_t>(3 + random.Below(20)) : 0;
        w.U16(count);
        for (uint16_t k = 0; k < count; ++k)
            w.U16(static_cast<uint16_t>(random.Below(vertices)));
    }
    uint8_t stride = static_cast<uint8_t>(skinned ? 0x40 : 0x10 + 4 * random.Below(8));
    w.U8(stride);
    w.U16(vertices);
    uint8_t buffers = static_cast<uint8_t>(1 + random.Below(3));
    w.U8(buffers);
    for (uint8_t b = 0; b < buffers; ++b) {
        bool scaled = !skinned && random.Below(2);
        w.U8(scaled);
        uint32_t size = stride * vertices;
        w.U32(size);
        for (uint32_t k = 0; k < size / 4; ++k) {
            if (skinned && k % (stride / 4) == 3) // (packed weights: mostly small)
                w.U32(lightWeights || random.Below(3) ? random.Below(0x800) : random.Next() << 8 ^ random.Next());
            else if (skinned && k % (stride / 4) == 4) // (bones)
                w.U32(random.Below(30) | random.Below(30) << 16);
            else
                w.F32(RandomFloat(random, -10, 10));
        }
        if (scaled) {
            uint32_t records = random.Below(vertices + 1u);
            w.U32(records);
            for (uint32_t r = 0; r < records; ++r) {
                for (int k = 0; k < 3; ++k)
                    w.U32(random.Next());
                for (int k = 0; k < 3; ++k)
                    w.F32(RandomFloat(random, 0, 1));
                for (int k = 0; k < 24; ++k)
                    w.F32(RandomFloat(random, -5, 5));
                for (int k = 0; k < 3; ++k)
                    w.U32(random.Below(3) == 0 ? 0xFFFFFFFFu : random.Below(4));
            }
        }
    }
    w.U32(skinned ? 0 : 1 + random.Below(8));
    w.U32(random.Below(9));
    w.U8(static_cast<uint8_t>(random.Below(32)));
    w.U8(static_cast<uint8_t>(random.Below(32)));
    w.U8(static_cast<uint8_t>(random.Below(32)));
    bool bytes = random.Below(3) == 0;
    w.U8(bytes);
    if (bytes)
        for (uint16_t k = 0; k < vertices; ++k)
            w.U8(static_cast<uint8_t>(random.Below(4)));
    uint32_t levels = random.Below(4);
    w.U32(levels);
    if (levels > 1)
        for (uint32_t k = 0; k < levels; ++k)
            w.F32(100.0f * static_cast<float>(k + 1));
    uint32_t shader = random.Below(4) == 0 ? 1 : random.Below(25);
    w.U32(shader);
    if (shader == 1) { // a chunk: id, size, contents
        w.U32(0xC0DE);
        w.U32(8);
        w.U32(random.Next());
        w.U32(random.Next());
    }
}

void TestPcMeshLoad()
{
    GameMemory memory({{0x007D1800, 4}, {0x007D69E0, 4}, {0x007865CC, 0x20}, {0x00775B64, 0x18}, {0x00786AB8, 1}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* vertexBuffer = com.Buffer(0x7B000000);
    FakeCom* indexBuffer = com.Buffer(0x1B000000);
    com.On(kDevice, 26, [vertexBuffer](Regs& r) {
        *reinterpret_cast<FakeCom**>(r.args[5]) = vertexBuffer;
        return 0u;
    });
    com.On(kDevice, 27, [indexBuffer](Regs& r) {
        *reinterpret_cast<FakeCom**>(r.args[5]) = indexBuffer;
        return 0u;
    });
    for (uint32_t tag : {0x7B000000u, 0x1B000000u}) {
        com.On(tag, 11, [](Regs& r) {
            *reinterpret_cast<uint8_t**>(r.args[3]) = g_card;
            return 0u;
        });
        com.On(tag, 12, [](Regs&) {
            Record({HashBytes(g_card, 0x400)});
            return 0u;
        });
    }
    s.At({0x00624031}, 0, [](Regs& r) {
        Record({0x4031, r.args[0]});
        return Bump(r.args[0] ? r.args[0] : 4);
    });
    s.At({0x0062411B}, 0, [](Regs& r) {
        Record({0x411B, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [](Regs& r) {
        Record({0x9EE0, r.args[1], r.args[2]});
        size_t bytes = static_cast<size_t>(r.args[1]) * r.args[2];
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_stream.data() + g_position, bytes);
        g_position += bytes;
        return static_cast<uint32_t>(bytes);
    });
    s.At({0x005B9FA0, AddressOf(&File::Seek)}, 0, [](Regs& r) {
        Record({0x9FA0, r.args[1], r.args[2]});
        g_position += static_cast<int>(r.args[1]);
        return static_cast<uint32_t>(g_position);
    });
    s.At({0x004DE420, AddressOf(&Nx::sScene::FindMaterial)}, 4, [](Regs& r) {
        Record({0xE420, Relocate(r.ecx), r.args[0]});
        return r.args[0] == 0xBAD ? 0u : reinterpret_cast<uint32_t>(g_material);
    });
    s.At({0x004DDC00, AddressOf(&Nx::MaterialPixelShader)}, 0, [](Regs& r) {
        Record({0xDC00, Relocate(r.args[0]), *reinterpret_cast<uint32_t*>(r.args[1])});
        *reinterpret_cast<uint32_t*>(r.args[1]) = 0x95500000;
        return 0u;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, r.args[0]});
        return 0u;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns("pc mesh load", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5CC0 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x007865E4) = 0;
            GameVariable<uint32_t>(0x007865E8) = 0;
            for (int k = 0; k < 6; ++k) // (the vertex formats by type: none empty)
                (&GameVariable<uint32_t>(0x00775B64))[k] = 0x152 + 0x100 * k;
            Lcg random(0x5CC00 + round);
            std::vector<uint32_t> trace;
            g_card = reinterpret_cast<uint8_t*>(Bump(0x400));
            std::memset(g_card, 0, 0x400);
            auto* scales = reinterpret_cast<float*>(Bump(0x40));
            for (int k = 0; k < 16; ++k)
                scales[k] = RandomFloat(random, 0.5f, 2);
            GameVariable<uint8_t>(0x007865CC) = static_cast<uint8_t>(random.Below(4) != 0);
            GameVariable<float*>(0x007865DC) = random.Below(5) ? scales : nullptr;
            g_material = reinterpret_cast<Nx::sMaterial*>(Bump(sizeof(Nx::sMaterial)));
            std::memset(g_material, 0, sizeof(Nx::sMaterial));
            g_material->passCount = 1 + random.Below(3);
            g_material->passFlags[0] = random.Below(2) ? 0x10 : 0;
            bool skinned = round % 4 == 3;
            g_stream.clear();
            Writer writer{g_stream};
            WriteMesh(writer, random, skinned, round % 8 == 3);
            g_stream.resize(g_stream.size() + 32, 0xCD);
            uint32_t checksum = random.Below(6) == 0 ? 0xBAD : 0x600D;
            auto* scene = reinterpret_cast<Nx::sScene*>(Bump(sizeof(Nx::sScene)));
            Nx::sVertexArena arena{vertexBuffer, 0, 0x40, nullptr};
            Nx::sVertexArena* useArena = random.Below(2) ? &arena : nullptr;
            // from memory
            auto* fromMemory = Fn<sMesh*(__thiscall*)(void*)>(original, 0x004D4BE0)(reinterpret_cast<void*>(Bump(sizeof(sMesh))));
            uint8_t* cursor = reinterpret_cast<uint8_t*>(Bump(static_cast<uint32_t>(g_stream.size())));
            std::memcpy(cursor, g_stream.data(), g_stream.size());
            uint8_t* start = cursor;
            if (checksum != 0xBAD || !skinned)
                Fn<void(__thiscall*)(void*, uint8_t**, uint32_t, void*, void*)>(original, 0x004D5CC0)(fromMemory, &cursor,
                                                                                                      checksum, scene,
                                                                                                      useArena);
            trace.push_back(static_cast<uint32_t>(cursor - start));
            trace.push_back(arena.size);
            // from a file
            auto* fromFile = Fn<sMesh*(__thiscall*)(void*)>(original, 0x004D4BE0)(reinterpret_cast<void*>(Bump(sizeof(sMesh))));
            g_position = 0;
            if (checksum != 0xBAD || !skinned)
                Fn<void(__thiscall*)(void*, void*, uint32_t, void*, void*, int)>(original, 0x004D6100)(
                    fromFile, reinterpret_cast<void*>(0xF11E0000), checksum, scene, useArena, static_cast<int>(random.Below(2)));
            trace.push_back(static_cast<uint32_t>(g_position));
            trace.push_back(arena.size);
            // skinning set up directly, into an arena
            if (round % 2 == 0) {
                auto* direct = Fn<sMesh*(__thiscall*)(void*)>(original, 0x004D4BE0)(reinterpret_cast<void*>(Bump(sizeof(sMesh))));
                direct->material = reinterpret_cast<uint8_t*>(g_material);
                direct->vertexCount = static_cast<uint16_t>(1 + random.Below(6));
                direct->stride = 0x40;
                auto* packed = reinterpret_cast<uint8_t*>(Bump(0x40 * direct->vertexCount));
                for (uint32_t k = 0; k < 0x10u * direct->vertexCount; ++k)
                    reinterpret_cast<uint32_t*>(packed)[k] = random.Next() << 8 ^ random.Next();
                Fn<void(__thiscall*)(void*, const void*, uint32_t, void*)>(original, 0x004D5940)(direct, packed, 0, &arena);
                trace.push_back(arena.size);
            }
            // the collision data
            sMesh* mesh = random.Below(2) ? fromMemory : fromFile;
            if (mesh->vertexBuffers[0] && mesh->vertexBuffers[0]->data && random.Below(4)) {
                if (random.Below(4) == 0)
                    mesh->positionCount = 3;
                Fn<void(__thiscall*)(void*)>(original, 0x004D6530)(mesh);
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc mesh load: meshes from memory and a file, scaled positions, skinning unpacked, collision data");
}

} // namespace

SELFTEST(Early, TestPcMeshLoad, RemoveStandIns);
