// Shadows: see shadow.h.

#include "game/gfx/shadow.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/device.h"
#include "game/gfx/geom.h"
#include "game/gfx/mesh.h"
#include "game/gfx/model.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/render_state.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
Lst::HashTable* Casters() { return GameVariable<Lst::HashTable*>(0x007CA970); }
float* ViewMatrix() { return &GameVariable<float>(0x00786BD0); }
float* ProjectionMatrix() { return &GameVariable<float>(0x00786C10); }
const float* WorldMatrix() { return &GameVariable<float>(0x00786B90); }
uint8_t& CustomTransforms() { return GameVariable<uint8_t>(0x00786CBC); } // the view / projection aren't the camera's
bool SkipRender() { return GameVariable<bool>(0x007D1EC0); }
uint32_t& Fog() { return GameVariable<uint32_t>(0x00787338); }
uint32_t& DepthBias() { return GameVariable<uint32_t>(0x00786E54); }
uint32_t& CullMode() { return GameVariable<uint32_t>(0x00786E5C); }
uint32_t& PixelShaderLocked() { return GameVariable<uint32_t>(0x00786EC4); }
bool TwoSidedStencil() { return GameVariable<bool>(0x007CA97C); }

// The projection buffer (ProjectShadowVertices) and the triangle buffer (ClipShadowTriangles).
struct ProjectedVertex {
    float position[3];
    float projected[3];
    uint32_t clip; // a bit per side it's outside of
};
static_assert(sizeof(ProjectedVertex) == 0x1C, "the buffer's stride");
ProjectedVertex* Projected() { return &GameVariable<ProjectedVertex>(0x0079B640); }
struct ShadowVertex {
    float position[3];
    float u, v;
};
static_assert(sizeof(ShadowVertex) == 0x14, "the draw's stride");
ShadowVertex* ShadowTriangles() { return &GameVariable<ShadowVertex>(0x00796640); }
constexpr float kFacingLimit = std::bit_cast<float>(0xB8D1B717u); // 0x0064AAA0, -1e-4

ShadowCompanionFn g_shadowCompanion = nullptr; // NOT ORIGINAL (SetShadowCompanion)

// D3DX (linked library code; stdcall).
void MatrixMultiply(float* out, const float* a, const float* b)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const float*)>(0x005BFA8C)(out, a, b);
}
void MatrixInverse(float* out, float* determinant, const float* m)
{
    GameFunction<void*(__stdcall*)(float*, float*, const float*)>(0x005BFF98)(out, determinant, m);
}
void TransformCoord(float* out, const float* v, const void* m)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const void*)>(0x005BF2F5)(out, v, m);
}
void MatrixLookAt(float* out, const float* eye, const float* at, const float* up)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const float*, const float*)>(0x005C0985)(out, eye, at, up);
}
void MatrixOrtho(float* out, float width, float height, float nearZ, float farZ)
{
    GameFunction<void*(__stdcall*)(float*, float, float, float, float)>(0x005C0B39)(out, width, height, nearZ, farZ);
}

// The models' meshes (not rewritten yet; thiscall).
int MeshCount(void* model) { return static_cast<CModel*>(model)->GetNumGeoms(); }
void* Mesh(void* model, int index) { return static_cast<CModel*>(model)->GetGeom(index); }
void DrawMesh(void* mesh, int flags) { RenderInstance(static_cast<sInstance*>(mesh), static_cast<uint32_t>(flags)); }
// A scene mesh's draw (thiscall).
void DrawSceneMesh(void* mesh) { static_cast<sMesh*>(mesh)->Render(); }

