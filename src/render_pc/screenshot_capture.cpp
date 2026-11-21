// NOT ORIGINAL - the screenshot key's renderer backend (render_pc/screenshot_capture.h): Direct3D 9.

#include "render_pc/screenshot_capture.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
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

// --- the clip recorder's ring ---

constexpr int kRingSlots = 3;
constexpr uint32_t kRingLatency = 2; // submits between a copy and its read: the GPU has finished it by then

struct RingSlot {
    render::GpuSurface* system = nullptr;
    uint64_t id = 0;
    uint64_t order = 0; // the submit count when it was filled
    bool pending = false;
};

struct Ring {
    std::array<RingSlot, kRingSlots> slots{};
    render::GpuSurface* resolved = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    uint32_t samples = 0;
    uint64_t submits = 0;
};

Ring& ring()
{
    static Ring state;
    return state;
}

// (the surfaces, the staging copies' contents with them)
void free_ring()
{
    Ring& state = ring();
    for (RingSlot& slot : state.slots) {
        get_release(slot.system);
        slot.pending = false;
    }
    get_release(state.resolved);
    state.width = 0;
    state.height = 0;
}

bool make_ring(const render::GpuSurfaceDesc& desc, std::string* error)
{
    Ring& state = ring();
    free_ring();
    render::Gpu& device = render::gpu();
    for (RingSlot& slot : state.slots) {
        if (device.create_offscreen_surface(desc.width, desc.height, desc.format, render::gpu_value::kPoolSystemMemory,
                                            &slot.system) < 0) {
            *error = "CreateOffscreenPlainSurface failed";
            free_ring();
            return false;
        }
    }
    if (desc.samples != 0 &&
        device.create_render_target({.width = desc.width,
                                     .height = desc.height,
                                     .format = desc.format,
                                     .samples = 0,
                                     .quality = 0,
                                     .lockable_or_discard = false},
                                    &state.resolved) < 0) {
        *error = "CreateRenderTarget failed";
        free_ring();
        return false;
    }
    state.width = desc.width;
    state.height = desc.height;
    state.format = desc.format;
    state.samples = desc.samples;
    return true;
}

// The back buffer copied into the next staging surface (the scene is ended).
bool ring_issue(uint64_t id, std::string* error)
{
    Ring& state = ring();
    render::Gpu& device = render::gpu();
    render::GpuSurface* back = nullptr;
    const long hr = device.back_buffer(&back);
    if (hr < 0 || !back) {
        *error = "no back buffer";
        return false;
    }
    render::GpuSurfaceDesc desc;
    device.surface_desc(back, &desc);
    bool ok = get_map(desc.format) != sys::screenshot::PixelFormat::kUnknown;
    if (!ok) {
        *error = "the back buffer's format isn't one we read";
    } else if (desc.width != state.width || desc.height != state.height || desc.format != state.format ||
               desc.samples != state.samples || state.slots.front().system == nullptr) {
        ok = make_ring(desc, error);
    }
    if (ok) {
        render::GpuSurface* source = back;
        if (state.resolved) {
            ok = device.copy_surface(back, nullptr, state.resolved, nullptr, render::gpu_value::kFilterNone) >= 0;
            source = state.resolved;
            if (!ok) {
                *error = "StretchRect failed";
            }
        }
        RingSlot& slot = state.slots.at(static_cast<size_t>(state.submits % kRingSlots));
        if (ok && device.read_render_target(source, slot.system) < 0) {
            *error = "GetRenderTargetData failed";
            ok = false;
        }
        if (ok) {
            slot.id = id;
            slot.order = ++state.submits;
            slot.pending = true;
        }
    }
    device.release_surface(back);
    return ok;
}

uint32_t ring_pending()
{
    uint32_t count = 0;
    for (const RingSlot& slot : ring().slots) {
        count += slot.pending ? 1U : 0U;
    }
    return count;
}

// The oldest pending staging surface read out (into `out` when given) and freed up.
bool ring_take(sys::screenshot::TickCycle* out, uint64_t* id)
{
    RingSlot* oldest = nullptr;
    for (RingSlot& slot : ring().slots) {
        if (slot.pending && (oldest == nullptr || slot.order < oldest->order)) {
            oldest = &slot;
        }
    }
    if (oldest == nullptr) {
        return false;
    }
    oldest->pending = false;
    *id = oldest->id;
    if (out == nullptr) {
        return true;
    }
    render::Gpu& device = render::gpu();
    render::GpuLockedRect locked = {};
    if (device.lock_surface(oldest->system, &locked, nullptr, render::gpu_value::kLockReadOnly) < 0) {
        out->bytes.clear();
        return true; // (the frame is counted, its pixels are not there)
    }
    const sys::screenshot::PixelFormat format = get_map(ring().format);
    const uint32_t width = ring().width;
    const uint32_t height = ring().height;
    const int row_bytes = static_cast<int>(width) * sys::screenshot::get_bytes_per_pixel(format);
    out->width = static_cast<int>(width);
    out->height = static_cast<int>(height);
    out->pitch = row_bytes;
    out->format = format;
    out->bytes.resize(static_cast<size_t>(row_bytes) * height);
    const std::span<const uint8_t> bits(static_cast<const uint8_t*>(locked.bits), static_cast<size_t>(locked.pitch) * height);
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(std::span<uint8_t>(out->bytes).subspan(static_cast<size_t>(row_bytes) * row).data(),
                    bits.subspan(static_cast<size_t>(locked.pitch) * row).data(), static_cast<size_t>(row_bytes));
    }
    device.unlock_surface(oldest->system);
    return true;
}

