// Texture projections on PC: see pc_texture_projection.h. The maths follows the original's x87 code: double precision
// where it keeps values in registers, floats where it stores them.

#include "game/gfx/pc_texture_projection.h"

#include <windows.h>

#include <d3d9.h>

#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/pixel_shaders.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "game/gfx/texture_splats.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kProjectionVtable = 0x0064A764;
constexpr uint32_t kBatchLimit = 10;
constexpr uint32_t kBatchVertices = 0x600;

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
IDirect3DDevice9* DrawDevice() { return GameVariable<IDirect3DDevice9*>(0x00786D0C); } // (the same device, kept twice)
uint32_t& BatchCount() { return GameVariable<uint32_t>(0x00705750); }
sProjectionBatch* Batches() { return &GameVariable<sProjectionBatch>(0x00705758); }
int Rand() { return GameFunction<int(__cdecl*)()>(0x00623BA3)(); } // (the game's C runtime's)

void TransformCoord(float* out, const float* v, const Mth::Matrix* m)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const Mth::Matrix*)>(0x005BF2F5)(out, v, m); // (D3DX)
}

// The vertex stack (0x006FC4A0) the splitter works on.
uint8_t* VertexStack() { return &GameVariable<uint8_t>(0x006FC4A0); }
void PushVertex(const uint8_t* vertex)
{
    GameFunction<void(__thiscall*)(uint8_t*, const uint8_t*)>(0x00490D50)(VertexStack(), vertex);
}

float* Uv(uint8_t* vertex) { return reinterpret_cast<float*>(vertex + 0x10); }
uint32_t& Colour(uint8_t* vertex) { return *reinterpret_cast<uint32_t*>(vertex + 0xC); }
uint32_t Opaque(uint32_t colour) { return (colour & 0xFFFFFF) | 0x80000000; }
float ToUv(float coordinate) { return static_cast<float>((static_cast<double>(coordinate) + 1.0f) * 0.5f); }
float FromUv(float uv)
{
    double value = uv;
    return static_cast<float>(value + value - 1.0f);
}

// A triangle (three vertices of 0x18 bytes) copied: positions, texture coordinates, then the colours made opaque.
void CopyTriangle(uint8_t* into, uint8_t* from)
{
    for (int k = 0; k < 3; ++k)
        std::memcpy(into + 0x18 * k, from + 0x18 * k, 12);
    for (int k = 0; k < 3; ++k)
        std::memcpy(Uv(into + 0x18 * k), Uv(from + 0x18 * k), 8);
    for (int k = 0; k < 3; ++k)
        Colour(into + 0x18 * k) = Opaque(Colour(from + 0x18 * k));
}

} // namespace

// 0x004C7610
sProjectionBatch* sProjectionBatch::Construct()
{
    GameFunction<void*(__thiscall*)(void*)>(0x004D1A90)(material);
    return this;
}

// 0x004C73D0
void sProjectionBatch::Destroy()
{
    reinterpret_cast<sMaterial*>(material)->DestroyBody();
}

// 0x004C73E0
sProjectionBatch* __cdecl GetProjectionBatch(uint32_t texture, uint32_t blendName)
{
    uint32_t count = BatchCount();
    for (uint32_t i = 0; i < count; ++i) {
        if (Batches()[i].texture == texture)
            return &Batches()[i];
    }
    if (count >= kBatchLimit) {
        Dbg::Printf("hmmm\n");
        return nullptr;
    }
    sProjectionBatch* batch = &Batches()[count];
    uint8_t* material = batch->material;
    *reinterpret_cast<uint32_t*>(material + 0x20) = 0x44; // (the pass's flags)
    int a = Rand();
    int b = Rand();
    *reinterpret_cast<int*>(material) = a * b; // a random checksum
    *reinterpret_cast<uint32_t*>(material + 0x08) = 1; // one pass
    *reinterpret_cast<uint32_t*>(material + 0x30) = texture;
    material[0x0D] = 1;
    material[0x11] = 4;
    *reinterpret_cast<uint32_t*>(material + 0x80) = BlendModeFromName(blendName);
    for (int k = 0; k < 3; ++k)
        reinterpret_cast<float*>(material + 0x40)[k] = 128.0f; // its colour
    *reinterpret_cast<uint32_t*>(material + 0x90) = 0x20002;
    *reinterpret_cast<uint32_t*>(material + 0xE0) = 0;
    material[0x10] = 1;
    MaterialPixelShader(material, &batch->pixelShader);
    batch->texture = texture;
    batch->triangleCount = 0;
    ++BatchCount();
    return batch;
}

