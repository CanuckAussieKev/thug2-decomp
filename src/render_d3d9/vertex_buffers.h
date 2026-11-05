#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/mesh_records.h"

// The GPU's vertex and index buffers (the boundary layer's gpu part: docs/architecture.md "Layers"): each a Direct3D
// buffer and a copy of its bytes in memory, written to the card when unlocked and written again after the device is
// reset. The vertex data itself is laid out by whoever fills it (the scene files' vertices go straight in - formats/
// scene_format.h -, the screen's quads by their own layouts).

namespace render {

// The device the buffers are created on: the renderer hands it over whenever it makes or drops its own
// (d3d9::State::set_d3d_device, render_d3d9/d3d9_state.h), so the gpu code asks nothing of the engine.
void set_buffer_device(GpuDevice* device);

// A vertex buffer (0x24 bytes, constructor 0x004D3FF0; made by 0x004D5760): its record (engine/gfx/mesh_records.h:
// the size, the Direct3D buffer - its own, or a share of an arena's (flag 4) -, a copy of the vertices in memory (none
// with flag 1) written to the card when unlocked, and the lock's state) and what it does. Every one is on a list
// (0x007865E4) of C runtime nodes, so they can be made again after the device is reset.
struct VertexBuffer : VertexBufferData {
    // 0x004D3FF0: construct - a buffer of its own (without flag 4: CreateVertexBuffer, default pool), the copy (but
    // with flag 1), on the list.
    // A vertex buffer's D3D usage flags and its own flags.
    struct BufferFlags {
        uint32_t usage_flags_{};
        uint32_t buffer_flags_{};
    };
    VertexBuffer(uint32_t bytes, BufferFlags creation);
    // 0x004D3BE0: taken off the list (its node freed).
    void unlist();
    // 0x004D4090: locked - *out the copy at `offset` (or, with D3DLOCK_DISCARD, the card's buffer itself: its Lock's
    // result returned; else 0).
    long get_lock(uint32_t offset, uint32_t bytes, uint8_t** out, uint32_t flags_for_lock);
    // 0x004D40E0: unlocked - the copy (from its start) written into the locked part of the card's buffer (shared
    // ones at their arena offset), or the card's buffer unlocked after a discarding lock; nothing after a read-only
    // one or for a share without a buffer.
    long get_unlock();
    // 0x004D41E0: made again on the card after a device reset (write-only; a share adds a reference) and the copy
    // written into it.
    void recreate();
    // 0x004D6720: destroyed - the card's buffer released, off the list, the copy and itself freed (the C runtime's).
    // The buffer's reference count after the release.
    uint32_t teardown();
};

// An index buffer (0x18 bytes, constructor 0x004D4270): its record (mesh_records.h: the size, the Direct3D buffer and a
// copy of the indices in memory, written to the card when unlocked) and what it does; on a list (0x007865E8) as the
// vertex buffers are.
struct IdxStore : IndexBufferData {
    // 0x004D4270: construct - CreateIndexBuffer (write-only, default pool), the copy, on the list.
    IdxStore(uint32_t bytes, uint32_t usage_flags, uint32_t index_format);
    // 0x004D3C40: taken off the list (its node freed).
    void unlist();
    // 0x004D42F0: locked - *out the copy (all of it, whatever the offset). 0.
    long get_lock(uint32_t offset, uint32_t bytes, uint16_t** out, uint32_t lock_flags);
    // 0x004D4300: unlocked - the whole copy written to the card. 0.
    long get_unlock();
    // 0x004D6790: destroyed, as the vertex buffers are. The reference count after the release.
    uint32_t teardown();
};

// Every buffer made and not torn down, in no particular order (for the device reset: engine/gfx's release_buffers /
// recreate_buffers walk them).
namespace buffer_lists {
// A node of the buffers' lists (the C runtime's heap): the buffer, the previous and the next node.
struct Node {
    void* buffer;
    Node* prev;
    Node* next;
};
// The first node of each list (the original's 0x007865E4 / 0x007865E8).
Node* vertex_buffers();
Node* index_buffers();
} // namespace buffer_lists

} // namespace render
