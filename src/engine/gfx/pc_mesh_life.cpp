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

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }

// The game's C runtime heap (what the buffers' copies and nodes come from).
void* alloc_block(size_t size) { return crt::alloc_block(size); }
void dealloc(void* memory) { crt::dealloc(memory); }

// The buffers' lists' nodes (as pc_buffers.cpp's): the buffer, the previous and the next node.
struct BufferNode {
    void* buffer;
    BufferNode* prev;
    BufferNode* next;
};
BufferNode* vertex_buffer_list() { return reinterpret_cast<BufferNode*>(g_vertex_buffer_list); }
BufferNode* index_buffer_list() { return reinterpret_cast<BufferNode*>(g_index_buffer_list); }

// The engine's scenes (a memory::ListNode list: data the scene; the head's data is -1) and a scene's mesh list
// (+0x280), whose arena vertex buffer is its +0x7C and the buffer's size its +0x80.
memory::ListNode* first_scene_node_bytes() { return render::first_scene_node(); }
memory::ListNode* next_scene_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next;
    return next->is_head() ? nullptr : next;
}
SceneRef* scene_meshes(memory::ListNode* node)
{
    auto* scene = static_cast<uint8_t*>(node->data);
    return scene ? *reinterpret_cast<SceneRef**>(scene + 0x280) : nullptr;
}

IDirect3DVertexBuffer9*& arena_buffer(SceneRef* meshes)
{
    return reinterpret_cast<IDirect3DVertexBuffer9*&>(meshes->com_object_);
}

} // namespace

