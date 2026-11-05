// NOT ORIGINAL - the screenshot key's renderer backend (render_pc/screenshot_capture.h): Direct3D 9.

#include "render_pc/screenshot_capture.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <span>
#include <string>

#include "base/core/format.h"
#include "platform/screenshot_files.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "render_pc/device.h"
#include "engine/gfx/engine_parts.h"

namespace gfx::screenshot_capture {

constexpr std::size_t kCaptureTextSize = 128;

namespace {

// The renderer's "device lost" flag (render::begin_frame_draw doesn't begin a scene while it's set).
bool device_lost_flag() { return static_cast<uint8_t>(render::engine_parts().loading_screen_up()) != 0; }

sys::screenshot::PixelFormat get_map(uint32_t format)
{
    using sys::screenshot::PixelFormat;
    switch (format) {
    case render::gpu_value::kFormatX8R8G8B8:
        return PixelFormat::kX8r8g8b8;
    case render::gpu_value::kFormatA8R8G8B8:
        return PixelFormat::kA8r8g8b8;
    case render::gpu_value::kFormatR5G6B5:
        return PixelFormat::kR5g6b5;
    case render::gpu_value::kFormatX1R5G5B5:
    case render::gpu_value::kFormatA1R5G5B5:
        return PixelFormat::kX1r5g5b5;
    default:
        return PixelFormat::kUnknown;
    }
}

void get_release(render::GpuSurface*& surface)
{
    if (surface) {
        render::gpu().release_surface(surface);
        surface = nullptr;
    }
}

// The copy, with the scene ended around it (StretchRect / GetRenderTargetData outside a scene, as Direct3D wants).
bool dup_from(sys::screenshot::TickCycle* out, std::string* error)
{
    render::Gpu& device = render::gpu();
    render::GpuSurface* back = nullptr;
    render::GpuSurface* resolved = nullptr;
    render::GpuSurface* system = nullptr;
    auto fail = [&](const char* why, long hr) {
        std::array<char, kCaptureTextSize> text{};
        text::format_to(text, "%s (0x%08lX)", why, static_cast<unsigned long>(hr));
        *error = text.data();
        get_release(system);
        get_release(resolved);
        get_release(back);
        return false;
    };
    long hr = device.back_buffer(&back);
    if (hr < 0 || !back) {
        return fail("no back buffer", hr);
    }
    render::GpuSurfaceDesc desc; // (filled by surface_desc)
    device.surface_desc(back, &desc);
    const sys::screenshot::PixelFormat format = get_map(desc.format);
    if (format == sys::screenshot::PixelFormat::kUnknown) {
        return fail("the back buffer's format isn't one we read", static_cast<long>(desc.format));
    }
    render::GpuSurface* source = back;
    if (desc.samples != 0) {
        // (multisampled - [Graphics] AntiAliasing: resolved into a plain render target first)
        hr = device.create_render_target({.width = desc.width,
                                          .height = desc.height,
                                          .format = desc.format,
                                          .samples = 0,
                                          .quality = 0,
                                          .lockable_or_discard = false},
                                         &resolved);
        if (hr < 0) {
            return fail("CreateRenderTarget failed", hr);
        }
        hr = device.copy_surface(back, nullptr, resolved, nullptr, render::gpu_value::kFilterNone);
        if (hr < 0) {
            return fail("StretchRect failed", hr);
        }
        source = resolved;
    }
    hr = device.create_offscreen_surface(desc.width, desc.height, desc.format, render::gpu_value::kPoolSystemMemory,
                                         &system);
    if (hr < 0) {
        return fail("CreateOffscreenPlainSurface failed", hr);
    }
    hr = device.read_render_target(source, system);
    if (hr < 0) {
        return fail("GetRenderTargetData failed", hr);
    }
    render::GpuLockedRect locked = {};
    hr = device.lock_surface(system, &locked, nullptr, render::gpu_value::kLockReadOnly);
    if (hr < 0) {
        return fail("LockRect failed", hr);
    }
    const int row_bytes = static_cast<int>(desc.width) * sys::screenshot::get_bytes_per_pixel(format);
    out->width = static_cast<int>(desc.width);
    out->height = static_cast<int>(desc.height);
    out->pitch = row_bytes;
    out->format = format;
    out->bytes.resize(static_cast<size_t>(row_bytes) * desc.height);
    for (uint32_t row = 0; row < desc.height; ++row) {
        std::memcpy(std::span<uint8_t>(out->bytes).subspan(static_cast<size_t>(row_bytes) * row).data(),
                    std::span<const uint8_t>(static_cast<const uint8_t*>(locked.bits),
                                             static_cast<size_t>(locked.pitch) * desc.height)
                        .subspan(static_cast<size_t>(locked.pitch) * row)
                        .data(),
                    row_bytes);
    }
    device.unlock_surface(system);
    get_release(system);
    get_release(resolved);
    get_release(back);
    return true;
}

} // namespace

bool capture_back_buffer(sys::screenshot::TickCycle* out, std::string* error)
{
    if (!render::device_made()) {
        *error = "no device yet";
        return false;
    }
    render::Gpu& device = render::gpu();
    if (device_lost_flag() || device.test_cooperative_level() != render::gpu_value::kResultOk) {
        *error = "the device is lost";
        return false;
    }
    // (the main loop's scene is open here: render::begin_frame_draw began it, the end of the frame - 0x004B2750 - ends it)
    device.end_scene();
    const bool ok = dup_from(out, error);
    device.begin_scene();
    return ok;
}

} // namespace gfx::screenshot_capture
