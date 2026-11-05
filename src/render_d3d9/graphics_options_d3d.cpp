// NOT ORIGINAL - the image-quality options on the device: see graphics_options_d3d.h.

#include "render_d3d9/graphics_options_d3d.h"
#include "render_d3d9/device_d3d.h"
#include "engine/gfx/gpu_handles.h"
#include "render_d3d9/d3d9_handles.h"
#include "render_d3d9/device.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/graphics_options.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

#include "platform/run_log.h"
#include "base/core/no_destroy.h"

namespace render::graphics_options {

namespace {

// NOT ORIGINAL grouping - the options' device state (were globals). Made on first use, never destroyed
// (device_state()).
struct DeviceState {
    int active_anisotropy_ = 0;
    int samples_ = 0;
    DWORD quality_ = 0;
    // The scene surface and its depth (default pool), and the full-size copy last drawn through them.
    GpuSurface* scene_colour_ = nullptr;
    GpuSurface* scene_depth_ = nullptr;
    GpuTexture* drawn_for_ = nullptr;
    bool unresolved_ = false;
};

DeviceState& device_state()
{
    static core::NoDestroy<DeviceState> state;
    return state.get();
}

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

void get_release(GpuSurface*& pointer)
{
    if (pointer) {
        gpu().release_surface(pointer);
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
    const HRESULT colour = gpu().create_render_target({.width = pp.BackBufferWidth,
                                                       .height = pp.BackBufferHeight,
                                                       .format = static_cast<uint32_t>(pp.BackBufferFormat),
                                                       .samples = static_cast<uint32_t>(pp.MultiSampleType),
                                                       .quality = pp.MultiSampleQuality,
                                                       .lockable_or_discard = false},
                                                      &device_state().scene_colour_);
    const HRESULT depth = gpu().create_depth_surface({.width = pp.BackBufferWidth,
                                                      .height = pp.BackBufferHeight,
                                                      .format = static_cast<uint32_t>(pp.AutoDepthStencilFormat),
                                                      .samples = static_cast<uint32_t>(pp.MultiSampleType),
                                                      .quality = pp.MultiSampleQuality,
                                                      .lockable_or_discard = true},
                                                     &device_state().scene_depth_);
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
    if (!in_use() || !target || (target != screen_copy_texture(0) && target != screen_copy_texture(1))) {
        return false;
    }
    gpu().set_render_target(0, device_state().scene_colour_);
    gpu().set_depth_surface(device_state().scene_depth_);
    device_state().drawn_for_ = target;
    device_state().unresolved_ = true;
    return true;
}

void resolve_scene_target(GpuTexture* target)
{
    if (!in_use() || !device_state().unresolved_ || target != device_state().drawn_for_ ||
        !target) {
        return;
    }
    GpuSurface* surface = nullptr;
    if (SUCCEEDED(gpu().texture_surface(target, 0, &surface)) && surface) {
        gpu().copy_surface(device_state().scene_colour_, nullptr, surface, nullptr, gpu_value::kFilterNone);
        gpu().release_surface(surface);
    }
    device_state().unresolved_ = false;
}

} // namespace render::graphics_options
