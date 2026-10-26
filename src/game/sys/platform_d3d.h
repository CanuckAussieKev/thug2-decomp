#pragma once

#include <windows.h>
#include <d3d9.h>

#include <cstdint>

// The Win32 platform layer's Direct3D side (0x005F45E0-0x005F5650): the renderer's dynamic vertex buffer (vertices
// written each frame, appended until full, then started again), the quads' index buffer, occlusion queries kept by
// a key until their answer is read, and releasing it all. (The hash_map, list and vector code between them,
// 0x005F4890-0x005F5390, is the C++ library's - stdext::hash_map's internals: here a std::unordered_map.)
namespace Platform {

// 0x005F45E0 (cdecl): `count` texels through a palette: out[i] = palette[indices[i]].
void ExpandPalette(uint32_t* out, const uint8_t* indices, int count, const uint32_t* palette);

// 0x005F4620 (cdecl): the dynamic vertex buffer, `size` bytes (dynamic, write-only). Returns CreateVertexBuffer's
// result.
HRESULT CreateDynamicVertexBuffer(uint32_t size);
// 0x005F4650 / 0x005F4670 (cdecl): the dynamic vertex buffer / the quads' index buffer released.
void ReleaseDynamicVertexBuffer();
void ReleaseQuadIndexBuffer();
// 0x005F4690 (cdecl): the index buffer for `quads` quads, two triangles each (0 1 2, 2 3 0). Returns Unlock's
// result.
HRESULT CreateQuadIndexBuffer(uint32_t quads);
// 0x005F4740 (cdecl): room for `count` vertices of `stride` bytes in the dynamic vertex buffer, locked: after the
// last ones (rounded up to a whole vertex, the buffer locked with NOOVERWRITE), or - when they don't fit - from the
// start (DISCARD). The buffer, the first vertex's index and (if asked) how many vertices fit from there are given
// back. Returns where to write.
void* LockDynamicVertices(uint32_t count, uint32_t stride, IDirect3DVertexBuffer9** buffer, uint32_t* firstVertex,
                          uint32_t* room);
// 0x005F4800 (cdecl): unlocked after writing `count` vertices of `stride` bytes; the next go after them (32-byte
// aligned).
void UnlockDynamicVertices(uint32_t count, uint32_t stride);

// 0x005F4840 (cdecl): an occlusion query begun (the one in progress). Returns the result of making or issuing it
// (a failed one released).
HRESULT BeginOcclusionQuery();
// 0x005F5650 (cdecl): the query in progress ended and kept under `key` (its Issue failing: released, but kept
// all the same). D3DERR_NOTFOUND without one, E_FAIL if `key` is taken.
HRESULT EndOcclusionQuery(uint32_t key);
// 0x005F4C80 (cdecl): the pixels `key`'s query saw: S_OK with them (the query forgotten and released), S_FALSE
// not yet, D3DERR_NOTFOUND for no such query.
HRESULT GetOcclusionResult(uint32_t key, DWORD* pixels);
// 0x005F52D0 (cdecl): every kept query and the one in progress released.
void ReleaseOcclusionQueries();

// 0x005F55E0 (cdecl): the buffers, the queries, the device and Direct3D released.
void ReleaseDirect3D();

} // namespace Platform
