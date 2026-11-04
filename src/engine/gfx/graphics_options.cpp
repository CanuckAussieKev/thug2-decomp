// NOT ORIGINAL - image-quality options (see graphics_options.h).

#include "engine/gfx/device_d3d.h"
#include "gpu/gpu_handles.h"
#include "gpu/d3d9_handles.h"
#include "engine/gfx/graphics_options.h"

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <array>

#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "base/core/ints.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/d3d9_state.h"
#include "engine/gfx/graphics_options_d3d.h"

namespace render::graphics_options {

constexpr std::size_t kOptionTextSize = 32;
constexpr int kMaxAnisotropy = 16;
constexpr int kMaxMultisample = 16;

namespace {

// NOT ORIGINAL grouping - graphics_options.cpp's state (were globals). Made on first use, never destroyed
// (graphics_options_state()).
struct GraphicsOptionsState {
    Options options_{};
    int active_anisotropy_ = 0;
    int samples_ = 0;
    DWORD quality_ = 0;
    // The scene surface and its depth (default pool), and the full-size copy last drawn through them.
    IDirect3DSurface9* scene_colour_ = nullptr;
    IDirect3DSurface9* scene_depth_ = nullptr;
    IDirect3DTexture9* drawn_for_ = nullptr;
    bool unresolved_ = false;
};

GraphicsOptionsState& graphics_options_state()
{
    static core::NoDestroy<GraphicsOptionsState> state;
    return state.get();
}

IDirect3DTexture9* screen_copy(int index) { return core::at(d3d9::state().screen_textures(), index); }

constexpr float kMinScale = 0.25F;
constexpr float kMaxScale = 8.0F;

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
    return graphics_options_state().samples_ > 0 && (graphics_options_state().scene_colour_ != nullptr) &&
           (graphics_options_state().scene_depth_ != nullptr);
}

} // namespace

Options& get_pull() { return graphics_options_state().options_; }

void read_in()
{
    graphics_options_state().options_.anisotropy = parse_anisotropy(build_info::get_int(build_info::kGraphics, "anisotropy", 0));
    graphics_options_state().options_.msaa = parse_multisample(build_info::get_int(build_info::kGraphics, "msaa", 0));
    std::array<char, kOptionTextSize> text{};
    build_info::fetch_text(build_info::kGraphics, "draw_distance_scale", "1", text.data(), sizeof(text));
    graphics_options_state().options_.draw_distance_scale_ = parse_scale(text.data());
    build_info::fetch_text(build_info::kGraphics, "lod_distance_scale", "1", text.data(), sizeof(text));
    graphics_options_state().options_.lod_distance_scale_ = parse_scale(text.data());
    if ((graphics_options_state().options_.anisotropy != 0) || (graphics_options_state().options_.msaa != 0) ||
        graphics_options_state().options_.draw_distance_scale_ != 1.0F ||
        graphics_options_state().options_.lod_distance_scale_ != 1.0F) {
        get_log("graphics options: anisotropy=%d msaa=%d draw_distance_scale=%.3g lod_distance_scale=%.3g",
                graphics_options_state().options_.anisotropy, graphics_options_state().options_.msaa, graphics_options_state().options_.draw_distance_scale_, graphics_options_state().options_.lod_distance_scale_);
    }
}

int parse_anisotropy(int value) { return value < 2 ? 0 : std::min(value, kMaxAnisotropy); }

int parse_multisample(int value) { return value < 2 ? 0 : std::min(value, kMaxMultisample); }

float parse_scale(const char* text)
{
    if (!text || (*text == '\0')) {
        return 1.0F;
    }
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || !std::isfinite(value) || !(value > 0.0)) {
        return 1.0F;
    }
    auto scale = static_cast<float>(value);
    return scale < kMinScale ? kMinScale : std::min(scale, kMaxScale);
}

int device_anisotropy(int option, bool device_can_filter, uint32_t max_anisotropy)
{
    if (option < 2 || !device_can_filter || max_anisotropy < 2) {
        return 0;
    }
    return ints::as_unsigned(option) > max_anisotropy ? static_cast<int>(max_anisotropy) : option;
}

uint32_t get_min_filter(bool linear, int anisotropy)
{
    if (!linear) {
        return D3DTEXF_POINT;
    }
    return anisotropy > 0 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR;
}

