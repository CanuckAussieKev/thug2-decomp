#pragma once

#include <d3d9.h>

namespace d3dx {

// (defined in d3dx.cpp, which includes d3dx9.h) A surface saved as a JPEG file: the HRESULT.
long save_surface_to_jpg(const char* file, IDirect3DSurface9* surface);

} // namespace d3dx
