#pragma once

// NOT ORIGINAL - the graphics handles (gpu/gpu_handles.h) as Direct3D 9's objects, for the files that talk to
// Direct3D: each handle is its object's own pointer, so the conversions only change the pointer's type (the same
// address, nothing done at run time). Included only by Direct3D implementation files.
#include <d3d9.h>

#include "gpu/gpu_handles.h"

namespace render::d3d9 {

namespace detail {
// The same address as a pointer to To (a handle and its object: never dereferenced as the other's type).
template <typename To, typename From>
To* same_address(From* pointer) noexcept
{
    void* const address = static_cast<void*>(pointer);
    return static_cast<To*>(address);
}
} // namespace detail

// The Direct3D object behind a handle.
inline IDirect3D9* object(GpuInstance* handle) noexcept { return detail::same_address<IDirect3D9>(handle); }
inline IDirect3DDevice9* object(GpuDevice* handle) noexcept { return detail::same_address<IDirect3DDevice9>(handle); }
inline IDirect3DTexture9* object(GpuTexture* handle) noexcept
{
    return detail::same_address<IDirect3DTexture9>(handle);
}
inline IDirect3DSurface9* object(GpuSurface* handle) noexcept
{
    return detail::same_address<IDirect3DSurface9>(handle);
}
inline IDirect3DVertexBuffer9* object(GpuVertexBuffer* handle) noexcept
{
    return detail::same_address<IDirect3DVertexBuffer9>(handle);
}
inline IDirect3DIndexBuffer9* object(GpuIndexBuffer* handle) noexcept
{
    return detail::same_address<IDirect3DIndexBuffer9>(handle);
}

// The handle of a Direct3D object.
inline GpuInstance* handle(IDirect3D9* object) noexcept { return detail::same_address<GpuInstance>(object); }
inline GpuDevice* handle(IDirect3DDevice9* object) noexcept { return detail::same_address<GpuDevice>(object); }
inline GpuTexture* handle(IDirect3DTexture9* object) noexcept { return detail::same_address<GpuTexture>(object); }
inline GpuSurface* handle(IDirect3DSurface9* object) noexcept { return detail::same_address<GpuSurface>(object); }
inline GpuVertexBuffer* handle(IDirect3DVertexBuffer9* object) noexcept
{
    return detail::same_address<GpuVertexBuffer>(object);
}
inline GpuIndexBuffer* handle(IDirect3DIndexBuffer9* object) noexcept
{
    return detail::same_address<GpuIndexBuffer>(object);
}

// A handle's storage as the out-parameter a Direct3D call fills (CreateTexture's IDirect3DTexture9**...): the call
// writes the new object's pointer straight into the handle, as the original's calls wrote into their fields - on
// failure too, whatever Direct3D leaves there.
inline IDirect3DTexture9** out(GpuTexture** slot) noexcept { return detail::same_address<IDirect3DTexture9*>(slot); }
inline IDirect3DSurface9** out(GpuSurface** slot) noexcept { return detail::same_address<IDirect3DSurface9*>(slot); }
inline IDirect3DVertexBuffer9** out(GpuVertexBuffer** slot) noexcept
{
    return detail::same_address<IDirect3DVertexBuffer9*>(slot);
}
inline IDirect3DIndexBuffer9** out(GpuIndexBuffer** slot) noexcept
{
    return detail::same_address<IDirect3DIndexBuffer9*>(slot);
}

} // namespace render::d3d9
