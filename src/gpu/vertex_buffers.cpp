// The GPU's vertex and index buffers: made, listed, locked and unlocked through their copies in memory, made again
// after a device reset, torn down.

#include "gpu/vertex_buffers.h"

#include <windows.h>
#include <span>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gsl/assert>
#include <gsl/pointers>

#include "base/core/raw_view.h"
#include "gpu/d3d9_handles.h"
#include "gpu/gpu_handles.h"
#include "platform/crt.h"
#include "base/core/owned_element.h"
#include "base/core/no_destroy.h"

namespace render {

namespace {

// The game's C runtime heap (the nodes, copies and buffers are freed through it).
gsl::owner<void*> alloc_block(size_t size) { return crt::alloc_block(size); }

// lock_flags_ while the buffer isn't locked (no flag set, all bits on).
constexpr uint32_t kNotLocked = 0xFFFFFFFF;
void dealloc(gsl::owner<void*> memory) { crt::dealloc(memory); }

// NOT ORIGINAL grouping - vertex_buffers.cpp's state (the original's globals). Made on first use, never destroyed
// (vertex_buffers_state()).
struct VertexBuffersState {
    buffer_lists::Node* vertex_buffer_list_ = nullptr; // (the original's 0x007865E4)
    buffer_lists::Node* index_buffer_list_ = nullptr; // (0x007865E8)
    GpuDevice* device_ = nullptr; // (the renderer's device, set through set_buffer_device)
};

VertexBuffersState& vertex_buffers_state()
{
    static core::NoDestroy<VertexBuffersState> state;
    return state.get();
}

// The renderer's device (set before any buffer is made; the buffers can't exist without one).
IDirect3DDevice9& buffer_device()
{
    IDirect3DDevice9* device = d3d9::object(vertex_buffers_state().device_);
    Expects(device != nullptr);
    return *device;
}

buffer_lists::Node*& vertex_buffers() { return vertex_buffers_state().vertex_buffer_list_; }
buffer_lists::Node*& index_buffers() { return vertex_buffers_state().index_buffer_list_; }

using BufferNode = buffer_lists::Node;

void enlist(BufferNode*& head, void* buffer)
{
    auto* node = static_cast<BufferNode*>(alloc_block(sizeof(BufferNode)));
    node->buffer = buffer;
    node->next = head;
    node->prev = nullptr;
    BufferNode* old = head;
    head = node;
    if (old) {
        old->prev = node;
    }
}

void unlist(BufferNode*& head, const void* buffer)
{
    BufferNode* node = head;
    if (!node) {
        return;
    }
    while (node->buffer != buffer) {
        node = node->next;
        if (!node) {
            return;
        }
    }
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }
    node->buffer = nullptr;
    node->prev = nullptr;
    node->next = nullptr;
    dealloc(memory::owned_element(node)); // (the list's)
}

IDirect3DVertexBuffer9* d_3d(const VertexBuffer* vb) { return d3d9::object(vb->buffer()); }
IDirect3DIndexBuffer9* d_3d(const IdxStore* ib) { return d3d9::object(ib->buffer()); }

constexpr uint32_t kNoCopy = 1;
constexpr uint32_t kShared = 4;

} // namespace

void set_buffer_device(GpuDevice* device) { vertex_buffers_state().device_ = device; }

buffer_lists::Node* buffer_lists::vertex_buffers() { return vertex_buffers_state().vertex_buffer_list_; }
buffer_lists::Node* buffer_lists::index_buffers() { return vertex_buffers_state().index_buffer_list_; }

// 0x004D3FF0
VertexBuffer::VertexBuffer(uint32_t bytes, BufferFlags creation)
{
    const auto usage_flags = creation.usage_flags_;
    const auto buffer_flags = creation.buffer_flags_;
    if ((buffer_flags & kShared) == 0U) {
        buffer_device().CreateVertexBuffer(bytes, usage_flags, 0, D3DPOOL_DEFAULT,
                         d3d9::out(&buffer_), nullptr);
    }
    size_ = bytes;
    usage = usage_flags;
    data_ = ((buffer_flags & kNoCopy) != 0U) ? nullptr : static_cast<uint8_t*>(alloc_block(bytes));
    flags = buffer_flags;
    arena_offset_ = 0;
    lock_flags_ = kNotLocked;
    enlist(vertex_buffers(), this);
}

// 0x004D3BE0
void VertexBuffer::unlist() { render::unlist(vertex_buffers(), this); }