void Identity(float* m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// The caster with this key (the table's own inlined lookup: the first item with the key and a value).
ShadowCaster* FindCaster(uint32_t key)
{
    Lst::HashTable* table = Casters();
    Lst::HashTable::Item* item = table->m_buckets + (key & ((1u << table->m_bits) - 1));
    for (; item; item = item->next) {
        if (item->key == key && item->value)
            return static_cast<ShadowCaster*>(item->value);
    }
    return nullptr;
}

// Iterate the casters: the first (or next) one, or null at the end (an empty slot's null ends it too).
ShadowCaster* NextCaster()
{
    Lst::HashTable* table = Casters();
    if (!table->IterateNext() || !table->m_iterItem)
        return nullptr;
    return static_cast<ShadowCaster*>(table->m_iterItem->value);
}
ShadowCaster* FirstCaster()
{
    Lst::HashTable* table = Casters();
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    return NextCaster();
}

} // namespace

// 0x004DA0F0
void __cdecl ProjectShadowVertices(const float* positions, uint32_t count, const void* matrix)
{
    ProjectedVertex* vertex = Projected();
    for (uint32_t i = 0; i < count; ++i, ++vertex, positions += 3) {
        TransformCoord(vertex->projected, positions, matrix);
        // The clip flags from the float bits: a bit for each of x < 0, x > 1, y < 0, y > 1, z (on the sign and
        // the exponent's top bit after adding 1 to the exponent).
        uint32_t x = std::bit_cast<uint32_t>(vertex->projected[0]) + 0x800000;
        uint32_t y = std::bit_cast<uint32_t>(vertex->projected[1]) + 0x800000;
        uint32_t z = std::bit_cast<uint32_t>(vertex->projected[2]) + 0x800000;
        uint32_t bits = (((y & 0xC0000003) | ((x >> 2) & 0x30000000)) >> 2) | (z & 0xC0000000);
        vertex->position[0] = positions[0];
        vertex->position[1] = positions[1];
        vertex->position[2] = positions[2];
        vertex->clip = ~((bits >> 1) & 0x54000000) & bits;
    }
}

// 0x004DA190
int __cdecl ClipShadowTriangles(const uint16_t* indices, uint32_t count)
{
    ShadowVertex* out = ShadowTriangles();
    for (uint32_t i = 0; i < count; ++i, indices += 3) {
        const ProjectedVertex& v0 = Projected()[indices[0]];
        const ProjectedVertex& v1 = Projected()[indices[1]];
        const ProjectedVertex& v2 = Projected()[indices[2]];
        if (v0.clip & v1.clip & v2.clip)
            continue; // all outside the same side
        double facing = (static_cast<double>(v2.projected[1]) - v1.projected[1]) *
                            (static_cast<double>(v1.projected[0]) - v0.projected[0]) -
                        (static_cast<double>(v2.projected[0]) - v1.projected[0]) *
                            (static_cast<double>(v1.projected[1]) - v0.projected[1]);
        if (facing > kFacingLimit)
            continue;
        for (const ProjectedVertex* v : {&v0, &v1, &v2}) {
            std::memcpy(out->position, v->position, sizeof(out->position));
            out->u = v->projected[0];
            out->v = v->projected[1];
            ++out;
        }
    }
    return static_cast<int>(out - ShadowTriangles()) / 3;
}

// 0x004DB880
void __cdecl ShadowTextureMatrix(const float* a, const float* b, float* out, const float* first)
{
    float view[16];
    Device()->GetTransform(D3DTS_VIEW, reinterpret_cast<D3DMATRIX*>(view));
    float inverse[16];
    MatrixInverse(inverse, nullptr, view); // (not used)
    // The shadow viewport (0x0067EF4C: x, y, width, height) as a scale and offset of the -1..1 range; depth 0..2^24.
    float scaleBias[16] = {};
    scaleBias[0] = static_cast<float>(static_cast<double>(GameVariable<float>(0x0067EF58)) * 0.5);
    scaleBias[5] = static_cast<float>(static_cast<double>(GameVariable<float>(0x0067EF54)) * -0.5);
    scaleBias[10] = std::bit_cast<float>(0x4B7FFFFFu); // 16777215
    scaleBias[12] = static_cast<float>((static_cast<double>(GameVariable<float>(0x0067EF50)) + 1.0) * 0.5);
    scaleBias[13] = static_cast<float>((static_cast<double>(GameVariable<float>(0x0067EF4C)) + 1.0) * 0.5);
    scaleBias[15] = 1.0f;
    float m[16];
    if (first) {
        std::memcpy(m, first, sizeof(m));
        MatrixMultiply(m, m, a);
    } else {
        std::memcpy(m, a, sizeof(m));
    }
    MatrixMultiply(m, m, b);
    MatrixMultiply(out, m, scaleBias);
}

