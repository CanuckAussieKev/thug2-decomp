// Self-tests for the PC meshes' drawing (src/game/gfx/pc_mesh_render.cpp, RFX-28): the original and ours over random
// meshes - skinned with one to three bones (weights of exactly 1 and 0 too), with a bone matrix of their own or not,
// levels of detail, mip / z-write flags, shaders or the fixed-function passes, fog on and off - a recording stand-in
// device and buffers; the material, the states, the culling and D3DX stood in.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/device.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/pc_fog.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::sMesh;
using Nx::sVertexBuffer;

constexpr uint32_t kDevice = 0xD3D00000;

uint8_t* g_card;
float g_distance;
uint32_t g_beyond;
uint32_t g_samplerState, g_zWrite;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

std::vector<uint8_t> LoadFloat(const float* value)
{
    std::vector<uint8_t> bytes = {0xD9, 0x05, 0, 0, 0, 0, 0xC3};
    uint32_t address = reinterpret_cast<uint32_t>(value);
    std::memcpy(&bytes[2], &address, 4);
    return bytes;
}

// D3DXVec3TransformCoord / TransformNormal (a stand-in: rows times the matrix, w divided out for the coordinate).
void Transform(Regs& r, bool coord)
{
    const auto* v = reinterpret_cast<const float*>(r.args[1]);
    const auto* m = reinterpret_cast<const float*>(r.args[2]);
    double out[4];
    for (int j = 0; j < 4; ++j)
        out[j] = static_cast<double>(v[0]) * m[j] + static_cast<double>(v[1]) * m[4 + j] +
                 static_cast<double>(v[2]) * m[8 + j] + (coord ? m[12 + j] : 0.0);
    auto* o = reinterpret_cast<float*>(r.args[0]);
    for (int j = 0; j < 3; ++j)
        o[j] = static_cast<float>(coord && out[3] != 0 ? out[j] / out[3] : out[j]);
}