// 0x004C7520
void __cdecl DrawProjectionBatches()
{
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    Device()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
    DWORD minFilter;
    Device()->GetSamplerState(0, D3DSAMP_MINFILTER, &minFilter);
    Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    for (uint32_t i = 0; i < BatchCount(); ++i) {
        sProjectionBatch* batch = &Batches()[i];
        GameFunction<void(__thiscall*)(uint8_t*)>(0x004D1F00)(batch->material); // (the material's set-up)
        SetPixelShader(batch->pixelShader, batch->material);
        DrawDevice()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, batch->triangleCount, batch->vertices, 0x18);
        ++GameVariable<uint32_t>(0x00786EBC); // (the frame's draw calls)
        GameVariable<uint32_t>(0x00786EC0) += batch->triangleCount; // (and triangles)
    }
    Device()->SetSamplerState(0, D3DSAMP_MINFILTER, minFilter);
    BatchCount() = 0;
}

// 0x004C7E80
CPcTextureProjection* CPcTextureProjection::Construct()
{
    GameFunction<void*(__thiscall*)(void*)>(0x0049FE20)(this);
    vtable = reinterpret_cast<void**>(kProjectionVtable);
    return this;
}

// 0x004C7EA0
CPcTextureProjection* CPcTextureProjection::Destroy(unsigned int freeFlags)
{
    GameFunction<void(__thiscall*)(void*)>(0x004A04A0)(this);
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C7620
void CPcTextureProjection::Project(void** sectors, int /*unused*/)
{
    sProjectionBatch* batch = nullptr;
    uint8_t* batchVertices = nullptr;
    for (; *sectors; ++sectors) {
        auto* geom = GameFunction<uint8_t*(__thiscall*)(void*)>(0x004A2BA0)(*sectors); // (the sector's geom)
        if (!geom)
            continue;
        auto** meshes = *reinterpret_cast<sMesh***>(geom + 0x3C);
        for (uint32_t m = 0; m < *reinterpret_cast<uint32_t*>(geom + 0x40); ++m) {
            sMesh* mesh = meshes[m];
            if (mesh->flags & 2)
                continue;
            uint8_t* vertices;
            GameFunction<long(__thiscall*)(void*, uint32_t, uint32_t, uint8_t**, uint32_t)>(0x004D4090)(
                mesh->vertexBuffers[0], 0, 0, &vertices, 0x10);
            uint16_t* indices = nullptr;
            GameFunction<long(__thiscall*)(void*, uint32_t, uint32_t, uint16_t**, uint32_t)>(0x004D42F0)(
                mesh->indexBuffers[0], 0, 0, &indices, 0x10);
            uint32_t a = indices[0], b = indices[1];
            for (uint32_t strip = 2; strip < mesh->indexCounts[0]; ++strip) {
                uint32_t c = indices[strip];
                uint32_t ia = a, ib = b, ic = c;
                a = b;
                b = c;
                if (ia == ib || ia == ic || ib == ic)
                    continue;
                uint32_t stride = mesh->stride;
                float t0[3], t1[3], t2[3];
                TransformCoord(t0, reinterpret_cast<const float*>(vertices + stride * ia), &projection);
                TransformCoord(t1, reinterpret_cast<const float*>(vertices + stride * ib), &projection);
                TransformCoord(t2, reinterpret_cast<const float*>(vertices + stride * ic), &projection);
                if (t0[2] < -1.0f && t1[2] < -1.0f && t2[2] < -1.0f) // (wholly behind)
                    continue;
                if (!TriangleOverlapsScreen(t0[0], t0[1], t1[0], t1[1], t2[0], t2[1]))
                    continue;
                if (!batchVertices) {
                    uint8_t* key = *reinterpret_cast<uint8_t**>(texture + 0x10);
                    if (texture[8] & 1)
                        key = *reinterpret_cast<uint8_t**>(key);
                    batch = GetProjectionBatch(reinterpret_cast<uint32_t>(key), blendName);
                    batchVertices = reinterpret_cast<uint8_t*>(batch) + 0x120; // (null: written near null, as the original)
                }
                // the triangle written at the batch's next slot (the pieces' scratch: the first piece kept goes there)
                uint32_t count = batch->triangleCount;
                uint8_t* v0 = batchVertices + count * 0x48;
                uint8_t* v1 = v0 + 0x18;
                uint8_t* v2 = v0 + 0x30;
                std::memcpy(v0, vertices + stride * ia, 12);
                std::memcpy(v1, vertices + stride * ib, 12);
                std::memcpy(v2, vertices + stride * ic, 12);
                Uv(v0)[0] = ToUv(t0[0]);
                Uv(v0)[1] = ToUv(t0[1]);
                Uv(v1)[0] = ToUv(t1[0]);
                Uv(v1)[1] = ToUv(t1[1]);
                Uv(v2)[0] = ToUv(t2[0]);
                Uv(v2)[1] = ToUv(t2[1]);
                uint32_t colourOffset = mesh->unknown1C[0];
                Colour(v0) = Opaque(*reinterpret_cast<uint32_t*>(vertices + stride * ia + colourOffset));
                Colour(v1) = Opaque(*reinterpret_cast<uint32_t*>(vertices + stride * ib + colourOffset));
                Colour(v2) = Opaque(*reinterpret_cast<uint32_t*>(vertices + stride * ic + colourOffset));
                uint8_t whole[3 * 0x18];
                uint8_t* pieces;
                uint8_t* end;
                uint8_t* format = nullptr;
                if (fineness) {
                    // how fine to split it: the cross product of its edges, squared, over 16 times the fineness
                    const float* p0 = reinterpret_cast<const float*>(v0);
                    const float* p1 = reinterpret_cast<const float*>(v1);
                    const float* p2 = reinterpret_cast<const float*>(v2);
                    double e1z = static_cast<double>(p1[2]) - p0[2], e1y = static_cast<double>(p1[1]) - p0[1];
                    double e1x = static_cast<double>(p1[0]) - p0[0];
                    float e2z = static_cast<float>(static_cast<double>(p2[2]) - p0[2]);
                    float e2y = static_cast<float>(static_cast<double>(p2[1]) - p0[1]);
                    double e2x = static_cast<double>(p2[0]) - p0[0];
                    float cz = static_cast<float>(e2y * e1x - e2x * e1y);
                    float cy = static_cast<float>(e2x * e1z - e2z * e1x);
                    double cx = e2z * e1y - e2y * e1z;
                    double total = static_cast<double>(cy) * cy + static_cast<double>(cz) * cz + cx * cx;
                    GameVariable<float>(0x0067DDF0) = static_cast<float>(total / (static_cast<double>(fineness) * 16.0f));
                    GameFunction<void(__thiscall*)(void*)>(0x00490D20)(VertexStack()); // (emptied)
                    GameVariable<uint32_t>(0x006FC4A4) = 0x18;
                    PushVertex(v0);
                    PushVertex(v1);
                    PushVertex(v2);
                    pieces = static_cast<uint8_t*>(Mem::New(0x2000));
                    std::memset(pieces, 0, 0x2000);
                    void* memory = Mem::New(0xD8);
                    format = memory ? GameFunction<uint8_t*(__thiscall*)(void*)>(0x004D4BE0)(memory) : nullptr;
                    format[0x1C] = 0x0C; // (no check: written through null, as the original)
                    format[0x1E] = 0x10;
                    end = pieces;
                    while (SplitNextTriangle(&end, reinterpret_cast<const sSplitFormat*>(format)))
                        ;
                } else {
                    std::memcpy(whole, v0, sizeof(whole));
                    pieces = whole;
                    end = whole + sizeof(whole);
                }
                // the pieces inside the projection's square, each into the batch's next triangle
                bool full = false;
                if (!fineness || pieces != end) {
                    for (uint8_t* piece = pieces; piece != end; piece += 0x48) {
                        if (!TriangleOverlapsScreen(FromUv(Uv(piece)[0]), FromUv(Uv(piece)[1]),
                                                    FromUv(Uv(piece + 0x18)[0]), FromUv(Uv(piece + 0x18)[1]),
                                                    FromUv(Uv(piece + 0x30)[0]), FromUv(Uv(piece + 0x30)[1])))
                            continue;
                        CopyTriangle(batchVertices + count * 0x48, piece);
                        ++count;
                        if (3 * count + 2 >= kBatchVertices) {
                            full = true;
                            break;
                        }
                    }
                }
                batch->triangleCount = count;
                if (!full)
                    batch->blendName = blendName;
                if (fineness) {
                    if (format) {
                        GameFunction<void(__thiscall*)(void*)>(0x004D69E0)(format); // (its destructor body)
                        Mem::Manager::Get()->Free(format);
                    }
                    Mem::Manager::Get()->Free(pieces);
                }
                if (full)
                    return; // (the buffers left locked, as the original)
            }
            GameFunction<void(__thiscall*)(void*)>(0x004D4300)(mesh->indexBuffers[0]); // (unlocked)
            GameFunction<void(__thiscall*)(void*)>(0x004D40E0)(mesh->vertexBuffers[0]);
        }
    }
}

} // namespace Nx

REPLACE(0x004C7610, &Nx::sProjectionBatch::Construct);
REPLACE(0x004C73D0, &Nx::sProjectionBatch::Destroy);
REPLACE(0x004C73E0, Nx::GetProjectionBatch);
REPLACE(0x004C7520, Nx::DrawProjectionBatches);
REPLACE(0x004C7E80, &Nx::CPcTextureProjection::Construct);
REPLACE(0x004C7EA0, &Nx::CPcTextureProjection::Destroy);
REPLACE(0x004C7620, &Nx::CPcTextureProjection::Project);
