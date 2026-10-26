// Self-tests for the PC geom (src/game/gfx/geom.cpp, RFX-4): the original and ours over stand-in meshes (vertex
// buffers with real vertices, materials with passes and UV wibbles), with the mesh, mesh list, instance, scene and
// D3DX calls stood in and allocations from an arena; traces and the arena compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/geom.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/mth/matrix.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPcGeom;
using Nx::sMaterial;
using Nx::sMesh;
using Nx::sScene;

uint32_t HashWords(const void* p, int count) { return HashBytes(static_cast<const uint8_t*>(p), 4 * count); }

void* g_sceneVtable[1]; // the stand-in PC scenes' (slot 0: destroy, recorded)

void StandInGeomCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x004D4090}, 16, [](Regs& r) { // sVertexBuffer::Lock(offset, size, &data, flags)
        auto* buffer = reinterpret_cast<Nx::sVertexBuffer*>(r.ecx);
        Record({0x4090, Relocate(r.ecx), r.args[0], r.args[1], r.args[3]});
        *reinterpret_cast<uint8_t**>(r.args[2]) = buffer->data;
        return 0u;
    });
    StandInThis(s, {0x004D40E0}, 0); // Unlock
    s.At({0x004D4D30}, 4, [](Regs& r) { // the mesh transform's position
        Record({0x4D30, Relocate(r.ecx)});
        auto* out = reinterpret_cast<float*>(r.args[0]);
        uint32_t seed = Relocate(r.ecx);
        out[0] = static_cast<float>(seed % 97) - 40.0f;
        out[1] = static_cast<float>(seed % 31) * 0.25f;
        out[2] = -static_cast<float>(seed % 13);
        out[3] = 1.0f;
        return 0u;
    });
    StandInThis(s, {0x004D3E50}, 1);
    StandInThis(s, {0x004D69E0}, 0);
    StandInThis(s, {0x004D57D0}, 0);
    s.At({0x004D1030, AddressOf(&Nx::sInstance::Construct)}, 16, [](Regs& r) { // sInstance's constructor
        auto* inst = reinterpret_cast<Nx::sInstance*>(r.ecx);
        Record({0x1030, Relocate(r.ecx), Relocate(r.args[0]), HashWords(reinterpret_cast<void*>(r.args[1]), 16),
                r.args[2], Relocate(r.args[3])});
        std::memset(inst, 0, sizeof(*inst));
        std::memcpy(&inst->transform, reinterpret_cast<void*>(r.args[1]), 64);
        inst->scene = reinterpret_cast<sScene*>(r.args[0]);
        inst->boneCount = static_cast<int>(r.args[2]);
        inst->bones = reinterpret_cast<Mth::Matrix*>(r.args[3]);
        inst->active = true;
        return r.ecx;
    });
    StandInThis(s, {0x004D10C0, AddressOf(&Nx::sInstance::DestroyBody)}, 0);
    s.At({0x004D1A60, AddressOf(&Nx::sUVWibble::Construct)}, 0, [](Regs& r) {
        Record({0x1A60, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x30);
        reinterpret_cast<float*>(r.ecx)[8] = 1.0f;
        return r.ecx;
    });
    StandInThis(s, {0x004D1A80, AddressOf(&Nx::sTextureAnim::Restart)}, 0);
    StandInThis(s, {0x0048E130}, 0);
    s.At({0x004C6130}, 4, [](Regs& r) { // the PC scene's constructor
        Record({0x6130, Relocate(r.ecx), r.args[0]});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x284);
        *reinterpret_cast<void***>(r.ecx) = g_sceneVtable;
        return r.ecx;
    });
    g_sceneVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xDE57, Relocate(r.ecx), r.args[0], Relocate(*reinterpret_cast<uint32_t*>(r.ecx + 0x280))});
        return r.ecx;
    });
    s.At({0x004D71F0, AddressOf(&Nx::sMesh::Clone)}, 4, [](Regs& r) {
        Record({0x71F0, Relocate(r.ecx), r.args[0] & 0xFF});
        uint32_t copy = Bump(sizeof(sMesh));
        std::memcpy(reinterpret_cast<void*>(copy), reinterpret_cast<void*>(r.ecx), sizeof(sMesh));
        return copy;
    });
    s.At({0x004D6E00, AddressOf(&Nx::sMesh::MoveTo)}, 4, [](Regs& r) {
        Record({0x6E00, Relocate(r.ecx), HashWords(reinterpret_cast<void*>(r.args[0]), 4)});
        return 0u;
    });
    StandInThis(s, {0x004D6FB0, AddressOf(&Nx::sMesh::TurnY)}, 1);
    // the mesh lists: counted (the count kept), the array made, the rest recorded
    s.At({0x004DDDF0, AddressOf(&Nx::sScene::CountMeshes)}, 8, [](Regs& r) {
        Record({0xDDF0, Relocate(r.ecx), r.args[0], Relocate(r.args[1])});
        // (kept aside until the array is made, so other users of the list see it unchanged)
        *reinterpret_cast<int*>(reinterpret_cast<sScene*>(r.ecx)->unknown38) += static_cast<int>(r.args[0]);
        return 0u;
    });
    s.At({0x004DE3E0, AddressOf(&Nx::sScene::AllocateMeshList)}, 0, [](Regs& r) {
        auto* list = reinterpret_cast<sScene*>(r.ecx);
        list->meshCount += *reinterpret_cast<int*>(list->unknown38);
        *reinterpret_cast<int*>(list->unknown38) = 0;
        Record({0xE3E0, Relocate(r.ecx), static_cast<uint32_t>(list->meshCount)});
        list->meshes = reinterpret_cast<uint8_t**>(Bump(4 * list->meshCount + 4));
        return 0u;
    });
    s.At({0x004DDE20, AddressOf(&Nx::sScene::AddMeshes)}, 8, [](Regs& r) {
        Record({0xDE20, Relocate(r.ecx), r.args[0], Relocate(r.args[1])});
        auto* list = reinterpret_cast<sScene*>(r.ecx);
        for (uint32_t i = 0; i < r.args[0] && list->filled < list->meshCount; ++i)
            list->meshes[list->filled++] = reinterpret_cast<uint8_t**>(r.args[1])[i];
        return 0u;
    });
    StandInThis(s, {0x004DDFA0, AddressOf(&Nx::sScene::SortMeshes)}, 0);
    StandInThis(s, {0x004DE340, AddressOf(&Nx::sScene::UpdateSphere)}, 0);
    StandInThis(s, {0x004DDE60, AddressOf(&Nx::sScene::RemoveMeshes)}, 2);
    StandInThis(s, {0x004DE060, AddressOf(&Nx::sScene::BreakStrips)}, 3);
    s.At({0x004DE300, AddressOf(&Nx::sScene::Construct)}, 0, [](Regs& r) {
        Record({0xE300, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, sizeof(sScene));
        return r.ecx;
    });
    StandInThis(s, {0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 1);
    StandInThis(s, {0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0);
    s.At({0x00404E20, AddressOf(&Mth::CreateRotateYMatrix)}, 0, [](Regs& r) {
        Record({0x4E20, r.args[1]});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = static_cast<float>(i) * 0.5f - 1.0f;
        return r.args[0];
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) { // D3DXMatrixMultiply(out, a, b)
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, Relocate(r.args[0]), HashWords(a, 16), HashWords(b, 16)});
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
}

// The world: meshes with vertex buffers and materials, built in the arena.
struct World {
    sMesh* meshes[6];
    sMaterial* materials[6];
    CPcGeom* plain;     // a sector's geometry: meshes of its own
    CPcGeom* instanced; // an instance of a mesh list
    sScene* list;
    uint8_t* scene;     // a PC scene (0x284) with `list`
    uint8_t* model;     // a model: +8 bone count, +0xC bones
    uint8_t* source;    // the mesh it's an instance of (0xC8)
};

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

World BuildWorld(Lcg& random, bool original, int meshCount)
{
    World w = {};
    for (int i = 0; i < 6; ++i) {
        auto* mesh = reinterpret_cast<sMesh*>(Bump(sizeof(sMesh)));
        std::memset(mesh, 0, sizeof(sMesh));
        mesh->flags = random.Next() & 0x4F;
        mesh->stride = static_cast<uint8_t>(0x18 + 4 * random.Below(3));
        mesh->unknown1C[0] = 0x0C + 4 * static_cast<uint8_t>(random.Below(2)); // the colour's offset
        mesh->vertexCount = static_cast<uint16_t>(i == 2 ? 0 : 1 + random.Below(9));
        mesh->vertexBufferCount = static_cast<uint8_t>(i == 3 ? 0 : 1 + random.Below(3));
        for (int b = 0; b < 3; ++b) {
            auto* buffer = reinterpret_cast<Nx::sVertexBuffer*>(Bump(0x40));
            std::memset(buffer, 0, 0x40);
            buffer->size = mesh->stride * mesh->vertexCount;
            buffer->data = reinterpret_cast<uint8_t*>(Bump(buffer->size + 16));
            for (uint32_t k = 0; k < buffer->size / 4; ++k)
                reinterpret_cast<float*>(buffer->data)[k] = RandomFloat(random, -50.0f, 50.0f);
            mesh->vertexBuffers[b] = buffer;
        }
        float* color = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(mesh) + 0x80);
        for (int k = 0; k < 3; ++k)
            color[k] = RandomFloat(random, 0.0f, 1.0f);
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x38) =
            random.Pick({0x7000u, 0x7001u, 0x7002u, 0x7003u});
        auto* material = reinterpret_cast<sMaterial*>(Bump(sizeof(sMaterial)));
        std::memset(material, 0, sizeof(sMaterial));
        material->nameChecksum = 0x1000 + 8 * static_cast<uint32_t>(random.Below(4)) + random.Below(3);
        material->passCount = 1 + random.Below(4);
        for (int p = 0; p < 4; ++p) {
            material->passFlags[p] = random.Next() & 0x81;
            if (random.Below(2)) {
                auto* wibble = reinterpret_cast<Nx::sUVWibble*>(Bump(0x30));
                std::memset(wibble, 0x11 * p, 0x30);
                material->uvWibble[p] = wibble;
            }
        }
        material->textureAnim = random.Below(2) ? reinterpret_cast<Nx::sTextureAnim*>(Bump(16)) : nullptr;
        mesh->material = i == 4 ? nullptr : reinterpret_cast<uint8_t*>(material);
        w.meshes[i] = mesh;
        w.materials[i] = material;
    }
    // (the vertex shaders the UV matrix swaps)
    for (int k = 0; k < 3; ++k) {
        GameVariable<uint32_t>(0x00775B70 + 4 * k) = 0x7000 + k;
        GameVariable<uint32_t>(0x00775B58 + 4 * k) = 0x7010 + k;
    }
    w.list = reinterpret_cast<sScene*>(Bump(sizeof(sScene)));
    std::memset(w.list, 0, sizeof(sScene));
    w.list->meshCount = meshCount;
    w.list->filled = meshCount;
    w.list->meshes = reinterpret_cast<uint8_t**>(Bump(4 * 6));
    for (int i = 0; i < 6; ++i)
        w.list->meshes[i] = reinterpret_cast<uint8_t*>(w.meshes[5 - i]);
    w.scene = reinterpret_cast<uint8_t*>(Bump(0x284));
    std::memset(w.scene, 0, 0x284);
    *reinterpret_cast<sScene**>(w.scene + 0x280) = w.list;
    for (CPcGeom** geom : {&w.plain, &w.instanced}) {
        *geom = reinterpret_cast<CPcGeom*>(Bump(sizeof(CPcGeom)));
        std::memset(*geom, 0, sizeof(CPcGeom));
        Fn<void*(__thiscall*)(void*)>(original, 0x004B4460)(*geom);
        (*geom)->scene = w.scene;
        (*geom)->meshes = reinterpret_cast<sMesh**>(Bump(4 * 6));
        (*geom)->meshCount = meshCount;
        for (int i = 0; i < 6; ++i)
            (*geom)->meshes[i] = w.meshes[i];
        (*geom)->bounds.min = {-1, -2, -3, 1};
        (*geom)->bounds.max = {4, 5, 6, 1};
    }
    auto* inst = reinterpret_cast<Nx::sInstance*>(Bump(sizeof(Nx::sInstance)));
    std::memset(inst, 0, sizeof(*inst));
    auto* m = reinterpret_cast<float*>(&inst->transform);
    for (int i = 0; i < 16; ++i)
        m[i] = RandomFloat(random, -2.0f, 2.0f);
    inst->scene = w.list;
    inst->boneCount = 3;
    inst->bones = reinterpret_cast<Mth::Matrix*>(Bump(64 * 8));
    inst->active = random.Below(2) != 0;
    w.instanced->instance = inst;
    w.model = reinterpret_cast<uint8_t*>(Bump(0x20));
    std::memset(w.model, 0, 0x20);
    *reinterpret_cast<int*>(w.model + 8) = 2 + static_cast<int>(random.Below(3));
    *reinterpret_cast<void**>(w.model + 0xC) = reinterpret_cast<void*>(Bump(64 * 4));
    w.source = reinterpret_cast<uint8_t*>(Bump(0xC8));
    std::memset(w.source, 0, 0xC8);
    *reinterpret_cast<uint8_t**>(w.source + 0xC4) = w.scene;
    *reinterpret_cast<uint32_t*>(w.source + 0xBC) = 0xB4EA0000;
    *reinterpret_cast<uint32_t*>(w.source + 0xC0) = 7;
    w.instanced->source = w.source;
    return w;
}