void TestPcMeshRender()
{
    GameMemory memory({{0x007D1EC0, 1}, {0x007D643A, 1}, {0x007D6436, 1}, {0x00786CA8, 8}, {0x00787340, 8},
                       {0x0078733C, 4}, {0x007D69E0, 4}, {0x00786AB8, 1}, {0x00786F1C, 8}, {0x00786AC4, 4},
                       {0x007825B0, 0xC}, {0x00786EBC, 8}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* vertexBuffer = com.Buffer(0x7B000000);
    FakeCom* indexBuffer = com.Buffer(0x1B000000);
    com.On(kDevice, 44, [](Regs& r) { // SetTransform
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[2]), 0x40)});
        return 0u;
    });
    com.On(kDevice, 45, [](Regs& r) { // GetTransform
        auto* m = reinterpret_cast<float*>(r.args[2]);
        for (int i = 0; i < 16; ++i)
            m[i] = static_cast<float>(i % 5) * 0.5f - 1.0f;
        return 0u;
    });
    com.On(kDevice, 68, [](Regs& r) { // GetSamplerState
        *reinterpret_cast<uint32_t*>(r.args[3]) = g_samplerState;
        return 0u;
    });
    com.On(kDevice, 58, [](Regs& r) { // GetRenderState
        *reinterpret_cast<uint32_t*>(r.args[2]) = g_zWrite;
        return 0u;
    });
    com.On(0x7B000000, 11, [](Regs& r) { // Lock: the card's buffer
        *reinterpret_cast<uint8_t**>(r.args[3]) = g_card;
        return 0u;
    });
    com.On(0x7B000000, 12, [](Regs&) {
        Record({HashBytes(g_card, 0x800)});
        return 0u;
    });
    s.At({0x005BF2F5}, 12, [](Regs& r) {
        Transform(r, true);
        return r.args[0];
    });
    s.At({0x005BF31F}, 12, [](Regs& r) {
        Transform(r, false);
        return r.args[0];
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) { // D3DXMatrixMultiply
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
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
    s.At({0x004DB6D0, AddressOf(&Nx::BeyondClipDistance)}, 0, [](Regs& r) {
        Record({0xB6D0, Relocate(r.args[0])});
        return g_beyond;
    });
    s.At({0x004DB790, AddressOf(&Nx::SphereInView)}, 0, [](Regs& r) {
        Record({0xB790, Relocate(r.args[0]), r.args[1]});
        return 1u;
    });
    s.Bytes(0x004DA0E0, LoadFloat(&g_distance));
    s.Bytes(AddressOf(&Nx::LastSphereDistance), LoadFloat(&g_distance));
    s.At({0x00490DE0, AddressOf(&Nx::SetFogEnabled)}, 0, [](Regs& r) {
        Record({0x0DE0, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x004D9DB0, AddressOf(&Nx::SetVertexFormat)}, 0, [](Regs& r) {
        Record({0x9DB0, r.args[0]});
        return 0u;
    });
    s.At({0x004DAAF0, AddressOf(&Nx::SetPixelShaderWithConstants)}, 0, [](Regs& r) {
        Record({0xAAF0, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0044F870, AddressOf(&Nx::SetRenderState)}, 0, [](Regs& r) {
        Record({0xF870, r.args[0], r.args[1]});
        return 0u;
    });
    StandInThis(s, {0x004D1F00, AddressOf(&Nx::sMaterial::Submit)}, 0);
    StandInThis(s, {0x004D23E0, AddressOf(&Nx::sMaterial::SubmitStates)}, 0);
    s.At({0x004D24D0, AddressOf(&Nx::sMaterial::SubmitPass)}, 8, [](Regs& r) {
        Record({0x24D0, Relocate(r.ecx), r.args[0],
                r.args[1] ? HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), 12) : 0u});
        return 0u;
    });
    StandInThis(s, {0x004D3D10, AddressOf(&Nx::sMesh::SetPixelShaderColored)}, 0);
    s.At({0x004D4CA0, AddressOf(&Nx::sMesh::ApplyVertexColors)}, 0, [](Regs& r) {
        Record({0x4CA0, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x004D40E0, AddressOf(&Nx::sVertexBuffer::Unlock)}, 0, [](Regs& r) {
        auto* vb = reinterpret_cast<sVertexBuffer*>(r.ecx);
        Record({0x40E0, Relocate(r.ecx), vb->lockOffset, vb->lockSize, vb->lockFlags, HashBytes(g_card, 0x800)});
        return 0u;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 32; ++round) {
        ok &= CompareTwoRuns("pc mesh render", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5290 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            Lcg random(0x52900 + round);
            std::vector<uint32_t> trace;
            g_card = reinterpret_cast<uint8_t*>(Bump(0x800));
            std::memset(g_card, 0, 0x800);
            g_beyond = random.Below(8) == 0;
            g_distance = RandomFloat(random, 0, 400);
            g_samplerState = random.Below(3);
            g_zWrite = random.Below(2);
            GameVariable<uint8_t>(0x007D1EC0) = random.Below(10) == 0;
            GameVariable<uint8_t>(0x007D643A) = static_cast<uint8_t>(random.Below(2));
            GameVariable<uint8_t>(0x007D6436) = static_cast<uint8_t>(random.Below(2));
            GameVariable<float>(0x00786CA8) = RandomFloat(random, 0, 500);
            GameVariable<float>(0x00786CAC) = RandomFloat(random, 500, 3000);
            GameVariable<uint8_t>(0x00786AB8) = static_cast<uint8_t>(random.Below(2));
            GameVariable<uint32_t>(0x00786F1C) = random.Below(3) == 0;
            GameVariable<int>(0x00786AC4) = static_cast<int>(random.Below(3));
            GameVariable<uint32_t>(0x007825B8) = random.Below(3);
            auto* bones = reinterpret_cast<float*>(Bump(0x40 * 8));
            for (int i = 0; i < 16 * 8; ++i)
                bones[i] = RandomFloat(random, -2, 2);
            GameVariable<float*>(0x007825B0) = bones;
            // the mesh
            auto* mesh = reinterpret_cast<sMesh*>(Bump(sizeof(sMesh)));
            std::memset(mesh, 0, sizeof(sMesh));
            mesh->flags = (random.Below(3) == 0 ? 4u : 0u) | (random.Below(3) == 0 ? 0x80u : 0u) |
                          (random.Below(3) == 0 ? 0x100u : 0u);
            mesh->vertexCount = static_cast<uint16_t>(1 + random.Below(12));
            mesh->unknownA4 = random.Below(3);
            mesh->stride = static_cast<uint8_t>(0x1C + 8 * mesh->unknownA4);
            mesh->fvf = 0x152;
            mesh->primitiveType = random.Below(2) ? 5 : 4;
            mesh->pixelShader = reinterpret_cast<void*>(0x95000000 + round);
            for (float& c : mesh->color)
                c = RandomFloat(random, 0, 1);
            mesh->unknown98 = random.Below(3) == 0 ? static_cast<int>(random.Below(8)) : -1;
            mesh->unknown9C = static_cast<int>(random.Below(3));
            mesh->unknownA0 = random.Below(3);
            mesh->unknownA8 = random.Below(5);
            auto* material = reinterpret_cast<Nx::sMaterial*>(Bump(sizeof(Nx::sMaterial)));
            std::memset(material, 0, sizeof(Nx::sMaterial));
            material->passCount = random.Below(4);
            mesh->material = reinterpret_cast<uint8_t*>(material);
            if (random.Below(3)) {
                mesh->perVertexData = reinterpret_cast<uint8_t*>(Bump(0x4C * mesh->vertexCount));
                for (uint32_t i = 0; i < mesh->vertexCount; ++i) {
                    auto* v = reinterpret_cast<float*>(mesh->perVertexData + 0x4C * i);
                    for (int k = 0; k < 15; ++k)
                        v[k] = RandomFloat(random, -3, 3);
                    auto* bytes = reinterpret_cast<uint8_t*>(v + 15);
                    for (int k = 0; k < 4; ++k)
                        bytes[k] = static_cast<uint8_t>(random.Below(8));
                    for (int k = 0; k < 3; ++k)
                        v[16 + k] = random.Below(4) == 0 ? (k == 0 ? 1.0f : 0.0f) : RandomFloat(random, 0, 1);
                }
            }
            mesh->vertexBufferCount = static_cast<uint8_t>(1 + random.Below(3));
            mesh->currentVertexBuffer = static_cast<uint8_t>(random.Below(mesh->vertexBufferCount));
            for (int i = 0; i < 3; ++i) {
                auto* vb = reinterpret_cast<sVertexBuffer*>(Bump(sizeof(sVertexBuffer)));
                std::memset(vb, 0, sizeof(sVertexBuffer));
                vb->size = 0x400;
                vb->data = reinterpret_cast<uint8_t*>(Bump(0x400));
                vb->buffer = vertexBuffer;
                vb->arenaOffset = 0x20 * random.Below(4);
                mesh->vertexBuffers[i] = vb;
            }
            int levels = static_cast<int>(random.Below(9));
            for (int i = 0; i < 8; ++i) {
                if (i < levels || i == 0) {
                    auto* ib = reinterpret_cast<Nx::sIndexBuffer*>(Bump(sizeof(Nx::sIndexBuffer)));
                    std::memset(ib, 0, sizeof(Nx::sIndexBuffer));
                    ib->buffer = indexBuffer;
                    mesh->indexBuffers[i] = ib;
                }
                mesh->indexCounts[i] = static_cast<uint16_t>(random.Below(6) == 0 && i == 0 ? 0 : 3 + random.Below(40));
            }
            if (levels == 8 && random.Below(2)) { // (past the last level: the fields after the index buffers read)
                g_distance = 1e9f;
                mesh->bufferUsage = reinterpret_cast<uint32_t>(mesh->indexBuffers[0]);
            }
            if (random.Below(2)) {
                auto* distances = reinterpret_cast<float*>(Bump(32));
                for (int i = 0; i < 8; ++i)
                    distances[i] = 50.0f * static_cast<float>(i + 1);
                mesh->unknown48 = distances;
            }
            if (round % 4 == 1)
                mesh->unknown98 = 0; // (bone 0)
            if (round % 5 == 2) {
                mesh->flags |= 0x80;
                g_samplerState = D3DTEXF_LINEAR;
            }
            if (round % 6 == 3)
                g_distance = 100.0f; // (exactly a level's distance)
            for (int pass = 0; pass < 2; ++pass)
                Fn<void(__thiscall*)(void*)>(original, 0x004D5290)(mesh);
            trace.push_back(mesh->currentVertexBuffer);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc mesh render: skinned, levels of detail, shaders / passes, fog");
}

} // namespace

SELFTEST(Early, TestPcMeshRender, RemoveStandIns);
