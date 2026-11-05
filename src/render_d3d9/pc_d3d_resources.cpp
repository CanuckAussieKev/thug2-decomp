// The renderer's Direct3D buffers and queries (the original's Win32 platform layer): see pc_d3d_resources.h.

#include "render_d3d9/pc_d3d_resources.h"

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "render_d3d9/dynamic_vertices.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include "render_d3d9/device.h"
#include "base/core/checked_index.h"
#include <unordered_map>

#include "base/core/no_destroy.h"
#include "render_d3d9/d3d9_state.h"

namespace os_layer {

constexpr int kIndicesPerQuad = 6;      // two triangles
constexpr int kQuadLastIndexSlot = 5;   // the second triangle's third index
constexpr int kVertexAlignSlack = 0x1F; // dynamic vertices start on 32 bytes
constexpr unsigned kVertexAlignMask = 0x1FU;

namespace {

// NOT ORIGINAL grouping - pc_d3d_resources.cpp's state (the original's globals). Made on first use, never destroyed
// (platform_d3d_state()).
struct PlatformD3dState {
    render::GpuVertexBuffer* dynamic_vertices_{};
    uint8_t* dynamic_vertices_locked_{};
    uint32_t dynamic_vertices_used_{};
    uint32_t dynamic_vertices_size_{};
    render::GpuQuery* query_in_progress_{};
};

PlatformD3dState& platform_d3d_state()
{
    static core::NoDestroy<PlatformD3dState> state;
    return state.get();
}

// The dynamic vertex buffer: its size, where the next vertices go and where its lock put them.
render::GpuVertexBuffer*& dynamic_vertices() { return platform_d3d_state().dynamic_vertices_; }
uint32_t& dynamic_vertices_size() { return platform_d3d_state().dynamic_vertices_size_; }
uint32_t& dynamic_vertices_used() { return platform_d3d_state().dynamic_vertices_used_; }
uint8_t*& dynamic_vertices_locked() { return platform_d3d_state().dynamic_vertices_locked_; }

// The occlusion query in progress, and the ended ones by key (the original's stdext::hash_map at 0x007D69FC,
// built and torn down by the exe's own static initialisers - unused now).
render::GpuQuery*& query_in_progress() { return platform_d3d_state().query_in_progress_; }
std::unordered_map<uint32_t, render::GpuQuery*>& ended_queries()
{
    static std::unordered_map<uint32_t, render::GpuQuery*> queries;
    return queries;
}

} // namespace

// 0x005F45E0
void expand_palette(uint32_t* out, const uint8_t* indices, int count, const uint32_t* palette)
{
    const std::span<uint32_t> pixels(out, static_cast<size_t>(count));
    const std::span<const uint8_t> texels(indices, static_cast<size_t>(count));
    const std::span<const uint32_t, 256> colours(palette, 256);
    for (int i = 0; i < count; ++i) {
        core::at(pixels, i) = core::at(colours, core::at(texels, i));
    }
}

// 0x005F4620
HRESULT create_dynamic_vertex_buffer(uint32_t size)
{
    const HRESULT result = render::gpu().create_vertex_buffer(
        size, render::gpu_value::kUsageDynamic | render::gpu_value::kUsageWriteOnly, 0, render::gpu_value::kPoolDefault,
        &dynamic_vertices());
    dynamic_vertices_size() = size;
    return result;
}

// 0x005F4650
void release_dynamic_vertex_buffer()
{
    if (dynamic_vertices()) {
        render::gpu().release_vertex_buffer(dynamic_vertices());
    }
    dynamic_vertices() = nullptr;
}

// 0x005F4670
void release_quad_index_buffer()
{
    if (render::quad_index_buffer()) {
        render::gpu().release_index_buffer(render::quad_index_buffer());
    }
    render::quad_index_buffer() = nullptr;
}

// 0x005F4690
HRESULT create_quad_index_buffer(uint32_t quads)
{
    uint16_t* index = nullptr;
    render::gpu().create_index_buffer(quads * kIndicesPerQuad * sizeof(uint16_t), render::gpu_value::kUsageWriteOnly,
                                      render::gpu_value::kFormatIndex16, render::gpu_value::kPoolDefault,
                                      &render::quad_index_buffer());
    void* locked = nullptr;
    render::gpu().lock_index_buffer(render::quad_index_buffer(), 0, 0, &locked, 0);
    index = static_cast<uint16_t*>(locked);
    const std::span<uint16_t> indices(index, static_cast<size_t>(quads) * 6);
    size_t at = 0;
    for (uint32_t quad = 0, first = 0; quad < quads; ++quad, first += 4, at += kIndicesPerQuad) {
        core::at(indices, at) = static_cast<uint16_t>(first);
        core::at(indices, at + 1) = static_cast<uint16_t>(first + 1);
        core::at(indices, at + 2) = static_cast<uint16_t>(first + 2);
        core::at(indices, at + 3) = static_cast<uint16_t>(first + 2);
        core::at(indices, at + 4) = static_cast<uint16_t>(first + 3);
        core::at(indices, at + kQuadLastIndexSlot) = static_cast<uint16_t>(first);
    }
    return render::gpu().unlock_index_buffer(render::quad_index_buffer());
}

// 0x005F4740
void* lock_dynamic_vertices(uint32_t count, uint32_t stride, render::GpuVertexBuffer** buffer, VertexRoom vertex_room)
{
    auto* const first_vertex = vertex_room.first_vertex_;
    auto* const room = vertex_room.room;
    const uint32_t first = (dynamic_vertices_used() + stride - 1) / stride;
    *buffer = dynamic_vertices();
    const uint32_t start = first * stride;
    if ((count * stride) + start > dynamic_vertices_size()) {
        // Full: start again at the beginning.
        void* locked = dynamic_vertices_locked(); // (left as it was if the lock writes nothing)
        render::gpu().lock_vertex_buffer(dynamic_vertices(), 0, 0, &locked, render::gpu_value::kLockDiscard);
        dynamic_vertices_locked() = static_cast<uint8_t*>(locked);
        dynamic_vertices_used() = 0;
        if (room) {
            *room = dynamic_vertices_size() / stride;
        }
        *first_vertex = 0;
        return dynamic_vertices_locked();
    }
    *first_vertex = first;
    dynamic_vertices_used() = start;
    if (room) {
        *room = (dynamic_vertices_size() - start) / stride;
    }
    void* locked = dynamic_vertices_locked(); // (left as it was if the lock writes nothing)
    render::gpu().lock_vertex_buffer(dynamic_vertices(), 0, 0, &locked, render::gpu_value::kLockNoOverwrite);
    dynamic_vertices_locked() = static_cast<uint8_t*>(locked);
    return std::span<uint8_t>(dynamic_vertices_locked(), dynamic_vertices_size()).subspan(start).data();
}

// 0x005F4800
void unlock_dynamic_vertices(uint32_t count, uint32_t stride)
{
    render::gpu().unlock_vertex_buffer(dynamic_vertices());
    dynamic_vertices_locked() = nullptr;
    dynamic_vertices_used() = ((count * stride) + dynamic_vertices_used() + kVertexAlignSlack) & ~kVertexAlignMask;
}

// 0x005F4840
long begin_occlusion_query()
{
    render::GpuQuery* query = nullptr;
    long result = render::gpu().create_occlusion_query(&query);
    if (FAILED(result)) {
        return result;
    }
    result = render::gpu().begin_query(query);
    if (FAILED(result)) {
        render::gpu().release_query(query);
        return result;
    }
    query_in_progress() = query;
    return result;
}

// 0x005F5650
long end_occlusion_query(uint32_t key)
{
    render::GpuQuery* query = query_in_progress();
    if (!query) {
        return D3DERR_NOTFOUND;
    }
    if (ended_queries().contains(key)) {
        return E_FAIL;
    }
    const long result = render::gpu().end_query(query);
    if (FAILED(result)) {
        render::gpu().release_query(query); // (and kept all the same, as in the original)
    }
    ended_queries().insert({key, query});
    query_in_progress() = nullptr;
    return result;
}

// 0x005F4C80
long get_occlusion_result(uint32_t key, uint32_t* pixels)
{
    auto it = ended_queries().find(key);
    if (it == ended_queries().end()) {
        return D3DERR_NOTFOUND;
    }
    render::GpuQuery* query = it->second;
    const HRESULT result = render::gpu().query_result(query, pixels, sizeof(uint32_t), 0);
    if (result == S_OK) {
        ended_queries().erase(it);
        render::gpu().release_query(query);
    }
    return result;
}

// 0x005F52D0
void release_occlusion_queries()
{
    for (auto& [key, query] : ended_queries()) {
        if (query) {
            render::gpu().release_query(query);
        }
    }
    ended_queries().clear();
    if (query_in_progress()) {
        render::gpu().release_query(query_in_progress());
        query_in_progress() = nullptr;
    }
}

// 0x005F55E0
void release_direct_3d()
{
    if (render::quad_index_buffer()) {
        render::gpu().release_index_buffer(render::quad_index_buffer());
    }
    if (dynamic_vertices()) {
        render::gpu().release_vertex_buffer(dynamic_vertices());
    }
    release_occlusion_queries();
    if (render::d3d9::state().d3d_device()) {
        render::d3d9::state().d3d_device()->Release();
    }
    if (render::d3d9::state().direct_3d()) {
        render::d3d9::state().direct_3d()->Release();
    }
    render::quad_index_buffer() = nullptr;
    dynamic_vertices() = nullptr;
    render::d3d9::state().set_d3d_device(nullptr);
    render::d3d9::state().set_direct_3d(nullptr);
    dynamic_vertices_size() = 0;
    dynamic_vertices_used() = 0;
    dynamic_vertices_locked() = nullptr;
}

} // namespace os_layer
