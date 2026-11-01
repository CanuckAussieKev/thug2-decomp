// Shadows: see shadow_base.h.

#include "engine/gfx/shadow_base.h"
#include "engine/gfx/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/device.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/model.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/pixel_shaders.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/lists/key_map.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/glow.h"

namespace render {

constexpr uint32_t kShadowTriangleBytes = 20480; // the original's triangle buffer (0x00796640)
static bool g_two_sided_stencil;
static float g_shadow_viewport[4]{256.0f, 256.0f, 256.0f, 256.0f};

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
lists::KeyMap* get_casters() { return g_shadow_casters; }
float* view_matrix() { return reinterpret_cast<float*>(&g_view_matrix); }
float* projection_matrix() { return reinterpret_cast<float*>(&g_projection_matrix); }
const float* world_matrix() { return reinterpret_cast<float*>(&g_world_matrix); }
uint8_t& custom_transforms() { return g_camera.custom_transforms_; } // the view / projection aren't the camera's
bool omit_from_draw() { return g_suppress_draw; }
uint32_t& get_fog() { return g_fog_active; }
uint32_t& depth_bias() { return g_depth_bias; }
uint32_t& cull_mode() { return g_cull_mode; }
uint32_t& pixel_shader_locked() { return g_pixel_shader_locked; }
bool two_sided_stencil() { return g_two_sided_stencil; }

// The projection buffer (project_shadow_vertices) and the triangle buffer (clip_shadow_triangles).
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
// after them (a call through g_shadow_companion holding a vertex's float). Their first sizes are the original's.
std::vector<ProjectedVertex> g_projected_vertices(4096);
std::vector<ShadowCornerPoint> g_shadow_vertices(kShadowTriangleBytes / sizeof(ShadowCornerPoint));
ProjectedVertex* get_projected(uint32_t count)
{
    if (g_projected_vertices.size() < count)
        g_projected_vertices.resize(count);
    return g_projected_vertices.data();
}
ProjectedVertex* get_projected() { return g_projected_vertices.data(); }
ShadowCornerPoint* shadow_triangles(uint32_t vertices)
{
    if (g_shadow_vertices.size() < vertices)
        g_shadow_vertices.resize(vertices);
    return g_shadow_vertices.data();
}
ShadowCornerPoint* shadow_triangles() { return g_shadow_vertices.data(); }
constexpr float kFacingLimit = std::bit_cast<float>(0xB8D1B717u); // 0x0064AAA0, -1e-4

ShadowCompanionFn g_shadow_companion = nullptr; // NOT ORIGINAL (set_shadow_companion)

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

void get_identity(float* m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// The caster with this key (the table's own inlined lookup: the first item with the key and a value).
ShadowCaster* find_caster(uint32_t key)
{
    lists::KeyMap* table = get_casters();
    lists::KeyMap::Item* item = table->buckets_ + (key & ((1u << table->bits_) - 1));
    for (; item; item = item->next) {
        if (item->key == key && item->value)
            return static_cast<ShadowCaster*>(item->value);
    }
    return nullptr;
}

// Iterate the casters: the first (or next) one, or null at the end (an empty slot's null ends it too).
ShadowCaster* next_caster()
{
    lists::KeyMap* table = get_casters();
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return static_cast<ShadowCaster*>(table->iter_item_->value);
}
ShadowCaster* first_caster()
{
    lists::KeyMap* table = get_casters();
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    return next_caster();
}

} // namespace

// 0x004DA0F0
void project_shadow_vertices(const float* positions, uint32_t count, const void* matrix)
{
    ProjectedVertex* vertex = get_projected(count);
    for (uint32_t i = 0; i < count; ++i, ++vertex, positions += 3) {
        transform_coord(vertex->projected, positions, matrix);
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
int clip_shadow_triangles(const uint16_t* indices, uint32_t count)
{
    ShadowCornerPoint* out = shadow_triangles(3 * count); // (at most every triangle kept)
    for (uint32_t i = 0; i < count; ++i, indices += 3) {
        const ProjectedVertex& v0 = get_projected()[indices[0]];
        const ProjectedVertex& v1 = get_projected()[indices[1]];
        const ProjectedVertex& v2 = get_projected()[indices[2]];
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
    return static_cast<int>(out - shadow_triangles()) / 3;
}

// 0x004DB880
void shadow_texture_matrix(const float* a, const float* b, float* out, const float* first)
{
    float view[16];
    input_port()->GetTransform(D3DTS_VIEW, reinterpret_cast<D3DMATRIX*>(view));
    float inverse[16];
    matrix_inverse(inverse, nullptr, view); // (not used)
    // The shadow viewport (0x0067EF4C: x, y, width, height) as a scale and offset of the -1..1 range; depth 0..2^24.
    float scale_bias[16] = {};
    scale_bias[0] = static_cast<float>(static_cast<double>(g_shadow_viewport[3]) * 0.5);
    scale_bias[5] = static_cast<float>(static_cast<double>(g_shadow_viewport[2]) * -0.5);
    scale_bias[10] = std::bit_cast<float>(0x4B7FFFFFu); // 16777215
    scale_bias[12] = static_cast<float>((static_cast<double>(g_shadow_viewport[1]) + 1.0) * 0.5);
    scale_bias[13] = static_cast<float>((static_cast<double>(g_shadow_viewport[0]) + 1.0) * 0.5);
    scale_bias[15] = 1.0f;
    float m[16];
    if (first) {
        std::memcpy(m, first, sizeof(m));
        matrix_multiply(m, m, a);
    } else {
        std::memcpy(m, a, sizeof(m));
    }
    matrix_multiply(m, m, b);
    matrix_multiply(out, m, scale_bias);
}

// 0x004DB9D0
void draw_shadow_volumes(const void* scene, int /*viewport*/)
{
    void* vertex_colour = g_pixel_shaders[kPixelShader94c];
    set_pixel_shader(vertex_colour, nullptr);
    pixel_shader_locked() = reinterpret_cast<uint32_t>(vertex_colour);
    set_texture(0, nullptr);
    change_phase(kZWrite, 0);
    change_phase(kZTest, 1);
    change_phase(kPhaseAlphaTest, 0);
    if (!two_sided_stencil())
        input_port()->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
    const auto* list = static_cast<const SceneRef*>(scene);
    MeshRef* const* meshes = list->meshes;
    for (int i = 0; i < list->mesh_count_; ++i) {
        MeshRef* mesh = meshes[i];
        uint32_t flags = mesh->flags;
        if (!(flags & 0x40) || !(flags & 0x200))
            continue;
        if (!sphere_in_view(reinterpret_cast<const maths::Vec4*>(mesh->centre), mesh->radius))
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
    pixel_shader_locked() = 0;
    change_phase(kZWrite, 1);
    change_phase(kCullMode, D3DCULL_CW);
    input_port()->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
}

// 0x004DBBD0
void render_shadow_textures()
{
    if (omit_from_draw())
        return;
    float saved_view[16], saved_projection[16];
    std::memcpy(saved_view, view_matrix(), sizeof(saved_view));
    std::memcpy(saved_projection, projection_matrix(), sizeof(saved_projection));
    IDirect3DSurface9* render_target = nullptr;
    IDirect3DSurface9* depth = nullptr;
    uint32_t saved_fog = get_fog();
    input_port()->GetRenderTarget(0, &render_target);
    input_port()->GetDepthStencilSurface(&depth);
    if (get_fog()) {
        get_fog() = 0;
        render::set_render_state(D3DRS_FOGENABLE, FALSE);
    }
    for (ShadowCaster* caster = first_caster(); caster; caster = next_caster()) {
        if (!caster->model)
            continue;
        input_port()->SetRenderTarget(0, *reinterpret_cast<IDirect3DSurface9**>(caster->target + 0x18));
        input_port()->SetDepthStencilSurface(nullptr);
        input_port()->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        std::memcpy(view_matrix(), caster->view, sizeof(caster->view));
        std::memcpy(projection_matrix(), caster->projection, sizeof(caster->projection));
        custom_transforms() = 1;
        input_port()->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(caster->view));
        input_port()->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(caster->projection));
        float identity[16];
        get_identity(identity);
        input_port()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(identity));
        auto draw_model = [](void* model) {
            int count = get_mesh_count(model);
            for (int i = 0; i < count; ++i) {
                // (the geom's instance: drawn when active, its mesh list flagged as a shadow meanwhile)
                Singleton* instance = static_cast<PcGeom*>(get_mesh(model, i))->instance;
                if (!instance->active)
                    continue;
                instance->scene->flags |= 0x80; // (drawn as a shadow)
                draw_mesh(instance, 8);
                instance->scene->flags &= ~0x80u;
                instance->scene->flags |= 0x200;
            }
        };
        draw_model(caster->model);
        // NOT ORIGINAL (ours: set_shadow_companion) - a model drawn into this caster's shadow as well.
        if (g_shadow_companion) {
            if (void* companion = g_shadow_companion(caster->model))
                draw_model(companion);
        }
    }
    input_port()->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    if (saved_fog != get_fog()) {
        get_fog() = saved_fog;
        input_port()->SetRenderState(D3DRS_FOGENABLE, saved_fog != 0);
    }
    pixel_shader_locked() = 0;
    std::memcpy(view_matrix(), saved_view, sizeof(saved_view));
    std::memcpy(projection_matrix(), saved_projection, sizeof(saved_projection));
    custom_transforms() = 0;
    input_port()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(world_matrix()));
    input_port()->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(view_matrix()));
    input_port()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    input_port()->SetRenderTarget(0, render_target);
    input_port()->SetDepthStencilSurface(depth);
    if (render_target)
        render_target->Release();
    if (depth)
        depth->Release();
}

