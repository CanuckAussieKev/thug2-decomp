// The PC meshes' life cycle (sub_mesh.h): constructed, their vertex buffers made, their per-vertex colours applied, their
// strips cleaned, destroyed; the vertex / index buffers destroyed, and all of them let go of and made again around a
// device reset.

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/mem.h"

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }

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
    const auto* scene = static_cast<const PcScene*>(node->data);
    return scene ? scene->meshes : nullptr;
}

IDirect3DVertexBuffer9*& arena_buffer(SceneRef* meshes) { return meshes->arena_buffer_; }

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
    unknown48 = nullptr;
    billboard = nullptr;
    transform = nullptr;
    color_offset_ = 0;
    uv_offset_ = 0;
    normal_offset_ = 0;
    stride = 0;
    fvf = 0;
    pixel_shader_ = nullptr;
    position_count_ = 0;
    positions = nullptr;
    triangle_count_ = 0;
    triangles = nullptr;
    per_vertex_data_ = nullptr;
    unknowna0_ = nullptr;
    bone_ = 0xFF;
    unknown9c_ = -1;
    unknown98 = -1;
    flags |= 0x40;
    viewports = 0xFF;
    primitive_type_ = 5;
}

// 0x004D4CA0
long MeshRef::apply_vertex_colors()
{
    if (!per_vertex_bytes_)
        return 0;
    VertexBuffer* vb = core::at(vertex_buffers_, current_vertex_buffer_);
    uint8_t* data = vb->data; // (locked whole, inline)
    vb->lock_size_ = vb->size;
    vb->lock_offset_ = 0;
    vb->lock_flags_ = 0;
    uint8_t* out = data + color_offset_;
    const uint32_t* colors = material->anim_colors_;
    for (uint32_t i = 0; i < vertex_count_; ++i) {
        const int index = int{static_cast<int8_t>(per_vertex_bytes_[i])};
        if (index)
            std::memcpy(out, &colors[index - 1], sizeof(uint32_t)); // (the vertex's colour word)
        out += stride;
    }
    return core::at(vertex_buffers_, current_vertex_buffer_)->get_unlock();
}

// 0x004D4D30
void MeshRef::fetch_place(float* out) const
{
    if (!transform) {
        out[0] = out[1] = out[2] = 0;
        out[3] = 1.0f;
        return;
    }
    std::memcpy(out, transform + 12, 16);
}

// 0x004D5760
gsl::owner<VertexBuffer*> new_vertex_buffer(uint32_t bytes, uint32_t usage, uint32_t flags)
{
    gsl::owner<void*> memory = alloc_block(sizeof(VertexBuffer));
    return memory ? memory::construct_in<VertexBuffer>(memory, bytes, usage, flags) : nullptr;
}

