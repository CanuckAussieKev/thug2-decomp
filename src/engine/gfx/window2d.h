#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"

// The engine's 2D windows (Nx; the PC's PcWindow - pc_billboards.h - derives from it): a rectangle of the 640 x 448
// screen the window screen elements clip their sprites to. Ten are made at start-up (the PC's create_window_pool) onto
// a free list (0x007013D0, linked through +0x14).
namespace render {

// The engine's 2D window (0x18 bytes, vtable 0x00649E58).
class ScreenWindow : public memory::HeapObject {
public:
    // 0x004A1580: at (x, y), width x height, not on a list.
    ScreenWindow(int x, int y, int width, int height);
    // 0x004A15B0 (0x004A1690 the scalar deleting destructor, vtable slot 0)
    virtual ~ScreenWindow() = default;
    ScreenWindow(const ScreenWindow&) = delete;
    ScreenWindow& operator=(const ScreenWindow&) = delete;
    // 0x004A1600 (vtable slot 1): the platform told of a change ("STUB: PlatUpdateEngine").
    virtual void tick_engine();
    // 0x004A15C0 / 0x004A15E0: the position / size set, then tick_engine.
    void change_location(int x, int y);
    void change_extent(int width, int height);

    int pos_x_, y_;           // +0x04
    int span_, level_y_;      // +0x0C
    ScreenWindow* following_; // +0x14 on the free list
};
static_assert(offsetof(ScreenWindow, following_) == 0x14 && sizeof(ScreenWindow) == 0x18,
              "layout must match the original");

// 0x004A1620: a window from the free list (none: written through null, as the original), placed and sized - each
// with tick_engine.
ScreenWindow* __cdecl allocate_window_2d(int x, int y, int width, int height);
// 0x004A1670: a window back onto the free list.
void __cdecl free_window_2d(ScreenWindow* window);

extern ScreenWindow* g_free_windows;

} // namespace render
