#pragma once

// NOT ORIGINAL - the graphics backend's objects as the engine's headers hold them: a pointer to an incomplete type per
// kind of object, so no header outside the backend names a graphics API (docs/architecture.md "Layers"). The pointer
// is the backend's own object - with Direct3D 9 its COM interface, the same address the original kept -, so a handle
// costs exactly what the original's pointer did: no wrapper, no table, no indirection. Only the files that talk to
// Direct3D see what a handle is (render_pc/d3d9_handles.h converts, a cast of the pointer's type and nothing at run time).
namespace render {

struct GpuInstance;     // the API itself (Direct3D 9: IDirect3D9)
struct GpuDevice;       // a device (IDirect3DDevice9)
struct GpuTexture;      // a texture (IDirect3DTexture9)
struct GpuSurface;      // a surface: a texture's level, the back buffer, a depth buffer (IDirect3DSurface9)
struct GpuVertexBuffer; // a vertex buffer on the card (IDirect3DVertexBuffer9)
struct GpuIndexBuffer;  // an index buffer on the card (IDirect3DIndexBuffer9)
struct GpuPixelShader;  // a pixel shader (IDirect3DPixelShader9)
struct GpuVertexShader; // a vertex shader (IDirect3DVertexShader9)
struct GpuQuery;        // a query: an occlusion count (IDirect3DQuery9)

} // namespace render
