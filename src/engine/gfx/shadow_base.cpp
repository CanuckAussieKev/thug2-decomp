// Shadows: see shadow_base.h.

#include "engine/gfx/shadow_base.h"
#include "platform/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/device.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/model.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/pixel_shaders.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/engine.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/lists/object_set.h"
#include <span>
#include "platform/d3d_matrix.h"
#include "engine/gfx/bitmap_texture.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"

namespace render {

constexpr uint32_t kShadowTriangleBytes = 20480; // the original's triangle buffer (0x00796640)
namespace {
constexpr bool kTwoSidedStencil{false};
std::array<float, 4>& shadow_viewport()
{
    static std::array<float, 4> shadow_viewport{{256.0f, 256.0f, 256.0f, 256.0f}};
    return shadow_viewport;
}

const float* view_matrix_data() { return renderer().view_matrix().data(); }
const float* projection_matrix_data() { return renderer().projection_matrix().data(); }
const float* world_matrix_data() { return renderer().world_matrix().data(); }
// The renderer's view and projection matrices from 16 floats each (a caster's, or the saved ones back); the camera's
// flag says whether they are the camera's own (0) or set by hand (1).
void set_view_and_projection(std::span<const float, 16> view, std::span<const float, 16> projection, uint8_t custom)
{
    maths::Mat4 matrix{};
    std::ranges::copy(view, matrix.floats().begin());
    renderer().set_view_matrix(matrix);
    std::ranges::copy(projection, matrix.floats().begin());
    renderer().set_projection_matrix(matrix);
    renderer().set_custom_transforms(custom);
}

bool two_sided_stencil() { return kTwoSidedStencil; }

// The projection buffer (project_shadow_vertices) and the triangle buffer (clip_shadow_triangles).
struct ProjectedVertex {
    std::array<float, 3> position;
    std::array<float, 3> projected;
    uint32_t clip; // a bit per side it's outside of
};
static_assert(sizeof(ProjectedVertex) == 0x1C, "the buffer's stride");
struct ShadowCornerPoint {
    std::array<float, 3> position;
    float u, v;
};
static_assert(sizeof(ShadowCornerPoint) == 0x14, "the draw's stride");
// NOT ORIGINAL - both buffers grow to fit. The original's are fixed (4096 projected vertices at 0x0079B640, 20480
// bytes - 1024 vertices - of triangles at 0x00796640) and unchecked: enough for THUG2's own scene meshes, but a
// ported level's mesh under a shadow (Proving Ground's: more than 4096 vertices) ran past them into the globals
// after them (a call through shadow_companion() holding a vertex's float). Their first sizes are the original's.
std::vector<ProjectedVertex>& projected_vertices()
{
    static std::vector<ProjectedVertex> value(4096);
    return value;
}
std::vector<ShadowCornerPoint>& shadow_vertices()
{
    static std::vector<ShadowCornerPoint> value(kShadowTriangleBytes / sizeof(ShadowCornerPoint));
    return value;
}
std::span<ProjectedVertex> get_projected(uint32_t count)
{
    if (projected_vertices().size() < count)
        projected_vertices().resize(count);
    return projected_vertices();
}
std::span<ProjectedVertex> get_projected() { return projected_vertices(); }
std::span<ShadowCornerPoint> shadow_triangles(uint32_t vertices)
{
    if (shadow_vertices().size() < vertices)
        shadow_vertices().resize(vertices);
    return shadow_vertices();
}
std::span<ShadowCornerPoint> shadow_triangles() { return shadow_vertices(); }
constexpr float kFacingLimit = std::bit_cast<float>(0xB8D1B717u); // 0x0064AAA0, -1e-4

ShadowCompanionFn& shadow_companion()
{
    static ShadowCompanionFn shadow_companion{nullptr}; // NOT ORIGINAL (set_shadow_companion)
    return shadow_companion;
}

// D3DX (linked library code; stdcall).
void matrix_multiply(float* out, const float* a, const float* b) { d3dx::matrix_multiply(out, a, b); }
void matrix_inverse(float* out, float* determinant, const float* m) { d3dx::matrix_inverse(out, determinant, m); }
void transform_coord(float* out, const float* v, const void* m) { d3dx::vec3_transform_coord(out, v, m); }
void matrix_look_at(float* out, const float* eye, const float* at, const float* up)
{
    d3dx::matrix_look_at_rh(out, eye, at, up);
}
void matrix_ortho(float* out, float width, float height, float near_z, float far_z)
{
    d3dx::matrix_ortho_rh(out, width, height, near_z, far_z);
}

// The models' meshes (not rewritten yet; thiscall).
int get_mesh_count(void* model) { return static_cast<RenderModel*>(model)->fetch_geom_total(); }
void* get_mesh(void* model, int index) { return static_cast<RenderModel*>(model)->fetch_geom(index); }
void draw_mesh(void* mesh, int flags)
{
    draw_one_instance(static_cast<Singleton*>(mesh), static_cast<uint32_t>(flags));
}
// A scene mesh's draw (thiscall).
void draw_scene_mesh(void* mesh) { static_cast<MeshRef*>(mesh)->render(); }

void get_identity(std::span<float, 16> m)
{
    for (int i = 0; i < 16; ++i)
        core::at(m, i) = (i % 5 == 0) ? 1.0f : 0.0f;
}

// The caster drawing into `key` (the table's own inlined lookup), or null.
ShadowCaster* find_caster(const TextureRef* key)
{
    return shadow_casters().find_if([key](const ShadowCaster* caster) { return caster->target == key; });
}

} // namespace

// 0x004DA0F0
void project_shadow_vertices(std::span<const float> positions, const void* matrix)
{
    const auto count = static_cast<uint32_t>(positions.size() / 3);
    const std::span<ProjectedVertex> vertices = get_projected(count);
    for (uint32_t i = 0; i < count; ++i) {
        ProjectedVertex* const vertex = &core::at(vertices, i);
        const std::span<const float, 3> position = positions.subspan(3 * static_cast<size_t>(i), 3).first<3>();
        transform_coord(vertex->projected.data(), position.data(), matrix);
        // The clip flags from the float bits: a bit for each of x < 0, x > 1, y < 0, y > 1, z (on the sign and
        // the exponent's top bit after adding 1 to the exponent).
        const uint32_t x = std::bit_cast<uint32_t>(core::at(vertex->projected, 0)) + 0x800000;
        const uint32_t y = std::bit_cast<uint32_t>(core::at(vertex->projected, 1)) + 0x800000;
        const uint32_t z = std::bit_cast<uint32_t>(core::at(vertex->projected, 2)) + 0x800000;
        const uint32_t bits = (((y & 0xC0000003) | ((x >> 2) & 0x30000000)) >> 2) | (z & 0xC0000000);
        core::at(vertex->position, 0) = core::at(position, 0);
        core::at(vertex->position, 1) = core::at(position, 1);
        core::at(vertex->position, 2) = core::at(position, 2);
        vertex->clip = ~((bits >> 1) & 0x54000000) & bits;
    }
}

// 0x004DA190
int clip_shadow_triangles(std::span<const uint16_t> indices)
{
    const auto count = static_cast<uint32_t>(indices.size() / 3);
    core::Cursor<ShadowCornerPoint> out(shadow_triangles(3 * count)); // (at most every triangle kept)
    for (uint32_t i = 0; i < count; ++i) {
        const std::span<const uint16_t> corner = indices.subspan(3 * static_cast<size_t>(i), 3);
        const ProjectedVertex& v0 = core::at(get_projected(), core::at(corner, 0));
        const ProjectedVertex& v1 = core::at(get_projected(), core::at(corner, 1));
        const ProjectedVertex& v2 = core::at(get_projected(), core::at(corner, 2));
        if (v0.clip & v1.clip & v2.clip)
            continue; // all outside the same side
        const double facing = (static_cast<double>(core::at(v2.projected, 1)) - core::at(v1.projected, 1)) *
                                  (static_cast<double>(core::at(v1.projected, 0)) - core::at(v0.projected, 0)) -
                              (static_cast<double>(core::at(v2.projected, 0)) - core::at(v1.projected, 0)) *
                                  (static_cast<double>(core::at(v1.projected, 1)) - core::at(v0.projected, 1));
        if (facing > kFacingLimit)
            continue;
        for (const ProjectedVertex* v : {&v0, &v1, &v2}) {
            std::memcpy(out->position.data(), v->position.data(), sizeof(out->position));
            out->u = core::at(v->projected, 0);
            out->v = core::at(v->projected, 1);
            ++out;
        }
    }
    return static_cast<int>(out.position()) / 3;
}

// 0x004DB880
void shadow_texture_matrix(const float* a, const float* b, float* out, const float* first)
{
    std::array<float, 16> view{};
    renderer().d3d_device()->GetTransform(D3DTS_VIEW, d3d::matrix(view.data()));
    std::array<float, 16> inverse{};
    matrix_inverse(inverse.data(), nullptr, view.data()); // (not used)
    // The shadow viewport (0x0067EF4C: x, y, width, height) as a scale and offset of the -1..1 range; depth 0..2^24.
    std::array<float, 16> scale_bias = {};
    core::at(scale_bias, 0) = static_cast<float>(static_cast<double>(core::at(shadow_viewport(), 3)) * 0.5);
    core::at(scale_bias, 5) = static_cast<float>(static_cast<double>(core::at(shadow_viewport(), 2)) * -0.5);
    core::at(scale_bias, 10) = std::bit_cast<float>(0x4B7FFFFFu); // 16777215
    core::at(scale_bias, 12) = static_cast<float>((static_cast<double>(core::at(shadow_viewport(), 1)) + 1.0) * 0.5);
    core::at(scale_bias, 13) = static_cast<float>((static_cast<double>(core::at(shadow_viewport(), 0)) + 1.0) * 0.5);
    core::at(scale_bias, 15) = 1.0f;
    std::array<float, 16> m{};
    if (first) {
        std::memcpy(m.data(), first, sizeof(m));
        matrix_multiply(m.data(), m.data(), a);
    } else {
        std::memcpy(m.data(), a, sizeof(m));
    }
    matrix_multiply(m.data(), m.data(), b);
    matrix_multiply(out, m.data(), scale_bias.data());
}

// 0x004DB9D0
void draw_shadow_volumes(const void* scene, int /*viewport*/)
{
    void* vertex_colour = core::at(pixel_shaders(), kPixelShader94c);
    set_pixel_shader(vertex_colour, nullptr);
    renderer().set_pixel_shader_locked(vertex_colour);
    set_texture(0, nullptr);
    change_phase(kZWrite, 0);
    change_phase(kZTest, 1);
    change_phase(kPhaseAlphaTest, 0);
    if (!two_sided_stencil())
        renderer().d3d_device()->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
    const auto* list = static_cast<const SceneRef*>(scene);
    for (MeshRef* const mesh : list->mesh_run()) {
        const uint32_t flags = mesh->flags();
        if (!(flags & 0x40) || !(flags & 0x200))
            continue;
        if (const maths::Vec4 sphere = sphere_of(mesh->centre(), mesh->radius()); !sphere_in_view(&sphere, mesh->radius()))
            continue;
        if (!two_sided_stencil()) {
            // One side's faces, then the other's.
            change_phase(kCullMode, D3DCULL_CW);
            choose_blend_mode(0x10000002);
            draw_scene_mesh(mesh);
            change_phase(kCullMode, D3DCULL_CCW);
            choose_blend_mode(0x10000004);
        } else {
            change_phase(kCullMode, D3DCULL_NONE);
            choose_blend_mode(0x30000006);
        }
        draw_scene_mesh(mesh);
    }
    renderer().set_pixel_shader_locked(nullptr);
    change_phase(kZWrite, 1);
    change_phase(kCullMode, D3DCULL_CW);
    renderer().d3d_device()->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
}

// 0x004DBBD0
void render_shadow_textures()
{
    if (engine_parts().suppress_draw())
        return;
    std::array<float, 16> saved_view{};
    std::array<float, 16> saved_projection{};
    std::memcpy(saved_view.data(), view_matrix_data(), sizeof(saved_view));
    std::memcpy(saved_projection.data(), projection_matrix_data(), sizeof(saved_projection));
    IDirect3DSurface9* render_target = nullptr;
    IDirect3DSurface9* depth = nullptr;
    const uint32_t saved_fog = renderer().fog_active();
    renderer().d3d_device()->GetRenderTarget(0, &render_target);
    renderer().d3d_device()->GetDepthStencilSurface(&depth);
    if (renderer().fog_active()) {
        renderer().set_fog_active(0);
        render::set_render_state(D3DRS_FOGENABLE, FALSE);
    }
    for (ShadowCaster* caster : shadow_casters()) {
        if (!caster->model)
            continue;
        renderer().d3d_device()->SetRenderTarget(0, caster->target->surface);
        renderer().d3d_device()->SetDepthStencilSurface(nullptr);
        renderer().d3d_device()->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        set_view_and_projection(caster->view, caster->projection, 1);
        renderer().d3d_device()->SetTransform(D3DTS_VIEW, d3d::matrix(caster->view.data()));
        renderer().d3d_device()->SetTransform(D3DTS_PROJECTION, d3d::matrix(caster->projection.data()));
        std::array<float, 16> identity{};
        get_identity(identity);
        renderer().d3d_device()->SetTransform(D3DTS_WORLD, d3d::matrix(identity.data()));
        auto draw_model = [](void* model) {
            const int count = get_mesh_count(model);
            for (int i = 0; i < count; ++i) {
                // (the geom's instance: drawn when active, its mesh list flagged as a shadow meanwhile)
                Singleton* instance = static_cast<PcGeom*>(get_mesh(model, i))->instance();
                if (!instance->active())
                    continue;
                instance->scene()->flags() |= 0x80; // (drawn as a shadow)
                draw_mesh(instance, 8);
                instance->scene()->flags() &= ~0x80u;
                instance->scene()->flags() |= 0x200;
            }
        };
        draw_model(caster->model);
        // NOT ORIGINAL (ours: set_shadow_companion) - a model drawn into this caster's shadow as well.
        if (shadow_companion()) {
            if (void* companion = shadow_companion()(caster->model))
                draw_model(companion);
        }
    }
    renderer().d3d_device()->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    if (saved_fog != renderer().fog_active()) {
        renderer().set_fog_active(saved_fog);
        renderer().d3d_device()->SetRenderState(D3DRS_FOGENABLE, saved_fog != 0);
    }
    renderer().set_pixel_shader_locked(nullptr);
    set_view_and_projection(saved_view, saved_projection, 0);
    renderer().d3d_device()->SetTransform(D3DTS_WORLD, d3d::matrix(world_matrix_data()));
    renderer().d3d_device()->SetTransform(D3DTS_VIEW, d3d::matrix(view_matrix_data()));
    renderer().d3d_device()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    renderer().d3d_device()->SetRenderTarget(0, render_target);
    renderer().d3d_device()->SetDepthStencilSurface(depth);
    if (render_target)
        render_target->Release();
    if (depth)
        depth->Release();
}

// 0x004DBF50
void draw_shadows(const void* /*unused*/, std::span<MeshRef* const> meshes)
{
    if (engine_parts().suppress_draw())
        return;
    DWORD saved_min_filter = 0;
    renderer().d3d_device()->GetSamplerState(0, D3DSAMP_MINFILTER, &saved_min_filter);
    renderer().d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    const uint32_t saved_fog = renderer().fog_active();
    if (renderer().fog_active()) {
        renderer().set_fog_active(0);
        render::set_render_state(D3DRS_FOGENABLE, FALSE);
    }
    for (ShadowCaster* caster : shadow_casters()) {
        std::array<float, 16> saved_view{};
        std::array<float, 16> saved_projection{};
        std::memcpy(saved_view.data(), view_matrix_data(), sizeof(saved_view));
        std::memcpy(saved_projection.data(), projection_matrix_data(), sizeof(saved_projection));
        // The receivers' vertices -> shadow texture coordinates.
        std::array<float, 16> texture_bias = {{0.5f, 0, 0, 0, 0, -0.5f, 0, 0, 0, 0, 1.0f, 0, 0.5f, 0.5f, 0, 1.0f}};
        std::array<float, 16> view_projection{};
        std::array<float, 16> to_texture{};
        matrix_multiply(view_projection.data(), caster->view.data(), caster->near_projection_.data());
        matrix_multiply(to_texture.data(), view_projection.data(), texture_bias.data());
        shadow_texture_matrix(caster->view.data(), caster->projection.data(), caster->texture_matrix_.data(), nullptr);
        set_pixel_shader(nullptr, nullptr);
        renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        renderer().d3d_device()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        renderer().d3d_device()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFA0A0A0); // the shadow's colour
        change_phase(kPhaseAlphaTest, 1);
        set_texture(0, nullptr);
        set_texture(2, nullptr);
        set_texture(3, nullptr);
        set_texture(1, nullptr);
        set_texture(0, caster->target->texture);
        change_phase(kAddressMode0, 0x20002); // border
        if (renderer().border_addressing_supported())
            renderer().d3d_device()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
        renderer().d3d_device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        renderer().d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        renderer().d3d_device()->SetTransform(D3DTS_PROJECTION, d3d::matrix(projection_matrix_data()));
        renderer().d3d_device()->SetTransform(D3DTS_VIEW, d3d::matrix(view_matrix_data()));
        std::array<float, 16> identity{};
        get_identity(identity);
        renderer().d3d_device()->SetTransform(D3DTS_WORLD, d3d::matrix(identity.data()));
        choose_blend_mode(0xF);
        set_view_and_projection(caster->view, caster->projection, 1);
        if (renderer().depth_bias() != 0xD) { // (the cache's depth bias 13, its float sent directly)
            renderer().set_depth_bias(0xD);
            renderer().d3d_device()->SetRenderState(D3DRS_DEPTHBIAS, 0xB7DA1A93);
        }
        for (const MeshRef* const mesh : meshes) {
            if (mesh->flags() & 2)
                continue;
            if (const maths::Vec4 sphere = sphere_of(mesh->centre(), mesh->radius()); !sphere_in_view(&sphere, mesh->radius()))
                continue;
            set_vertex_format(D3DFVF_XYZ | D3DFVF_TEX1);
            project_shadow_vertices(std::span<const float>(mesh->positions(), 3 * static_cast<size_t>(mesh->position_count())), to_texture.data());
            const int triangles = clip_shadow_triangles(std::span<const uint16_t>(mesh->triangles(), 3 * static_cast<size_t>(mesh->triangle_count())));
            if (triangles) {
                renderer().d3d_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, shadow_triangles().data(),
                                              sizeof(ShadowCornerPoint));
                renderer().count_draw(triangles); // the frame's draw calls and triangles
            }
        }
        set_view_and_projection(saved_view, saved_projection, 0);
    }
    set_pixel_shader(nullptr, nullptr);
    renderer().d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, saved_min_filter);
    if (saved_fog != renderer().fog_active()) {
        renderer().set_fog_active(saved_fog);
        renderer().d3d_device()->SetRenderState(D3DRS_FOGENABLE, saved_fog != 0);
    }
    if (renderer().depth_bias() != 0) {
        renderer().set_depth_bias(0);
        renderer().d3d_device()->SetRenderState(D3DRS_DEPTHBIAS, 0x80000000); // -0
    }
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    renderer().d3d_device()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    renderer().d3d_device()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    renderer().d3d_device()->SetRenderState(D3DRS_ZENABLE, TRUE);
    set_texture(0, nullptr);
}

