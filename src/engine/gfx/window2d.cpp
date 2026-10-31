// The engine's 2D windows: see window2d.h.

#include "engine/gfx/window2d.h"

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/core/code_address.h"

namespace render {

ScreenWindow* g_free_windows;

namespace {

extern void** const kWindowVtable;

ScreenWindow*& free_windows() { return g_free_windows; }
void get_changed(ScreenWindow* window)
{
    reinterpret_cast<void(__thiscall*)(ScreenWindow*)>(window->vtable_[1])(window);
}

} // namespace

// 0x004A1580
ScreenWindow* ScreenWindow::make_new(int x, int y, int width, int height)
{
    pos_x_ = x;
    y_ = y;
    vtable_ = reinterpret_cast<void**>(kWindowVtable);
    span_ = width;
    level_y_ = height;
    following_ = nullptr;
    return this;
}

// 0x004A15B0
void ScreenWindow::destroy_body()
{
    vtable_ = reinterpret_cast<void**>(kWindowVtable);
}

// 0x004A1690
ScreenWindow* ScreenWindow::teardown(unsigned int free_flags)
{
    vtable_ = reinterpret_cast<void**>(kWindowVtable);
    if (free_flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004A1600
void ScreenWindow::tick_engine()
{
    debug::printf("STUB: PlatUpdateEngine\n");
}

// 0x004A15C0
void ScreenWindow::change_location(int x, int y)
{
    pos_x_ = x;
    y_ = y;
    get_changed(this);
}

// 0x004A15E0
void ScreenWindow::change_extent(int width, int height)
{
    span_ = width;
    level_y_ = height;
    get_changed(this);
}

// 0x004A1620
ScreenWindow* __cdecl allocate_window_2d(int x, int y, int width, int height)
{
    ScreenWindow* window = free_windows();
    if (window)
        free_windows() = window->following_;
    window->pos_x_ = x;
    window->y_ = y;
    get_changed(window);
    window->span_ = width;
    window->level_y_ = height;
    get_changed(window);
    return window;
}

// 0x004A1670
void __cdecl free_window_2d(ScreenWindow* window)
{
    window->following_ = free_windows();
    free_windows() = window;
}

namespace {
static VtableSlot g_window_vtable_slots[] = {&render::ScreenWindow::teardown, &render::ScreenWindow::tick_engine};
void** const kWindowVtable = reinterpret_cast<void**>(g_window_vtable_slots);
} // namespace

} // namespace Nx
