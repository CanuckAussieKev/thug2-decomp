#pragma once

#include <cstdint>

namespace render {

// NOT ORIGINAL (the class; its fields were the original's globals): the display the game draws to - the resolution
// setting and its back buffer's size, antialiasing, whether the refresh rate may be set, the screen offset and the
// factors that scale the game's 640x480 coordinates to the back buffer. The backend fills it when it makes or resets
// the device; the draw code and the UI read it. Held by the renderer.
class DisplaySettings {
public:
    [[nodiscard]] uint8_t frequency_setting_allowed() const { return set_frequency_allowed_; }
    void set_frequency_setting_allowed(uint8_t value) { set_frequency_allowed_ = value; }
    [[nodiscard]] int resolution_setting() const { return resolution_setting_; }
    void set_resolution_setting(int value) { resolution_setting_ = value; }
    [[nodiscard]] bool antialiasing() const { return antialiasing_; }
    void set_antialiasing(bool value) { antialiasing_ = value; }
    [[nodiscard]] int screen_offset_x() const { return screen_offset_x_; }
    void set_screen_offset_x(int value) { screen_offset_x_ = value; }
    [[nodiscard]] int screen_offset_y() const { return screen_offset_y_; }
    void set_screen_offset_y(int value) { screen_offset_y_ = value; }
    [[nodiscard]] float display_factor_x() const { return display_factor_x_; }
    void set_display_factor_x(float value) { display_factor_x_ = value; }
    [[nodiscard]] float display_factor_y() const { return display_factor_y_; }
    void set_display_factor_y(float value) { display_factor_y_ = value; }
    [[nodiscard]] int back_buffer_width() const { return back_buffer_width_; }
    void set_back_buffer_width(int value) { back_buffer_width_ = value; }
    [[nodiscard]] int back_buffer_height() const { return back_buffer_height_; }
    void set_back_buffer_height(int value) { back_buffer_height_ = value; }

private:
    uint8_t set_frequency_allowed_{1};
    int resolution_setting_{};
    bool antialiasing_{};
    int screen_offset_x_{};
    int screen_offset_y_{};
    float display_factor_x_{}; // 640x480 -> back buffer scale
    float display_factor_y_{};
    int back_buffer_width_{};
    int back_buffer_height_{};
};

} // namespace render
