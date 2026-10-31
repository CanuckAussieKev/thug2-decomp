// The PC vertex and index buffers (sub_mesh.h): made, listed, locked and unlocked through their copies in memory, made
// again after a device reset; a mesh's buffers by index, its move into a vertex arena, its coloured pixel shader; the
// skinned meshes' scaling while they load; the pixel shaders by a file's index.

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"

namespace render {

uint8_t g_skin_scaling_on;
void* g_vertex_buffer_list; // the first node of each list (SBufferNode)
void* g_index_buffer_list;
uint32_t g_skin_settings[5];
uint8_t g_saved_pixel_shader_constants[64]; // (4 passes of 16 bytes: the adapter identifier follows)

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }

// The game's C runtime heap (the nodes and copies are freed by code that is still the game's).
void* alloc_block(size_t size) { return crt::alloc_block(size); }
void dealloc(void* memory) { crt::dealloc(memory); }

// A node of the buffers' lists: the buffer, the previous and the next node.
struct BufferNode {
    void* buffer;
    BufferNode* prev;
    BufferNode* next;
};
BufferNode*& vertex_buffers() { return reinterpret_cast<BufferNode*&>(g_vertex_buffer_list); }
BufferNode*& index_buffers() { return reinterpret_cast<BufferNode*&>(g_index_buffer_list); }

void enlist(BufferNode*& head, void* buffer)
{
    auto* node = static_cast<BufferNode*>(alloc_block(sizeof(BufferNode)));
    node->buffer = buffer;
    node->next = head;
    node->prev = nullptr;
    BufferNode* old = head;
    head = node;
    if (old)
        old->prev = node;
}

void unlist(BufferNode*& head, const void* buffer)
{
    BufferNode* node = head;
    if (!node)
        return;
    while (node->buffer != buffer) {
        node = node->next;
        if (!node)
            return;
    }
    if (node->prev)
        node->prev->next = node->next;
    else
        head = node->next;
    if (node->next)
        node->next->prev = node->prev;
    node->buffer = nullptr;
    node->prev = nullptr;
    node->next = nullptr;
    dealloc(node);
}

IDirect3DVertexBuffer9* d_3d(const VertexBuffer* vb) { return static_cast<IDirect3DVertexBuffer9*>(vb->buffer); }
IDirect3DIndexBuffer9* d_3d(const IdxStore* ib) { return static_cast<IDirect3DIndexBuffer9*>(ib->buffer); }

constexpr uint32_t kNoCopy = 1;
constexpr uint32_t kShared = 4;

} // namespace

// 0x004D3FF0
VertexBuffer* VertexBuffer::make_new(uint32_t bytes, uint32_t usage_flags, uint32_t buffer_flags)
{
    buffer = nullptr;
    if (!(buffer_flags & kShared))
        input_port()->CreateVertexBuffer(bytes, usage_flags, 0, D3DPOOL_DEFAULT,
                                         reinterpret_cast<IDirect3DVertexBuffer9**>(&buffer), nullptr);
    size = bytes;
    usage = usage_flags;
    data = (buffer_flags & kNoCopy) ? nullptr : static_cast<uint8_t*>(alloc_block(bytes));
    flags = buffer_flags;
    arena_offset_ = 0;
    lock_flags_ = 0xFFFFFFFF;
    enlist(vertex_buffers(), this);
    return this;
}

// 0x004D3BE0
void VertexBuffer::unlist() { render::unlist(vertex_buffers(), this); }

// 0x004D4090
long VertexBuffer::get_lock(uint32_t offset, uint32_t bytes, uint8_t** out, uint32_t flags_for_lock)
{
    *out = data + offset;
    lock_offset_ = offset;
    lock_size_ = bytes ? bytes : size;
    lock_flags_ = flags_for_lock;
    if (flags_for_lock & D3DLOCK_DISCARD)
        return d_3d(this)->Lock(offset, bytes, reinterpret_cast<void**>(out), flags_for_lock);
    return 0;
}

// 0x004D40E0
long VertexBuffer::get_unlock()
{
    if ((flags & kShared) && !buffer) {
        lock_flags_ = 0xFFFFFFFF;
        return 0;
    }
    if (!(lock_flags_ & D3DLOCK_READONLY)) {
        if (lock_flags_ & D3DLOCK_DISCARD) {
            lock_flags_ = 0xFFFFFFFF;
            return d_3d(this)->Unlock();
        }
        if (flags & kShared)
            lock_offset_ += arena_offset_;
        void* card;
        if (d_3d(this)->Lock(lock_offset_, lock_size_, &card, lock_flags_) >= 0) {
            std::memcpy(card, data, lock_size_); // (from the copy's start, whatever the offset)
            d_3d(this)->Unlock();
        }
    }
    lock_flags_ = 0xFFFFFFFF;
    return 0;
}

// 0x004D41E0
void VertexBuffer::recreate()
{
    if (flags & kShared)
        d_3d(this)->AddRef();
    else
        input_port()->CreateVertexBuffer(size, usage | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                         reinterpret_cast<IDirect3DVertexBuffer9**>(&buffer), nullptr);
    if (flags & kNoCopy)
        return;
    void* card = nullptr;
    if (d_3d(this)->Lock((flags & kShared) ? arena_offset_ : 0, size, &card, 0) >= 0) {
        std::memcpy(card, data, size);
        d_3d(this)->Unlock();
    }
}

