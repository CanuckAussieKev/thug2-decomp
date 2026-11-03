// The PC meshes' drawing (sub_mesh.h): skinned on the CPU (one, two or three bones a vertex, into the vertex buffer) and
// drawn - its material set up for the pixel shaders or pass by pass through the fixed-function stages, its level of
// detail picked by distance, fog from three quarters of the way to the far plane.

#include <cstddef>
#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <array>
#include <span>

#include "engine/gfx/device.h"
#include "engine/maths/mat4.h"
#include "gpu/vertex_buffers.h"
#include "platform/d3dx.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/pc_fog.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/core/checked_index.h"
#include "gpu/vertex_view.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"
#include "engine/gfx/mesh_pass.h"
#include "engine/gfx/engine_parts.h"
#include "engine/maths/vec4.h"
#include <bit>

namespace render {

constexpr std::size_t kUvFloats = 8;
constexpr std::size_t kBlendedFloats = 6; // position and normal
constexpr int kColourOffset = 0x18;
constexpr int kUvOffset = 0x1C;
constexpr float kNearFadeShare = 0.25F;
constexpr int kFlagForceLinear = 0x80;
constexpr int kFlagKeepZWrite = 0x100;
constexpr int kLodLevels = 8;
constexpr std::size_t kLodLevelCount = 8;
constexpr int kByteMax = 255;

namespace {

// D3DX (the statically linked library's, through its thunks).
void transform_coord(float* out, const float* v, const maths::Mat4& m) { d3dx::vec3_transform_coord(out, v, &m); }
void transform_normal(float* out, const float* v, const maths::Mat4& m) { d3dx::vec3_transform_normal(out, v, &m); }
void matrix_multiply(void* out, const void* a, const void* b) { d3dx::matrix_multiply(out, a, b); }

// A vertex's skinning data (0x4C bytes): its position, normal, colour, texture coordinates, bones and weights.
struct SkinVertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    uint32_t color;   // +0x18
    std::array<float, kUvFloats> uv;      // +0x1C: (u, v) a set
    std::array<uint8_t, 4> bones; // +0x3C
    std::array<float, 3> weights; // +0x40
};
constexpr std::size_t kSkinVertexBytes = 0x4C;

static_assert(sizeof(SkinVertex) == kSkinVertexBytes, "layout must match the original");

const maths::Mat4& skin_matrix(uint8_t bone) { return core::at(bone_palette(), bone); }

// A skinned vertex's bone and its weight.
struct BoneWeight {
    uint8_t bone{};
    float weight{};
};

// Another bone's share added: its transformed position and normal times the weight, onto what's there (each sum
// rounded to a float as the original stores it).
void add_bone(std::span<float, kBlendedFloats> blended, const SkinVertex& v, BoneWeight bone_weight)
{
    const auto bone = bone_weight.bone;
    const auto weight = bone_weight.weight;
    std::array<float, 3> other{};
    transform_coord(other.data(), v.position.data(), skin_matrix(bone));
    for (int k = 0; k < 3; ++k)
        core::at(blended, k) =
            static_cast<float>((static_cast<double>(core::at(other, k)) * weight) + core::at(blended, k));
    transform_normal(other.data(), v.normal.data(), skin_matrix(bone));
    for (int k = 0; k < 3; ++k)
        core::at(blended, 3 + k) =
            static_cast<float>((static_cast<double>(core::at(other, k)) * weight) + core::at(blended, 3 + k));
}

// The vertices skinned with up to `bones` bones each: the first bone's transform, then - unless its weight is
// exactly 1 - scaled by it and the others' added (the third's only when its weight isn't 0).
void get_skin(MeshRef* mesh, uint8_t* out, int bones)
{
    const gpu::VertexView vertices(mesh->per_vertex_data(), sizeof(SkinVertex), mesh->vertex_count()); // (the CPU copy: SkinVertex records)
    const gpu::VertexWriter written(out, mesh->stride(), mesh->vertex_count());
    for (uint32_t i = 0; i < mesh->vertex_count(); ++i) {
        const auto vertex = vertices.value<SkinVertex>(i, 0);
        const SkinVertex* v = &vertex;
        std::array<float, kBlendedFloats> blended{};
        transform_coord(blended.data(), v->position.data(), skin_matrix(core::at(v->bones, 0)));
        transform_normal(std::span(blended).subspan(3).data(), v->normal.data(), skin_matrix(core::at(v->bones, 0)));
        if (bones > 1 && !(core::at(v->weights, 0) == 1.0F)) {
            for (float& value : blended)
                value = static_cast<float>(static_cast<double>(value) * core::at(v->weights, 0));
            add_bone(blended, *v, {.bone = core::at(v->bones, 1), .weight = core::at(v->weights, 1)});
            if (bones > 2 && !(core::at(v->weights, 2) == 0.0F))
                add_bone(blended, *v, {.bone = core::at(v->bones, 2), .weight = core::at(v->weights, 2)});
        }
        written.set_floats<kBlendedFloats>(i, 0, blended); // the position and normal
        written.set_word(i, kColourOffset, v->color);
        for (uint32_t k = 0; k < 2 * mesh->unknowna4(); ++k)
            written.set_value(i, kUvOffset + (4 * k), core::at(v->uv, k));
    }
}

// The index buffers and their counts by level (the original read a level past the eighth on into the fields after
// them: here it stops the game with a report).
IdxStore* index_buffer_at(MeshRef* mesh, int level) { return core::at(mesh->index_buffers(), level); }
uint16_t index_count_at(MeshRef* mesh, int level) { return core::at(mesh->index_counts(), level); }

void count_draw(uint32_t primitives)
{
    renderer().count_draw(primitives);
}

} // namespace

