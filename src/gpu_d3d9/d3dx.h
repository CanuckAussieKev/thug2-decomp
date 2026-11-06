// The D3DX9 utility calls the Direct3D 9 Gpu makes (gpu_d3d9.cpp), behind our own functions: the pixel shaders'
// assembly (Gpu::create_pixel_shader) and a surface's JPEG (Gpu::save_surface_jpeg - the screen shot script command's
// front buffer). d3dx.cpp is the only place that talks to D3DX; the renderer's maths - the vector transforms and the
// viewing matrices - is plain C++ (base/maths/view_transforms.h).
//
// Microsoft's D3DX9 (d3dx9_43.dll, from the Microsoft.DXSDK.D3DX NuGet package), through the standard API only - MinGW-w64's
// d3dx9 headers and libd3dx9_43.a offer the same calls.
#pragma once

#include <cstdint>

#include "engine/gfx/gpu_handles.h"

namespace d3dx {

// (defined in d3dx.cpp, which includes d3dx9.h)
// `source`: ps_1_x assembler text. `shader`, `errors`: receive an ID3DXBuffer* each; `errors` may be null.
long assemble_shader(const char* source, uint32_t length, void** shader, void** errors);
// An ID3DXBuffer's bytes, and the buffer released.
void* buffer_pointer(void* buffer);
void release_buffer(void* buffer);
// A surface saved as a JPEG file: the HRESULT.
long save_surface_to_jpg(const char* file, render::GpuSurface* surface);

} // namespace d3dx
