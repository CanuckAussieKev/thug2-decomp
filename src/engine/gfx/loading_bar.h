#pragma once

#include <cstdint>
#include <array>

namespace render {

// Work out the loading bar's colours from its settings: the gradient's end colours, its width in pixels, the
// border colour, and the per-pixel gradient table.
void update_loading_bar_colors();

// The gradient table's buffer on PC (100000 bytes, in place of the original's 0x00701688).
uint8_t* loading_bar_gradient_buffer();

// 0x004B6FF0: the loading screen's frame, a timeSetEvent callback (0x004B7400 starts it, every 29 ms, winmm's timer
// thread): with the loading screen up (0x00786D04) and the timer's id set (0x00786D08), under the loading screen's
// lock (0x007052AC): the picture (0x004B6CD0), the bar filled by time (0x007052A0 seconds a tick added to 0x007052A4,
// of 0x007052A8), the loading console's panel (not original - gfx/loading_console.h), presented.
void __stdcall draw_loading_screen_frame(unsigned int id, unsigned int message, unsigned long user, unsigned long,
                                         unsigned long);

extern int g_gradient_width;
// The bar's settings: position, size (width, height), border, and the three colours (r, g, b, 0x80).
extern std::array<int, 2> g_bar_position;
extern int g_progress_width;
extern int g_progress_height;
extern std::array<int, 2> g_bar_border;
extern std::array<uint8_t, 4> g_progress_from_color;
extern std::array<uint8_t, 4> g_progress_to_color;
extern std::array<uint8_t, 4> g_progress_edge_color;

extern float g_tick_seconds;

extern float g_elapsed_seconds;

extern float g_total_seconds;

extern float g_float_ten;
extern void* g_screen_mutex;
} // namespace render