// 0x004DB9D0
void __cdecl DrawShadowVolumes(const void* scene, int /*viewport*/)
{
    void* vertexColour = GameVariable<void*>(0x007CA94C);
    SetPixelShader(vertexColour, nullptr);
    PixelShaderLocked() = reinterpret_cast<uint32_t>(vertexColour);
    SetTexture(0, nullptr);
    SetState(kZWrite, 0);
    SetState(kZTest, 1);
    SetState(kAlphaRef, 0);
    if (!TwoSidedStencil())
        Device()->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
    const auto* bytes = static_cast<const uint8_t*>(scene);
    void* const* meshes = *reinterpret_cast<void* const* const*>(bytes + 0x1C);
    for (int i = 0; i < *reinterpret_cast<const int*>(bytes + 0x20); ++i) {
        auto* mesh = static_cast<uint8_t*>(meshes[i]);
        uint32_t flags = *reinterpret_cast<uint32_t*>(mesh);
        if (!(flags & 0x40) || !(flags & 0x200))
            continue;
        if (!SphereInView(reinterpret_cast<const Mth::Vector*>(mesh + 4), *reinterpret_cast<float*>(mesh + 0x10)))
            continue;
        if (!TwoSidedStencil()) {
            // One side's faces, then the other's.
            SetState(kCullMode, D3DCULL_CW);
            SetBlendMode(0x10000002);
            DrawSceneMesh(mesh);
            SetState(kCullMode, D3DCULL_CCW);
            SetBlendMode(0x10000004);
        } else {
            SetState(kCullMode, D3DCULL_NONE);
            SetBlendMode(0x30000006);
        }
        DrawSceneMesh(mesh);
    }
    PixelShaderLocked() = 0;
    SetState(kZWrite, 1);
    SetState(kCullMode, D3DCULL_CW);
    Device()->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
}

// 0x004DBBD0
void __cdecl RenderShadowTextures()
{
    if (SkipRender())
        return;
    float savedView[16], savedProjection[16];
    std::memcpy(savedView, ViewMatrix(), sizeof(savedView));
    std::memcpy(savedProjection, ProjectionMatrix(), sizeof(savedProjection));
    IDirect3DSurface9* renderTarget = nullptr;
    IDirect3DSurface9* depth = nullptr;
    uint32_t savedFog = Fog();
    Device()->GetRenderTarget(0, &renderTarget);
    Device()->GetDepthStencilSurface(&depth);
    if (Fog()) {
        Fog() = 0;
        Nx::SetRenderState(D3DRS_FOGENABLE, FALSE);
    }
    for (ShadowCaster* caster = FirstCaster(); caster; caster = NextCaster()) {
        if (!caster->model)
            continue;
        Device()->SetRenderTarget(0, *reinterpret_cast<IDirect3DSurface9**>(caster->target + 0x18));
        Device()->SetDepthStencilSurface(nullptr);
        Device()->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        std::memcpy(ViewMatrix(), caster->view, sizeof(caster->view));
        std::memcpy(ProjectionMatrix(), caster->projection, sizeof(caster->projection));
        CustomTransforms() = 1;
        Device()->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(caster->view));
        Device()->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(caster->projection));
        float identity[16];
        Identity(identity);
        Device()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(identity));
        auto drawModel = [](void* model) {
            int count = MeshCount(model);
            for (int i = 0; i < count; ++i) {
                auto* mesh = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(Mesh(model, i)) + 0x4C);
                if (!mesh[0x4C])
                    continue;
                uint32_t* flags = *reinterpret_cast<uint32_t**>(mesh + 0x58);
                *flags |= 0x80; // (drawn as a shadow)
                DrawMesh(mesh, 8);
                flags = *reinterpret_cast<uint32_t**>(mesh + 0x58);
                *flags &= ~0x80u;
                flags = *reinterpret_cast<uint32_t**>(mesh + 0x58);
                *flags |= 0x200;
            }
        };
        drawModel(caster->model);
        // NOT ORIGINAL (ours: SetShadowCompanion) - a model drawn into this caster's shadow as well.
        if (g_shadowCompanion) {
            if (void* companion = g_shadowCompanion(caster->model))
                drawModel(companion);
        }
    }
    Device()->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    if (savedFog != Fog()) {
        Fog() = savedFog;
        Device()->SetRenderState(D3DRS_FOGENABLE, savedFog != 0);
    }
    PixelShaderLocked() = 0;
    std::memcpy(ViewMatrix(), savedView, sizeof(savedView));
    std::memcpy(ProjectionMatrix(), savedProjection, sizeof(savedProjection));
    CustomTransforms() = 0;
    Device()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(WorldMatrix()));
    Device()->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(ViewMatrix()));
    Device()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    Device()->SetRenderTarget(0, renderTarget);
    Device()->SetDepthStencilSurface(depth);
    if (renderTarget)
        renderTarget->Release();
    if (depth)
        depth->Release();
}

