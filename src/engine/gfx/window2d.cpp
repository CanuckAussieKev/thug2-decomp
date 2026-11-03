// The engine's 2D windows: see window2d.h.

#include "engine/gfx/window2d.h"
#include <gsl/assert>

#include "engine/core/dbg.h"
#include "engine/gfx/screen_2d.h"
#include "engine/maths/rect.h"
namespace render {

// 0x004A1580
ScreenWindow::ScreenWindow(maths::PixelPoint at, maths::PixelSize size)
    : pos_x_(at.x), y_(at.y), span_(size.width), level_y_(size.height)
{
}

// 0x004A1600
void ScreenWindow::tick_engine() { debug::printf("STUB: PlatUpdateEngine\n"); }

// 0x004A15C0
void ScreenWindow::change_location(maths::PixelPoint at)
{
    const auto x = at.x;
    const auto y = at.y;
    pos_x_ = x;
    y_ = y;
    tick_engine();
}

// 0x004A15E0
void ScreenWindow::change_extent(maths::PixelSize extent)
{
    const auto width = extent.width;
    const auto height = extent.height;
    span_ = width;
    level_y_ = height;
    tick_engine();
}

// 0x004A1620
ScreenWindow* allocate_window_2d(maths::PixelPoint at, maths::PixelSize size)
{
    const auto width = size.width;
    const auto height = size.height;
    const auto x = at.x;
    const auto y = at.y;
    ScreenWindow* window = screen_2d().free_windows();
    if (window)
        screen_2d().set_free_windows(window->following());
    Expects(window != nullptr);
    window->set_pos_x(x);
    window->set_y(y);
    window->tick_engine();
    window->set_span(width);
    window->set_level_y(height);
    window->tick_engine();
    return window;
}

// 0x004A1670
void free_window_2d(ScreenWindow* window)
{
    window->set_following(screen_2d().free_windows());
    screen_2d().set_free_windows(window);
}

} // namespace render
