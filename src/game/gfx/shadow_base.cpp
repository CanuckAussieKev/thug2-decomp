// Shadows: see shadow_base.h.

#include "game/gfx/shadow_base.h"
#include "game/gfx/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/device.h"
#include "game/gfx/geom.h"
#include "game/gfx/sub_mesh.h"
#include "game/gfx/model.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/pixel_shaders.h"
#include "game/gfx/draw_frame.h"
#include "game/gfx/render_state.h"
#include "game/lists/key_map.h"
#include "game/gfx/shader_slots.h"
#include "game/gfx/engine.h"
#include "game/gfx/glow.h"

namespace Engine3D {

constexpr uint32_t kShadowTriangleBytes = 20480; // the original's triangle buffer (0x00796640)
bool g_twoSidedStencil;
float g_shadowViewport[4]{256.0f, 256.0f, 256.0f, 256.0f};

namespace {

IDirect3DDevice9* InputPort() { return g_d3dDevice; }
ListKit::KeyMap* Casters() { return g_shadowCasters; }
float* ViewMatrix() { return reinterpret_cast<float*>(&g_viewMatrix); }
float* ProjectionMatrix() { return reinterpret_cast<float*>(&g_projectionMatrix); }
const float* WorldMatrix() { return reinterpret_cast<float*>(&g_worldMatrix); }
uint8_t& CustomTransforms() { return g_camera.customTransforms; } // the view / projection aren't the camera's
bool OmitFromDraw() { return g_suppressDraw; }
uint32_t& Fog() { return g_fogActive; }
uint32_t& DepthBias() { return g_depthBias; }
uint32_t& CullMode() { return g_cullMode; }
uint32_t& PixelShaderLocked() { return g_pixelShaderLocked; }
bool TwoSidedStencil() { return g_twoSidedStencil; }

// The projection buffer (ProjectShadowVertices) and the triangle buffer (ClipShadowTriangles).
struct ProjectedVertex {
    float position[3];
    float projected[3];
    uint32_t clip; // a bit per side it's outside of
};
static_assert(sizeof(ProjectedVertex) == 0x1C, "the buffer's stride");
struct ShadowCornerPoint {
    float position[3];
    float u, v;
};
static_assert(sizeof(ShadowCornerPoint) == 0x14, "the draw's stride");
// NOT ORIGINAL - both buffers grow to fit. The original's are fixed (4096 projected vertices at 0x0079B640, 20480
// bytes - 1024 vertices - of triangles at 0x00796640) and unchecked: enough for THUG2's own scene meshes, but a
// ported level's mesh under a shadow (Proving Ground's: more than 4096 vertices) ran past them into the globals
// after them (a call through g_shadowCompanion holding a vertex's float). Their first sizes are the original's.
std::vector<ProjectedVertex> g_projectedVertices(4096);
std::vector<ShadowCornerPoint> g_shadowVertices(kShadowTriangleBytes / sizeof(ShadowCornerPoint));
ProjectedVertex* Projected(uint32_t count)
{
    if (g_projectedVertices.size() < count)
        g_projectedVertices.resize(count);
    return g_projectedVertices.data();
}
ProjectedVertex* Projected() { return g_projectedVertices.data(); }
ShadowCornerPoint* ShadowTriangles(uint32_t vertices)
{
    if (g_shadowVertices.size() < vertices)
        g_shadowVertices.resize(vertices);
    return g_shadowVertices.data();
}
ShadowCornerPoint* ShadowTriangles() { return g_shadowVertices.data(); }
constexpr float kFacingLimit = std::bit_cast<float>(0xB8D1B717u); // 0x0064AAA0, -1e-4

ShadowCompanionFn g_shadowCompanion = nullptr; // NOT ORIGINAL (SetShadowCompanion)

// D3DX (linked library code; stdcall).
void MatrixMultiply(float* out, const float* a, const float* b)
{
    D3dx::MatrixMultiply(out, a, b);
}
void MatrixInverse(float* out, float* determinant, const float* m)
{
    D3dx::MatrixInverse(out, determinant, m);
}
void TransformCoord(float* out, const float* v, const void* m)
{
    D3dx::Vec3TransformCoord(out, v, m);
}
void MatrixLookAt(float* out, const float* eye, const float* at, const float* up)
{
    D3dx::MatrixLookAtRH(out, eye, at, up);
}
void MatrixOrtho(float* out, float width, float height, float nearZ, float farZ)
{
    D3dx::MatrixOrthoRH(out, width, height, nearZ, farZ);
}

// The models' meshes (not rewritten yet; thiscall).
int MeshCount(void* model) { return static_cast<CRenderModel*>(model)->FetchGeomTotal(); }
void* Mesh(void* model, int index) { return static_cast<CRenderModel*>(model)->FetchGeom(index); }
void DrawMesh(void* mesh, int flags) { DrawOneInstance(static_cast<sSingleton*>(mesh), static_cast<uint32_t>(flags)); }
// A scene mesh's draw (thiscall).
void DrawSceneMesh(void* mesh) { static_cast<meshRef*>(mesh)->Render(); }

void Identity(float* m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// The caster with this key (the table's own inlined lookup: the first item with the key and a value).
ShadowCaster* FindCaster(uint32_t key)
{
    ListKit::KeyMap* table = Casters();
    ListKit::KeyMap::Item* item = table->m_buckets + (key & ((1u << table->m_bits) - 1));
    for (; item; item = item->next) {
        if (item->key == key && item->value)
            return static_cast<ShadowCaster*>(item->value);
    }
    return nullptr;
}

// Iterate the casters: the first (or next) one, or null at the end (an empty slot's null ends it too).
ShadowCaster* NextCaster()
{
    ListKit::KeyMap* table = Casters();
    if (!table->StepIterator() || !table->m_iterItem)
        return nullptr;
    return static_cast<ShadowCaster*>(table->m_iterItem->value);
}
ShadowCaster* FirstCaster()
{
    ListKit::KeyMap* table = Casters();
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    return NextCaster();
}

} // namespace

// 0x004DA0F0
void __cdecl ProjectShadowVertices(const float* positions, uint32_t count, const void* matrix)
{
    ProjectedVertex* vertex = Projected(count);
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
    ShadowCornerPoint* out = ShadowTriangles(3 * count); // (at most every triangle kept)
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
    InputPort()->GetTransform(D3DTS_VIEW, reinterpret_cast<D3DMATRIX*>(view));
    float inverse[16];
    MatrixInverse(inverse, nullptr, view); // (not used)
    // The shadow viewport (0x0067EF4C: x, y, width, height) as a scale and offset of the -1..1 range; depth 0..2^24.
    float scaleBias[16] = {};
    scaleBias[0] = static_cast<float>(static_cast<double>(g_shadowViewport[3]) * 0.5);
    scaleBias[5] = static_cast<float>(static_cast<double>(g_shadowViewport[2]) * -0.5);
    scaleBias[10] = std::bit_cast<float>(0x4B7FFFFFu); // 16777215
    scaleBias[12] = static_cast<float>((static_cast<double>(g_shadowViewport[1]) + 1.0) * 0.5);
    scaleBias[13] = static_cast<float>((static_cast<double>(g_shadowViewport[0]) + 1.0) * 0.5);
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
    void* vertexColour = g_pixelShaders[kPixelShader94C];
    SetPixelShader(vertexColour, nullptr);
    PixelShaderLocked() = reinterpret_cast<uint32_t>(vertexColour);
    SetTexture(0, nullptr);
    ChangePhase(kZWrite, 0);
    ChangePhase(kZTest, 1);
    ChangePhase(kPhaseAlphaTest, 0);
    if (!TwoSidedStencil())
        InputPort()->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
    const auto* bytes = static_cast<const uint8_t*>(scene);
    void* const* meshes = *reinterpret_cast<void* const* const*>(bytes + 0x1C);
    for (int i = 0; i < *reinterpret_cast<const int*>(bytes + 0x20); ++i) {
        auto* mesh = static_cast<uint8_t*>(meshes[i]);
        uint32_t flags = *reinterpret_cast<uint32_t*>(mesh);
        if (!(flags & 0x40) || !(flags & 0x200))
            continue;
        if (!SphereInView(reinterpret_cast<const Maths::Vec4*>(mesh + 4), *reinterpret_cast<float*>(mesh + 0x10)))
            continue;
        if (!TwoSidedStencil()) {
            // One side's faces, then the other's.
            ChangePhase(kCullMode, D3DCULL_CW);
            ChooseBlendMode(0x10000002);
            DrawSceneMesh(mesh);
            ChangePhase(kCullMode, D3DCULL_CCW);
            ChooseBlendMode(0x10000004);
        } else {
            ChangePhase(kCullMode, D3DCULL_NONE);
            ChooseBlendMode(0x30000006);
        }
        DrawSceneMesh(mesh);
    }
    PixelShaderLocked() = 0;
    ChangePhase(kZWrite, 1);
    ChangePhase(kCullMode, D3DCULL_CW);
    InputPort()->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
}

// 0x004DBBD0
void __cdecl RenderShadowTextures()
{
    if (OmitFromDraw())
        return;
    float savedView[16], savedProjection[16];
    std::memcpy(savedView, ViewMatrix(), sizeof(savedView));
    std::memcpy(savedProjection, ProjectionMatrix(), sizeof(savedProjection));
    IDirect3DSurface9* renderTarget = nullptr;
    IDirect3DSurface9* depth = nullptr;
    uint32_t savedFog = Fog();
    InputPort()->GetRenderTarget(0, &renderTarget);
    InputPort()->GetDepthStencilSurface(&depth);
    if (Fog()) {
        Fog() = 0;
        Engine3D::SetRenderState(D3DRS_FOGENABLE, FALSE);
    }
    for (ShadowCaster* caster = FirstCaster(); caster; caster = NextCaster()) {
        if (!caster->model)
            continue;
        InputPort()->SetRenderTarget(0, *reinterpret_cast<IDirect3DSurface9**>(caster->target + 0x18));
        InputPort()->SetDepthStencilSurface(nullptr);
        InputPort()->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        std::memcpy(ViewMatrix(), caster->view, sizeof(caster->view));
        std::memcpy(ProjectionMatrix(), caster->projection, sizeof(caster->projection));
        CustomTransforms() = 1;
        InputPort()->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(caster->view));
        InputPort()->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(caster->projection));
        float identity[16];
        Identity(identity);
        InputPort()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(identity));
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
    InputPort()->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    if (savedFog != Fog()) {
        Fog() = savedFog;
        InputPort()->SetRenderState(D3DRS_FOGENABLE, savedFog != 0);
    }
    PixelShaderLocked() = 0;
    std::memcpy(ViewMatrix(), savedView, sizeof(savedView));
    std::memcpy(ProjectionMatrix(), savedProjection, sizeof(savedProjection));
    CustomTransforms() = 0;
    InputPort()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(WorldMatrix()));
    InputPort()->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(ViewMatrix()));
    InputPort()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    InputPort()->SetRenderTarget(0, renderTarget);
    InputPort()->SetDepthStencilSurface(depth);
    if (renderTarget)
        renderTarget->Release();
    if (depth)
        depth->Release();
}

