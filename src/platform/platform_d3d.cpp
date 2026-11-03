// The Win32 platform layer's Direct3D side: see platform_d3d.h.

#include "platform/platform_d3d.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include "engine/core/checked_index.h"
#include <unordered_map>

#include "engine/gfx/pc_flexible_particles.h"
#include "engine/core/raw_view.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"

namespace os_layer {

constexpr int kIndicesPerQuad = 6;      // two triangles
constexpr int kQuadLastIndexSlot = 5;   // the second triangle's third index
constexpr int kVertexAlignSlack = 0x1F; // dynamic vertices start on 32 bytes
constexpr unsigned kVertexAlignMask = 0x1Fu;

namespace {

// NOT ORIGINAL grouping - platform_d3d.cpp's state (the original's globals). Made on first use, never destroyed
// (platform_d3d_state()).
struct PlatformD3dState {
    IDirect3DVertexBuffer9* dynamic_vertices_{};
    uint8_t* dynamic_vertices_locked_{};
    uint32_t dynamic_vertices_used_{};
    uint32_t dynamic_vertices_size_{};
    IDirect3DQuery9* query_in_progress_{};
};

PlatformD3dState& platform_d3d_state()
{
    static core::NoDestroy<PlatformD3dState> state;
    return state.get();
}

// The dynamic vertex buffer: its size, where the next vertices go and where its lock put them.
IDirect3DVertexBuffer9*& dynamic_vertices() { return platform_d3d_state().dynamic_vertices_; }
uint32_t& dynamic_vertices_size() { return platform_d3d_state().dynamic_vertices_size_; }
uint32_t& dynamic_vertices_used() { return platform_d3d_state().dynamic_vertices_used_; }
uint8_t*& dynamic_vertices_locked() { return platform_d3d_state().dynamic_vertices_locked_; }

// The occlusion query in progress, and the ended ones by key (the original's stdext::hash_map at 0x007D69FC,
// built and torn down by the exe's own static initialisers - unused now).
IDirect3DQuery9*& query_in_progress() { return platform_d3d_state().query_in_progress_; }
std::unordered_map<uint32_t, IDirect3DQuery9*>& ended_queries()
{
    static std::unordered_map<uint32_t, IDirect3DQuery9*> queries;
    return queries;
}

} // namespace

// 0x005F45E0
void expand_palette(uint32_t* out, const uint8_t* indices, int count, const uint32_t* palette)
{
    const std::span<uint32_t> pixels(out, static_cast<size_t>(count));
    const std::span<const uint8_t> texels(indices, static_cast<size_t>(count));
    const std::span<const uint32_t, 256> colours(palette, 256);
    for (int i = 0; i < count; ++i)
        core::at(pixels, i) = core::at(colours, core::at(texels, i));
}

// 0x005F4620
HRESULT create_dynamic_vertex_buffer(uint32_t size)
{
    const HRESULT result = render::renderer().d3d_device()->CreateVertexBuffer(size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                                            D3DPOOL_DEFAULT, &dynamic_vertices(), nullptr);
    dynamic_vertices_size() = size;
    return result;
}

// 0x005F4650
void release_dynamic_vertex_buffer()
{
    if (dynamic_vertices())
        dynamic_vertices()->Release();
    dynamic_vertices() = nullptr;
}

// 0x005F4670
void release_quad_index_buffer()
{
    if (render::quad_index_buffer())
        render::quad_index_buffer()->Release();
    render::quad_index_buffer() = nullptr;
}

// 0x005F4690
HRESULT create_quad_index_buffer(uint32_t quads)
{
    uint16_t* index = nullptr;
    render::renderer().d3d_device()->CreateIndexBuffer(quads * kIndicesPerQuad * sizeof(uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
                                    &render::quad_index_buffer(), nullptr);
    render::quad_index_buffer()->Lock(0, 0, raw::as<void*>(&index), 0);
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
    return render::quad_index_buffer()->Unlock();
}

// 0x005F4740
void* lock_dynamic_vertices(uint32_t count, uint32_t stride, IDirect3DVertexBuffer9** buffer, VertexRoom vertex_room)
{
    const auto first_vertex = vertex_room.first_vertex_;
    const auto room = vertex_room.room;
    const uint32_t first = (dynamic_vertices_used() + stride - 1) / stride;
    *buffer = dynamic_vertices();
    const uint32_t start = first * stride;
    if (count * stride + start > dynamic_vertices_size()) {
        // Full: start again at the beginning.
        dynamic_vertices()->Lock(0, 0, raw::as<void*>(&dynamic_vertices_locked()), D3DLOCK_DISCARD);
        dynamic_vertices_used() = 0;
        if (room)
            *room = dynamic_vertices_size() / stride;
        *first_vertex = 0;
        return dynamic_vertices_locked();
    }
    *first_vertex = first;
    dynamic_vertices_used() = start;
    if (room)
        *room = (dynamic_vertices_size() - start) / stride;
    dynamic_vertices()->Lock(0, 0, raw::as<void*>(&dynamic_vertices_locked()), D3DLOCK_NOOVERWRITE);
    return std::span<uint8_t>(dynamic_vertices_locked(), dynamic_vertices_size()).subspan(start).data();
}

// 0x005F4800
void unlock_dynamic_vertices(uint32_t count, uint32_t stride)
{
    dynamic_vertices()->Unlock();
    dynamic_vertices_locked() = nullptr;
    dynamic_vertices_used() = (count * stride + dynamic_vertices_used() + kVertexAlignSlack) & ~kVertexAlignMask;
}

// 0x005F4840
HRESULT begin_occlusion_query()
{
    IDirect3DQuery9* query = nullptr;
    HRESULT result = render::renderer().d3d_device()->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query);
    if (FAILED(result))
        return result;
    result = query->Issue(D3DISSUE_BEGIN);
    if (FAILED(result)) {
        query->Release();
        return result;
    }
    query_in_progress() = query;
    return result;
}

// 0x005F5650
HRESULT end_occlusion_query(uint32_t key)
{
    IDirect3DQuery9* query = query_in_progress();
    if (!query)
        return D3DERR_NOTFOUND;
    if (ended_queries().find(key) != ended_queries().end())
        return E_FAIL;
    const HRESULT result = query->Issue(D3DISSUE_END);
    if (FAILED(result))
        query->Release(); // (and kept all the same, as in the original)
    ended_queries().insert({key, query});
    query_in_progress() = nullptr;
    return result;
}

// 0x005F4C80
HRESULT get_occlusion_result(uint32_t key, DWORD* pixels)
{
    auto it = ended_queries().find(key);
    if (it == ended_queries().end())
        return D3DERR_NOTFOUND;
    IDirect3DQuery9* query = it->second;
    const HRESULT result = query->GetData(pixels, sizeof(DWORD), 0);
    if (result == S_OK) {
        ended_queries().erase(it);
        query->Release();
    }
    return result;
}

// 0x005F52D0
void release_occlusion_queries()
{
    for (auto& [key, query] : ended_queries()) {
        if (query)
            query->Release();
    }
    ended_queries().clear();
    if (query_in_progress()) {
        query_in_progress()->Release();
        query_in_progress() = nullptr;
    }
}

// 0x005F55E0
void release_direct_3d()
{
    if (render::quad_index_buffer())
        render::quad_index_buffer()->Release();
    if (dynamic_vertices())
        dynamic_vertices()->Release();
    release_occlusion_queries();
    if (render::renderer().d3d_device())
        render::renderer().d3d_device()->Release();
    if (render::renderer().direct_3d())
        render::renderer().direct_3d()->Release();
    render::quad_index_buffer() = nullptr;
    dynamic_vertices() = nullptr;
    render::renderer().set_d3d_device(nullptr);
    render::renderer().set_direct_3d(nullptr);
    dynamic_vertices_size() = 0;
    dynamic_vertices_used() = 0;
    dynamic_vertices_locked() = nullptr;
}

uint64_t imgui_texture_id(IDirect3DTexture9* texture) { return static_cast<uint64_t>(cell::address(texture)); }
IDirect3DTexture9* imgui_texture_of(uint64_t id) { return cell::pointer<IDirect3DTexture9>(static_cast<uintptr_t>(id)); }

} // namespace os_layer