// 0x004DB0A0
void remove_shadow_caster(const TextureRef* key)
{
    ShadowCaster* caster = find_caster(key);
    if (!caster)
        return;
    shadow_casters().erase(caster);
    memory::Hub::sole()->dealloc(caster);
}

// 0x004DB100
void aim_shadow_caster(const TextureRef* key, std::span<const float, 3> position, std::span<const float, 3> target)
{
    ShadowCaster* caster = find_caster(key);
    if (!caster)
        return;
    // Up is y, or z when looking straight up or down.
    std::array<float, 3> up = {{0.0f, 1.0f, 0.0f}};
    if (core::at(position, 0) == core::at(target, 0) && core::at(position, 2) == core::at(target, 2)) {
        core::at(up, 1) = 0.0f;
        core::at(up, 2) = 1.0f;
    }
    matrix_look_at(caster->view.data(), position.data(), target.data(), up.data());
    matrix_ortho(caster->projection.data(), 120.0f, 120.0f, 5.0f, 500.0f);
    matrix_ortho(caster->near_projection_.data(), 120.0f, 120.0f, 80.0f, 500.0f);
}

// 0x004DDC90
void add_shadow_caster(TextureRef* target, void* model, uint32_t unknown)
{
    auto* caster = static_cast<ShadowCaster*>(memory::alloc(sizeof(ShadowCaster)));
    caster->unknown8 = unknown;
    caster->model = model;
    caster->target = target;
    get_identity(caster->view);
    get_identity(caster->projection);
    shadow_casters().insert(caster);
}

// NOT ORIGINAL
void set_shadow_companion(ShadowCompanionFn fn) { shadow_companion() = fn; }

} // namespace render
