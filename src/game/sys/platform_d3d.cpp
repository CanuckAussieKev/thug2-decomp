// The Win32 platform layer's Direct3D side: see platform_d3d.h.

#include "game/sys/platform_d3d.h"

#include <unordered_map>

#include "hook/game.h"
#include "hook/hook.h"

namespace Platform {

namespace {

IDirect3D9*& Direct3D() { return GameVariable<IDirect3D9*>(0x007D69DC); }
IDirect3DDevice9*& Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }

// The dynamic vertex buffer: its size, where the next vertices go and where its lock put them.
IDirect3DVertexBuffer9*& DynamicVertices() { return GameVariable<IDirect3DVertexBuffer9*>(0x007D69E4); }
uint32_t& DynamicVerticesSize() { return GameVariable<uint32_t>(0x007D69F4); }
uint32_t& DynamicVerticesUsed() { return GameVariable<uint32_t>(0x007D69F0); }
uint8_t*& DynamicVerticesLocked() { return GameVariable<uint8_t*>(0x007D69EC); }
IDirect3DIndexBuffer9*& QuadIndices() { return GameVariable<IDirect3DIndexBuffer9*>(0x007D69E8); }

// The occlusion query in progress, and the ended ones by key (the original's stdext::hash_map at 0x007D69FC,
// built and torn down by the exe's own static initialisers - unused now).
IDirect3DQuery9*& QueryInProgress() { return GameVariable<IDirect3DQuery9*>(0x007D69F8); }
std::unordered_map<uint32_t, IDirect3DQuery9*>& EndedQueries()
{
    static std::unordered_map<uint32_t, IDirect3DQuery9*> queries;
    return queries;
}

} // namespace

// 0x005F45E0
void ExpandPalette(uint32_t* out, const uint8_t* indices, int count, const uint32_t* palette)
{
    for (int i = 0; i < count; ++i)
        out[i] = palette[indices[i]];
}

// 0x005F4620
HRESULT CreateDynamicVertexBuffer(uint32_t size)
{
    HRESULT result = Device()->CreateVertexBuffer(size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                                  &DynamicVertices(), nullptr);
    DynamicVerticesSize() = size;
    return result;
}

// 0x005F4650
void ReleaseDynamicVertexBuffer()
{
    if (DynamicVertices())
        DynamicVertices()->Release();
    DynamicVertices() = nullptr;
}

// 0x005F4670
void ReleaseQuadIndexBuffer()
{
    if (QuadIndices())
        QuadIndices()->Release();
    QuadIndices() = nullptr;
}

// 0x005F4690
HRESULT CreateQuadIndexBuffer(uint32_t quads)
{
    uint16_t* index = nullptr;
    Device()->CreateIndexBuffer(quads * 6 * sizeof(uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
                                &QuadIndices(), nullptr);
    QuadIndices()->Lock(0, 0, reinterpret_cast<void**>(&index), 0);
    for (uint32_t quad = 0, first = 0; quad < quads; ++quad, first += 4, index += 6) {
        index[0] = static_cast<uint16_t>(first);
        index[1] = static_cast<uint16_t>(first + 1);
        index[2] = static_cast<uint16_t>(first + 2);
        index[3] = static_cast<uint16_t>(first + 2);
        index[4] = static_cast<uint16_t>(first + 3);
        index[5] = static_cast<uint16_t>(first);
    }
    return QuadIndices()->Unlock();
}

// 0x005F4740
void* LockDynamicVertices(uint32_t count, uint32_t stride, IDirect3DVertexBuffer9** buffer, uint32_t* firstVertex,
                          uint32_t* room)
{
    uint32_t first = (DynamicVerticesUsed() + stride - 1) / stride;
    *buffer = DynamicVertices();
    uint32_t start = first * stride;
    if (count * stride + start > DynamicVerticesSize()) {
        // Full: start again at the beginning.
        DynamicVertices()->Lock(0, 0, reinterpret_cast<void**>(&DynamicVerticesLocked()), D3DLOCK_DISCARD);
        DynamicVerticesUsed() = 0;
        if (room)
            *room = DynamicVerticesSize() / stride;
        *firstVertex = 0;
        return DynamicVerticesLocked();
    }
    *firstVertex = first;
    DynamicVerticesUsed() = start;
    if (room)
        *room = (DynamicVerticesSize() - start) / stride;
    DynamicVertices()->Lock(0, 0, reinterpret_cast<void**>(&DynamicVerticesLocked()), D3DLOCK_NOOVERWRITE);
    return DynamicVerticesLocked() + start;
}

// 0x005F4800
void UnlockDynamicVertices(uint32_t count, uint32_t stride)
{
    DynamicVertices()->Unlock();
    DynamicVerticesLocked() = nullptr;
    DynamicVerticesUsed() = (count * stride + DynamicVerticesUsed() + 0x1F) & ~0x1Fu;
}

// 0x005F4840
HRESULT BeginOcclusionQuery()
{
    IDirect3DQuery9* query = nullptr;
    HRESULT result = Device()->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query);
    if (FAILED(result))
        return result;
    result = query->Issue(D3DISSUE_BEGIN);
    if (FAILED(result)) {
        query->Release();
        return result;
    }
    QueryInProgress() = query;
    return result;
}