// 0x004D4090
long VertexBuffer::get_lock(uint32_t offset, uint32_t bytes, uint8_t** out, uint32_t flags_for_lock)
{
    *out = data_ ? std::span<uint8_t>(data_, size_).subspan(offset).data() : data_;
    lock_offset_ = offset;
    lock_size_ = (bytes != 0U) ? bytes : size_;
    lock_flags_ = flags_for_lock;
    if ((flags_for_lock & D3DLOCK_DISCARD) != 0U) {
        return d_3d(this)->Lock(offset, bytes, raw::as<void*>(out), flags_for_lock);
    }
    return 0;
}

// 0x004D40E0
long VertexBuffer::get_unlock()
{
    if (((flags & kShared) != 0U) && !buffer_) {
        lock_flags_ = kNotLocked;
        return 0;
    }
    if ((lock_flags_ & D3DLOCK_READONLY) == 0U) {
        if ((lock_flags_ & D3DLOCK_DISCARD) != 0U) {
            lock_flags_ = kNotLocked;
            return d_3d(this)->Unlock();
        }
        if ((flags & kShared) != 0U) {
            lock_offset_ += arena_offset_;
        }
        void* card = nullptr;
        if (d_3d(this)->Lock(lock_offset_, lock_size_, &card, lock_flags_) >= 0) {
            std::memcpy(card, data_, lock_size_); // (from the copy's start, whatever the offset)
            d_3d(this)->Unlock();
        }
    }
    lock_flags_ = kNotLocked;
    return 0;
}

// 0x004D41E0
void VertexBuffer::recreate()
{
    if ((flags & kShared) != 0U) {
        d_3d(this)->AddRef();
    } else {
        buffer_device().CreateVertexBuffer(size_, usage | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                         d3d9::out(&buffer_), nullptr);
    }
    if ((flags & kNoCopy) != 0U) {
        return;
    }
    void* card = nullptr;
    if (d_3d(this)->Lock(((flags & kShared) != 0U) ? arena_offset_ : 0, size_, &card, 0) >= 0) {
        std::memcpy(card, data_, size_);
        d_3d(this)->Unlock();
    }
}

// 0x004D4270
IdxStore::IdxStore(uint32_t bytes, uint32_t usage_flags, uint32_t index_format)
    : size_(bytes), usage_(usage_flags), format_(index_format), indices_(static_cast<uint16_t*>(alloc_block(bytes)))
{
    buffer_device().CreateIndexBuffer(bytes, usage_flags | D3DUSAGE_WRITEONLY, static_cast<D3DFORMAT>(index_format),
                    D3DPOOL_DEFAULT, d3d9::out(&buffer_), nullptr);
    enlist(index_buffers(), this);
}

// 0x004D3C40
void IdxStore::unlist() { render::unlist(index_buffers(), this); }

// 0x004D42F0
long IdxStore::get_lock(uint32_t /*offset*/, uint32_t /*bytes*/, uint16_t** out, uint32_t /*lockFlags*/)
{
    *out = indices_;
    return 0;
}

// 0x004D4300
long IdxStore::get_unlock()
{
    void* card = nullptr;
    if (d_3d(this)->Lock(0, 0, &card, 0) >= 0) {
        std::memcpy(card, indices_, size_);
        d_3d(this)->Unlock();
    }
    return 0;
}

// 0x004D6720
uint32_t VertexBuffer::teardown()
{
    // (NOT ORIGINAL: a buffer the card never made - its creation failed - has nothing to release)
    const uint32_t references = buffer_ != nullptr ? d3d9::object(buffer_)->Release() : 0;
    buffer_ = nullptr;
    unlist();
    if (data_) {
        dealloc(memory::owned_element(data_));
    }
    std::memset(this, 0, sizeof(*this));
    dealloc(memory::owned_element(this)); // (its own block)
    return references;
}

// 0x004D6790
uint32_t IdxStore::teardown()
{
    // (NOT ORIGINAL: a buffer the card never made - its creation failed - has nothing to release)
    const uint32_t references = buffer_ != nullptr ? d3d9::object(buffer_)->Release() : 0;
    buffer_ = nullptr;
    unlist();
    if (indices_) {
        dealloc(memory::owned_element(indices_));
    }
    std::memset(this, 0, sizeof(*this));
    dealloc(memory::owned_element(this)); // (its own block)
    return references;
}

} // namespace render