// 0x004D57D0
void MeshRef::clean_strip()
{
    uint16_t* strip = core::at(index_buffers_, 0)->indices;
    uint32_t a = strip[0], b = strip[1];
    uint32_t run = 0; // degenerate triangles in a row
    for (uint32_t i = 2; i < core::at(index_counts_, 0); ++i) {
        const uint32_t c = strip[i];
        if (a == b || a == c || b == c) {
            ++run;
        } else {
            if (run > 5) { // (a long run of degenerates: cut to the four or five a strip join needs)
                const uint32_t start = i - run;
                uint16_t* from = &strip[i - 3];
                *from = strip[i - 2];
                const uint16_t last = strip[start - 1];
                strip[start] = last;
                uint32_t kept = 0;
                if (!(run & 1)) {
                    std::memmove(&strip[start + 1], from, (core::at(index_counts_, 0) - i) * 2 + 6);
                    kept = 4;
                } else {
                    strip[start + 1] = last;
                    std::memmove(&strip[start + 2], from, (core::at(index_counts_, 0) - i) * 2 + 6);
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
void MeshRef::change_bound_sphere(const float* centre_in, float radius_in)
{
    core::at(centre, 0) = centre_in[0];
    core::at(centre, 1) = centre_in[1];
    core::at(centre, 2) = centre_in[2];
    radius = radius_in;
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
    uint8_t* data = vb->data; // (locked whole, inline)
    vb->lock_offset_ = 0;
    vb->lock_size_ = vb->size;
    vb->lock_flags_ = 0;
    std::memcpy(data, vertices, bytes);
    core::at(vertex_buffers_, 0)->get_unlock();
}



// 0x004D67F0
void release_buffers()
{
    input_port()->SetStreamSource(0, nullptr, 0, 0);
    input_port()->SetIndices(nullptr);
    for (const BufferNode* node = vertex_buffer_list(); node; node = node->next) {
        auto* vb = static_cast<VertexBuffer*>(node->buffer);
        if (!vb)
            continue;
        IDirect3DVertexBuffer9* buffer = vb->buffer;
        if (buffer) {
            static_cast<IDirect3DVertexBuffer9*>(buffer)->Release();
            vb->buffer = nullptr;
        }
        vb->buffer = buffer; // (kept: recreate_buffers matches the shares by it)
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
        if (ib->buffer)
            static_cast<IDirect3DIndexBuffer9*>(ib->buffer)->Release();
        ib->buffer = nullptr;
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
            input_port()->CreateVertexBuffer(meshes->unknown80, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                             &arena_buffer(meshes), nullptr);
            for (const BufferNode* vb_node = vertex_buffer_list(); vb_node; vb_node = vb_node->next) {
                auto* vb = static_cast<VertexBuffer*>(vb_node->buffer);
                if (vb && vb->buffer == old)
                    vb->buffer = arena_buffer(meshes); // (the shares moved to the new arena buffer)
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
        input_port()->CreateIndexBuffer(ib->size, ib->usage | D3DUSAGE_WRITEONLY, static_cast<D3DFORMAT>(ib->format),
                                        D3DPOOL_DEFAULT, &ib->buffer,
                                        nullptr);
        void* card = nullptr;
        if (static_cast<IDirect3DIndexBuffer9*>(ib->buffer)->Lock(0, 0, &card, 0) >= 0) {
            std::memcpy(card, ib->indices, ib->size);
            static_cast<IDirect3DIndexBuffer9*>(ib->buffer)->Unlock();
        }
    }
}

// 0x004D69E0
MeshRef::~MeshRef()
{
    if (flags & 0x10)
        g_billboard_pool.remove_mesh(this);
    memory::Hub* heap = memory::Hub::get_pull();
    if (transform)
        heap->dealloc(transform);
    if (!(flags & 1)) { // (its own buffers, not another mesh's)
        for (IdxStore*& ib : index_buffers_) {
            if (ib)
                ib->teardown();
            ib = nullptr;
        }
        if (per_vertex_bytes_) {
            heap->dealloc(per_vertex_bytes_);
            per_vertex_bytes_ = nullptr;
        }
        if (unknown48) {
            heap->dealloc(unknown48);
            unknown48 = nullptr;
        }
        if (billboard) {
            heap->dealloc(billboard);
            billboard = nullptr;
        }
        dealloc(memory::owned_element(per_vertex_data_));
        IDirect3DVertexBuffer9* stream = nullptr;
        UINT offset = 0, stream_stride = 0;
        input_port()->GetStreamSource(0, &stream, &offset, &stream_stride);
        if (stream)
            stream->Release(); // (only compared with below)
        for (uint32_t i = 0; i < vertex_buffer_count_; ++i) {
            if (core::at(vertex_buffers_, i)) {
                if (stream == core::at(vertex_buffers_, i)->buffer)
                    input_port()->SetStreamSource(0, nullptr, 0, 0);
                core::at(vertex_buffers_, i)->teardown();
                core::at(vertex_buffers_, i) = nullptr;
            }
            if (core::at(extra_buffers_, i)) {
                if (stream == core::at(extra_buffers_, i)->buffer)
                    input_port()->SetStreamSource(0, nullptr, 0, 0);
                core::at(extra_buffers_, i)->teardown(); // (left pointing at it: cleared with the rest below)
            }
        }
        if (last_buffer_) {
            last_buffer_->teardown();
            last_buffer_ = nullptr;
        }
    }
    if (triangles)
        dealloc(memory::owned_element(triangles));
    if (positions)
        dealloc(memory::owned_element(positions));
    // NOLINTNEXTLINE(bugprone-undefined-memory-manipulation): the original's whole-object clear / copy
    std::memset(this, 0, sizeof(*this));
}

} // namespace render
