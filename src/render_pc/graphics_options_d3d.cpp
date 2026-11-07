// NOT ORIGINAL - the image-quality options on the device: see graphics_options_d3d.h.

#include "render_pc/graphics_options_d3d.h"
#include "engine/gfx/gpu_handles.h"
#include "render_pc/device.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/graphics_options.h"

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
    uint32_t quality_ = 0;
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
    uint32_t colour, depth;
    bool windowed;
    uint32_t quality; // out: the quality levels both formats have
};

bool get_supported(int samples, void* context)
{
    auto* query = static_cast<SupportQuery*>(context);
    const uint32_t levels = gpu_context().sample_quality_levels({.samples = static_cast<uint32_t>(samples),
                                                                 .colour_format = query->colour,
                                                                 .depth_format = query->depth,
                                                                 .windowed = query->windowed});
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

// (not while the game view draws into its picture - engine/gfx/game_view.h: its screen copies are drawn directly, the
// multisampled surface is the window's size)
bool in_use()
{
    return device_state().samples_ > 0 && (device_state().scene_colour_ != nullptr) &&
           (device_state().scene_depth_ != nullptr) && !view_targets_in_use();
}

} // namespace

uint32_t get_min_filter(bool linear, int anisotropy)
{
    if (!linear) {
        return gpu_value::kFilterPoint;
    }
    return anisotropy > 0 ? gpu_value::kFilterAnisotropic : gpu_value::kFilterLinear;
}

int& active_anisotropy() { return device_state().active_anisotropy_; }

int chosen_samples() { return device_state().samples_; }

uint32_t chosen_quality() { return device_state().quality_; }

void choose_multisample(uint32_t back_buffer_format, uint32_t depth_format, bool windowed)
{
    device_state().samples_ = 0;
    device_state().quality_ = 0;
    if (get_pull().msaa == 0) {
        return;
    }
    SupportQuery query = {.colour = back_buffer_format, .depth = depth_format, .windowed = windowed, .quality = 0};
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
    const GpuSettings pp = gpu_context().settings();
    if (device_state().samples_ <= 0 || pp.samples == 0) {
        return; // (the device was made without: the fallbacks in init_direct_3d)
    }
    const long colour = gpu().create_render_target({.width = pp.width,
                                                    .height = pp.height,
                                                    .format = pp.colour_format,
                                                    .samples = pp.samples,
                                                    .quality = pp.sample_quality,
                                                    .lockable_or_discard = false},
                                                   &device_state().scene_colour_);
    const long depth = gpu().create_depth_surface({.width = pp.width,
                                                   .height = pp.height,
                                                   .format = pp.depth_format,
                                                   .samples = pp.samples,
                                                   .quality = pp.sample_quality,
                                                   .lockable_or_discard = true},
                                                  &device_state().scene_depth_);
    if (colour < 0 || depth < 0) {
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
    if (gpu().texture_surface(target, 0, &surface) >= 0 && surface) {
        gpu().copy_surface(device_state().scene_colour_, nullptr, surface, nullptr, gpu_value::kFilterNone);
        gpu().release_surface(surface);
    }
    device_state().unresolved_ = false;
}

} // namespace render::graphics_options