// 0x004DBF50
void __cdecl DrawShadows(const void* /*unused*/, void* const* meshes, int count)
{
    if (OmitFromDraw())
        return;
    DWORD savedMinFilter;
    InputPort()->GetSamplerState(0, D3DSAMP_MINFILTER, &savedMinFilter);
    InputPort()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    InputPort()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    InputPort()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    uint32_t savedFog = Fog();
    if (Fog()) {
        Fog() = 0;
        Engine3D::SetRenderState(D3DRS_FOGENABLE, FALSE);
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
        InputPort()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        InputPort()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        InputPort()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        InputPort()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        InputPort()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        InputPort()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFA0A0A0); // the shadow's colour
        ChangePhase(kPhaseAlphaTest, 1);
        SetTexture(0, nullptr);
        SetTexture(2, nullptr);
        SetTexture(3, nullptr);
        SetTexture(1, nullptr);
        SetTexture(0, *reinterpret_cast<void**>(caster->target + 0x10));
        ChangePhase(kAddressMode0, 0x20002); // border
        if (g_borderAddressingSupported)
            InputPort()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
        InputPort()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        InputPort()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        InputPort()->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(ProjectionMatrix()));
        InputPort()->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(ViewMatrix()));
        float identity[16];
        Identity(identity);
        InputPort()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(identity));
        ChooseBlendMode(0xF);
        std::memcpy(ViewMatrix(), caster->view, sizeof(caster->view));
        std::memcpy(ProjectionMatrix(), caster->projection, sizeof(caster->projection));
        CustomTransforms() = 1;
        if (DepthBias() != 0xD) { // (the cache's depth bias 13, its float sent directly)
            DepthBias() = 0xD;
            InputPort()->SetRenderState(D3DRS_DEPTHBIAS, 0xB7DA1A93);
        }
        for (int i = 0; i < count; ++i) {
            auto* mesh = static_cast<uint8_t*>(meshes[i]);
            if (mesh[0] & 2)
                continue;
            if (!SphereInView(reinterpret_cast<const Maths::Vec4*>(mesh + 4), *reinterpret_cast<float*>(mesh + 0x10)))
                continue;
            SetVertexFormat(D3DFVF_XYZ | D3DFVF_TEX1);
            ProjectShadowVertices(*reinterpret_cast<const float**>(mesh + 0xB0), *reinterpret_cast<uint32_t*>(mesh + 0xAC),
                                  toTexture);
            int triangles = ClipShadowTriangles(*reinterpret_cast<const uint16_t**>(mesh + 0xB8),
                                                *reinterpret_cast<uint32_t*>(mesh + 0xB4));
            if (triangles) {
                InputPort()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, ShadowTriangles(), sizeof(ShadowCornerPoint));
                ++g_drawCalls; // draws
                g_drawnTriangles += triangles;
            }
        }
        std::memcpy(ViewMatrix(), savedView, sizeof(savedView));
        std::memcpy(ProjectionMatrix(), savedProjection, sizeof(savedProjection));
        CustomTransforms() = 0;
    }
    SetPixelShader(nullptr, nullptr);
    InputPort()->SetSamplerState(0, D3DSAMP_MINFILTER, savedMinFilter);
    if (savedFog != Fog()) {
        Fog() = savedFog;
        InputPort()->SetRenderState(D3DRS_FOGENABLE, savedFog != 0);
    }
    if (DepthBias() != 0) {
        DepthBias() = 0;
        InputPort()->SetRenderState(D3DRS_DEPTHBIAS, 0x80000000); // -0
    }
    InputPort()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    InputPort()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    InputPort()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    InputPort()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    InputPort()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    InputPort()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    InputPort()->SetRenderState(D3DRS_ZENABLE, TRUE);
    SetTexture(0, nullptr);
}

// 0x004DB0A0
void __cdecl RemoveShadowCaster(uint32_t key)
{
    ShadowCaster* caster = FindCaster(key);
    if (!caster)
        return;
    Casters()->EraseEntry(key);
    MemSys::Hub::Sole()->Dealloc(caster);
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
    auto* caster = static_cast<ShadowCaster*>(MemSys::Alloc(sizeof(ShadowCaster)));
    caster->unknown8 = unknown;
    caster->model = model;
    caster->target = target;
    Identity(caster->view);
    Identity(caster->projection);
    uint32_t key = reinterpret_cast<uint32_t>(target);
    Casters()->StoreEntry(key, caster);
}

// NOT ORIGINAL
void SetShadowCompanion(ShadowCompanionFn fn) { g_shadowCompanion = fn; }

} // namespace Nx
