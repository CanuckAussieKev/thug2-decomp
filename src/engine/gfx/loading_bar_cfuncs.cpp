// The loading bar's script commands and the engine's loading screen calls (Nx, cdecl): see loading_bar_cfuncs.h.

#include "engine/gfx/loading_bar_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "engine/gfx/loading_bar.h"
#include "engine/gfx/loading_screen.h"
#include "engine/script/param_struct.h"

namespace render {

uint8_t g_loading_screen_shown;

namespace {

// Two integers into two settings (each left unset when missing - the original's stack garbage; 0 here).
bool choose_pair(script::Struct* params, uint32_t first, uint32_t second, int& setting_a, int& setting_b)
{
    int a = 0, b = 0;
    params->fetch_int_value(first, &a, 0);
    params->fetch_int_value(second, &b, 0);
    setting_a = a;
    setting_b = b;
    update_loading_bar_colors();
    return true;
}

// A colour from r, g, b (alpha 0x80).
bool change_colour(script::Struct* params, uint8_t (&setting)[4])
{
    int r = 0, g = 0, b = 0; // (unset when missing in the original)
    params->fetch_int_value(0x93F60062u, &r, 0);
    params->fetch_int_value(0xFE2BE489u, &g, 0);
    params->fetch_int_value(0x8E411006u, &b, 0);
    uint8_t colour[4] = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 0x80};
    std::memcpy(setting, colour, sizeof(colour));
    update_loading_bar_colors();
    return true;
}

bool& loading_screen_shown() { return reinterpret_cast<bool&>(g_loading_screen_shown); }

} // namespace

float g_loading_bar_seconds;

// 0x00490770
void set_loading_bar_time(float seconds)
{
    if (seconds <= 0.0f) // (NaN: started)
        return;
    g_loading_bar_seconds = seconds;
    update_loading_bar_colors();
    show_progress_bar(seconds);
}

// 0x004907A0
bool cfunc_set_loading_bar_pos(script::Struct* params, script::Script* /*script*/)
{
    return choose_pair(params, 0x7323E97Cu, 0x0424D9EAu, g_bar_position[0], g_bar_position[1]);
}

// 0x004907F0
bool cfunc_set_loading_bar_size(script::Struct* params, script::Script* /*script*/)
{
    return choose_pair(params, 0x73E5BAD0u, 0x0AB21AF0u, g_progress_width, g_progress_height);
}

// 0x00490840
bool cfunc_set_loading_bar_border(script::Struct* params, script::Script* /*script*/)
{
    return choose_pair(params, 0x73E5BAD0u, 0x0AB21AF0u, g_bar_border[0], g_bar_border[1]);
}

// 0x004908E0
bool cfunc_set_loading_bar_start_color(script::Struct* params, script::Script* /*script*/)
{
    return change_colour(params, g_progress_from_color);
}

// 0x00490960
bool cfunc_set_loading_bar_end_color(script::Struct* params, script::Script* /*script*/)
{
    return change_colour(params, g_progress_to_color);
}

// 0x004909E0
bool cfunc_set_loading_bar_border_color(script::Struct* params, script::Script* /*script*/)
{
    return change_colour(params, g_progress_edge_color);
}

// 0x00490890
void display_loading_screen(const char* name, bool keep, bool black)
{
    free_loading_screen_sprite();
    show_loading_screen(name, keep, black);
    loading_screen_shown() = true;
}

// 0x004908C0
void remove_loading_screen()
{
    hide_loading_screen();
    free_loading_screen_sprite();
    loading_screen_shown() = false;
}

} // namespace render