// 0x005F5650
HRESULT EndOcclusionQuery(uint32_t key)
{
    IDirect3DQuery9* query = QueryInProgress();
    if (!query)
        return D3DERR_NOTFOUND;
    if (EndedQueries().find(key) != EndedQueries().end())
        return E_FAIL;
    HRESULT result = query->Issue(D3DISSUE_END);
    if (FAILED(result))
        query->Release(); // (and kept all the same, as in the original)
    EndedQueries().insert({key, query});
    QueryInProgress() = nullptr;
    return result;
}

// 0x005F4C80
HRESULT GetOcclusionResult(uint32_t key, DWORD* pixels)
{
    auto it = EndedQueries().find(key);
    if (it == EndedQueries().end())
        return D3DERR_NOTFOUND;
    IDirect3DQuery9* query = it->second;
    HRESULT result = query->GetData(pixels, sizeof(DWORD), 0);
    if (result == S_OK) {
        EndedQueries().erase(it);
        query->Release();
    }
    return result;
}

// 0x005F52D0
void ReleaseOcclusionQueries()
{
    for (auto& [key, query] : EndedQueries()) {
        if (query)
            query->Release();
    }
    EndedQueries().clear();
    if (QueryInProgress()) {
        QueryInProgress()->Release();
        QueryInProgress() = nullptr;
    }
}

// 0x005F55E0
void ReleaseDirect3D()
{
    if (QuadIndices())
        QuadIndices()->Release();
    if (DynamicVertices())
        DynamicVertices()->Release();
    ReleaseOcclusionQueries();
    if (Device())
        Device()->Release();
    if (Direct3D())
        Direct3D()->Release();
    QuadIndices() = nullptr;
    DynamicVertices() = nullptr;
    Device() = nullptr;
    Direct3D() = nullptr;
    DynamicVerticesSize() = 0;
    DynamicVerticesUsed() = 0;
    DynamicVerticesLocked() = nullptr;
}

} // namespace Platform

REPLACE(0x005F45E0, Platform::ExpandPalette);
REPLACE(0x005F4620, Platform::CreateDynamicVertexBuffer);
REPLACE(0x005F4650, Platform::ReleaseDynamicVertexBuffer);
REPLACE(0x005F4670, Platform::ReleaseQuadIndexBuffer);
REPLACE(0x005F4690, Platform::CreateQuadIndexBuffer);
REPLACE(0x005F4740, Platform::LockDynamicVertices);
REPLACE(0x005F4800, Platform::UnlockDynamicVertices);
REPLACE(0x005F4840, Platform::BeginOcclusionQuery);
REPLACE(0x005F5650, Platform::EndOcclusionQuery);
REPLACE(0x005F4C80, Platform::GetOcclusionResult);
REPLACE(0x005F52D0, Platform::ReleaseOcclusionQueries);
REPLACE(0x005F55E0, Platform::ReleaseDirect3D);
