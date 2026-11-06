#pragma once

#include <basetsd.h>

#include <cstdint>
#include <array>

namespace render {

// Work out the loading bar's colours from its settings: the gradient's end colours, its width in pixels, the
// border colour, and the per-pixel gradient table.
void update_loading_bar_colors();

// 0x004B6FF0: the loading screen's frame, a timeSetEvent callback (0x004B7400 starts it, every 29 ms, winmm's timer
// thread): with the loading screen up (0x00786D04) and the timer's id set (0x00786D08), under the loading screen's
// lock (0x007052AC): the picture (0x004B6CD0), the bar filled by time (0x007052A0 seconds a tick added to 0x007052A4,
// of 0x007052A8), the loading console's panel (not original - gfx/loading_console.h), presented. The last three
// arguments are LPTIMECALLBACK's DWORD_PTR words (32 bits on x86, 64 on x64).
void __stdcall draw_loading_screen_frame(unsigned int id, unsigned int message, DWORD_PTR user, DWORD_PTR /*unused*/,
                                         DWORD_PTR /*unused*/);

// The bar's settings: position, size (width, height), border, and the three colours (r, g, b, 0x80).

} // namespace render