// The REC marker: a red dot and the elapsed time as seven-segment digits (mm:ss), drawn as cleared rectangles over a
// dark plate in the top left corner.
void draw_marker(int seconds, bool lit)
{
    render::Gpu& device = render::gpu();
    render::GpuSurface* back = nullptr;
    if (device.back_buffer(&back) < 0 || !back) {
        return;
    }
    render::GpuSurfaceDesc desc;
    device.surface_desc(back, &desc);
    device.release_surface(back);
    const int unit = std::max(2, static_cast<int>(desc.height) / 180);
    const int origin_x = unit * 4;
    const int origin_y = unit * 4;
    constexpr uint32_t kPlate = 0xFF101010;
    constexpr uint32_t kRed = 0xFFE01818;
    constexpr uint32_t kWhite = 0xFFF0F0F0;
    auto fill = [&](int left, int top, int width, int height, uint32_t colour) {
        const render::GpuRect rect{.left = left, .top = top, .right = left + width, .bottom = top + height};
        device.clear(1, &rect, render::gpu_value::kClearTarget, colour, 1.0F, 0);
    };
    const int digit_width = unit * 4;
    const int gap = unit;
    const int plate_width = unit * 7 + 4 * (digit_width + gap) + unit * 3;
    fill(origin_x, origin_y, plate_width, unit * 9, kPlate);
    if (lit) {
        fill(origin_x + unit, origin_y + unit * 2, unit * 5, unit * 5, kRed);
    }
    // segments: a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle
    static constexpr std::array<uint8_t, 10> kSegments = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    const int minutes = std::clamp(seconds / 60, 0, 99);
    const std::array<int, 4> digits = {minutes / 10, minutes % 10, (seconds % 60) / 10, seconds % 10};
    int x = origin_x + unit * 7;
    const int top = origin_y + unit;
    for (size_t index = 0; index < digits.size(); ++index) {
        const uint8_t mask = kSegments.at(static_cast<size_t>(digits.at(index)));
        const int half = unit * 3;
        auto bar = [&](int bit, int left, int up, int width, int height) {
            if ((mask & (1 << bit)) != 0) {
                fill(x + left, top + up, width, height, kWhite);
            }
        };
        bar(0, unit, 0, digit_width - unit * 2, unit);
        bar(1, digit_width - unit, unit, unit, half - unit);
        bar(2, digit_width - unit, half, unit, half - unit);
        bar(3, unit, half * 2 - unit, digit_width - unit * 2, unit);
        bar(4, 0, half, unit, half - unit);
        bar(5, 0, unit, unit, half - unit);
        bar(6, unit, half - unit / 2, digit_width - unit * 2, unit);
        x += digit_width + gap;
        if (index == 1) {
            fill(x, top + unit * 2, unit, unit, kWhite);
            fill(x, top + unit * 4, unit, unit, kWhite);
            x += unit * 2;
        }
    }
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

bool ring_submit(uint64_t id, int marker_seconds, bool marker_lit, sys::screenshot::TickCycle* ready, uint64_t* ready_id,
                 bool* have, std::string* error)
{
    *have = false;
    if (!render::device_made()) {
        *error = "no device yet";
        return false;
    }
    render::Gpu& device = render::gpu();
    if (device_lost_flag() || device.test_cooperative_level() != render::gpu_value::kResultOk) {
        *error = "the device is lost";
        return false;
    }
    device.end_scene();
    const bool ok = ring_issue(id, error);
    if (ok && ring_pending() > kRingLatency) {
        *have = ring_take(ready, ready_id);
    }
    device.begin_scene();
    if (ok && marker_seconds >= 0) {
        draw_marker(marker_seconds, marker_lit);
    }
    return ok;
}

bool ring_drain(sys::screenshot::TickCycle* ready, uint64_t* ready_id)
{
    return render::device_made() && ring_take(ready, ready_id);
}

void ring_release()
{
    if (render::device_made()) {
        free_ring();
    }
}

} // namespace gfx::screenshot_capture
