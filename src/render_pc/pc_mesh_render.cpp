// The PC meshes' drawing (sub_mesh.h): skinned on the CPU (one, two or three bones a vertex, into the vertex buffer) and
// drawn - its material set up for the pixel shaders or pass by pass through the fixed-function stages, its level of
// detail picked by distance, fog from three quarters of the way to the far plane.

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "base/maths/view_transforms.h"
#include "engine/gfx/gpu.h"
#include "render_pc/device.h"
#include "base/maths/mat4.h"
#include "render_pc/vertex_buffers.h"
#include "render_pc/geom.h"
#include "engine/gfx/surface_material.h"
#include "render_pc/sub_mesh.h"
#include "engine/gfx/occlusion.h"
#include "render_pc/pc_fog.h"
#include "engine/gfx/render_camera.h"
#include "render_pc/render_state.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "base/core/checked_index.h"
#include "engine/gfx/vertex_view.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"
#include "render_pc/mesh_pass.h"
#include "render_pc/outline_draw.h"
#include "engine/gfx/engine_parts.h"
#include "base/maths/vec4.h"
#include "render_pc/rt_record.h"
#include "render_pc/rt_relit_world.h"
#include "engine/gfx/rt_cel.h"
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

// The original's D3DX (the statically linked library's, through its thunks): base/maths/view_transforms.h, mat4.h
// (16 floats a matrix: Direct3D's or the game's).
void transform_coord(float* out, const float* v, const maths::Mat4& matrix)
{
    maths::transform_coord(out, v, matrix.data());
}
void transform_normal(float* out, const float* v, const maths::Mat4& matrix)
{
    maths::transform_normal(out, v, matrix.data());
}
void matrix_multiply(void* out, const void* a, const void* b)
{
    maths::matrix_multiply(static_cast<float*>(out), static_cast<const float*>(a), static_cast<const float*>(b));
}

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

// Not original - a mesh's draws' key for motion vectors (Gpu::MotionSource): the mesh and the instance it is drawn
// for (none: the level's own meshes, which don't move - key 0).
uint64_t motion_key(const void* mesh, const void* instance)
{
    if (instance == nullptr) {
        return 0;
    }
    constexpr uint64_t kMix = 0x9E3779B97F4A7C15ULL;
    return (std::bit_cast<uintptr_t>(mesh) * kMix) ^ std::bit_cast<uintptr_t>(instance);
}

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
    for (int k = 0; k < 3; ++k) {
        core::at(blended, k) =
            static_cast<float>((static_cast<double>(core::at(other, k)) * weight) + core::at(blended, k));
    }
    transform_normal(other.data(), v.normal.data(), skin_matrix(bone));
    for (int k = 0; k < 3; ++k) {
        core::at(blended, 3 + k) =
            static_cast<float>((static_cast<double>(core::at(other, k)) * weight) + core::at(blended, 3 + k));
    }
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
            for (float& value : blended) {
                value = static_cast<float>(static_cast<double>(value) * core::at(v->weights, 0));
            }
            add_bone(blended, *v, {.bone = core::at(v->bones, 1), .weight = core::at(v->weights, 1)});
            if (bones > 2 && !(core::at(v->weights, 2) == 0.0F)) {
                add_bone(blended, *v, {.bone = core::at(v->bones, 2), .weight = core::at(v->weights, 2)});
            }
        }
        written.set_floats<kBlendedFloats>(i, 0, blended); // the position and normal
        written.set_word(i, kColourOffset, v->color);
        for (uint32_t k = 0; k < 2 * mesh->unknowna4(); ++k) {
            written.set_value(i, kUvOffset + (4 * k), core::at(v->uv, k));
        }
    }
}

// The index buffers and their counts by level (the original read a level past the eighth on into the fields after
// them: here it stops the game with a report).
IdxStore* index_buffer_at(MeshRef* mesh, int level) { return mesh->index_store(level); }
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

