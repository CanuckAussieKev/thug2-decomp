// The Win32 platform layer's Direct3D side: see platform_d3d.h.

#include "platform/platform_d3d.h"

#include <cstdint>
#include <unordered_map>

#include "engine/gfx/device.h"
#include "engine/gfx/pc_flexible_particles.h"
#include "engine/core/raw_view.h"
#include "engine/core/pointer_cell.h"

namespace os_layer {

namespace {

IDirect3D9*& direct_3d() { return render::g_direct_3d; }
IDirect3DDevice9*& input_port() { return render::g_d3d_device; }

IDirect3DVertexBuffer9* g_dynamic_vertices;
uint8_t* g_dynamic_vertices_locked;
uint32_t g_dynamic_vertices_used;
uint32_t g_dynamic_vertices_size;
IDirect3DQuery9* g_query_in_progress;

// The dynamic vertex buffer: its size, where the next vertices go and where its lock put them.
IDirect3DVertexBuffer9*& dynamic_vertices() { return g_dynamic_vertices; }
uint32_t& dynamic_vertices_size() { return g_dynamic_vertices_size; }
uint32_t& dynamic_vertices_used() { return g_dynamic_vertices_used; }
uint8_t*& dynamic_vertices_locked() { return g_dynamic_vertices_locked; }
IDirect3DIndexBuffer9*& quad_indices() { return render::g_quad_indices; }

// The occlusion query in progress, and the ended ones by key (the original's stdext::hash_map at 0x007D69FC,
// built and torn down by the exe's own static initialisers - unused now).
IDirect3DQuery9*& query_in_progress() { return g_query_in_progress; }
std::unordered_map<uint32_t, IDirect3DQuery9*>& ended_queries()
{
    static std::unordered_map<uint32_t, IDirect3DQuery9*> queries;
    return queries;
}

} // namespace

// 0x005F45E0
void expand_palette(uint32_t* out, const uint8_t* indices, int count, const uint32_t* palette)
{
    for (int i = 0; i < count; ++i)
        out[i] = palette[indices[i]];
}

// 0x005F4620
HRESULT create_dynamic_vertex_buffer(uint32_t size)
{
    const HRESULT result = input_port()->CreateVertexBuffer(size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
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
    if (quad_indices())
        quad_indices()->Release();
    quad_indices() = nullptr;
}

// 0x005F4690
HRESULT create_quad_index_buffer(uint32_t quads)
{
    uint16_t* index = nullptr;
    input_port()->CreateIndexBuffer(quads * 6 * sizeof(uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
                                    &quad_indices(), nullptr);
    quad_indices()->Lock(0, 0, raw::as<void*>(&index), 0);
    for (uint32_t quad = 0, first = 0; quad < quads; ++quad, first += 4, index += 6) {
        index[0] = static_cast<uint16_t>(first);
        index[1] = static_cast<uint16_t>(first + 1);
        index[2] = static_cast<uint16_t>(first + 2);
        index[3] = static_cast<uint16_t>(first + 2);
        index[4] = static_cast<uint16_t>(first + 3);
        index[5] = static_cast<uint16_t>(first);
    }
    return quad_indices()->Unlock();
}

// 0x005F4740
void* lock_dynamic_vertices(uint32_t count, uint32_t stride, IDirect3DVertexBuffer9** buffer, uint32_t* first_vertex,
                            uint32_t* room)
{
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
    return dynamic_vertices_locked() + start;
}

// 0x005F4800
void unlock_dynamic_vertices(uint32_t count, uint32_t stride)
{
    dynamic_vertices()->Unlock();
    dynamic_vertices_locked() = nullptr;
    dynamic_vertices_used() = (count * stride + dynamic_vertices_used() + 0x1F) & ~0x1Fu;
}

// 0x005F4840
HRESULT begin_occlusion_query()
{
    IDirect3DQuery9* query = nullptr;
    HRESULT result = input_port()->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query);
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
    if (quad_indices())
        quad_indices()->Release();
    if (dynamic_vertices())
        dynamic_vertices()->Release();
    release_occlusion_queries();
    if (input_port())
        input_port()->Release();
    if (direct_3d())
        direct_3d()->Release();
    quad_indices() = nullptr;
    dynamic_vertices() = nullptr;
    input_port() = nullptr;
    direct_3d() = nullptr;
    dynamic_vertices_size() = 0;
    dynamic_vertices_used() = 0;
    dynamic_vertices_locked() = nullptr;
}

uint64_t imgui_texture_id(IDirect3DTexture9* texture) { return static_cast<uint64_t>(cell::address(texture)); }
IDirect3DTexture9* imgui_texture_of(uint64_t id) { return cell::pointer<IDirect3DTexture9>(static_cast<uintptr_t>(id)); }

} // namespace os_layer
