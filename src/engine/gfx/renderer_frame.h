#pragma once

#include <cstdint>

namespace render {

// NOT ORIGINAL (the class; its fields were the original's globals): the frames' counters and clocks - frames drawn,
// this frame's draw calls and triangles, the step clock the animated textures run on (whole seconds), which of the two
// screen copies is current, and the loading screen's timer while it draws on its own (its id; 0 when none). Held by
// the renderer.
class FrameCounters {
public:
    [[nodiscard]] uint32_t loading_bar_timer() const { return loading_bar_timer_; }
    void set_loading_bar_timer(uint32_t value) { loading_bar_timer_ = value; }
    [[nodiscard]] int frames_rendered() const { return frames_rendered_; }
    void count_frame() { ++frames_rendered_; }
    [[nodiscard]] int step_seconds() const { return step_seconds_; }
    void set_step_seconds(int value) { step_seconds_ = value; }
    [[nodiscard]] int screen_copy_index() const { return screen_copy_index_; }
    void set_screen_copy_index(int value) { screen_copy_index_ = value; }
    // One draw call of `triangles` more this frame (the frame's counts).
    void count_draw(uint32_t triangles)
    {
        ++draw_calls_;
        drawn_triangles_ += triangles;
    }
    void reset_draw_counts()
    {
        draw_calls_ = 0;
        drawn_triangles_ = 0;
    }

private:
    uint32_t loading_bar_timer_{}; // the timer's id (timeSetEvent)
    int frames_rendered_{};
    int step_seconds_{};
    int screen_copy_index_{};
    uint32_t draw_calls_{};
    uint32_t drawn_triangles_{};
};

} // namespace render
