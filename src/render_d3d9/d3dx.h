// The D3DX9 utility calls the renderer makes, behind our own functions: the pixel shaders' assembly and the front
// buffer's JPEG (d3dx_jpg.h). The Direct3D 9 backend is the only place that talks to D3DX; its maths - the vector
// transforms and the viewing matrices - is plain C++ since stage 11 round 16 (base/maths/view_transforms.h).
//
// Microsoft's D3DX9 (d3dx9_43.dll, from the Microsoft.DXSDK.D3DX NuGet package), through the standard API only - MinGW-w64's
// d3dx9 headers and libd3dx9_43.a offer the same calls.
#pragma once

#include <cstdint>

namespace d3dx {

// (defined in d3dx.cpp, which includes d3dx9.h)
// `source`: ps_1_x assembler text. `shader`, `errors`: receive an ID3DXBuffer* each; `errors` may be null.
long assemble_shader(const char* source, uint32_t length, void** shader, void** errors);
// An ID3DXBuffer's bytes, and the buffer released.
void* buffer_pointer(void* buffer);
void release_buffer(void* buffer);

} // namespace d3dx
