// The GPU's vertex and index buffers: made, listed, locked and unlocked through their copies in memory, made again
// after a device reset, torn down.

#include "gpu/vertex_buffers.h"

#include <windows.h>
#include <span>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gsl/pointers>

#include "engine/core/raw_view.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/core/mem.h"

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }

// The game's C runtime heap (the nodes, copies and buffers are freed through it).
gsl::owner<void*> alloc_block(size_t size) { return crt::alloc_block(size); }
void dealloc(gsl::owner<void*> memory) { crt::dealloc(memory); }

buffer_lists::Node* g_vertex_buffer_list = nullptr; // (the original's 0x007865E4)
buffer_lists::Node* g_index_buffer_list = nullptr;  // (0x007865E8)
buffer_lists::Node*& vertex_buffers() { return g_vertex_buffer_list; }
buffer_lists::Node*& index_buffers() { return g_index_buffer_list; }

using BufferNode = buffer_lists::Node;

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
    dealloc(memory::owned_element(node)); // (the list's)
}

IDirect3DVertexBuffer9* d_3d(const VertexBuffer* vb) { return static_cast<IDirect3DVertexBuffer9*>(vb->buffer); }
IDirect3DIndexBuffer9* d_3d(const IdxStore* ib) { return static_cast<IDirect3DIndexBuffer9*>(ib->buffer); }

constexpr uint32_t kNoCopy = 1;
constexpr uint32_t kShared = 4;

} // namespace

buffer_lists::Node* buffer_lists::vertex_buffers() { return g_vertex_buffer_list; }
buffer_lists::Node* buffer_lists::index_buffers() { return g_index_buffer_list; }

// 0x004D3FF0
VertexBuffer::VertexBuffer(uint32_t bytes, uint32_t usage_flags, uint32_t buffer_flags)
{
    if (!(buffer_flags & kShared))
        input_port()->CreateVertexBuffer(bytes, usage_flags, 0, D3DPOOL_DEFAULT,
                                         raw::as<IDirect3DVertexBuffer9*>(&buffer), nullptr);
    size = bytes;
    usage = usage_flags;
    data = (buffer_flags & kNoCopy) ? nullptr : static_cast<uint8_t*>(alloc_block(bytes));
    flags = buffer_flags;
    arena_offset_ = 0;
    lock_flags_ = 0xFFFFFFFF;
    enlist(vertex_buffers(), this);
}

// 0x004D3BE0
void VertexBuffer::unlist() { render::unlist(vertex_buffers(), this); }

// 0x004D4090
long VertexBuffer::get_lock(uint32_t offset, uint32_t bytes, uint8_t** out, uint32_t flags_for_lock)
{
    *out = data ? std::span<uint8_t>(data, size).subspan(offset).data() : data;
    lock_offset_ = offset;
    lock_size_ = bytes ? bytes : size;
    lock_flags_ = flags_for_lock;
    if (flags_for_lock & D3DLOCK_DISCARD)
        return d_3d(this)->Lock(offset, bytes, raw::as<void*>(out), flags_for_lock);
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
        void* card = nullptr;
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
                                         raw::as<IDirect3DVertexBuffer9*>(&buffer), nullptr);
    if (flags & kNoCopy)
        return;
    void* card = nullptr;
    if (d_3d(this)->Lock((flags & kShared) ? arena_offset_ : 0, size, &card, 0) >= 0) {
        std::memcpy(card, data, size);
        d_3d(this)->Unlock();
    }
}

// 0x004D4270
IdxStore::IdxStore(uint32_t bytes, uint32_t usage_flags, uint32_t index_format)
{
    input_port()->CreateIndexBuffer(bytes, usage_flags | D3DUSAGE_WRITEONLY, static_cast<D3DFORMAT>(index_format),
                                    D3DPOOL_DEFAULT, raw::as<IDirect3DIndexBuffer9*>(&buffer), nullptr);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    size = bytes;
    usage = usage_flags;
    format = index_format;
    indices = static_cast<uint16_t*>(alloc_block(bytes));
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    enlist(index_buffers(), this);
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

// 0x004D6720
uint32_t VertexBuffer::teardown()
{
    const uint32_t references = static_cast<IDirect3DVertexBuffer9*>(buffer)->Release();
    buffer = nullptr;
    unlist();
    if (data)
        dealloc(memory::owned_element(data));
    std::memset(this, 0, sizeof(*this));
    dealloc(memory::owned_element(this)); // (its own block)
    return references;
}

// 0x004D6790
uint32_t IdxStore::teardown()
{
    const uint32_t references = static_cast<IDirect3DIndexBuffer9*>(buffer)->Release();
    buffer = nullptr;
    unlist();
    if (indices)
        dealloc(memory::owned_element(indices));
    std::memset(this, 0, sizeof(*this));
    dealloc(memory::owned_element(this)); // (its own block)
    return references;
}

} // namespace render
