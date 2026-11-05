#pragma once

#include "render_d3d9/dynamic_vertices.h"

#include <windows.h>
#include <d3d9.h>

#include <cstdint>

// The original's Win32 platform layer's Direct3D side (0x005F45E0-0x005F5650): the renderer's dynamic vertex buffer (vertices
// written each frame, appended until full, then started again), the quads' index buffer, occlusion queries kept by
// a key until their answer is read, and releasing it all. (The hash_map, list and vector code between them,
// 0x005F4890-0x005F5390, is the C++ library's - stdext::hash_map's internals: here a std::unordered_map.)
namespace os_layer {

// 0x005F45E0 (cdecl): `count` texels through a palette: out[i] = palette[indices[i]].
void expand_palette(uint32_t* out, const uint8_t* indices, int count, const uint32_t* palette);

// 0x005F4620 (cdecl): the dynamic vertex buffer, `size` bytes (dynamic, write-only). Returns CreateVertexBuffer's
// result.
HRESULT create_dynamic_vertex_buffer(uint32_t size);
// 0x005F4650 / 0x005F4670 (cdecl): the dynamic vertex buffer / the quads' index buffer released.
void release_dynamic_vertex_buffer();
void release_quad_index_buffer();
// 0x005F4690 (cdecl): the index buffer for `quads` quads, two triangles each (0 1 2, 2 3 0). Returns Unlock's
// result.
HRESULT create_quad_index_buffer(uint32_t quads);
// 0x005F4840 (cdecl): an occlusion query begun (the one in progress). Returns the result of making or issuing it
// (a failed one released).
HRESULT begin_occlusion_query();
// 0x005F5650 (cdecl): the query in progress ended and kept under `key` (its Issue failing: released, but kept
// all the same). D3DERR_NOTFOUND without one, E_FAIL if `key` is taken.
HRESULT end_occlusion_query(uint32_t key);
// 0x005F52D0 (cdecl): every kept query and the one in progress released.
void release_occlusion_queries();

} // namespace os_layer
