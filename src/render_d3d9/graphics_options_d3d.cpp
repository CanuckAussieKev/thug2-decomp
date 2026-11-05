// NOT ORIGINAL - the image-quality options on the device: see graphics_options_d3d.h.

#include "render_d3d9/graphics_options_d3d.h"
#include "render_d3d9/device_d3d.h"
#include "engine/gfx/gpu_handles.h"
#include "render_d3d9/d3d9_handles.h"
#include "engine/gfx/graphics_options.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "render_d3d9/d3d9_state.h"

namespace render::graphics_options {

namespace {

// NOT ORIGINAL grouping - the options' device state (were globals). Made on first use, never destroyed
// (device_state()).
struct DeviceState {
    int active_anisotropy_ = 0;
    int samples_ = 0;
    DWORD quality_ = 0;
    // The scene surface and its depth (default pool), and the full-size copy last drawn through them.
    IDirect3DSurface9* scene_colour_ = nullptr;
    IDirect3DSurface9* scene_depth_ = nullptr;
    IDirect3DTexture9* drawn_for_ = nullptr;
    bool unresolved_ = false;
};

DeviceState& device_state()
{
    static core::NoDestroy<DeviceState> state;
    return state.get();
}

IDirect3DTexture9* screen_copy(int index) { return core::at(d3d9::state().screen_textures(), index); }

struct SupportQuery {
    IDirect3D9* d3d;
    D3DFORMAT colour, depth;
    BOOL windowed;
    DWORD quality; // out: the quality levels both formats have
};

bool get_supported(int samples, void* context)
{
    auto* query = static_cast<SupportQuery*>(context);
    auto type = static_cast<D3DMULTISAMPLE_TYPE>(samples);
    DWORD colour_levels = 0;
    DWORD depth_levels = 0;
    if (FAILED(query->d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, query->colour,
                                                      query->windowed, type, &colour_levels)) ||
        FAILED(query->d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, query->depth, query->windowed,
                                                      type, &depth_levels))) {
        return false;
    }
    const DWORD levels = colour_levels < depth_levels ? colour_levels : depth_levels;
    if (levels == 0) {
        return false;
    }
    query->quality = levels;
    return true;
}

template <typename T>
void get_release(T*& pointer)
{
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

bool in_use()
{
    return device_state().samples_ > 0 && (device_state().scene_colour_ != nullptr) &&
           (device_state().scene_depth_ != nullptr);
}

} // namespace

uint32_t get_min_filter(bool linear, int anisotropy)
{
    if (!linear) {
        return D3DTEXF_POINT;
    }
    return anisotropy > 0 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR;
}

int& active_anisotropy() { return device_state().active_anisotropy_; }

int chosen_samples() { return device_state().samples_; }

uint32_t chosen_quality() { return device_state().quality_; }

void choose_multisample(GpuInstance* api, uint32_t back_buffer_format, uint32_t depth_format, bool windowed)
{
    IDirect3D9* const d3d = d3d9::object(api);
    device_state().samples_ = 0;
    device_state().quality_ = 0;
    if ((get_pull().msaa == 0) || !d3d) {
        return;
    }
    SupportQuery query = {.d3d = d3d, .colour = static_cast<D3DFORMAT>(back_buffer_format), .depth = static_cast<D3DFORMAT>(depth_format),
                          .windowed = windowed ? TRUE : FALSE, .quality = 0};
    device_state().samples_ = pick_samples(get_pull().msaa, &get_supported, &query);
    if (device_state().samples_ != 0) {
        device_state().quality_ = query.quality - 1; // (the highest)
    }
    get_log("graphics options: msaa=%d asked, %d samples (quality %lu) chosen", get_pull().msaa, device_state().samples_,
            static_cast<unsigned long>(device_state().quality_));
}

void create_scene_target()
{
    release_scene_target();
    const D3DPRESENT_PARAMETERS& pp = present_parameters();
    if (device_state().samples_ <= 0 || pp.MultiSampleType == D3DMULTISAMPLE_NONE) {
        return; // (the device was made without: the fallbacks in init_direct_3d)
    }
    IDirect3DDevice9* device = d3d9::state().d3d_device();
    const HRESULT colour =
        device->CreateRenderTarget(pp.BackBufferWidth, pp.BackBufferHeight, pp.BackBufferFormat, pp.MultiSampleType,
                                   pp.MultiSampleQuality, FALSE, &device_state().scene_colour_, nullptr);
    const HRESULT depth =
        device->CreateDepthStencilSurface(pp.BackBufferWidth, pp.BackBufferHeight, pp.AutoDepthStencilFormat,
                                          pp.MultiSampleType, pp.MultiSampleQuality, TRUE, &device_state().scene_depth_, nullptr);
    if (FAILED(colour) || FAILED(depth)) {
        get_log("graphics options: no multisampled scene surface (0x%08lX / 0x%08lX) - screen effects unsmoothed",
                static_cast<unsigned long>(colour), static_cast<unsigned long>(depth));
        release_scene_target();
    }
}

void release_scene_target()
{
    get_release(device_state().scene_colour_);
    get_release(device_state().scene_depth_);
    device_state().drawn_for_ = nullptr;
    device_state().unresolved_ = false;
}

bool set_scene_target(GpuTexture* target)
{
    IDirect3DTexture9* const texture = d3d9::object(target);
    if (!in_use() || !texture || (texture != screen_copy(0) && texture != screen_copy(1))) {
        return false;
    }
    d3d9::state().d3d_device()->SetRenderTarget(0, device_state().scene_colour_);
    d3d9::state().d3d_device()->SetDepthStencilSurface(device_state().scene_depth_);
    device_state().drawn_for_ = texture;
    device_state().unresolved_ = true;
    return true;
}

void resolve_scene_target(GpuTexture* target)
{
    IDirect3DTexture9* const texture = d3d9::object(target);
    if (!in_use() || !device_state().unresolved_ || texture != device_state().drawn_for_ ||
        !texture) {
        return;
    }
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) && surface) {
        d3d9::state().d3d_device()->StretchRect(device_state().scene_colour_, nullptr, surface, nullptr, D3DTEXF_NONE);
        surface->Release();
    }
    device_state().unresolved_ = false;
}

} // namespace render::graphics_options