// 0x004D4270
IdxStore* IdxStore::make_new(uint32_t bytes, uint32_t usage_flags, uint32_t index_format)
{
    buffer = nullptr;
    input_port()->CreateIndexBuffer(bytes, usage_flags | D3DUSAGE_WRITEONLY, static_cast<D3DFORMAT>(index_format),
                                    D3DPOOL_DEFAULT, reinterpret_cast<IDirect3DIndexBuffer9**>(&buffer), nullptr);
    size = bytes;
    usage = usage_flags;
    format = index_format;
    indices = static_cast<uint16_t*>(alloc_block(bytes));
    enlist(index_buffers(), this);
    return this;
}

// 0x004D3C40
void IdxStore::unlist() { render::unlist(index_buffers(), this); }

// 0x004D42F0
long IdxStore::get_lock(uint32_t /*offset*/, uint32_t /*bytes*/, uint16_t** out, uint32_t /*lockFlags*/)
{
    *out = indices;
    return 0;
}

// 0x004D4300
long IdxStore::get_unlock()
{
    void* card = nullptr;
    if (d_3d(this)->Lock(0, 0, &card, 0) >= 0) {
        std::memcpy(card, indices, size);
        d_3d(this)->Unlock();
    }
    return 0;
}

// 0x004D3FC0
VertexBuffer* MeshRef::fetch_vertex_store(int index) { return vertex_buffers_[index]; }

// 0x004D3FD0
void MeshRef::set_vertex_buffer(int index, VertexBuffer* vb) { vertex_buffers_[index] = vb; }

// 0x004D3E50
void MeshRef::move_into_arena(VertexArena* arena)
{
    VertexBuffer* vb = vertex_buffers_[0];
    if (vb->buffer)
        return; // (the original adds 0x1F & ~0x1F - nothing - to the arena's offset)
    vb->buffer = arena->buffer;
    vb->arena_offset_ = arena->used;
    std::memcpy(static_cast<uint8_t*>(arena->data) + arena->used, vb->data, vb->size);
    static_cast<IDirect3DVertexBuffer9*>(arena->buffer)->AddRef();
    arena->used += (vb->size + 0x1F) & ~0x1Fu;
}

// 0x004D3D10
void MeshRef::set_pixel_shader_colored()
{
    uint8_t* mat = material;
    uint32_t bytes = *reinterpret_cast<uint32_t*>(mat + 8) << 4; // 16 a pass
    auto* constants = g_ps_constant_bank;
    auto* saved = g_saved_pixel_shader_constants;
    std::memcpy(saved, constants, bytes);
    double r, g, b;
    if (g_instance_colour_on) {
        r = g_instance_colour[0] * static_cast<double>(1.0f / 128);
        g = g_instance_colour[1] * static_cast<double>(1.0f / 128);
        b = g_instance_colour[2] * static_cast<double>(1.0f / 128);
    } else {
        r = static_cast<double>(color[0]) + color[0];
        g = static_cast<double>(color[1]) + color[1];
        b = static_cast<double>(color[2]) + color[2];
    }
    for (uint32_t pass = 0; pass < *reinterpret_cast<uint32_t*>(material + 8); ++pass) {
        if (static_cast<int8_t>(material[0x20 + 4 * pass]) < 0)
            continue;
        float* c = constants + 4 * pass;
        c[0] = static_cast<float>(r * c[0]);
        c[1] = static_cast<float>(g * c[1]);
        c[2] = static_cast<float>(b * c[2]);
    }
    g_pixel_shader_changed = 1; // (the constants changed)
    set_pixel_shader_with_constants(*reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(this) + 0x44),
                                    *reinterpret_cast<uint32_t*>(material + 8));
    std::memcpy(constants, saved, bytes);
    g_pixel_shader_changed = 1;
}

// 0x004D3CA0
void __cdecl set_skin_scaling(const uint32_t* settings)
{
    g_skin_scaling_on = 1;
    g_skin_settings[1] = settings[1];
    g_skin_settings[0] = settings[0];
    g_skin_settings[3] = settings[3];
    g_skin_settings[2] = settings[2];
    g_skin_settings[4] = 0;
}

// 0x004D3CE0
void __cdecl clear_skin_scaling()
{
    g_skin_scaling_on = 0;
    g_skin_settings[1] = 0;
    g_skin_settings[0] = 0;
    g_skin_settings[3] = 0;
    g_skin_settings[2] = 0;
    g_skin_settings[4] = 0;
}

// 0x004D3ED0
void* __stdcall pixel_shader_from_index(uint32_t index)
{
    // (kPixelShader948 is skipped: no index names it)
    static const int kShaders[0x13] = {-1,
                                       -1,
                                       kPixelShader964,
                                       kPixelShader960,
                                       kPixelShader95c,
                                       kPixelShader958,
                                       kPixelShader954,
                                       kPixelShader950,
                                       kPixelShader94c,
                                       kPixelShader944,
                                       kPixelShader940,
                                       kPixelShader93c,
                                       kPixelShader938,
                                       kPixelShader934,
                                       kPixelShader930,
                                       kPixelShader92c,
                                       kPixelShader928,
                                       kPixelShader924,
                                       kPixelShader920};
    if (index > 0x12)
        return reinterpret_cast<void*>(index); // (the original returns the index itself)
    return kShaders[index] >= 0 ? g_pixel_shaders[kShaders[index]] : nullptr;
}

} // namespace render