// 0x004D4BE0
MeshRef::MeshRef()
{
    flags = 0;
    vertex_buffer_count_ = 1;
    current_vertex_buffer_ = 0;
    for (int i = 0; i < 3; ++i) {
        extra_buffers_[i] = nullptr;
        vertex_buffers_[i] = nullptr;
    }
    for (int i = 0; i < 8; ++i) {
        index_buffers_[i] = nullptr;
        index_counts_[i] = 0;
    }
    last_buffer_ = nullptr;
    per_vertex_bytes_ = nullptr;
    unknown48 = nullptr;
    billboard = nullptr;
    transform = nullptr;
    unknown1c_[0] = 0;
    unknown1c_[2] = 0;
    unknown1c_[1] = 0;
    stride = 0;
    fvf = 0;
    pixel_shader_ = nullptr;
    position_count_ = 0;
    positions = nullptr;
    triangle_count_ = 0;
    triangles = nullptr;
    per_vertex_data_ = nullptr;
    unknowna0_ = 0;
    unknown1c_[3] = 0xFF;
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
    VertexBuffer* vb = vertex_buffers_[current_vertex_buffer_];
    uint8_t* data = vb->data; // (locked whole, inline)
    vb->lock_size_ = vb->size;
    vb->lock_offset_ = 0;
    vb->lock_flags_ = 0;
    uint8_t* out = data + unknown1c_[0];
    const uint32_t* colors = reinterpret_cast<MaterialRef*>(material)->anim_colors_;
    for (uint32_t i = 0; i < vertex_count_; ++i) {
        int index = static_cast<int8_t>(per_vertex_bytes_[i]);
        if (index)
            *reinterpret_cast<uint32_t*>(out) = colors[index - 1];
        out += stride;
    }
    return vertex_buffers_[current_vertex_buffer_]->get_unlock();
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
VertexBuffer* new_vertex_buffer(uint32_t bytes, uint32_t usage, uint32_t flags)
{
    void* memory = alloc_block(sizeof(VertexBuffer));
    return memory ? new (memory) VertexBuffer(bytes, usage, flags) : nullptr;
}

// 0x004D57D0
void MeshRef::clean_strip()
{
    uint16_t* strip = index_buffers_[0]->indices;
    uint32_t a = strip[0], b = strip[1];
    uint32_t run = 0; // degenerate triangles in a row
    for (uint32_t i = 2; i < index_counts_[0]; ++i) {
        uint32_t c = strip[i];
        if (a == b || a == c || b == c) {
            ++run;
        } else {
            if (run > 5) { // (a long run of degenerates: cut to the four or five a strip join needs)
                uint32_t start = i - run;
                uint16_t* from = &strip[i - 3];
                *from = strip[i - 2];
                uint16_t last = strip[start - 1];
                strip[start] = last;
                uint32_t kept;
                if (!(run & 1)) {
                    std::memmove(&strip[start + 1], from, (index_counts_[0] - i) * 2 + 6);
                    kept = 4;
                } else {
                    strip[start + 1] = last;
                    std::memmove(&strip[start + 2], from, (index_counts_[0] - i) * 2 + 6);
                    kept = 5;
                }
                i = start + kept;
                index_counts_[0] = static_cast<uint16_t>(index_counts_[0] + kept - run);
            }
            run = 0;
        }
        a = b;
        b = c;
    }
    index_buffers_[0]->get_unlock();
}

// 0x004D5900
void MeshRef::change_bound_sphere(const float* centre_in, float radius_in, int, int)
{
    centre[0] = centre_in[0];
    centre[1] = centre_in[1];
    centre[2] = centre_in[2];
    radius = radius_in;
}

// 0x004D5C30
void MeshRef::create_vertex_buffer(const void* vertices, uint32_t bytes, VertexArena* arena)
{
    if (arena)
        buffer_flags_ |= 4; // (a share of the arena's buffer)
    vertex_buffers_[0] = new_vertex_buffer(bytes, buffer_usage_, buffer_flags_);
    if (arena)
        arena->size += (bytes + 0x1F) & ~0x1Fu;
    VertexBuffer* vb = vertex_buffers_[0];
    uint8_t* data = vb->data; // (locked whole, inline)
    vb->lock_offset_ = 0;
    vb->lock_size_ = vb->size;
    vb->lock_flags_ = 0;
    std::memcpy(data, vertices, bytes);
    vertex_buffers_[0]->get_unlock();
}

// 0x004D6720
uint32_t VertexBuffer::teardown()
{
    uint32_t references = static_cast<IDirect3DVertexBuffer9*>(buffer)->Release();
    buffer = nullptr;
    unlist();
    if (data)
        dealloc(data);
    std::memset(this, 0, sizeof(*this));
    dealloc(this);
    return references;
}

// 0x004D6790
uint32_t IdxStore::teardown()
{
    uint32_t references = static_cast<IDirect3DIndexBuffer9*>(buffer)->Release();
    buffer = nullptr;
    unlist();
    if (indices)
        dealloc(indices);
    std::memset(this, 0, sizeof(*this));
    dealloc(this);
    return references;
}

// 0x004D67F0
void release_buffers()
{
    input_port()->SetStreamSource(0, nullptr, 0, 0);
    input_port()->SetIndices(nullptr);
    for (BufferNode* node = vertex_buffer_list(); node; node = node->next) {
        auto* vb = static_cast<VertexBuffer*>(node->buffer);
        if (!vb)
            continue;
        void* buffer = vb->buffer;
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
    for (BufferNode* node = index_buffer_list(); node; node = node->next) {
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
            IDirect3DVertexBuffer9* old = arena_buffer(meshes);
            input_port()->CreateVertexBuffer(meshes->unknown80, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                             &arena_buffer(meshes), nullptr);
            for (BufferNode* vb_node = vertex_buffer_list(); vb_node; vb_node = vb_node->next) {
                auto* vb = static_cast<VertexBuffer*>(vb_node->buffer);
                if (vb && vb->buffer == old)
                    vb->buffer = arena_buffer(meshes); // (the shares moved to the new arena buffer)
            }
        }
        node = next;
    }
    for (BufferNode* node = vertex_buffer_list(); node; node = node->next)
        if (node->buffer)
            static_cast<VertexBuffer*>(node->buffer)->recreate();
    for (BufferNode* node = index_buffer_list(); node; node = node->next) {
        auto* ib = static_cast<IdxStore*>(node->buffer);
        if (!ib)
            continue;
        input_port()->CreateIndexBuffer(ib->size, ib->usage | D3DUSAGE_WRITEONLY, static_cast<D3DFORMAT>(ib->format),
                                        D3DPOOL_DEFAULT, reinterpret_cast<IDirect3DIndexBuffer9**>(&ib->buffer),
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
        dealloc(per_vertex_data_);
        IDirect3DVertexBuffer9* stream = nullptr;
        UINT offset = 0, stream_stride = 0;
        input_port()->GetStreamSource(0, &stream, &offset, &stream_stride);
        if (stream)
            stream->Release(); // (only compared with below)
        for (uint32_t i = 0; i < vertex_buffer_count_; ++i) {
            if (vertex_buffers_[i]) {
                if (stream == vertex_buffers_[i]->buffer)
                    input_port()->SetStreamSource(0, nullptr, 0, 0);
                vertex_buffers_[i]->teardown();
                vertex_buffers_[i] = nullptr;
            }
            if (extra_buffers_[i]) {
                if (stream == extra_buffers_[i]->buffer)
                    input_port()->SetStreamSource(0, nullptr, 0, 0);
                extra_buffers_[i]->teardown(); // (left pointing at it: cleared with the rest below)
            }
        }
        if (last_buffer_) {
            last_buffer_->teardown();
            last_buffer_ = nullptr;
        }
    }
    if (triangles)
        dealloc(triangles);
    if (positions)
        dealloc(positions);
    // NOLINTNEXTLINE(bugprone-undefined-memory-manipulation): the original's whole-object clear / copy
    std::memset(this, 0, sizeof(*this));
}

} // namespace render
