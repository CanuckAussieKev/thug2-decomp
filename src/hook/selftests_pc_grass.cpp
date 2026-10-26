// Self-tests for the PC grass (src/game/gfx/pc_grass.cpp, RFX-30): the original and ours over random grass meshes -
// layered (0-7 layers, materials found or not) and animated (the sky texture found or not, one or two texture
// coordinate sets) - a recording stand-in device and buffers; the heap, the mesh's copy, the material lookups, the
// pixel shaders and the geom's mesh list stood in.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/geom.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_grass.h"
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

uint8_t* g_skyScene;
uint8_t* g_textureEntry;
bool g_materialFound;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

Nx::sVertexBuffer* MakeBuffer(uint32_t bytes, Lcg& random)
{
    auto* vb = reinterpret_cast<Nx::sVertexBuffer*>(Bump(sizeof(Nx::sVertexBuffer)));
    std::memset(vb, 0, sizeof(Nx::sVertexBuffer));
    vb->size = bytes;
    vb->data = reinterpret_cast<uint8_t*>(Bump(bytes));
    for (uint32_t i = 0; i < bytes / 4; ++i)
        reinterpret_cast<float*>(vb->data)[i] = RandomFloat(random, -100, 100);
    vb->lockFlags = 0xFFFFFFFF;
    return vb;
}

