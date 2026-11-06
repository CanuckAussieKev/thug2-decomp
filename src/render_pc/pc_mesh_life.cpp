// The PC meshes' life cycle (sub_mesh.h): constructed, their vertex buffers made, their per-vertex colours applied, their
// strips cleaned, destroyed; the vertex / index buffers destroyed, and all of them let go of and made again around a
// device reset.

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <span>

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/memory/arena_alloc.h"
#include "engine/memory/manager.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/surface_material.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/mesh_list.h"
#include "render_pc/pc_billboards.h"
#include "render_pc/pc_scene.h"
#include "base/maths/vec4.h"
#include "render_pc/vertex_buffers.h"
#include "platform/crt.h"
#include "base/core/checked_index.h"
#include "engine/gfx/vertex_view.h"
#include <gsl/pointers>
#include "engine/memory/mem.h"
#include "base/maths/mat4.h"
#include "base/core/raw_view.h"

namespace render {

constexpr int kMeshViewportCount = 8;
constexpr int kNoBone = 0xFF;
constexpr int kMeshFlagActive = 0x40;
constexpr int kAllViewports = 0xFF;
constexpr int kDefaultPrimitiveType = 5;
constexpr int kMaxDegenerateRun = 5;
constexpr int kArenaAlignSlack = 0x1F;
constexpr unsigned kArenaAlignMask = 0x1FU;
constexpr int kMeshFlagBillboard = 0x10;

namespace {

// The game's C runtime heap (what the buffers' copies and nodes come from).
gsl::owner<void*> alloc_block(size_t size) { return crt::alloc_block(size); }
void dealloc(gsl::owner<void*> memory) { crt::dealloc(memory); }

using BufferNode = buffer_lists::Node;
BufferNode* vertex_buffer_list() { return buffer_lists::vertex_buffers(); }
BufferNode* index_buffer_list() { return buffer_lists::index_buffers(); }

// The engine's scenes (a memory::ListNode list: data the scene; the head's data is -1) and a scene's mesh list
// (+0x280), whose arena vertex buffer is its +0x7C and the buffer's size its +0x80.
memory::ListNode* first_scene_node_bytes() { return render::first_scene_node(); }
SceneRef* scene_meshes(memory::ListNode* node)
{
    const auto* scene = static_cast<const PcScene*>(node->data());
    return scene ? scene->meshes() : nullptr;
}

GpuVertexBuffer*& arena_buffer(SceneRef* meshes) { return meshes->arena_buffer(); }

} // namespace

// 0x004D4BE0
MeshRef::MeshRef()
{
    for (int i = 0; i < 3; ++i) {
        core::at(extra_buffers_, i) = nullptr;
        core::at(vertex_buffers_, i) = nullptr;
    }
    for (int i = 0; i < kMeshViewportCount; ++i) {
        core::at(index_buffers_, i) = nullptr;
        core::at(index_counts_, i) = 0;
    }
    last_buffer_ = nullptr;
    per_vertex_bytes_ = nullptr;
    unknown48_ = nullptr;
    billboard_ = nullptr;
    transform = nullptr;
    color_offset_ = 0;
    uv_offset_ = 0;
    normal_offset_ = 0;
    stride_ = 0;
    fvf_ = 0;
    pixel_shader_ = nullptr;
    position_count_ = 0;
    positions_ = nullptr;
    triangle_count_ = 0;
    triangles_ = nullptr;
    per_vertex_data_ = nullptr;
    unknowna0_ = nullptr;
    bone_ = kNoBone;
    unknown9c_ = -1;
    unknown98 = -1;
    flags_ |= kMeshFlagActive;
    viewports_ = kAllViewports;
    primitive_type_ = kDefaultPrimitiveType;
}

// 0x004D4CA0
long MeshRef::apply_vertex_colors()
{
    if (!per_vertex_bytes_) {
        return 0;
    }
    VertexBuffer* vb = vertex_store(current_vertex_buffer_);
    vb->set_lock_size(vb->size());
    vb->set_lock_offset(0);
    vb->set_lock_flags(0);
    const gpu::VertexWriter vertices(vb->data(), stride_, vertex_count_); // (locked whole, inline)
    const std::span<const uint32_t> colors = material_->anim_colour_run();
    const std::span<const uint8_t> per_vertex(per_vertex_bytes_, vertex_count_);
    for (uint32_t i = 0; i < vertex_count_; ++i) {
        const int index = int{static_cast<int8_t>(core::at(per_vertex, i))};
        if (index != 0) {
            vertices.set_word(i, color_offset_, core::at(colors, index - 1)); // (the vertex's colour word)
        }
    }
    return vertex_store(current_vertex_buffer_)->get_unlock();
}

// 0x004D4D30
void MeshRef::fetch_place(maths::Vec4* out) const
{
    if (!transform) {
        out->x = out->y = out->z = 0;
        out->w = 1.0F;
        return;
    }
    const std::span<float, 16> matrix_floats = transform_floats();
    *out = {.x = core::at(matrix_floats, maths::kMatTx),
            .y = core::at(matrix_floats, maths::kMatTy),
            .z = core::at(matrix_floats, maths::kMatTz),
            .w = core::at(matrix_floats, maths::kMatTw)}; // (the last row)
}

// 0x004D5760
gsl::owner<VertexBuffer*> new_vertex_buffer(uint32_t bytes, uint32_t usage, uint32_t flags)
{
    gsl::owner<void*> memory = alloc_block(sizeof(VertexBuffer));
    return memory ? memory::construct_in<VertexBuffer>(memory, bytes, VertexBuffer::BufferFlags{.usage_flags_ = usage, .buffer_flags_ = flags}) : nullptr;
}

// 0x004D57D0
void MeshRef::clean_strip()
{
    const std::span<uint16_t> strip = core::at(index_buffers_, 0)->index_run();
    uint32_t first = core::at(strip, 0);
    uint32_t second = core::at(strip, 1);
    uint32_t run = 0; // degenerate triangles in a row
    for (uint32_t i = 2; i < core::at(index_counts_, 0); ++i) {
        const uint32_t third = core::at(strip, i);
        if (first == second || first == third || second == third) {
            ++run;
        } else {
            if (run > kMaxDegenerateRun) { // (a long run of degenerates: cut to the four or five a strip join needs)
                const uint32_t start = i - run;
                const size_t from = i - 3;
                core::at(strip, from) = core::at(strip, i - 2);
                const uint16_t last = core::at(strip, start - 1);
                core::at(strip, start) = last;
                uint32_t kept = 0;
                const size_t moved = static_cast<size_t>(core::at(index_counts_, 0) - i) + 3; // (indices: bytes / 2)
                if ((run & 1) == 0U) {
                    std::ranges::copy(strip.subspan(from, moved), std::next(strip.begin(), static_cast<std::ptrdiff_t>(start) + 1));
                    kept = 4;
                } else {
                    core::at(strip, start + 1) = last;
                    std::ranges::copy(strip.subspan(from, moved), std::next(strip.begin(), static_cast<std::ptrdiff_t>(start) + 2));
                    kept = kMaxDegenerateRun;
                }
                i = start + kept;
                core::at(index_counts_, 0) = static_cast<uint16_t>(core::at(index_counts_, 0) + kept - run);
            }
            run = 0;
        }
        first = second;
        second = third;
    }
    index_store(0)->get_unlock();
}

// 0x004D5900
void MeshRef::change_bound_sphere(std::span<const float, 3> centre_in, float radius_in)
{
    core::at(centre_, 0) = core::at(centre_in, 0);
    core::at(centre_, 1) = core::at(centre_in, 1);
    core::at(centre_, 2) = core::at(centre_in, 2);
    radius_ = radius_in;
}

// 0x004D5C30
void MeshRef::create_vertex_buffer(const void* vertices, uint32_t bytes, VertexArena* arena)
{
    if (arena) {
        buffer_flags_ |= 4; // (a share of the arena's buffer)
    }
    gsl::owner<VertexBuffer*> made = new_vertex_buffer(bytes, buffer_usage_, buffer_flags_); // (the mesh's)
    core::at(vertex_buffers_, 0) = made;
    if (arena) {
        arena->size += (bytes + kArenaAlignSlack) & ~kArenaAlignMask;
    }
    VertexBuffer* vb = vertex_store(0);
    uint8_t* data = vb->data(); // (locked whole, inline)
    vb->set_lock_offset(0);
    vb->set_lock_size(vb->size());
    vb->set_lock_flags(0);
    std::memcpy(data, vertices, bytes);
    vertex_store(0)->get_unlock();
}

// 0x004D67F0
void release_buffers()
{
    gpu().set_stream_source(0, nullptr, 0, 0);
    gpu().set_indices(nullptr);
    for (const BufferNode* node = vertex_buffer_list(); node; node = node->next) {
        auto* vb = static_cast<VertexBuffer*>(node->buffer);
        if (!vb) {
            continue;
        }
        GpuVertexBuffer* buffer = vb->buffer();
        if (buffer) {
            gpu().release_vertex_buffer(buffer);
            vb->set_buffer(nullptr);
        }
        vb->set_buffer(buffer); // (kept: recreate_buffers matches the shares by it)
    }
    for (memory::ListNode* node = first_scene_node_bytes(); node;) {
        memory::ListNode* next = next_scene_node(node);
        if (SceneRef* meshes = scene_meshes(node)) {
            if (GpuVertexBuffer* arena = arena_buffer(meshes)) {
                gpu().release_vertex_buffer(arena);
            }
        }
        node = next;
    }
    for (const BufferNode* node = index_buffer_list(); node; node = node->next) {
        auto* ib = static_cast<IdxStore*>(node->buffer);
        if (!ib) {
            continue;
        }
        if (ib->buffer()) {
            gpu().release_index_buffer(ib->buffer());
        }
        ib->buffer() = nullptr;
    }
}

// 0x004D68C0
void recreate_buffers()
{
    for (memory::ListNode* node = first_scene_node_bytes(); node;) {
        memory::ListNode* next = next_scene_node(node);
        SceneRef* meshes = scene_meshes(node);
        if (meshes && arena_buffer(meshes)) {
            const GpuVertexBuffer* old = arena_buffer(meshes);
            gpu().create_vertex_buffer(meshes->unknown80(), gpu_value::kUsageWriteOnly, 0, gpu_value::kPoolDefault,
                                       &arena_buffer(meshes));
            for (const BufferNode* vb_node = vertex_buffer_list(); vb_node; vb_node = vb_node->next) {
                auto* vb = static_cast<VertexBuffer*>(vb_node->buffer);
                if (vb && vb->buffer() == old) {
                    vb->set_buffer(arena_buffer(meshes)); // (the shares moved to the new arena buffer)
                }
            }
        }
        node = next;
    }
    for (const BufferNode* node = vertex_buffer_list(); node; node = node->next) {
        if (node->buffer) {
            static_cast<VertexBuffer*>(node->buffer)->recreate();
        }
    }
    for (const BufferNode* node = index_buffer_list(); node; node = node->next) {
        auto* ib = static_cast<IdxStore*>(node->buffer);
        if (!ib) {
            continue;
        }
        gpu().create_index_buffer(ib->size(), ib->usage() | gpu_value::kUsageWriteOnly, ib->format(),
                                  gpu_value::kPoolDefault, &ib->buffer());
        void* card = nullptr;
        if (gpu().lock_index_buffer(ib->buffer(), 0, 0, &card, 0) >= 0) {
            std::memcpy(card, ib->indices(), ib->size());
            gpu().unlock_index_buffer(ib->buffer());
        }
    }
}

// 0x004D69E0
MeshRef::~MeshRef()
{
    if ((flags_ & kMeshFlagBillboard) != 0U) {
        billboard_pool().remove_mesh(this);
    }
    if (transform) {
        memory::Hub::dealloc(transform);
    }
    if ((flags_ & 1) == 0U) { // (its own buffers, not another mesh's)
        for (IndexBufferData*& ib : index_buffers_) {
            if (ib) {
                static_cast<IdxStore*>(ib)->teardown();
            }
            ib = nullptr;
        }
        if (per_vertex_bytes_) {
            memory::Hub::dealloc(per_vertex_bytes_);
            per_vertex_bytes_ = nullptr;
        }
        if (unknown48_) {
            memory::Hub::dealloc(unknown48_);
            unknown48_ = nullptr;
        }
        if (billboard_) {
            memory::Hub::dealloc(billboard_);
            billboard_ = nullptr;
        }
        dealloc(memory::owned_element(per_vertex_data_));
        GpuVertexBuffer* stream = nullptr;
        uint32_t offset = 0;
        uint32_t stream_stride = 0;
        gpu().get_stream_source(0, &stream, &offset, &stream_stride);
        if (stream) {
            gpu().release_vertex_buffer(stream); // (only compared with below)
        }
        for (uint32_t i = 0; i < vertex_buffer_count_; ++i) {
            if (core::at(vertex_buffers_, i)) {
                if (stream == core::at(vertex_buffers_, i)->buffer()) {
                    gpu().set_stream_source(0, nullptr, 0, 0);
                }
                vertex_store(i)->teardown();
                core::at(vertex_buffers_, i) = nullptr;
            }
            if (core::at(extra_buffers_, i)) {
                if (stream == core::at(extra_buffers_, i)->buffer()) {
                    gpu().set_stream_source(0, nullptr, 0, 0);
                }
                extra_store(i)->teardown(); // (left pointing at it: cleared with the rest below)
            }
        }
        if (last_buffer_) {
            static_cast<VertexBuffer*>(last_buffer_)->teardown();
            last_buffer_ = nullptr;
        }
    }
    if (triangles_) {
        dealloc(memory::owned_element(triangles_));
    }
    if (positions_) {
        dealloc(memory::owned_element(positions_));
    }
    raw::zero_object(this);
}

} // namespace render
