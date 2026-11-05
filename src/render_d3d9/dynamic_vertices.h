#pragma once

// NOT ORIGINAL grouping - the part of the original's platform layer (pc_d3d_resources.h) the particle, weather and
// glow code uses, in the graphics handles' terms (engine/gfx/gpu_handles.h), so those files include no Direct3D
// header: the renderer's dynamic vertex buffer, the quads' index buffer and the occlusion answers.
#include <cstdint>
#include <span>

#include "base/core/cursor.h"
#include "engine/gfx/gpu_handles.h"

namespace os_layer {

// Where the first vertex and the room left go.
struct VertexRoom {
    uint32_t* first_vertex_{};
    uint32_t* room{};
};

// 0x005F4740 (cdecl): room for `count` vertices of `stride` bytes in the dynamic vertex buffer, locked: after the
// last ones (rounded up to a whole vertex, the buffer locked with NOOVERWRITE), or - when they don't fit - from the
// start (DISCARD). The buffer, the first vertex's index and (if asked) how many vertices fit from there are given
// back. Returns where to write.
void* lock_dynamic_vertices(uint32_t count, uint32_t stride, render::GpuVertexBuffer** buffer, VertexRoom vertex_room);
// lock_dynamic_vertices for vertices of type V (the stride sizeof(V)): the vertices that fit, as a checked cursor at the
// first. `room` is how many fit.
template <typename V>
core::Cursor<V> lock_vertices(uint32_t count, render::GpuVertexBuffer** buffer, uint32_t* first_vertex, uint32_t* room)
{
    void* where = lock_dynamic_vertices(count, sizeof(V), buffer, {.first_vertex_ = first_vertex, .room = room});
    return core::Cursor<V>(std::span<V>(static_cast<V*>(where), *room));
}
// 0x005F4800 (cdecl): unlocked after writing `count` vertices of `stride` bytes; the next go after them (32-byte
// aligned).
void unlock_dynamic_vertices(uint32_t count, uint32_t stride);

// 0x005F4C80 (cdecl): the pixels `key`'s query saw: 0 with them (the query forgotten and released), 1 not yet, a
// negative result (Direct3D 9's D3DERR_NOTFOUND) for no such query.
long get_occlusion_result(uint32_t key, uint32_t* pixels);

} // namespace os_layer

namespace render {

// The quads' index buffer (pc_flexible_particles.cpp's; made and released in pc_d3d_resources.cpp).
GpuIndexBuffer*& quad_index_buffer();

} // namespace render