void TestPcGrass()
{
    GameMemory memory({{0x007D1800, 4}, {0x007D69E0, 4}, {0x007865E4, 8}, {0x00782350, 0x260}, {0x007CA924, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* vertexBuffer = com.Buffer(0x7B000000);
    com.On(kDevice, 26, [vertexBuffer](Regs& r) {
        *reinterpret_cast<FakeCom**>(r.args[5]) = vertexBuffer;
        return 0u;
    });
    s.At({0x00624031}, 0, [](Regs& r) {
        Record({0x4031, r.args[0]});
        return Bump(r.args[0] ? r.args[0] : 4);
    });
    s.At({0x0062411B}, 0, [](Regs& r) {
        Record({0x411B, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xB110});
        return 0u;
    });
    s.At({0x0048C7B0}, 0, [](Regs&) { return reinterpret_cast<uint32_t>(g_skyScene); });
    s.At({0x0049F540}, 4, [](Regs& r) {
        Record({0xF540, Relocate(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_textureEntry);
    });
    s.At({0x004D71F0, AddressOf(&Nx::sMesh::Clone)}, 4, [](Regs& r) { // a copy with copies of its buffers
        Record({0x71F0, Relocate(r.ecx), r.args[0]});
        auto* mesh = reinterpret_cast<sMesh*>(r.ecx);
        auto* copy = reinterpret_cast<sMesh*>(Bump(sizeof(sMesh)));
        std::memcpy(copy, mesh, sizeof(sMesh));
        auto* vb = reinterpret_cast<Nx::sVertexBuffer*>(Bump(sizeof(Nx::sVertexBuffer)));
        std::memcpy(vb, mesh->vertexBuffers[0], sizeof(Nx::sVertexBuffer));
        vb->data = reinterpret_cast<uint8_t*>(Bump(vb->size));
        std::memcpy(vb->data, mesh->vertexBuffers[0]->data, vb->size);
        copy->vertexBuffers[0] = vb;
        return reinterpret_cast<uint32_t>(copy);
    });
    s.At({0x004DE420, AddressOf(&Nx::sScene::FindMaterial)}, 4, [](Regs& r) {
        Record({0xE420, Relocate(r.ecx), r.args[0]});
        auto* material = reinterpret_cast<Nx::sMaterial*>(Bump(sizeof(Nx::sMaterial)));
        std::memset(material, 0, sizeof(Nx::sMaterial));
        material->passFlags[0] = 0x1000;
        return reinterpret_cast<uint32_t>(material);
    });
    s.At({0x004DDC00, AddressOf(&Nx::MaterialPixelShader)}, 0, [](Regs& r) {
        Record({0xDC00, Relocate(r.args[0]), Relocate(r.args[1])});
        return 0u;
    });
    s.At({0x004B52F0, AddressOf(&Nx::CPcGeom::AddMesh)}, 4, [](Regs& r) {
        auto* mesh = reinterpret_cast<sMesh*>(r.args[0]);
        Record({0x52F0, Relocate(r.ecx), Relocate(r.args[0]), HashBytes(mesh->vertexBuffers[0]->data, mesh->vertexBuffers[0]->size)});
        return 0u;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns("pc grass", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x0D30 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x007865E4) = 0;
            auto* manager = reinterpret_cast<uint8_t*>(Bump(0x5E8)); // (the heaps: the bottom-up one, +0x114)
            std::memset(manager, 0, 0x5E8);
            *reinterpret_cast<uint32_t*>(manager + 0x114) = 0xB0770000;
            GameVariable<uint8_t*>(0x007D1800) = manager;
            Lcg random(0x0D300 + round);
            std::vector<uint32_t> trace;
            // the sky's texture (or none)
            g_skyScene = reinterpret_cast<uint8_t*>(Bump(0x90));
            *reinterpret_cast<uint32_t*>(g_skyScene + 0x88) = 0x5C700000;
            g_textureEntry = nullptr;
            if (random.Below(4)) {
                g_textureEntry = reinterpret_cast<uint8_t*>(Bump(0x20));
                std::memset(g_textureEntry, 0, 0x20);
                auto* texture = reinterpret_cast<uint8_t*>(Bump(0x20));
                for (int i = 0; i < 0x20; ++i)
                    texture[i] = static_cast<uint8_t>(random.Next());
                g_textureEntry[8] = static_cast<uint8_t>(random.Below(2));
                if (g_textureEntry[8] & 1) {
                    auto* holder = reinterpret_cast<uint8_t**>(Bump(4));
                    *holder = texture;
                    *reinterpret_cast<uint8_t***>(g_textureEntry + 0x10) = holder;
                } else {
                    *reinterpret_cast<uint8_t**>(g_textureEntry + 0x10) = texture;
                }
            }
            // a geom (its scene's mesh list) and a mesh
            auto* geom = reinterpret_cast<Nx::CPcGeom*>(Bump(sizeof(Nx::CPcGeom)));
            std::memset(geom, 0, sizeof(Nx::CPcGeom));
            geom->scene = reinterpret_cast<uint8_t*>(Bump(0x284));
            *reinterpret_cast<uint32_t*>(geom->scene + 0x280) = Bump(sizeof(Nx::sScene));
            auto* mesh = reinterpret_cast<sMesh*>(Bump(sizeof(sMesh)));
            std::memset(mesh, 0, sizeof(sMesh));
            mesh->vertexCount = static_cast<uint16_t>(1 + random.Below(10));
            mesh->unknown1C[2] = static_cast<uint8_t>(0xC + 4 * random.Below(4));
            mesh->stride = static_cast<uint8_t>(mesh->unknown1C[2] + (random.Below(2) ? 8 : 16));
            mesh->fvf = 0x152 + (random.Below(2) ? 0 : 0x100);
            mesh->bufferUsage = 0x200;
            mesh->vertexBuffers[0] = MakeBuffer(mesh->stride * mesh->vertexCount + 16, random);
            mesh->vertexBuffers[0]->buffer = vertexBuffer;
            auto* material = reinterpret_cast<Nx::sMaterial*>(Bump(sizeof(Nx::sMaterial)));
            std::memset(material, 0, sizeof(Nx::sMaterial));
            material->passFlags[0] = (random.Below(3) == 0 ? 0x8000000u : 0u) | (random.Below(2) ? 9u : 0u);
            material->unknown18 = random.Below(8);
            float height = RandomFloat(random, 0.5f, 20);
            std::memcpy(&material->unknown14, &height, 4);
            mesh->material = random.Below(8) ? reinterpret_cast<uint8_t*>(material) : nullptr;
            if (round % 6 == 5) { // (animated, without the sky's texture)
                material->passFlags[0] |= 0x8000000;
                mesh->material = reinterpret_cast<uint8_t*>(material);
                g_textureEntry = nullptr;
            }
            trace.push_back(Fn<uint32_t(__cdecl*)(void*, void*)>(original, 0x004D0D30)(geom, mesh) & 0xFF);
            // the animated material on its own
            auto* other = reinterpret_cast<Nx::sMaterial*>(Bump(sizeof(Nx::sMaterial)));
            std::memset(other, 0xAB, sizeof(Nx::sMaterial));
            Fn<void(__cdecl*)(void*)>(original, 0x004D08A0)(other);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc grass: layered and animated grass, the animated material");
}

} // namespace

SELFTEST(Early, TestPcGrass, RemoveStandIns);
