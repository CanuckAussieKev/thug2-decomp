// The PC meshes' drawing (sub_mesh.h): skinned on the CPU (one, two or three bones a vertex, into the vertex buffer) and
// drawn - its material set up for the pixel shaders or pass by pass through the fixed-function stages, its level of
// detail picked by distance, fog from three quarters of the way to the far plane.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/gfx/device.h"
#include "engine/gfx/d3dx.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/pc_fog.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/settings.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/core/bits_as.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
bool pixel_shaders() { return g_shaders_available; }

// D3DX (the statically linked library's, through its thunks).
void transform_coord(float* out, const float* v, const float* m) { d3dx::vec3_transform_coord(out, v, m); }
void transform_normal(float* out, const float* v, const float* m) { d3dx::vec3_transform_normal(out, v, m); }
void matrix_multiply(void* out, const void* a, const void* b) { d3dx::matrix_multiply(out, a, b); }

// A vertex's skinning data (0x4C bytes): its position, normal, colour, texture coordinates, bones and weights.
struct SkinVertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    uint32_t color;   // +0x18
    std::array<float, 8> uv;      // +0x1C: (u, v) a set
    std::array<uint8_t, 4> bones; // +0x3C
    std::array<float, 3> weights; // +0x40
};
static_assert(sizeof(SkinVertex) == 0x4C, "layout must match the original");

const float* skin_matrix_ptr(uint8_t bone) { return memory::bits_as<float*>(g_palette_base) + 16 * bone; }

// Another bone's share added: its transformed position and normal times the weight, onto what's there (each sum
// rounded to a float as the original stores it).
void add_bone(float* blended, const SkinVertex& v, uint8_t bone, float weight)
{
    std::array<float, 3> other{};
    transform_coord(other.data(), v.position.data(), skin_matrix_ptr(bone));
    for (int k = 0; k < 3; ++k)
        blended[k] = static_cast<float>(static_cast<double>(core::at(other, k)) * weight + blended[k]);
    transform_normal(other.data(), v.normal.data(), skin_matrix_ptr(bone));
    for (int k = 0; k < 3; ++k)
        blended[3 + k] = static_cast<float>(static_cast<double>(core::at(other, k)) * weight + blended[3 + k]);
}

// The vertices skinned with up to `bones` bones each: the first bone's transform, then - unless its weight is
// exactly 1 - scaled by it and the others' added (the third's only when its weight isn't 0).
void get_skin(MeshRef* mesh, uint8_t* out, int bones)
{
    const auto* v = raw::as<const SkinVertex>(mesh->per_vertex_data_);
    for (uint32_t i = 0; i < mesh->vertex_count_; ++i, ++v) {
        std::array<float, 6> blended{};
        transform_coord(blended.data(), v->position.data(), skin_matrix_ptr(core::at(v->bones, 0)));
        transform_normal(blended.data() + 3, v->normal.data(), skin_matrix_ptr(core::at(v->bones, 0)));
        if (bones > 1 && !(core::at(v->weights, 0) == 1.0f)) {
            for (float& value : blended)
                value = static_cast<float>(static_cast<double>(value) * core::at(v->weights, 0));
            add_bone(blended.data(), *v, core::at(v->bones, 1), core::at(v->weights, 1));
            if (bones > 2 && !(core::at(v->weights, 2) == 0.0f))
                add_bone(blended.data(), *v, core::at(v->bones, 2), core::at(v->weights, 2));
        }
        auto* vertex = raw::as<uint32_t>(out);
        std::memcpy(vertex, blended.data(), sizeof(blended));
        vertex[6] = v->color;
        for (uint32_t k = 0; k < 2 * mesh->unknowna4_; ++k)
            raw::as<float>(vertex)[7 + k] = core::at(v->uv, k);
        out += mesh->stride;
    }
}

// The index buffers and their counts by level (a level past the eighth reads on into the fields after them, as the
// original does).
IdxStore* index_buffer_at(MeshRef* mesh, int level)
{
    return *raw::field_at<IdxStore*>(mesh, 0x54 + 4 * level);
}
uint16_t index_count_at(MeshRef* mesh, int level)
{
    return *raw::field_at<uint16_t>(mesh, 0x24 + 2 * level);
}

void count_draw(uint32_t primitives)
{
    ++g_draw_calls;
    g_drawn_triangles += primitives;
}

} // namespace

// 0x004D4D70 / 0x004D4E50 / 0x004D5020
void MeshRef::skin_one_bone(uint8_t* out) { get_skin(this, out, 1); }
void MeshRef::skin_two_bones(uint8_t* out) { get_skin(this, out, 2); }
void MeshRef::skin_three_bones(uint8_t* out) { get_skin(this, out, 3); }