// 0x004D4D70 / 0x004D4E50 / 0x004D5020
void MeshRef::skin_one_bone(uint8_t* out) { get_skin(this, out, 1); }
void MeshRef::skin_two_bones(uint8_t* out) { get_skin(this, out, 2); }
void MeshRef::skin_three_bones(uint8_t* out) { get_skin(this, out, 3); }

// 0x004D5290
void MeshRef::render()
{
    if (engine_parts().suppress_draw() || beyond_clip_distance(this))
        return;
    if (view_setup().distance_clipping() && view_setup().fog_setting()) { // (distance fog)
        const float end = renderer().render_camera().far_z_;
        renderer().set_fog_begin_distance(0.0F);
        renderer().set_fog_end(0.0F);
        auto start = static_cast<float>(
            end - ((static_cast<double>(end) - renderer().render_camera().near_z_) * kNearFadeShare));
        toggle_fog(true);
        renderer().d3d_device()->SetRenderState(D3DRS_FOGSTART, std::bit_cast<DWORD>(start));
        renderer().d3d_device()->SetRenderState(D3DRS_FOGEND, std::bit_cast<DWORD>(end));
        renderer().d3d_device()->SetRenderState(D3DRS_FOGCOLOR, engine_parts().fog_colour());
    }
    if (!core::at(index_counts_, 0))
        return;
    D3DMATRIX world;
    if (unknown98 != -1) { // (one bone: its matrix before the world's)
        renderer().d3d_device()->GetTransform(D3DTS_WORLD, &world);
        D3DMATRIX boned;
        matrix_multiply(&boned, &core::at(bone_palette(), unknown98), &world);
        renderer().d3d_device()->SetTransform(D3DTS_WORLD, &boned);
    }
    apply_vertex_colors();
    set_vertex_format(fvf_);
    auto* mat = material_;
    if (renderer().shaders_available()) {
        mat->hand_in();
        if (!renderer().instance_colour_on() && !(flags_ & 4))
            set_pixel_shader_with_constants(pixel_shader_, mat->pass_count());
        else
            set_pixel_shader_colored();
    } else {
        mat->submit_states();
    }
    DWORD min_filter = 0;
    DWORD z_write = 0;
    if (flags_ & kFlagForceLinear) {
        renderer().d3d_device()->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
        if (min_filter != D3DTEXF_LINEAR)
            renderer().d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    }
    if (flags_ & kFlagKeepZWrite) {
        renderer().d3d_device()->GetRenderState(D3DRS_ZWRITEENABLE, &z_write);
        if (z_write == 1)
            render::set_render_state(D3DRS_ZWRITEENABLE, 0);
    }
    VertexBuffer* vb = core::at(vertex_buffers_, current_vertex_buffer_);
    if (per_vertex_data_ &&
        (unknown9c_ != renderer().frames_rendered() ||
         unknowna0_ != mesh_pass().current_instance())) { // (once a frame per instance)
        unknown9c_ = renderer().frames_rendered();
        unknowna0_ = mesh_pass().current_instance();
        uint8_t* out = vb->data(); // (locked whole with DISCARD, inline: the card's buffer written)
        vb->set_lock_size(vb->size());
        vb->set_lock_offset(0);
        vb->set_lock_flags(D3DLOCK_DISCARD);
        void* locked = out;
        vb->buffer()->Lock(0, 0, &locked, D3DLOCK_DISCARD);
        out = static_cast<uint8_t*>(locked);
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
        default:
            break;
        }
        vb->get_unlock();
    }
    if (vertex_buffer_count_ > 1)
        current_vertex_buffer_ = static_cast<uint8_t>((current_vertex_buffer_ + 1) % vertex_buffer_count_);
    renderer().d3d_device()->SetStreamSource(0, vb->buffer(), vb->arena_offset(), stride_);
    int level = 0;
    if (unknown48_) { // (the levels of detail's distances)
        const maths::Vec4 sphere = sphere_of(centre_, radius_);
        sphere_in_view(&sphere, radius_);
        const float distance = view_setup().last_sphere_distance();
        for (; level < kLodLevels; ++level) {
            if (!core::at(index_buffers_, level)) {
                if (level > 0)
                    --level;
                break;
            }
            if (distance < core::at(std::span<const float, kLodLevelCount>(static_cast<const float*>(unknown48_), kLodLevelCount), level))
                break;
        }
    }
    renderer().d3d_device()->SetIndices(index_buffer_at(this, level)->buffer());
    const uint32_t primitives = index_count_at(this, level) - 2U;
    if (renderer().shaders_available()) {
        count_draw(primitives);
        renderer().d3d_device()->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primitive_type_), 0, 0, vertex_count_, 0,
                                           primitives);
    } else {
        for (uint32_t pass = 0; pass < mat->pass_count(); ++pass) {
            std::array<float, 3> instance{};
            const float* tint = nullptr;
            if (renderer().instance_colour_on()) {
                for (int k = 0; k < 3; ++k)
                    core::at(instance, k) = static_cast<float>(core::at(renderer().instance_colour(), k) *
                                                               static_cast<double>(1.0F / kByteMax));
                tint = instance.data();
            } else if (flags_ & 4) {
                tint = color_.data();
            }
            mat->submit_pass(pass, tint);
            renderer().d3d_device()->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primitive_type_), 0, 0,
                                                          vertex_count_, 0, index_count_at(this, level) - 2U);
            count_draw(index_count_at(this, level) - 2U);
        }
    }
    if ((flags_ & kFlagForceLinear) && min_filter != D3DTEXF_LINEAR)
        renderer().d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, min_filter);
    if ((flags_ & kFlagKeepZWrite) && z_write == 1)
        renderer().d3d_device()->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    if (unknown98 != -1)
        renderer().d3d_device()->SetTransform(D3DTS_WORLD, &world);
    toggle_fog(false);
}

} // namespace render
