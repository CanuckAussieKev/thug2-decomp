#pragma once

#include <cstdint>

namespace script {
class Context;
class Struct;
} // namespace script

// The loading bar's script commands and the engine's loading screen calls (Nx, cdecl).
namespace render {

// 0x00490770: with a time above 0 (or NaN), the loading bar run for it (kept at 0x006FC498; its colours worked out
// first).
void set_loading_bar_time(float seconds);
// 0x004907A0 / 0x004907F0 / 0x00490840: SetLoadingBarPos x y / SetLoadingBarSize width height /
// SetLoadingBarBorder width height (0x0067DDB4 / 0x0067DDBC / 0x0067DDC4; the colours worked out again). True.
bool cfunc_set_loading_bar_pos(script::Struct* params, script::Context context);
bool cfunc_set_loading_bar_size(script::Struct* params, script::Context context);
bool cfunc_set_loading_bar_border(script::Struct* params, script::Context context);
// 0x004908E0 / 0x00490960 / 0x004909E0: SetLoadingBarStartColor / EndColor / border_color r g b (alpha 0x80;
// 0x0067DDCC / 0x0067DDD0 / 0x0067DDD4). True.
bool cfunc_set_loading_bar_start_color(script::Struct* params, script::Context context);
bool cfunc_set_loading_bar_end_color(script::Struct* params, script::Context context);
bool cfunc_set_loading_bar_border_color(script::Struct* params, script::Context context);
// 0x00490890: a loading screen shown (the last one's sprite freed first; 0x006FC49C set).
void display_loading_screen(const char* name, bool keep, bool black);
// 0x004908C0: the loading screen hidden and its sprite freed (0x006FC49C cleared).
void remove_loading_screen();

uint8_t loading_screen_shown();
} // namespace render
