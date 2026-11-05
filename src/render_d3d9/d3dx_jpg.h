#pragma once

#include "engine/gfx/gpu_handles.h"

namespace d3dx {

// (defined in d3dx.cpp, which includes d3dx9.h) A surface saved as a JPEG file: the HRESULT. The Direct3D 9 backend's
// own: another backend reads the surface's pixels back (Gpu::lock_surface) and encodes them itself.
long save_surface_to_jpg(const char* file, render::GpuSurface* surface);

} // namespace d3dx