// 0x004DBF50
void draw_shadows(const void* /*unused*/, void* const* meshes, int count)
{
    if (omit_from_draw())
        return;
    DWORD saved_min_filter;
    input_port()->GetSamplerState(0, D3DSAMP_MINFILTER, &saved_min_filter);
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    input_port()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    input_port()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    uint32_t saved_fog = get_fog();
    if (get_fog()) {
        get_fog() = 0;
        render::set_render_state(D3DRS_FOGENABLE, FALSE);
    }
    for (ShadowCaster* caster = first_caster(); caster; caster = next_caster()) {
        float saved_view[16], saved_projection[16];
        std::memcpy(saved_view, view_matrix(), sizeof(saved_view));
        std::memcpy(saved_projection, projection_matrix(), sizeof(saved_projection));
        // The receivers' vertices -> shadow texture coordinates.
        float texture_bias[16] = {0.5f, 0, 0, 0, 0, -0.5f, 0, 0, 0, 0, 1.0f, 0, 0.5f, 0.5f, 0, 1.0f};
        float view_projection[16], to_texture[16];
        matrix_multiply(view_projection, caster->view, caster->near_projection_);
        matrix_multiply(to_texture, view_projection, texture_bias);
        shadow_texture_matrix(caster->view, caster->projection, caster->texture_matrix_, nullptr);
        set_pixel_shader(nullptr, nullptr);
        input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        input_port()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        input_port()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        input_port()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        input_port()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFA0A0A0); // the shadow's colour
        change_phase(kPhaseAlphaTest, 1);
        set_texture(0, nullptr);
        set_texture(2, nullptr);
        set_texture(3, nullptr);
        set_texture(1, nullptr);
        set_texture(0, *reinterpret_cast<void**>(caster->target + 0x10));
        change_phase(kAddressMode0, 0x20002); // border
        if (g_border_addressing_supported)
            input_port()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
        input_port()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        input_port()->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(projection_matrix()));
        input_port()->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(view_matrix()));
        float identity[16];
        get_identity(identity);
        input_port()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(identity));
        choose_blend_mode(0xF);
        std::memcpy(view_matrix(), caster->view, sizeof(caster->view));
        std::memcpy(projection_matrix(), caster->projection, sizeof(caster->projection));
        custom_transforms() = 1;
        if (depth_bias() != 0xD) { // (the cache's depth bias 13, its float sent directly)
            depth_bias() = 0xD;
            input_port()->SetRenderState(D3DRS_DEPTHBIAS, 0xB7DA1A93);
        }
        for (int i = 0; i < count; ++i) {
            auto* mesh = static_cast<const MeshRef*>(meshes[i]);
            if (mesh->flags & 2)
                continue;
            if (!sphere_in_view(reinterpret_cast<const maths::Vec4*>(mesh->centre), mesh->radius))
                continue;
            set_vertex_format(D3DFVF_XYZ | D3DFVF_TEX1);
            project_shadow_vertices(mesh->positions, mesh->position_count_, to_texture);
            int triangles = clip_shadow_triangles(mesh->triangles, mesh->triangle_count_);
            if (triangles) {
                input_port()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, shadow_triangles(),
                                              sizeof(ShadowCornerPoint));
                ++g_draw_calls; // draws
                g_drawn_triangles += triangles;
            }
        }
        std::memcpy(view_matrix(), saved_view, sizeof(saved_view));
        std::memcpy(projection_matrix(), saved_projection, sizeof(saved_projection));
        custom_transforms() = 0;
    }
    set_pixel_shader(nullptr, nullptr);
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, saved_min_filter);
    if (saved_fog != get_fog()) {
        get_fog() = saved_fog;
        input_port()->SetRenderState(D3DRS_FOGENABLE, saved_fog != 0);
    }
    if (depth_bias() != 0) {
        depth_bias() = 0;
        input_port()->SetRenderState(D3DRS_DEPTHBIAS, 0x80000000); // -0
    }
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    input_port()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    input_port()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    input_port()->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    input_port()->SetRenderState(D3DRS_ZENABLE, TRUE);
    set_texture(0, nullptr);
}

