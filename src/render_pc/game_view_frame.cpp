// NOT ORIGINAL - the game view's frame (game_view_frame.h).

#include "render_pc/game_view_frame.h"

#include <algorithm>
#include <cstdint>

#include "base/core/no_destroy.h"
#include "engine/gfx/game_view.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/render_camera.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/ui_backend.h"
#include "render_pc/device.h"

namespace render::game_view_frame {

namespace {

constexpr uint32_t kPictureClear = 0xFF000000; // the picture before the frame (black, as letterboxed frames start)
constexpr uint32_t kWindowClear = 0xFF1C1D1F;  // the window under the UI's panels

// What begin changed, put back by end: the renderer's back buffer size and its 640x480 layout's factors.
struct SavedDisplay {
    bool active{};
    int back_buffer_width{};
    int back_buffer_height{};
    float factor_x{};
    float factor_y{};
    int offset_x{};
    int offset_y{};
    game_view::Request request;
};

SavedDisplay& saved_display()
{
    static core::NoDestroy<SavedDisplay> saved;
    return saved.get();
}

// A target's size.
struct TargetSize {
    uint32_t width{};
    uint32_t height{};
};

// The device's targets (the frame's: device.h) set, the whole of them the viewport, and cleared.
void switch_targets(TargetSize size, uint32_t argb)
{
    const uint32_t width = size.width;
    const uint32_t height = size.height;
    gpu().set_render_target(0, back_buffer_surface());
    gpu().set_depth_surface(back_depth_surface());
    const DeviceViewport whole = {.x = 0, .y = 0, .width = width, .height = height, .min_z_ = 0.0F, .max_z_ = 1.0F};
    renderer().set_device_viewport(whole);
    gpu().set_viewport({.x = 0, .y = 0, .width = width, .height = height, .min_z = 0.0F, .max_z = 1.0F});
    gpu().clear(0, nullptr, gpu_value::kClearTarget | gpu_value::kClearDepth, argb, 1.0F, 0);
}

} // namespace

bool begin()
{
    game_view::Request request;
    if (!game_view::take_request(&request) || !device_made() || view_targets_in_use()) {
        if (view_picture() && !view_targets_in_use()) {
            release_view_targets();
        }
        game_view::note_window();
        return false;
    }
    // (never larger than the window's back buffer: the panel showing it is in the window)
    const auto width = static_cast<uint32_t>(std::clamp(request.width, 1, static_cast<int>(presented_width())));
    const auto height = static_cast<uint32_t>(std::clamp(request.height, 1, static_cast<int>(presented_height())));
    if (!make_view_targets(width, height)) {
        game_view::note_window();
        return false;
    }
    auto& gfx = renderer();
    SavedDisplay& saved = saved_display();
    saved = {.active = true,
             .back_buffer_width = gfx.back_buffer_width(),
             .back_buffer_height = gfx.back_buffer_height(),
             .factor_x = gfx.display_factor_x(),
             .factor_y = gfx.display_factor_y(),
             .offset_x = gfx.screen_offset_x(),
             .offset_y = gfx.screen_offset_y(),
             .request = request};
    use_view_targets(true);
    switch_targets({.width = width, .height = height}, kPictureClear);
    gfx.set_back_buffer_width(static_cast<int>(width));
    gfx.set_back_buffer_height(static_cast<int>(height));
    // (noted first: the layout's factors follow the screen's shape, which is the picture's from here on)
    game_view::note_drawn({.size = {.width = static_cast<int>(width), .height = static_cast<int>(height)},
                           .shown = request.shown,
                           .texture = 0,
                           .layout = {}});
    set_letterbox(gfx.render_camera().letterbox_active_ != 0U);
    return true;
}

void end()
{
    SavedDisplay& saved = saved_display();
    if (!saved.active) {
        return;
    }
    saved.active = false;
    auto& gfx = renderer();
    const game_view::LayoutMap layout = {.scale_x = gfx.display_factor_x(),
                                         .scale_y = gfx.display_factor_y(),
                                         .offset_x = gfx.screen_offset_x(),
                                         .offset_y = gfx.screen_offset_y()};
    const game_view::Size size = {.width = gfx.back_buffer_width(), .height = gfx.back_buffer_height()};
    gfx.set_back_buffer_width(saved.back_buffer_width);
    gfx.set_back_buffer_height(saved.back_buffer_height);
    gfx.set_display_factor_x(saved.factor_x);
    gfx.set_display_factor_y(saved.factor_y);
    gfx.set_screen_offset_x(saved.offset_x);
    gfx.set_screen_offset_y(saved.offset_y);
    use_view_targets(false);
    switch_targets({.width = static_cast<uint32_t>(saved.back_buffer_width),
                    .height = static_cast<uint32_t>(saved.back_buffer_height)},
                   kWindowClear);
    game_view::note_drawn({.size = size,
                           .shown = saved.request.shown,
                           .texture = ui_backend().texture_id(view_picture()),
                           .layout = layout});
}

} // namespace render::game_view_frame
