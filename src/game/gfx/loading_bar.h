#pragma once

#include <cstdint>

namespace Nx {

// Work out the loading bar's colours from its settings: the gradient's end colours, its width in pixels, the
// border colour, and the per-pixel gradient table.
void UpdateLoadingBarColors();

// The gradient table's buffer on PC (100000 bytes, in place of the original's 0x00701688).
uint8_t* LoadingBarGradientBuffer();

// 0x004B6FF0: the loading screen's frame, a timeSetEvent callback (0x004B7400 starts it, every 29 ms, winmm's timer
// thread): with the loading screen up (0x00786D04) and the timer's id set (0x00786D08), under the loading screen's
// lock (0x007052AC): the picture (0x004B6CD0), the bar filled by time (0x007052A0 seconds a tick added to 0x007052A4,
// of 0x007052A8), the loading console's panel (not original - gfx/loading_console.h), presented.
void __stdcall DrawLoadingScreenFrame(unsigned int id, unsigned int message, unsigned long user, unsigned long, unsigned long);

extern int g_gradientWidth;
// The bar's settings: position, size (width, height), border, and the three colours (r, g, b, 0x80).
extern int g_barPosition[2];
extern int g_barWidth;
extern int g_barHeight;
extern int g_barBorder[2];
extern uint8_t g_barStartColor[4];
extern uint8_t g_barEndColor[4];
extern uint8_t g_barBorderColor[4];

extern float g_tickSeconds;

extern float g_elapsedSeconds;

extern float g_totalSeconds;

extern float g_floatTen;
extern void* g_screenMutex;
} // namespace Nx
