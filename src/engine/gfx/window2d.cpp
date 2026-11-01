// The engine's 2D windows: see window2d.h.

#include "engine/gfx/window2d.h"

#include "engine/core/dbg.h"
#include "engine/core/manager.h"

namespace render {

ScreenWindow* g_free_windows;

namespace {

ScreenWindow*& free_windows() { return g_free_windows; }

} // namespace

// 0x004A1580
ScreenWindow::ScreenWindow(int x, int y, int width, int height) : pos_x_(x), y_(y), span_(width), level_y_(height) {}

// 0x004A1600
void ScreenWindow::tick_engine() { debug::printf("STUB: PlatUpdateEngine\n"); }

// 0x004A15C0
void ScreenWindow::change_location(int x, int y)
{
    pos_x_ = x;
    y_ = y;
    tick_engine();
}

// 0x004A15E0
void ScreenWindow::change_extent(int width, int height)
{
    span_ = width;
    level_y_ = height;
    tick_engine();
}

// 0x004A1620
ScreenWindow* allocate_window_2d(int x, int y, int width, int height)
{
    ScreenWindow* window = free_windows();
    if (window)
        free_windows() = window->following_;
    window->pos_x_ = x;
    window->y_ = y;
    window->tick_engine();
    window->span_ = width;
    window->level_y_ = height;
    window->tick_engine();
    return window;
}

// 0x004A1670
void free_window_2d(ScreenWindow* window)
{
    window->following_ = free_windows();
    free_windows() = window;
}

} // namespace render
