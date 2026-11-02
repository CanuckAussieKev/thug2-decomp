// The screen's shape and full-screen effects (Nx engine, PC) - with PartyMod's widescreen support
// (pillar- / letterboxing to the aspect wanted) and its DisableBlur setting.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include <cstdint>

#include "engine/gfx/screen.h"

#include "engine/gfx/draw_frame.h"
#include "engine/gfx/settings.h"
#include "engine/script/params.h"
#include "platform/sdl_window.h"
#include "platform/run_log.h"
#include "game/skate/career.h"
#include "game/skate/skate_module.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/effects_state.h"
#include "engine/gfx/view_setup.h"

namespace render {

namespace {
// NOT ORIGINAL grouping - screen.cpp's state (the original's globals). Made on first use, never destroyed
// (screen_state()).
struct ScreenState {
    float width_over_height_{};
    int script_screen_blur_{};
};

ScreenState& screen_state()
{
    static core::NoDestroy<ScreenState> state;
    return state.get();
}

} // namespace

namespace {

// Whether the device can do the blur (its render-target textures were made).
bool blur_available() { return core::at(renderer().screen_surfaces(), 0) != nullptr; }

// The last amount the script asked for (kept even when the blur is off).
auto& script_screen_blur() { return screen_state().script_screen_blur_; }

// The screen's aspect (width / height), and the original's angle factor constant (1.0).
float& view_ratio_slot() { return screen_state().width_over_height_; }

// The 640x480 -> back buffer conversion (init_direct_3d) and the letterbox flag.
uint8_t& cinema_bars_on() { return renderer().render_camera().letterbox_active_; }

void set_screen_offset_x(uint32_t x) { renderer().set_screen_offset_x(static_cast<int>(x)); }
void set_screen_offset_y(uint32_t y) { renderer().set_screen_offset_y(static_cast<int>(y)); }
uint32_t get_back_buffer_width() { return static_cast<uint32_t>(renderer().back_buffer_width()); }
uint32_t get_back_buffer_height() { return static_cast<uint32_t>(renderer().back_buffer_height()); }

} // namespace

uint8_t current_level()
{
    const modules::Skate* skate = modules::Skate::sole();
    if (!skate)
        return 0;
    return static_cast<uint8_t>(skate->career()->level_); // (its low byte, as the original reads it)
}

float desired_aspect_ratio()
{
    // (the window's size as unsigned numbers)
    auto width = static_cast<uint32_t>(sys::get_window_width());
    auto height = static_cast<uint32_t>(sys::get_window_height());
    if (current_level() == 0)
        return 4.0f / 3.0f;
    return static_cast<float>(width) / static_cast<float>(height);
}

bool& widescreen_menus()
{
    static bool on = config_int("features", "widescreen_menus", 1) != 0;
    return on;
}

bool get_front_end_widescreen() { return widescreen_menus() && current_level() == 0; }

float desired_3d_aspect_ratio()
{
    if (!get_front_end_widescreen())
        return desired_aspect_ratio();
    auto width = static_cast<uint32_t>(sys::get_window_width());
    auto height = static_cast<uint32_t>(sys::get_window_height());
    return static_cast<float>(width) / static_cast<float>(height);
}

// 0x004A0780. The original stores `aspect` as the screen's aspect (0x00701340). Originally from PartyMod Fixes: the aspect
// wanted instead (4:3 in the front end, the window's shape in a level); `aspect` is ignored. (The 3D view's
// aspect: with widescreen_menus the window's shape in the front end too.)
void set_screen_aspect_ratio(float /*aspect*/) { view_ratio_slot() = desired_3d_aspect_ratio(); }

// 0x004A0790
float fetch_view_ratio() { return view_ratio_slot(); }

// 0x004A07A0
void set_screen_angle(float angle) { view_setup().set_base_fov_angle(angle); }

// 0x004A07B0

// 0x004A07C0. The original returns the constant at 0x0067E544 (1.0). Originally from PartyMod Fixes: the view is widened for
// screens wider than 4:3 (the aspect / (4/3)) - unless that constant isn't 1 - and the screen's aspect
// (0x00701340) is updated too.
float fetch_fov_ratio()
{
    const float aspect = desired_3d_aspect_ratio();
    // (narrower than 4:3: the horizontal view isn't narrowed - the vertical one grows instead)
    const float factor = aspect > 4.0f / 3.0f ? aspect / (4.0f / 3.0f) : 1.0f;
    view_ratio_slot() = aspect;
    if (view_setup().angle_factor() != 1.0f)
        return view_setup().angle_factor();
    return factor;
}

// 0x004B2330. The original: if (blur_available()) effects_state().screen_blur() = amount;
// Originally from PartyMod Fixes: the DisableBlur setting (on by default) turns the blur off for good instead.
void set_screen_blur(uint32_t amount)
{
    if (blur_disabled()) {
        effects_state().screen_blur() = 0;
        return;
    }
    if (blur_available())
        effects_state().screen_blur() = amount;
}

// 0x004B2440. The original squashes the 3D view vertically to 3/4 of the screen, centred, and back:
//     int quarter = backbufferHeight / 4;
//     if (letterbox) { if (!active) { offsetY += quarter / 2; scaleY = 0.75f; active = true; } }
//     else if (active) { offsetY += -quarter / 2; scaleY = 1.0f; active = false; }
// (active 0x00786CBE, scaleY 0x00786D84, offsetY 0x00786D8C, backbufferHeight 0x00786D70). Originally from PartyMod Fixes:
// the 640x480 screen layout is fitted to the aspect wanted (pillarbox or letterbox) and the flag set, which then
// selects the fitted display region (set_display_region).
void set_letterbox(bool letterbox)
{
    const float back_buffer_aspect =
        static_cast<float>(get_back_buffer_width()) / static_cast<float>(get_back_buffer_height());
    const float desired = desired_aspect_ratio();
    if (back_buffer_aspect > desired) {
        // wider: bars left and right
        renderer().set_display_factor_x(desired / back_buffer_aspect * (static_cast<float>(get_back_buffer_width()) / 640.0f));
        renderer().set_display_factor_y(static_cast<float>(get_back_buffer_height()) / 480.0f);
        auto width = static_cast<uint32_t>(renderer().display_factor_x() * 640.0f);
        set_screen_offset_x((get_back_buffer_width() - width) / 2);
        set_screen_offset_y(static_cast<uint32_t>(16.0f * renderer().display_factor_y()));
    } else {
        renderer().set_display_factor_x(static_cast<float>(get_back_buffer_width()) / 640.0f);
        renderer().set_display_factor_y(back_buffer_aspect / desired * (static_cast<float>(get_back_buffer_height()) / 480.0f));
        auto height = static_cast<uint32_t>(renderer().display_factor_y() * 480.0f);
        set_screen_offset_x(0);
        set_screen_offset_y(static_cast<uint32_t>(16.0f * renderer().display_factor_y() +
                                                  // NOLINTNEXTLINE(bugprone-integer-division): as the original
                                                  static_cast<float>((get_back_buffer_height() - height) / 2)));
    }
    cinema_bars_on() = letterbox;
}

// 0x0048C170: SetScreenBlur <amount>. (PartyMod rewrote this function's first bytes with what they already are
// when DisableBlur is off - some patched executables "in the wild" remove the blur here.)
bool cfunc_set_screen_blur(script::Struct* params, script::Script* /*script*/)
{
    int amount = 0;
    params->fetch_int_value(0u, &amount, false); // the unnamed integer
    script_screen_blur() = amount;
    set_screen_blur(static_cast<uint32_t>(amount));
    return true;
}

} // namespace render
