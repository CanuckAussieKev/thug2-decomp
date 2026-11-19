// NOT ORIGINAL - the loading screen kept over a level's first frames (load_cover.h).
#include "render_pc/load_cover.h"

#include <windows.h>

#include <chrono>
#include <cstdint>

#include "base/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/level_cover.h"
#include "engine/gfx/loading_console.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/screen_2d.h"
#include "engine/sys/stopwatch.h"
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
    uint64_t steps_at_hide = 0;    // (round LS2) the simulation's steps (frame_clock's vblanks) at the original's hide
    bool light_noted = false;      // (round LS2) the console told the level's first light is ready
    bool exposure_noted = false;   // ... and the camera's exposure settled
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
    level_cover::set_covered(false);
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
    own.steps_at_hide = frame_clock::fetch_retrace_count();
    own.light_noted = false;
    own.exposure_noted = false;
    level_cover::set_covered(true); // (the main loop holds the simulation from its next loop - level_cover.h)
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
    // (round LS2) the world shows the level's light: its first light, and the lighting's cut shown (rt_daylight.h
    // cut_settling - the stores still being written for the light the clock published, at most kCutMostSeconds)
    const bool lit = !light_held && !rt::cut_settling();
    // (round LS2) the camera's exposure settled - asked once the light is ready: before it the picture still changes
    const bool exposure = lit && gpu().exposure_settled();
    const bool ready = movie || (drew_from_ready && lit && exposure);
    const double seconds = seconds_since(own.since);
    if (lit && !own.light_noted) {
        own.light_noted = true;
        gfx::loading_console::get_note("first frames: the first light ready (%.0f ms)", seconds * kMillisecondsPerSecond);
    }
    if (exposure && !own.exposure_noted) {
        own.exposure_noted = true;
        gfx::loading_console::get_note("first frames: the camera's exposure settled (%.0f ms)",
                                       seconds * kMillisecondsPerSecond);
    }
    if (ready || seconds >= kMostSeconds) {
        own.covering = false;
        level_cover::set_covered(false); // (the simulation's next step on the next loop: the player's first control)
        const uint64_t steps = frame_clock::fetch_retrace_count() - own.steps_at_hide;
        const char* why = "not ready: the camera's exposure";
        if (!lit) {
            why = "not ready: its first light";
        } else if (!drew_from_ready) {
            why = "not ready: still making pipelines";
        }
        get_log("loading screen: the level shown %.0f ms after the original's hide, %u frames covered, %llu simulation "
                "steps under them - %s",
                seconds * kMillisecondsPerSecond, own.frames - 1, static_cast<unsigned long long>(steps),
                ready ? "ready" : why);
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