// 0x004D5290
void MeshRef::render()
{
    if (g_suppress_draw || beyond_clip_distance(this))
        return;
    if (g_distance_clipping && g_fog_setting) { // (distance fog)
        float end = g_camera.far_z_;
        g_fog_begin_distance = 0.0f;
        g_fog_end = 0.0f;
        auto start = static_cast<float>(end - (static_cast<double>(end) - g_camera.near_z_) * 0.25f);
        toggle_fog(true);
        input_port()->SetRenderState(D3DRS_FOGSTART, *raw::as<DWORD>(&start));
        input_port()->SetRenderState(D3DRS_FOGEND, *raw::as<DWORD>(&end));
        input_port()->SetRenderState(D3DRS_FOGCOLOR, g_fog_colour);
    }
    if (!core::at(index_counts_, 0))
        return;
    D3DMATRIX world;
    if (unknown98 != -1) { // (one bone: its matrix before the world's)
        input_port()->GetTransform(D3DTS_WORLD, &world);
        D3DMATRIX boned;
        matrix_multiply(&boned, skin_matrix_ptr(static_cast<uint8_t>(0)) + 16 * unknown98, &world);
        input_port()->SetTransform(D3DTS_WORLD, &boned);
    }
    apply_vertex_colors();
    set_vertex_format(fvf);
    auto* mat = material;
    if (pixel_shaders()) {
        mat->hand_in();
        if (!g_instance_colour_on && !(flags & 4))
            set_pixel_shader_with_constants(pixel_shader_, mat->pass_count_);
        else
            set_pixel_shader_colored();
    } else {
        mat->submit_states();
    }
    DWORD min_filter = 0, z_write = 0;
    if (flags & 0x80) {
        input_port()->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
        if (min_filter != D3DTEXF_LINEAR)
            input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    }
    if (flags & 0x100) {
        input_port()->GetRenderState(D3DRS_ZWRITEENABLE, &z_write);
        if (z_write == 1)
            render::set_render_state(D3DRS_ZWRITEENABLE, 0);
    }
    VertexBuffer* vb = core::at(vertex_buffers_, current_vertex_buffer_);
    if (per_vertex_data_ &&
        (unknown9c_ != g_frames_rendered ||
         unknowna0_ != memory::bits_as<uint32_t>(g_current_instance))) { // (once a frame per instance)
        unknown9c_ = g_frames_rendered;
        unknowna0_ = memory::bits_as<uint32_t>(g_current_instance);
        uint8_t* out = vb->data; // (locked whole with DISCARD, inline: the card's buffer written)
        vb->lock_size_ = vb->size;
        vb->lock_offset_ = 0;
        vb->lock_flags_ = D3DLOCK_DISCARD;
        static_cast<IDirect3DVertexBuffer9*>(vb->buffer)->Lock(0, 0, raw::as<void*>(&out), D3DLOCK_DISCARD);
        // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
        switch (unknowna8_) {
        case 1:
            skin_one_bone(out);
            break;
        case 2:
            skin_two_bones(out);
            break;
        case 3:
            skin_three_bones(out);
            break;
        }
        vb->get_unlock();
    }
    if (vertex_buffer_count_ > 1)
        current_vertex_buffer_ = static_cast<uint8_t>((current_vertex_buffer_ + 1) % vertex_buffer_count_);
    input_port()->SetStreamSource(0, static_cast<IDirect3DVertexBuffer9*>(vb->buffer), vb->arena_offset_, stride);
    int level = 0;
    if (unknown48) { // (the levels of detail's distances)
        sphere_in_view(raw::as<const maths::Vec4>(centre.data()), radius);
        const float distance = last_sphere_distance();
        for (; level < 8; ++level) {
            if (!core::at(index_buffers_, level)) {
                if (level > 0)
                    --level;
                break;
            }
            if (distance < static_cast<const float*>(unknown48)[level])
                break;
        }
    }
    input_port()->SetIndices(static_cast<IDirect3DIndexBuffer9*>(index_buffer_at(this, level)->buffer));
    const uint32_t primitives = index_count_at(this, level) - 2u;
    if (pixel_shaders()) {
        count_draw(primitives);
        input_port()->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primitive_type_), 0, 0, vertex_count_, 0,
                                           primitives);
    } else {
        for (uint32_t pass = 0; pass < mat->pass_count_; ++pass) {
            std::array<float, 3> instance{};
            const float* tint = nullptr;
            if (g_instance_colour_on) {
                for (int k = 0; k < 3; ++k)
                    core::at(instance, k) = static_cast<float>(core::at(g_instance_colour, k) * static_cast<double>(1.0f / 255));
                tint = instance.data();
            } else if (flags & 4) {
                tint = color.data();
            }
            mat->submit_pass(pass, tint);
            input_port()->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primitive_type_), 0, 0, vertex_count_, 0,
                                               index_count_at(this, level) - 2u);
            count_draw(index_count_at(this, level) - 2u);
        }
    }
    if ((flags & 0x80) && min_filter != D3DTEXF_LINEAR)
        input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, min_filter);
    if ((flags & 0x100) && z_write == 1)
        input_port()->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    if (unknown98 != -1)
        input_port()->SetTransform(D3DTS_WORLD, &world);
    toggle_fog(false);
}

} // namespace render
