// The PC meshes' life cycle (sub_mesh.h): constructed, their vertex buffers made, their per-vertex colours applied, their
// strips cleaned, destroyed; the vertex / index buffers destroyed, and all of them let go of and made again around a
// device reset.

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <span>

#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pc_scene.h"
#include "engine/maths/vec4.h"
#include "gpu/vertex_buffers.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "gpu/vertex_view.h"
#include <gsl/pointers>
#include "engine/core/mem.h"
#include "engine/gfx/renderer.h"

namespace render {

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

IDirect3DVertexBuffer9*& arena_buffer(SceneRef* meshes) { return meshes->arena_buffer(); }

} // namespace

// 0x004D4BE0
MeshRef::MeshRef()
{
    for (int i = 0; i < 3; ++i) {
        core::at(extra_buffers_, i) = nullptr;
        core::at(vertex_buffers_, i) = nullptr;
    }
    for (int i = 0; i < 8; ++i) {
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
    bone_ = 0xFF;
    unknown9c_ = -1;
    unknown98 = -1;
    flags_ |= 0x40;
    viewports_ = 0xFF;
    primitive_type_ = 5;
}

// 0x004D4CA0
long MeshRef::apply_vertex_colors()
{
    if (!per_vertex_bytes_)
        return 0;
    VertexBuffer* vb = core::at(vertex_buffers_, current_vertex_buffer_);
    vb->set_lock_size(vb->size());
    vb->set_lock_offset(0);
    vb->set_lock_flags(0);
    const gpu::VertexWriter vertices(vb->data(), stride_, vertex_count_); // (locked whole, inline)
    const std::span<const uint32_t> colors = material_->anim_colour_run();
    const std::span<const uint8_t> per_vertex(per_vertex_bytes_, vertex_count_);
    for (uint32_t i = 0; i < vertex_count_; ++i) {
        const int index = int{static_cast<int8_t>(core::at(per_vertex, i))};
        if (index)
            vertices.set_word(i, color_offset_, core::at(colors, index - 1)); // (the vertex's colour word)
    }
    return core::at(vertex_buffers_, current_vertex_buffer_)->get_unlock();
}

// 0x004D4D30
void MeshRef::fetch_place(maths::Vec4* out) const
{
    if (!transform) {
        out->x = out->y = out->z = 0;
        out->w = 1.0f;
        return;
    }
    const std::span<float, 16> t = transform_floats();
    *out = {.x = core::at(t, 12), .y = core::at(t, 13), .z = core::at(t, 14), .w = core::at(t, 15)}; // (the last row)
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
    uint32_t a = core::at(strip, 0), b = core::at(strip, 1);
    uint32_t run = 0; // degenerate triangles in a row
    for (uint32_t i = 2; i < core::at(index_counts_, 0); ++i) {
        const uint32_t c = core::at(strip, i);
        if (a == b || a == c || b == c) {
            ++run;
        } else {
            if (run > 5) { // (a long run of degenerates: cut to the four or five a strip join needs)
                const uint32_t start = i - run;
                const size_t from = i - 3;
                core::at(strip, from) = core::at(strip, i - 2);
                const uint16_t last = core::at(strip, start - 1);
                core::at(strip, start) = last;
                uint32_t kept = 0;
                const size_t moved = static_cast<size_t>(core::at(index_counts_, 0) - i) + 3; // (indices: bytes / 2)
                if (!(run & 1)) {
                    std::ranges::copy(strip.subspan(from, moved), std::next(strip.begin(), static_cast<std::ptrdiff_t>(start) + 1));
                    kept = 4;
                } else {
                    core::at(strip, start + 1) = last;
                    std::ranges::copy(strip.subspan(from, moved), std::next(strip.begin(), static_cast<std::ptrdiff_t>(start) + 2));
                    kept = 5;
                }
                i = start + kept;
                core::at(index_counts_, 0) = static_cast<uint16_t>(core::at(index_counts_, 0) + kept - run);
            }
            run = 0;
        }
        a = b;
        b = c;
    }
    core::at(index_buffers_, 0)->get_unlock();
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
    if (arena)
        buffer_flags_ |= 4; // (a share of the arena's buffer)
    gsl::owner<VertexBuffer*> made = new_vertex_buffer(bytes, buffer_usage_, buffer_flags_); // (the mesh's)
    core::at(vertex_buffers_, 0) = made;
    if (arena)
        arena->size += (bytes + 0x1F) & ~0x1Fu;
    VertexBuffer* vb = core::at(vertex_buffers_, 0);
    uint8_t* data = vb->data(); // (locked whole, inline)
    vb->set_lock_offset(0);
    vb->set_lock_size(vb->size());
    vb->set_lock_flags(0);
    std::memcpy(data, vertices, bytes);
    core::at(vertex_buffers_, 0)->get_unlock();
}

// 0x004D67F0
void release_buffers()
{
    renderer().d3d_device()->SetStreamSource(0, nullptr, 0, 0);
    renderer().d3d_device()->SetIndices(nullptr);
    for (const BufferNode* node = vertex_buffer_list(); node; node = node->next) {
        auto* vb = static_cast<VertexBuffer*>(node->buffer);
        if (!vb)
            continue;
        IDirect3DVertexBuffer9* buffer = vb->buffer();
        if (buffer) {
            static_cast<IDirect3DVertexBuffer9*>(buffer)->Release();
            vb->set_buffer(nullptr);
        }
        vb->set_buffer(buffer); // (kept: recreate_buffers matches the shares by it)
    }
    for (memory::ListNode* node = first_scene_node_bytes(); node;) {
        memory::ListNode* next = next_scene_node(node);
        if (SceneRef* meshes = scene_meshes(node))
            if (IDirect3DVertexBuffer9* arena = arena_buffer(meshes))
                arena->Release();
        node = next;
    }
    for (const BufferNode* node = index_buffer_list(); node; node = node->next) {
        auto* ib = static_cast<IdxStore*>(node->buffer);
        if (!ib)
            continue;
        if (ib->buffer())
            static_cast<IDirect3DIndexBuffer9*>(ib->buffer())->Release();
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
            const IDirect3DVertexBuffer9* old = arena_buffer(meshes);
            renderer().d3d_device()->CreateVertexBuffer(meshes->unknown80(), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                             &arena_buffer(meshes), nullptr);
            for (const BufferNode* vb_node = vertex_buffer_list(); vb_node; vb_node = vb_node->next) {
                auto* vb = static_cast<VertexBuffer*>(vb_node->buffer);
                if (vb && vb->buffer() == old)
                    vb->set_buffer(arena_buffer(meshes)); // (the shares moved to the new arena buffer)
            }
        }
        node = next;
    }
    for (const BufferNode* node = vertex_buffer_list(); node; node = node->next)
        if (node->buffer)
            static_cast<VertexBuffer*>(node->buffer)->recreate();
    for (const BufferNode* node = index_buffer_list(); node; node = node->next) {
        auto* ib = static_cast<IdxStore*>(node->buffer);
        if (!ib)
            continue;
        renderer().d3d_device()->CreateIndexBuffer(ib->size(), ib->usage() | D3DUSAGE_WRITEONLY, static_cast<D3DFORMAT>(ib->format()),
                                        D3DPOOL_DEFAULT, &ib->buffer(),
                                        nullptr);
        void* card = nullptr;
        if (static_cast<IDirect3DIndexBuffer9*>(ib->buffer())->Lock(0, 0, &card, 0) >= 0) {
            std::memcpy(card, ib->indices(), ib->size());
            static_cast<IDirect3DIndexBuffer9*>(ib->buffer())->Unlock();
        }
    }
}