int pick_samples(int option, bool (*supported)(int samples, void* context), void* context)
{
    for (int samples = option; samples >= 2; --samples) {
        if (supported(samples, context)) {
            return samples;
        }
    }
    return 0;
}

float get_scaled(float distance, float scale) { return scale == 1.0F ? distance : distance * scale; }

int& active_anisotropy() { return graphics_options_state().active_anisotropy_; }

int chosen_samples() { return graphics_options_state().samples_; }

uint32_t chosen_quality() { return graphics_options_state().quality_; }

void choose_multisample(GpuInstance* api, uint32_t back_buffer_format, uint32_t depth_format, bool windowed)
{
    IDirect3D9* const d3d = d3d9::object(api);
    graphics_options_state().samples_ = 0;
    graphics_options_state().quality_ = 0;
    if ((graphics_options_state().options_.msaa == 0) || !d3d) {
        return;
    }
    SupportQuery query = {.d3d = d3d, .colour = static_cast<D3DFORMAT>(back_buffer_format), .depth = static_cast<D3DFORMAT>(depth_format),
                          .windowed = windowed ? TRUE : FALSE, .quality = 0};
    graphics_options_state().samples_ = pick_samples(graphics_options_state().options_.msaa, &get_supported, &query);
    if (graphics_options_state().samples_ != 0) {
        graphics_options_state().quality_ = query.quality - 1; // (the highest)
    }
    get_log("graphics options: msaa=%d asked, %d samples (quality %lu) chosen", graphics_options_state().options_.msaa, graphics_options_state().samples_,
            static_cast<unsigned long>(graphics_options_state().quality_));
}

void create_scene_target()
{
    release_scene_target();
    const D3DPRESENT_PARAMETERS& pp = present_parameters();
    if (graphics_options_state().samples_ <= 0 || pp.MultiSampleType == D3DMULTISAMPLE_NONE) {
        return; // (the device was made without: the fallbacks in init_direct_3d)
    }
    IDirect3DDevice9* device = d3d9::state().d3d_device();
    const HRESULT colour =
        device->CreateRenderTarget(pp.BackBufferWidth, pp.BackBufferHeight, pp.BackBufferFormat, pp.MultiSampleType,
                                   pp.MultiSampleQuality, FALSE, &graphics_options_state().scene_colour_, nullptr);
    const HRESULT depth =
        device->CreateDepthStencilSurface(pp.BackBufferWidth, pp.BackBufferHeight, pp.AutoDepthStencilFormat,
                                          pp.MultiSampleType, pp.MultiSampleQuality, TRUE, &graphics_options_state().scene_depth_, nullptr);
    if (FAILED(colour) || FAILED(depth)) {
        get_log("graphics options: no multisampled scene surface (0x%08lX / 0x%08lX) - screen effects unsmoothed",
                static_cast<unsigned long>(colour), static_cast<unsigned long>(depth));
        release_scene_target();
    }
}

void release_scene_target()
{
    get_release(graphics_options_state().scene_colour_);
    get_release(graphics_options_state().scene_depth_);
    graphics_options_state().drawn_for_ = nullptr;
    graphics_options_state().unresolved_ = false;
}

bool set_scene_target(GpuTexture* target)
{
    IDirect3DTexture9* const texture = d3d9::object(target);
    if (!in_use() || !texture || (texture != screen_copy(0) && texture != screen_copy(1))) {
        return false;
    }
    d3d9::state().d3d_device()->SetRenderTarget(0, graphics_options_state().scene_colour_);
    d3d9::state().d3d_device()->SetDepthStencilSurface(graphics_options_state().scene_depth_);
    graphics_options_state().drawn_for_ = texture;
    graphics_options_state().unresolved_ = true;
    return true;
}

void resolve_scene_target(GpuTexture* target)
{
    IDirect3DTexture9* const texture = d3d9::object(target);
    if (!in_use() || !graphics_options_state().unresolved_ || texture != graphics_options_state().drawn_for_ ||
        !texture) {
        return;
    }
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) && surface) {
        d3d9::state().d3d_device()->StretchRect(graphics_options_state().scene_colour_, nullptr, surface, nullptr, D3DTEXF_NONE);
        surface->Release();
    }
    graphics_options_state().unresolved_ = false;
}

} // namespace render::graphics_options
