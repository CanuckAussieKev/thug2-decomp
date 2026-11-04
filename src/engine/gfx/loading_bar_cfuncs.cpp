// The loading bar's script commands and the engine's loading screen calls (Nx, cdecl): see loading_bar_cfuncs.h.

#include "engine/gfx/loading_bar_cfuncs.h"

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/gfx/loading_bar.h"
#include "engine/gfx/loading_screen.h"
#include "engine/script/param_struct.h"
#include "base/core/checked_index.h"
#include "engine/gfx/screen_2d.h"

namespace render {

constexpr uint32_t kRed = 0x93F60062U;   // text unknown (r)
constexpr uint32_t kGreen = 0xFE2BE489U; // text unknown (g)
constexpr uint32_t kBlue = 0x8E411006U;  // text unknown (b)
constexpr uint8_t kHalfAlpha = 0x80;
constexpr uint32_t kPosX = 0x7323E97CU;   // text unknown (the x of a position)
constexpr uint32_t kPosY = 0x0424D9EAU;   // text unknown (the y)
constexpr uint32_t kWidth = 0x73E5BAD0U;  // Width
constexpr uint32_t kHeight = 0x0AB21AF0U; // Height

namespace {

uint8_t& stored_loading_screen_shown()
{
    static uint8_t loading_screen_shown{};
    return loading_screen_shown;
}

} // namespace

uint8_t loading_screen_shown() { return stored_loading_screen_shown(); }

namespace {

// Two integers into two settings (each left unset when missing - the original's stack garbage; 0 here).
bool choose_pair(script::Struct* params, uint32_t first, uint32_t second, int& setting_a, int& setting_b)
{
    int first_value = 0;
    int second_value = 0;
    params->fetch_int_value(first, &first_value, 0);
    params->fetch_int_value(second, &second_value, 0);
    setting_a = first_value;
    setting_b = second_value;
    update_loading_bar_colors();
    return true;
}

// A colour from r, g, b (alpha 0x80).
bool change_colour(script::Struct* params, std::array<uint8_t, 4>& setting)
{
    int red = 0;
    int green = 0;
    int blue = 0; // (unset when missing in the original)
    params->fetch_int_value(kRed, &red, 0);
    params->fetch_int_value(kGreen, &green, 0);
    params->fetch_int_value(kBlue, &blue, 0);
    std::array<uint8_t, 4> colour = {
        {static_cast<uint8_t>(red), static_cast<uint8_t>(green), static_cast<uint8_t>(blue), kHalfAlpha}};
    std::memcpy(setting.data(), colour.data(), sizeof(colour));
    update_loading_bar_colors();
    return true;
}

float& loading_bar_seconds()
{
    static float loading_bar_seconds{};
    return loading_bar_seconds;
}
} // namespace

// 0x00490770
void set_loading_bar_time(float seconds)
{
    if (seconds <= 0.0F) { // (NaN: started)
        return;
    }
    loading_bar_seconds() = seconds;
    update_loading_bar_colors();
    show_progress_bar(seconds);
}

// 0x004907A0
bool cfunc_set_loading_bar_pos(script::Struct* params, script::Script* /*script*/)
{
    return choose_pair(params, kPosX, kPosY, core::at(screen_2d().bar_position(), 0), core::at(screen_2d().bar_position(), 1));
}

// 0x004907F0
bool cfunc_set_loading_bar_size(script::Struct* params, script::Script* /*script*/)
{
    return choose_pair(params, kWidth, kHeight, screen_2d().progress_width(), screen_2d().progress_height());
}

// 0x00490840
bool cfunc_set_loading_bar_border(script::Struct* params, script::Script* /*script*/)
{
    return choose_pair(params, kWidth, kHeight, core::at(screen_2d().bar_border(), 0), core::at(screen_2d().bar_border(), 1));
}

// 0x004908E0
bool cfunc_set_loading_bar_start_color(script::Struct* params, script::Script* /*script*/)
{
    return change_colour(params, screen_2d().progress_from_color());
}

// 0x00490960
bool cfunc_set_loading_bar_end_color(script::Struct* params, script::Script* /*script*/)
{
    return change_colour(params, screen_2d().progress_to_color());
}

// 0x004909E0
bool cfunc_set_loading_bar_border_color(script::Struct* params, script::Script* /*script*/)
{
    return change_colour(params, screen_2d().progress_edge_color());
}

// 0x00490890
void display_loading_screen(const char* name, bool keep, bool black)
{
    free_loading_screen_sprite();
    show_loading_screen(name, keep, black);
    stored_loading_screen_shown() = 1U;
}

// 0x004908C0
void remove_loading_screen()
{
    hide_loading_screen();
    free_loading_screen_sprite();
    stored_loading_screen_shown() = 0U;
}

} // namespace render
