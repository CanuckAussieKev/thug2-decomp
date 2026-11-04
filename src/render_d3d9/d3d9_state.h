#pragma once

// NOT ORIGINAL grouping - the renderer's Direct3D 9 objects (the original's globals beside the renderer's state,
// render::Renderer in renderer.h, which keeps what no graphics API names): Direct3D itself and the device, the depth
// surfaces, the back buffer and the screen copies. Only the files that talk to Direct3D include it.
#include <d3d9.h>

#include <array>
#include <cstddef>

#include "render_d3d9/d3d9_handles.h"
#include "render_d3d9/vertex_buffers.h"

namespace render::d3d9 {

constexpr std::size_t kScreenTextureCount = 9;

// The Direct3D objects (device.cpp makes and releases them; the draw code uses them). Trivially destructible with
// constant initial values: the accessor is a constant-initialised static, no guard.
class State {
public:
    [[nodiscard]] IDirect3DDevice9* d3d_device() const { return d3d_device_; }
    void set_d3d_device(IDirect3DDevice9* value)
    {
        d3d_device_ = value;
        set_buffer_device(handle(value)); // (the gpu buffers' device)
    }
    [[nodiscard]] IDirect3DDevice9* draw_device() const { return draw_device_; }
    void set_draw_device(IDirect3DDevice9* value) { draw_device_ = value; }
    std::array<IDirect3DTexture9*, kScreenTextureCount>& screen_textures() { return screen_textures_; }
    [[nodiscard]] IDirect3DSurface9* z_buffer_surface() const { return z_buffer_surface_; }
    void set_z_buffer_surface(IDirect3DSurface9* value) { z_buffer_surface_ = value; }
    std::array<IDirect3DSurface9*, kScreenTextureCount>& screen_surfaces() { return screen_surfaces_; }
    [[nodiscard]] IDirect3DSurface9* back_depth() const { return back_depth_; }
    void set_back_depth(IDirect3DSurface9* value) { back_depth_ = value; }
    [[nodiscard]] IDirect3DSurface9* back_buffer() const { return back_buffer_; }
    void set_back_buffer(IDirect3DSurface9* value) { back_buffer_ = value; }
    [[nodiscard]] IDirect3D9* direct_3d() const { return direct_3d_; }
    void set_direct_3d(IDirect3D9* value) { direct_3d_ = value; }

private:
    IDirect3DDevice9* d3d_device_{};
    IDirect3DDevice9* draw_device_{}; // the same device, kept twice
    std::array<IDirect3DTexture9*, kScreenTextureCount> screen_textures_{}; // full size to a sixteenth
    IDirect3DSurface9* z_buffer_surface_{};
    std::array<IDirect3DSurface9*, kScreenTextureCount> screen_surfaces_{}; // their level 0
    IDirect3DSurface9* back_depth_{};
    IDirect3DSurface9* back_buffer_{};
    IDirect3D9* direct_3d_{};
};

// The Direct3D objects (made on first use, never destroyed).
State& state();

} // namespace render::d3d9
