// NOT ORIGINAL - the loading screen kept over a level's first frames (load_cover.h).
#include "render_pc/load_cover.h"

#include <windows.h>

#include <chrono>
#include <cstdint>

#include "base/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/loading_console.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/screen_2d.h"
#include "platform/run_log.h"
#include "render_pc/loading_bar.h"
#include "render_pc/loading_console_panel.h"
#include "render_pc/loading_screen.h"
#include "render_pc/rt_relit_world.h"

namespace render::load_cover {

namespace {

using Clock = std::chrono::steady_clock;
constexpr double kMillisecondsPerSecond = 1000.0;

struct Cover {
    bool shown = false;    // a loading screen with a picture (or black) was shown since the last hide
    bool blank = false;    // it was black
    bool bar = false;      // its loading bar ran (SetLoadingBarTime - 0x004B7400's timer)
    bool covering = false; // the level's first frames: covered until ready
    Clock::time_point since;       // the original's hide
    Clock::time_point bar_time;    // the bar's time last moved on
    uint64_t pipelines_before = 0; // the pipelines made when the last frame's cover was drawn (or at the hide)
    uint32_t frames = 0;           // the frames covered
};

Cover& cover()
{
    static core::NoDestroy<Cover> state;
    return state.get();
}

double seconds_since(Clock::time_point then) { return std::chrono::duration<double>(Clock::now() - then).count(); }

} // namespace

void screen_shown(bool kept, bool blank, bool dialog)
{
    Cover& own = cover();
    own.covering = false; // (a new loading screen: the last level's first frames are over)
    own.shown = !kept && !dialog;
    own.blank = blank;
}

void screen_hidden()
{
    Cover& own = cover();
    if (!own.shown) {
        return;
    }
    own.shown = false;
    own.covering = true;
    own.bar = renderer().loading_bar_timer() != 0U; // (asked before hide_loading_screen stops it)
    own.since = Clock::now();
    own.bar_time = own.since;
    own.pipelines_before = gpu().pipelines_made();
    own.frames = 0;
}

void loading_frame() { (void)rt_relit_world::hold_first_light(true); }

bool frame_covered()
{
    Cover& own = cover();
    // (every frame: the first light's hold runs its own state - rt_relit_world.h)
    const bool light_held = rt_relit_world::hold_first_light(false);
    if (!own.covering) {
        gfx::loading_console::level_shown(); // (a hide with nothing to cover: the console ends with it)
        return false;
    }
    ++own.frames;
    const uint64_t made = gpu().pipelines_made();
    const bool drew_from_ready = made == own.pipelines_before;
    const bool movie = engine_parts().current_movie() != nullptr;
    const bool ready = movie || (drew_from_ready && !light_held);
    const double seconds = seconds_since(own.since);
    if (ready || seconds >= kMostSeconds) {
        own.covering = false;
        get_log("loading screen: the level shown %.0f ms after the original's hide, %u frames covered - %s", seconds * kMillisecondsPerSecond,
                own.frames - 1, ready ? "ready" : "not ready (shown as it is)");
        gfx::loading_console::level_shown();
        return false;
    }
    if (!drew_from_ready) {
        gfx::loading_console::get_note("first frames: %llu pipelines made", static_cast<unsigned long long>(made - own.pipelines_before));
    }
    return true;
}

void draw_cover()
{
    Cover& own = cover();
    WaitForSingleObject(screen_2d().screen_mutex(), INFINITE);
    if (own.blank) {
        gpu().clear(0, nullptr, gpu_value::kClearTarget, 0, 1.0F, 0);
    } else {
        draw_loading_screen_picture();
    }
    // (the bar by the time since it was last drawn, as the loading screen's timer moves it on - 0x004B6FF0)
    const Clock::time_point now = Clock::now();
    const double moved = std::chrono::duration<double>(now - own.bar_time).count();
    own.bar_time = now;
    if (own.bar) {
        draw_progress_bar(moved + screen_2d().elapsed_seconds());
    }
    gfx::loading_console::draw_panel();
    ReleaseMutex(screen_2d().screen_mutex());
    // (the cover's own draws are not the frame's: the next frame is measured from here)
    own.pipelines_before = gpu().pipelines_made();
}

} // namespace render::load_cover