// 0x004DBF50
void __cdecl DrawShadows(const void* /*unused*/, void* const* meshes, int count)
{
    if (SkipRender())
        return;
    DWORD savedMinFilter;
    Device()->GetSamplerState(0, D3DSAMP_MINFILTER, &savedMinFilter);
    Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    Device()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    Device()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    uint32_t savedFog = Fog();
    if (Fog()) {
        Fog() = 0;
        Nx::SetRenderState(D3DRS_FOGENABLE, FALSE);
    }
    for (ShadowCaster* caster = FirstCaster(); caster; caster = NextCaster()) {
        float savedView[16], savedProjection[16];
        std::memcpy(savedView, ViewMatrix(), sizeof(savedView));
        std::memcpy(savedProjection, ProjectionMatrix(), sizeof(savedProjection));
        // The receivers' vertices -> shadow texture coordinates.
        float textureBias[16] = {0.5f, 0, 0, 0, 0, -0.5f, 0, 0, 0, 0, 1.0f, 0, 0.5f, 0.5f, 0, 1.0f};
        float viewProjection[16], toTexture[16];
        MatrixMultiply(viewProjection, caster->view, caster->nearProjection);
        MatrixMultiply(toTexture, viewProjection, textureBias);
        ShadowTextureMatrix(caster->view, caster->projection, caster->textureMatrix, nullptr);
        SetPixelShader(nullptr, nullptr);
        Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        Device()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        Device()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        Device()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        Device()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFA0A0A0); // the shadow's colour
        SetState(kAlphaRef, 1);
        SetTexture(0, nullptr);
        SetTexture(2, nullptr);
        SetTexture(3, nullptr);
        SetTexture(1, nullptr);
        SetTexture(0, *reinterpret_cast<void**>(caster->target + 0x10));
        SetState(kAddressMode0, 0x20002); // border
        if (GameVariable<bool>(0x00786AC0))
            Device()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
        Device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        Device()->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(ProjectionMatrix()));
        Device()->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(ViewMatrix()));
        float identity[16];
        Identity(identity);
        Device()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(identity));
        SetBlendMode(0xF);
        std::memcpy(ViewMatrix(), caster->view, sizeof(caster->view));
        std::memcpy(ProjectionMatrix(), caster->projection, sizeof(caster->projection));
        CustomTransforms() = 1;
        if (DepthBias() != 0xD) { // (the cache's depth bias 13, its float sent directly)
            DepthBias() = 0xD;
            Device()->SetRenderState(D3DRS_DEPTHBIAS, 0xB7DA1A93);
        }
        for (int i = 0; i < count; ++i) {
            auto* mesh = static_cast<uint8_t*>(meshes[i]);
            if (mesh[0] & 2)
                continue;
            if (!SphereInView(reinterpret_cast<const Mth::Vector*>(mesh + 4), *reinterpret_cast<float*>(mesh + 0x10)))
                continue;
            SetVertexFormat(D3DFVF_XYZ | D3DFVF_TEX1);
            ProjectShadowVertices(*reinterpret_cast<const float**>(mesh + 0xB0), *reinterpret_cast<uint32_t*>(mesh + 0xAC),
                                  toTexture);
            int triangles = ClipShadowTriangles(*reinterpret_cast<const uint16_t**>(mesh + 0xB8),
                                                *reinterpret_cast<uint32_t*>(mesh + 0xB4));
            if (triangles) {
                Device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, ShadowTriangles(), sizeof(ShadowVertex));
                ++GameVariable<int>(0x00786EBC); // draws
                GameVariable<int>(0x00786EC0) += triangles;
            }
        }
        std::memcpy(ViewMatrix(), savedView, sizeof(savedView));
        std::memcpy(ProjectionMatrix(), savedProjection, sizeof(savedProjection));
        CustomTransforms() = 0;
    }
    SetPixelShader(nullptr, nullptr);
    Device()->SetSamplerState(0, D3DSAMP_MINFILTER, savedMinFilter);
    if (savedFog != Fog()) {
        Fog() = savedFog;
        Device()->SetRenderState(D3DRS_FOGENABLE, savedFog != 0);
    }
    if (DepthBias() != 0) {
        DepthBias() = 0;
        Device()->SetRenderState(D3DRS_DEPTHBIAS, 0x80000000); // -0
    }
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    Device()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    Device()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    Device()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    Device()->SetRenderState(D3DRS_ZENABLE, TRUE);
    SetTexture(0, nullptr);
}

