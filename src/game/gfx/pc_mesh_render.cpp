// The PC meshes' drawing (mesh.h): skinned on the CPU (one, two or three bones a vertex, into the vertex buffer) and
// drawn - its material set up for the pixel shaders or pass by pass through the fixed-function stages, its level of
// detail picked by distance, fog from three quarters of the way to the far plane.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>

#include "game/gfx/device.h"
#include "game/gfx/geom.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/pc_fog.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/gfx/settings.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }
bool PixelShaders() { return g_shadersAvailable; }

// D3DX (the statically linked library's, through its thunks).
void TransformCoord(float* out, const float* v, const float* m)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const float*)>(0x005BF2F5)(out, v, m);
}
void TransformNormal(float* out, const float* v, const float* m)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const float*)>(0x005BF31F)(out, v, m);
}
void MatrixMultiply(void* out, const void* a, const void* b)
{
    GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C)(out, a, b);
}

// A vertex's skinning data (0x4C bytes): its position, normal, colour, texture coordinates, bones and weights.
struct sSkinVertex {
    float position[3];
    float normal[3];
    uint32_t color;    // +0x18
    float uv[8];       // +0x1C: (u, v) a set
    uint8_t bones[4];  // +0x3C
    float weights[3];  // +0x40
};
static_assert(sizeof(sSkinVertex) == 0x4C, "layout must match the original");

const float* BoneMatrix(uint8_t bone) { return reinterpret_cast<float*&>(g_boneMatrices) + 16 * bone; }

// Another bone's share added: its transformed position and normal times the weight, onto what's there (each sum
// rounded to a float as the original stores it).
void AddBone(float* blended, const sSkinVertex& v, uint8_t bone, float weight)
{
    float other[3];
    TransformCoord(other, v.position, BoneMatrix(bone));
    for (int k = 0; k < 3; ++k)
        blended[k] = static_cast<float>(static_cast<double>(other[k]) * weight + blended[k]);
    TransformNormal(other, v.normal, BoneMatrix(bone));
    for (int k = 0; k < 3; ++k)
        blended[3 + k] = static_cast<float>(static_cast<double>(other[k]) * weight + blended[3 + k]);
}

// The vertices skinned with up to `bones` bones each: the first bone's transform, then - unless its weight is
// exactly 1 - scaled by it and the others' added (the third's only when its weight isn't 0).
void Skin(sMesh* mesh, uint8_t* out, int bones)
{
    const auto* v = reinterpret_cast<const sSkinVertex*>(mesh->perVertexData);
    for (uint32_t i = 0; i < mesh->vertexCount; ++i, ++v) {
        float blended[6];
        TransformCoord(blended, v->position, BoneMatrix(v->bones[0]));
        TransformNormal(blended + 3, v->normal, BoneMatrix(v->bones[0]));
        if (bones > 1 && !(v->weights[0] == 1.0f)) {
            for (float& value : blended)
                value = static_cast<float>(static_cast<double>(value) * v->weights[0]);
            AddBone(blended, *v, v->bones[1], v->weights[1]);
            if (bones > 2 && !(v->weights[2] == 0.0f))
                AddBone(blended, *v, v->bones[2], v->weights[2]);
        }
        auto* vertex = reinterpret_cast<uint32_t*>(out);
        std::memcpy(vertex, blended, sizeof(blended));
        vertex[6] = v->color;
        for (uint32_t k = 0; k < 2 * mesh->unknownA4; ++k)
            reinterpret_cast<float*>(vertex)[7 + k] = v->uv[k];
        out += mesh->stride;
    }
}

// The index buffers and their counts by level (a level past the eighth reads on into the fields after them, as the
// original does).
sIndexBuffer* IndexBufferAt(sMesh* mesh, int level)
{
    return *reinterpret_cast<sIndexBuffer**>(reinterpret_cast<uint8_t*>(mesh) + 0x54 + 4 * level);
}
uint16_t IndexCountAt(sMesh* mesh, int level)
{
    return *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x24 + 2 * level);
}

void CountDraw(uint32_t primitives)
{
    ++reinterpret_cast<uint32_t&>(g_drawCalls);
    reinterpret_cast<uint32_t&>(g_drawnTriangles) += primitives;
}

} // namespace

// 0x004D4D70 / 0x004D4E50 / 0x004D5020
void sMesh::SkinOneBone(uint8_t* out) { Skin(this, out, 1); }
void sMesh::SkinTwoBones(uint8_t* out) { Skin(this, out, 2); }
void sMesh::SkinThreeBones(uint8_t* out) { Skin(this, out, 3); }