// 0x004D69E0
MeshRef::~MeshRef()
{
    if (flags_ & 0x10)
        billboard_pool().remove_mesh(this);
    memory::Hub* heap = memory::Hub::get_pull();
    if (transform)
        heap->dealloc(transform);
    if (!(flags_ & 1)) { // (its own buffers, not another mesh's)
        for (IdxStore*& ib : index_buffers_) {
            if (ib)
                ib->teardown();
            ib = nullptr;
        }
        if (per_vertex_bytes_) {
            heap->dealloc(per_vertex_bytes_);
            per_vertex_bytes_ = nullptr;
        }
        if (unknown48_) {
            heap->dealloc(unknown48_);
            unknown48_ = nullptr;
        }
        if (billboard_) {
            heap->dealloc(billboard_);
            billboard_ = nullptr;
        }
        dealloc(memory::owned_element(per_vertex_data_));
        IDirect3DVertexBuffer9* stream = nullptr;
        UINT offset = 0, stream_stride = 0;
        renderer().d3d_device()->GetStreamSource(0, &stream, &offset, &stream_stride);
        if (stream)
            stream->Release(); // (only compared with below)
        for (uint32_t i = 0; i < vertex_buffer_count_; ++i) {
            if (core::at(vertex_buffers_, i)) {
                if (stream == core::at(vertex_buffers_, i)->buffer())
                    renderer().d3d_device()->SetStreamSource(0, nullptr, 0, 0);
                core::at(vertex_buffers_, i)->teardown();
                core::at(vertex_buffers_, i) = nullptr;
            }
            if (core::at(extra_buffers_, i)) {
                if (stream == core::at(extra_buffers_, i)->buffer())
                    renderer().d3d_device()->SetStreamSource(0, nullptr, 0, 0);
                core::at(extra_buffers_, i)->teardown(); // (left pointing at it: cleared with the rest below)
            }
        }
        if (last_buffer_) {
            last_buffer_->teardown();
            last_buffer_ = nullptr;
        }
    }
    if (triangles_)
        dealloc(memory::owned_element(triangles_));
    if (positions_)
        dealloc(memory::owned_element(positions_));
    // NOLINTNEXTLINE(bugprone-undefined-memory-manipulation): the original's whole-object clear / copy
    std::memset(this, 0, sizeof(*this));
}

} // namespace render