// Every member on a sector's geometry and an instance's (and on one with no meshes): colours (the "none" colours
// too), material colours by checksum and pass (fixed passes, named and not), visibility, activity, spheres, positions,
// orientations, quarter turns (none too), scales, bones, strip breaks, vertices and their colours (meshes with no
// vertices and no buffers), UV wibbles and matrices, texture animations; the load list made into the array (with and
// without a vertex arena), the mesh lists, the clones, the instance set-up, the destructors.
void TestPcGeom()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x00701610, 0x5C}, {0x00775B58, 0x24}});
    StandIns& s = *new StandIns();
    StandInGeomCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 8; ++round) {
        ok &= CompareTwoRuns("geom", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x6E0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            alignas(16) static uint8_t manager[0x600]; // (its top-down heap at +0x110)
            std::memset(manager, 0, sizeof(manager));
            *reinterpret_cast<uint32_t*>(manager + 0x110) = 0x70D0;
            GameVariable<void*>(0x007D1800) = manager;
            Lcg random(0x9E0 + round);
            int meshCount = round == 0 ? 0 : 1 + static_cast<int>(random.Below(6));
            World w = BuildWorld(random, original, meshCount);
            std::vector<uint32_t> trace;
            auto call0 = [&](CPcGeom* g, uint32_t address) { Fn<void(__thiscall*)(void*)>(original, address)(g); };
            auto call1 = [&](CPcGeom* g, uint32_t address, uint32_t a) {
                Fn<void(__thiscall*)(void*, uint32_t)>(original, address)(g, a);
            };
            // (with the result kept)
            auto callR0 = [&](CPcGeom* g, uint32_t address) {
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, address)(g)));
            };
            auto callR1 = [&](CPcGeom* g, uint32_t address, uint32_t a) {
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, address)(g, a)));
            };
            CPcGeom* empty = reinterpret_cast<CPcGeom*>(Bump(sizeof(CPcGeom)));
            std::memset(empty, 0, sizeof(CPcGeom)); // (geoms are allocated zeroed)
            Fn<void*(__thiscall*)(void*)>(original, 0x004B4460)(empty);
            for (CPcGeom* g : {w.plain, w.instanced, empty}) {
                bool hasMeshes = g->meshes != nullptr;
                // colours
                uint32_t colors[5] = {0x00808080, 0x00808081, 0x7F808080, 0x12345678 + random.Next(),
                                      0x00818080};
                for (uint32_t c : colors) {
                    call1(g, 0x004B48B0, c);
                    trace.push_back(g->instance ? g->instance->flags : 0x99);
                    uint32_t out = 0xCCCCCCCC;
                    callR1(g, 0x004B4B20, reinterpret_cast<uint32_t>(&out));
                    trace.push_back(out);
                }
                call0(g, 0x004B4970);
                for (int k = 0; k < 14; ++k) {
                    uint32_t checksum = 0x1000 - 8 * random.Below(3) - random.Below(3) + 8 * random.Below(2);
                    if (k == 4 || k >= 6)
                        checksum = 0x0F70 + random.Below(16); // (0x80 and more below the names: none)
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, int, uint32_t)>(original, 0x004B49C0)(
                                        g, checksum, static_cast<int>(random.Below(5)) - (k == 5 ? 1 : 0),
                                        random.Next()) &
                                    0xFF);
                }
                call1(g, 0x004B4870, random.Next());
                trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004B4120)(g));
                for (uint32_t on : {0x100u, 0x101u, 0u}) {
                    call1(g, 0x004B4630, on);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004B4690)(g) & 0xFF);
                }
                Mth::Vector v = {RandomFloat(random, -9, 9), RandomFloat(random, -9, 9), RandomFloat(random, -9, 9),
                                 RandomFloat(random, 0, 9)};
                call1(g, 0x004B4730, reinterpret_cast<uint32_t>(&v));
                Mth::Vector out;
                trace.push_back(
                    ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x004B4760)(g, &out)) ==
                    reinterpret_cast<uint32_t>(&out));
                trace.push_back(HashWords(&out, 4));
                if (hasMeshes) {
                    call1(g, 0x004B55D0, reinterpret_cast<uint32_t>(&v));
                    callR0(g, 0x004B46A0);
                }
                Mth::Matrix orientation;
                for (int i = 0; i < 16; ++i)
                    reinterpret_cast<float*>(&orientation)[i] = RandomFloat(random, -1, 1);
                call1(g, 0x004B5650, reinterpret_cast<uint32_t>(&orientation));
                callR0(g, 0x004B56E0);
                trace.push_back(HashWords(&GameVariable<uint32_t>(0x00701628), 16));
                if (hasMeshes || g->instance) {
                    call1(g, 0x004B57C0, 0);
                    call1(g, 0x004B57C0, 1 + random.Below(3));
                    call1(g, 0x004B57C0, static_cast<uint32_t>(-1));
                    if (g->instance)
                        trace.push_back(HashWords(&g->instance->transform, 16));
                }
                Mth::Vector scale = {RandomFloat(random, 0.5f, 2), RandomFloat(random, 0.5f, 2),
                                     RandomFloat(random, 0.5f, 2), 1.0f};
                call1(g, 0x004B4DB0, reinterpret_cast<uint32_t>(&scale));
                callR1(g, 0x004B4ED0, reinterpret_cast<uint32_t>(&out));
                trace.push_back(HashWords(&out, 4));
                Mth::Matrix bones[4];
                for (int i = 0; i < 64; ++i)
                    reinterpret_cast<float*>(bones)[i] = RandomFloat(random, -1, 1);
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*, void*, int, int, int)>(original, 0x004B47B0)(
                                    g, &orientation, bones, 3, 7, 8) &
                                0xFF);
                Fn<void(__thiscall*)(void*, void*, int)>(original, 0x004B47F0)(g, bones + 1, 2);
                if (g->instance) {
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004B4840)(g, 0x55) & 0xFF);
                    call0(g, 0x004B45F0);
                }
                // vertices and colours
                uint32_t vertices = Fn<uint32_t(__thiscall*)(void*)>(original, 0x004B4130)(g);
                trace.push_back(vertices);
                std::vector<Mth::Vector> positions(vertices + 1);
                call1(g, 0x004B4BB0, reinterpret_cast<uint32_t>(positions.data()));
                trace.push_back(HashWords(positions.data(), 4 * static_cast<int>(vertices)));
                for (Mth::Vector& p : positions)
                    p = {RandomFloat(random, -30, 30), RandomFloat(random, -30, 30), RandomFloat(random, -30, 30),
                         1.0f};
                call1(g, 0x004B4C50, reinterpret_cast<uint32_t>(positions.data()));
                std::vector<uint8_t> bytes(4 * vertices + 4);
                call1(g, 0x004B4160, reinterpret_cast<uint32_t>(bytes.data()));
                trace.push_back(HashBytes(bytes.data(), bytes.size()));
                for (uint8_t& b : bytes)
                    b = static_cast<uint8_t>(random.Next());
                call1(g, 0x004B4210, reinterpret_cast<uint32_t>(bytes.data()));
                // UV wibbles
                float wib[8];
                for (float& f : wib)
                    f = RandomFloat(random, -3, 3);
                Fn<void(__thiscall*)(void*, float, float, float, float, float, float, float, float)>(original,
                                                                                                    0x004B4300)(
                    g, wib[0], wib[1], wib[2], wib[3], wib[4], wib[5], wib[6], wib[7]);
                Fn<void(__thiscall*)(void*, float, float)>(original, 0x004B43E0)(g, wib[1], wib[2]);
                for (sMaterial* material : w.materials)
                    trace.push_back(HashWords(material->passFlags, 4));
                call1(g, 0x004B4EF0, 0);
                for (int k = 0; k < 3; ++k) {
                    uint32_t checksum = 0x1000 - 8 * random.Below(3) - random.Below(3);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, int, void*)>(original, 0x004B4F60)(
                                        g, checksum, static_cast<int>(random.Below(4)), &orientation) &
                                    0xFF);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, int)>(original, 0x004B5130)(
                                        g, checksum + 8, static_cast<int>(random.Below(4))) &
                                    0xFF);
                }
                call0(g, 0x004B52A0);
                if (g->scene)
                    trace.push_back(
                        Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004B44D0)(g, random.Below(2)) & 0xFF);
                if (hasMeshes)
                    callR0(g, 0x004B4510);
                Fn<void(__thiscall*)(void*, Mth::Matrix*, int)>(original, 0x004B4820)(g, bones, 4);
            }
            *reinterpret_cast<int*>(w.list->unknown38) = 0; // (counts left by AddToMeshList's counting)
            // clones
            for (uint32_t share : {0x100u, 0x101u}) {
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t, void*)>(original, 0x004B58C0)(
                    w.plain, share, w.scene)));
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t, void*)>(original, 0x004B5B80)(
                    w.instanced, share, w.model)));
            }
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*, void*, int)>(original, 0x004B54B0)(
                                empty, w.source, w.model, 9) &
                            0xFF);
            // the load list: meshes added, made into the array, with and without a vertex arena
            for (int k = 0; k < 2; ++k) {
                CPcGeom* loading = reinterpret_cast<CPcGeom*>(Bump(sizeof(CPcGeom)));
                std::memset(loading, 0, sizeof(CPcGeom));
                Fn<void*(__thiscall*)(void*)>(original, 0x004B4460)(loading);
                call0(loading, 0x004B5F60);
                for (int i = 0; i < (k ? meshCount : 0); ++i)
                    call1(loading, 0x004B52F0, reinterpret_cast<uint32_t>(w.meshes[i]));
                call1(loading, 0x004B5360, k ? 0x0A7E0000u : 0u);
                if (loading->meshes)
                    call1(loading, 0x004B5360, 1); // (meshes already: nothing)
                loading->scene = k ? w.scene : nullptr;
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004B5FC0)(
                    loading, k ? 1u : 2u)));
            }
            // a node and a head by themselves
            for (uint32_t address : {0x004B5E50u, 0x004B5E90u}) {
                auto* node = reinterpret_cast<Nx::sMeshListNode*>(Bump(0x14));
                auto* other = reinterpret_cast<Nx::sMeshListNode*>(Bump(0x14));
                node->next = node->prev = other;
                other->next = other->prev = node;
                trace.push_back(ArenaRelocate(
                    Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, address)(node, address == 0x004B5E50 ? 2u : 1u)));
            }
            callR1(w.instanced, 0x004B5FC0, 0);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "geom: every member of the PC geom, its clones and its load list");
}

} // namespace

SELFTEST(Early, TestPcGeom, RemoveStandIns);