// The level of detail drawn: the first whose distance the mesh is nearer than (by its sphere's distance from the
// camera), or the last one there is.
int MeshRef::lod_level()
{
    int level = 0;
    if (unknown48_) { // (the levels of detail's distances)
        const maths::Vec4 sphere = sphere_of(centre_, radius_);
        sphere_in_view(&sphere, radius_);
        const float distance = view_setup().last_sphere_distance();
        for (; level < kLodLevels; ++level) {
            if (!core::at(index_buffers_, level)) {
                if (level > 0) {
                    --level;
                }
                break;
            }
            if (distance <
                core::at(std::span<const float, kLodLevelCount>(static_cast<const float*>(unknown48_), kLodLevelCount),
                         level)) {
                break;
            }
        }
    }
    return level;
}

// 0x004D5290
void MeshRef::render()
{
    if (engine_parts().suppress_draw() || beyond_clip_distance(this)) {
        return;
    }
    // Not original - the outlines' mask (render_pc/outline_draw.h): the mesh in a flat colour, without fog or its
    // material. Original behaviour: always drawn with them.
    const bool flat = outline_draw::flat_on();
    if (!flat && view_setup().distance_clipping() && view_setup().fog_setting()) { // (distance fog)
        const float end = renderer().render_camera().far_z_;
        renderer().set_fog_begin_distance(0.0F);
        renderer().set_fog_end(0.0F);
        auto start = static_cast<float>(
            end - ((static_cast<double>(end) - renderer().render_camera().near_z_) * kNearFadeShare));
        toggle_fog(true);
        gpu().set_render_state(GpuRenderState::kFogStart, std::bit_cast<uint32_t>(start));
        gpu().set_render_state(GpuRenderState::kFogEnd, std::bit_cast<uint32_t>(end));
        gpu().set_render_state(GpuRenderState::kFogColour, engine_parts().fog_colour());
    }
    if (core::at(index_counts_, 0) == 0U) {
        return;
    }
    GpuMatrix world;
    if (unknown98 != -1) { // (one bone: its matrix before the world's)
        gpu().get_transform(GpuTransform::kWorld, world.data());
        GpuMatrix boned;
        matrix_multiply(&boned, &core::at(bone_palette(), unknown98), &world);
        gpu().set_transform(GpuTransform::kWorld, boned.data());
    }
    apply_vertex_colors();
    set_vertex_format(fvf_);
    auto* mat = material_;
    if (flat) {
        outline_draw::flat_states();
    } else if (renderer().shaders_available()) {
        mat->hand_in();
        if ((renderer().instance_colour_on() == 0U) && ((flags_ & 4) == 0U)) {
            set_pixel_shader_with_constants(pixel_shader_, mat->pass_count());
        } else {
            set_pixel_shader_colored();
        }
    } else {
        mat->submit_states();
    }
    uint32_t min_filter = 0;
    uint32_t z_write = 0;
    if ((flags_ & kFlagForceLinear) != 0U) {
        gpu().get_sampler_state(0, GpuSamplerState::kMinFilter, &min_filter);
        if (min_filter != gpu_value::kFilterLinear) {
            gpu().set_sampler_state(0, GpuSamplerState::kMinFilter, gpu_value::kFilterLinear);
        }
    }
    if (!flat && (flags_ & kFlagKeepZWrite) != 0U) { // (the mask writes no depth anyway)
        gpu().get_render_state(GpuRenderState::kZWriteEnable, &z_write);
        if (z_write == 1) {
            render::set_render_state(GpuRenderState::kZWriteEnable, 0);
        }
    }
    // (the outlines' mask: a skinned mesh already skinned this frame for this instance - the buffer drawn last again)
    const bool skinned_now = (per_vertex_data_ != nullptr) && unknown9c_ == renderer().frames_rendered() &&
                             unknowna0_ == mesh_pass().current_instance();
    const bool again = flat && skinned_now && vertex_buffer_count_ > 1;
    const size_t drawn_buffer =
        again ? static_cast<size_t>((current_vertex_buffer_ + vertex_buffer_count_ - 1) % vertex_buffer_count_)
              : current_vertex_buffer_;
    VertexBuffer* vb = vertex_store(drawn_buffer);
    // Not original - motion vectors (Gpu::MotionSource; the Vulkan Gpu's upscaler): skinned the frame before into the
    // buffer before this one (the buffers take turns). Original behaviour: nothing.
    const bool skinned_last_frame = per_vertex_data_ != nullptr && vertex_buffer_count_ > 1 &&
                                    unknown9c_ + 1 == renderer().frames_rendered();
    if (per_vertex_data_ &&
        (unknown9c_ != renderer().frames_rendered() ||
         unknowna0_ != mesh_pass().current_instance())) { // (once a frame per instance)
        unknown9c_ = renderer().frames_rendered();
        unknowna0_ = mesh_pass().current_instance();
        uint8_t* out = vb->data(); // (locked whole with DISCARD, inline: the card's buffer written)
        vb->set_lock_size(vb->size());
        vb->set_lock_offset(0);
        vb->set_lock_flags(gpu_value::kLockDiscard);
        void* locked = out;
        gpu().lock_vertex_buffer(vb->buffer(), 0, 0, &locked, gpu_value::kLockDiscard);
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
    if (vertex_buffer_count_ > 1 && !again) {
        current_vertex_buffer_ = static_cast<uint8_t>((current_vertex_buffer_ + 1) % vertex_buffer_count_);
    }
    gpu().set_stream_source(0, vb->buffer(), vb->arena_offset(), stride_);
    {
        Gpu::MotionSource motion{.key = motion_key(this, mesh_pass().current_instance())};
        if (skinned_last_frame) {
            const VertexBuffer* before = vertex_store((drawn_buffer + vertex_buffer_count_ - 1) % vertex_buffer_count_);
            motion.previous_vertices = before->buffer();
            motion.previous_offset = before->arena_offset();
        }
        gpu().set_motion_source(motion);
    }
    // Not original - cel shading (engine/gfx/rt_cel.h): the relit world's stream for this mesh's draws (none: its
    // vertex colour banded). Original behaviour: nothing.
    if (rt::cel_shading_on()) {
        gpu().set_cel_source(
            {.vertices = vb->buffer(), .offset = vb->arena_offset(), .stream = rt_relit_world::cel_stream(vb)});
    }
    const int level = lod_level();
    gpu().set_indices(index_buffer_at(this, level)->buffer());
    const uint32_t primitives = index_count_at(this, level) - 2U;
    rt_record::mesh_drawn(this); // (not original: ray tracing's record - a check's draw count)
    if (flat || renderer().shaders_available()) {
        count_draw(primitives);
        gpu().draw_indexed(static_cast<GpuPrimitive>(primitive_type_), 0, 0, vertex_count_, 0, primitives);
    } else {
        for (uint32_t pass = 0; pass < mat->pass_count(); ++pass) {
            std::array<float, 3> instance{};
            const float* tint = nullptr;
            if (renderer().instance_colour_on() != 0U) {
                for (int k = 0; k < 3; ++k) {
                    core::at(instance, k) = static_cast<float>(core::at(renderer().instance_colour(), k) *
                                                               static_cast<double>(1.0F / kByteMax));
                }
                tint = instance.data();
            } else if ((flags_ & 4) != 0U) {
                tint = color_.data();
            }
            mat->submit_pass(pass, tint);
            gpu().draw_indexed(static_cast<GpuPrimitive>(primitive_type_), 0, 0, vertex_count_, 0,
                               index_count_at(this, level) - 2U);
            count_draw(index_count_at(this, level) - 2U);
        }
    }
    if (((flags_ & kFlagForceLinear) != 0U) && min_filter != gpu_value::kFilterLinear) {
        gpu().set_sampler_state(0, GpuSamplerState::kMinFilter, min_filter);
    }
    if (((flags_ & kFlagKeepZWrite) != 0U) && z_write == 1) {
        gpu().set_render_state(GpuRenderState::kZWriteEnable, 1);
    }
    if (unknown98 != -1) {
        gpu().set_transform(GpuTransform::kWorld, world.data());
    }
    gpu().set_motion_source({}); // (not original: the motion source's draws done)
    toggle_fog(false);
}

} // namespace render
