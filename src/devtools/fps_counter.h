#pragma once

#include <vector>

namespace ext {
class Extension;
}

// NOT ORIGINAL - the frame-rate counter: a small readout in the top-right corner of the frames drawn a second, the
// game's own ticks a second (60 when it keeps up - engine/sys/frame_pacing.h: frames above 60 are drawn between ticks,
// the simulation stays at 60), the mean frame time and the 1% low. [Graphics] show_fps=1 (default 0; the Options
// screen's Show FPS, read again when the ini changes) or the key [Keybinds] FpsCounter (default F3; a toggle for this
// run only). The ImGui context is the counter's own (as the console's); nothing of the game's state is read but the
// frame's kind (frame_blend::frame), so the simulation is unchanged with it on or off.
namespace devtools::fps_counter {

struct Stats {
    double fps = 0.0;        // frames drawn in the last second
    double ticks = 0.0;      // of which ticks (not render-only frames)
    double mean_ms = 0.0;    // the mean frame time
    double low_fps = 0.0;    // the 1% low: 1000 / the 99th percentile of the frame times
    double worst_ms = 0.0;   // the slowest frame
};

// The second's statistics from its frame times (milliseconds, any order) and how many of them were ticks (pure: the
// check's).
[[nodiscard]] Stats summarise(std::vector<double> frame_ms, int ticks);

ext::Extension& extension();

} // namespace devtools::fps_counter