// 0x004D5290
void sMesh::Render()
{
    if (g_skipRender || BeyondClipDistance(this))
        return;
    if (g_distanceClipping && g_fogSetting) { // (distance fog)
        float end = g_camera.farZ;
        reinterpret_cast<uint32_t&>(g_fogStart) = 0;
        reinterpret_cast<uint32_t&>(g_fogEnd) = 0;
        float start = static_cast<float>(end - (static_cast<double>(end) - g_camera.nearZ) * 0.25f);
        SetFogEnabled(true);
        Device()->SetRenderState(D3DRS_FOGSTART, *reinterpret_cast<DWORD*>(&start));
        Device()->SetRenderState(D3DRS_FOGEND, *reinterpret_cast<DWORD*>(&end));
        Device()->SetRenderState(D3DRS_FOGCOLOR, g_fogColour);
    }
    if (!indexCounts[0])
        return;
    D3DMATRIX world;
    if (unknown98 != -1) { // (one bone: its matrix before the world's)
        Device()->GetTransform(D3DTS_WORLD, &world);
        D3DMATRIX boned;
        MatrixMultiply(&boned, BoneMatrix(static_cast<uint8_t>(0)) + 16 * unknown98, &world);
        Device()->SetTransform(D3DTS_WORLD, &boned);
    }
    ApplyVertexColors();
    SetVertexFormat(fvf);
    auto* mat = reinterpret_cast<sMaterial*>(material);
    if (PixelShaders()) {
        mat->Submit();
        if (!g_instanceColourOn && !(flags & 4))
            SetPixelShaderWithConstants(pixelShader, mat->passCount);
        else
            SetPixelShaderColored();
    } else {
        mat->SubmitStates();
    }
    DWORD minFilter = 0, zWrite = 0;
    if (flags & 0x80) {
        Device()->GetSamplerState(0, D3DSAMP_MINFILTER, &minFilter);
        if (minFilter != D3DTEXF_LINEAR)
            Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    }
    if (flags & 0x100) {
        Device()->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
        if (zWrite == 1)
            Nx::SetRenderState(D3DRS_ZWRITEENABLE, 0);
    }
    sVertexBuffer* vb = vertexBuffers[currentVertexBuffer];
    if (perVertexData && (unknown9C != g_framesRendered ||
                          unknownA0 != reinterpret_cast<uint32_t&>(g_currentInstance))) { // (once a frame per instance)
        unknown9C = g_framesRendered;
        unknownA0 = reinterpret_cast<uint32_t&>(g_currentInstance);
        uint8_t* out = vb->data; // (locked whole with DISCARD, inline: the card's buffer written)
        vb->lockSize = vb->size;
        vb->lockOffset = 0;
        vb->lockFlags = D3DLOCK_DISCARD;
        static_cast<IDirect3DVertexBuffer9*>(vb->buffer)->Lock(0, 0, reinterpret_cast<void**>(&out), D3DLOCK_DISCARD);
        switch (unknownA8) {
        case 1: SkinOneBone(out); break;
        case 2: SkinTwoBones(out); break;
        case 3: SkinThreeBones(out); break;
        }
        vb->Unlock();
    }
    if (vertexBufferCount > 1)
        currentVertexBuffer = static_cast<uint8_t>((currentVertexBuffer + 1) % vertexBufferCount);
    Device()->SetStreamSource(0, static_cast<IDirect3DVertexBuffer9*>(vb->buffer), vb->arenaOffset, stride);
    int level = 0;
    if (unknown48) { // (the levels of detail's distances)
        SphereInView(reinterpret_cast<const Mth::Vector*>(centre), radius);
        float distance = LastSphereDistance();
        for (; level < 8; ++level) {
            if (!indexBuffers[level]) {
                if (level > 0)
                    --level;
                break;
            }
            if (distance < static_cast<const float*>(unknown48)[level])
                break;
        }
    }
    Device()->SetIndices(static_cast<IDirect3DIndexBuffer9*>(IndexBufferAt(this, level)->buffer));
    uint32_t primitives = IndexCountAt(this, level) - 2u;
    if (PixelShaders()) {
        CountDraw(primitives);
        Device()->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primitiveType), 0, 0, vertexCount, 0, primitives);
    } else {
        for (uint32_t pass = 0; pass < mat->passCount; ++pass) {
            float instance[3];
            const float* tint = nullptr;
            if (g_instanceColourOn) {
                for (int k = 0; k < 3; ++k)
                    instance[k] = static_cast<float>(g_instanceColour[k] *
                                                     static_cast<double>(1.0f / 255));
                tint = instance;
            } else if (flags & 4) {
                tint = color;
            }
            mat->SubmitPass(pass, tint);
            Device()->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primitiveType), 0, 0, vertexCount, 0,
                                           IndexCountAt(this, level) - 2u);
            CountDraw(IndexCountAt(this, level) - 2u);
        }
    }
    if ((flags & 0x80) && minFilter != D3DTEXF_LINEAR)
        Device()->SetSamplerState(0, D3DSAMP_MINFILTER, minFilter);
    if ((flags & 0x100) && zWrite == 1)
        Device()->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    if (unknown98 != -1)
        Device()->SetTransform(D3DTS_WORLD, &world);
    SetFogEnabled(false);
}

} // namespace Nx

REPLACE(0x004D4D70, &Nx::sMesh::SkinOneBone);
REPLACE(0x004D4E50, &Nx::sMesh::SkinTwoBones);
REPLACE(0x004D5020, &Nx::sMesh::SkinThreeBones);
REPLACE(0x004D5290, &Nx::sMesh::Render);