// 0x004DB0A0
void remove_shadow_caster(uint32_t key)
{
    ShadowCaster* caster = find_caster(key);
    if (!caster)
        return;
    get_casters()->erase_entry(key);
    memory::Hub::sole()->dealloc(caster);
}

// 0x004DB100
void aim_shadow_caster(uint32_t key, const float* position, const float* target)
{
    ShadowCaster* caster = find_caster(key);
    if (!caster)
        return;
    // Up is y, or z when looking straight up or down.
    float up[3] = {0.0f, 1.0f, 0.0f};
    if (position[0] == target[0] && position[2] == target[2]) {
        up[1] = 0.0f;
        up[2] = 1.0f;
    }
    matrix_look_at(caster->view, position, target, up);
    matrix_ortho(caster->projection, 120.0f, 120.0f, 5.0f, 500.0f);
    matrix_ortho(caster->near_projection_, 120.0f, 120.0f, 80.0f, 500.0f);
}

// 0x004DDC90
void add_shadow_caster(uint8_t* target, void* model, uint32_t unknown)
{
    auto* caster = static_cast<ShadowCaster*>(memory::alloc(sizeof(ShadowCaster)));
    caster->unknown8 = unknown;
    caster->model = model;
    caster->target = target;
    get_identity(caster->view);
    get_identity(caster->projection);
    auto key = reinterpret_cast<uint32_t>(target);
    get_casters()->store_entry(key, caster);
}

// NOT ORIGINAL
void set_shadow_companion(ShadowCompanionFn fn) { g_shadow_companion = fn; }

} // namespace render