// 0x004DB0A0
void __cdecl RemoveShadowCaster(uint32_t key)
{
    ShadowCaster* caster = FindCaster(key);
    if (!caster)
        return;
    Casters()->FlushItem(key);
    Mem::Manager::Instance()->Free(caster);
}

// 0x004DB100
void __cdecl AimShadowCaster(uint32_t key, const float* position, const float* target)
{
    ShadowCaster* caster = FindCaster(key);
    if (!caster)
        return;
    // Up is y, or z when looking straight up or down.
    float up[3] = {0.0f, 1.0f, 0.0f};
    if (position[0] == target[0] && position[2] == target[2]) {
        up[1] = 0.0f;
        up[2] = 1.0f;
    }
    MatrixLookAt(caster->view, position, target, up);
    MatrixOrtho(caster->projection, 120.0f, 120.0f, 5.0f, 500.0f);
    MatrixOrtho(caster->nearProjection, 120.0f, 120.0f, 80.0f, 500.0f);
}

// 0x004DDC90
void __cdecl AddShadowCaster(uint8_t* target, void* model, uint32_t unknown)
{
    auto* caster = static_cast<ShadowCaster*>(Mem::New(sizeof(ShadowCaster)));
    caster->unknown8 = unknown;
    caster->model = model;
    caster->target = target;
    Identity(caster->view);
    Identity(caster->projection);
    uint32_t key = reinterpret_cast<uint32_t>(target);
    Casters()->PutItem(key, caster);
}

// NOT ORIGINAL
void SetShadowCompanion(ShadowCompanionFn fn) { g_shadowCompanion = fn; }

} // namespace Nx

REPLACE(0x004DA0F0, Nx::ProjectShadowVertices);
REPLACE(0x004DA190, Nx::ClipShadowTriangles);
REPLACE(0x004DB880, Nx::ShadowTextureMatrix);
REPLACE(0x004DB9D0, Nx::DrawShadowVolumes);
REPLACE(0x004DBBD0, Nx::RenderShadowTextures);
REPLACE(0x004DBF50, Nx::DrawShadows);
REPLACE(0x004DB0A0, Nx::RemoveShadowCaster);
REPLACE(0x004DB100, Nx::AimShadowCaster);
REPLACE(0x004DDC90, Nx::AddShadowCaster);
