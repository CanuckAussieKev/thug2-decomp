// The D3DX9 calls the renderer makes (see d3dx.h): Microsoft's D3DX9 through its standard API.
#include "render_pc/d3dx.h"
#include "base/core/raw_view.h"

#include <windows.h>

#include <d3d9.h>
#include <d3dx9.h>

#include <cstdint>
#include "engine/gfx/gpu_handles.h"
#include "render_pc/d3d9_handles.h"
#include "render_pc/d3dx_jpg.h"

namespace d3dx {

long save_surface_to_jpg(const char* file, render::GpuSurface* surface)
{
    return D3DXSaveSurfaceToFileA(file, D3DXIFF_JPG, render::d3d9::object(surface), nullptr, nullptr);
}

long assemble_shader(const char* source, uint32_t length, void** shader, void** errors)
{
    // (D3DXAssembleShader's ID3DXBuffer is a COM object with the same vtable the callers use)
    return D3DXAssembleShader(source, length, nullptr, nullptr, 0, raw::as<LPD3DXBUFFER>(shader),
                              raw::as<LPD3DXBUFFER>(errors));
}
void* buffer_pointer(void* buffer) { return static_cast<ID3DXBuffer*>(buffer)->GetBufferPointer(); }
void release_buffer(void* buffer) { static_cast<ID3DXBuffer*>(buffer)->Release(); }

} // namespace d3dx
