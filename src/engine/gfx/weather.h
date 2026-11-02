#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/mem.h"

namespace script {
class Struct;
class Script;
} // namespace script

// The engine's weather (Nx; the PC's PcWeather - pc_weather.h - derives from it): the rain's, splashes' and snow's
// settings the Weather* script commands set; the drawing is the PC's.
namespace render {

// The engine's weather (vtable 0x00649E30; its fields up to +0x5C - see PcWeather for the rest).
class WeatherSystem : public memory::HeapObject {
public:
    // 0x004A14C0: the defaults (rain 2000 high, 40 frames, 100 long, rate 0, black to white; splashes rate 0, 8
    // frames, 16 big, grey; snow 500 high, 254 frames, 4 big, rate 0, grey; rain chosen, no grid).
    WeatherSystem();
    // 0x004A1080 (0x004A1550 the scalar deleting destructor, vtable slot 0)
    virtual ~WeatherSystem() = default;
    WeatherSystem(const WeatherSystem&) = delete;
    WeatherSystem& operator=(const WeatherSystem&) = delete;
    // 0x004A1020 / 0x004A1030 / 0x004A1040 (vtable slots 1 - 3; 0x004A1040 is shared by other vtables): the grid
    // made again, a frame (the slow-motion frame time), drawn - the platform's ("STUB: plat_update_grid" /
    // "plat_process" / "plat_render").
    virtual void tick_grid();
    virtual void tick_frame(float frame_seconds);
    virtual void render();
    // 0x004A1050 / 0x004A1060 / 0x004A1070 (slots 4 - 6): the blend modes ("STUB: plat_set_..._blend_mode").
    virtual void set_rain_blend_mode(uint32_t name, uint32_t fixed_alpha);
    virtual void set_splash_blend_mode(uint32_t name, uint32_t fixed_alpha);
    virtual void set_snow_blend_mode(uint32_t name, uint32_t fixed_alpha);

    // The script commands' setters (thiscall with the command's parameters; true, except the blend modes without a
    // mode's name): each value's default when it isn't given; the frames kept within 0-254.
    bool script_set_rain_height(script::Struct* params, script::Script* script);       // 0x004A10B0
    bool script_set_rain_frames(script::Struct* params, script::Script* script);       // 0x004A10D0
    bool script_set_rain_length(script::Struct* params, script::Script* script);       // 0x004A1110
    bool script_set_rain_blend_mode(script::Struct* params, script::Script* script);   // 0x004A1130
    bool script_set_rain_rate(script::Struct* params, script::Script* script);         // 0x004A1190
    bool script_set_rain_color(script::Struct* params, script::Script* script);        // 0x004A11B0: top=, bottom=
    bool script_set_splash_rate(script::Struct* params, script::Script* script);       // 0x004A1240
    bool script_set_splash_life(script::Struct* params, script::Script* script);       // 0x004A1260
    bool script_set_splash_size(script::Struct* params, script::Script* script);       // 0x004A1280
    bool script_set_splash_color(script::Struct* params, script::Script* script);      // 0x004A12A0
    bool script_set_splash_blend_mode(script::Struct* params, script::Script* script); // 0x004A12F0
    bool script_set_snow_height(script::Struct* params, script::Script* script);       // 0x004A1350
    bool script_set_snow_frames(script::Struct* params, script::Script* script);       // 0x004A1370
    bool script_set_snow_size(script::Struct* params, script::Script* script);         // 0x004A13B0
    bool script_set_snow_blend_mode(script::Struct* params, script::Script* script);   // 0x004A13D0
    bool script_set_snow_rate(script::Struct* params, script::Script* script);         // 0x004A1430
    bool script_set_snow_color(script::Struct* params, script::Script* script);        // 0x004A1450
    bool script_set_snow_active(script::Struct* params, script::Script* script);       // 0x004A14A0: rain off
    bool script_set_rain_active(script::Struct* params, script::Script* script);       // 0x004A14B0

    // The fields other code reads and sets.
    [[nodiscard]] float rain_height() const { return rain_height_; }
    [[nodiscard]] int rain_frames() const { return rain_frames_; }
    [[nodiscard]] float rain_length() const { return rain_length_; }
    float& rain_rate() { return rain_rate_; }
    [[nodiscard]] float rain_rate() const { return rain_rate_; }
    [[nodiscard]] const std::array<uint8_t, 4>& rain_top() const { return rain_top_; }
    [[nodiscard]] const std::array<uint8_t, 4>& rain_bottom() const { return rain_bottom_; }
    [[nodiscard]] float splash_rate() const { return splash_rate_; }
    [[nodiscard]] int splash_life() const { return splash_life_; }
    [[nodiscard]] float splash_size() const { return splash_size_; }
    [[nodiscard]] const std::array<uint8_t, 4>& splash_colour() const { return splash_colour_; }
    int& drops() { return drops_; }
    [[nodiscard]] int drops() const { return drops_; }
    [[nodiscard]] bool active() const { return active_; }
    void set_active(bool value) { active_ = value; }
    [[nodiscard]] float snow_height() const { return snow_height_; }
    [[nodiscard]] int snow_frames() const { return snow_frames_; }
    [[nodiscard]] float snow_size() const { return snow_size_; }
    float& snow_rate() { return snow_rate_; }
    [[nodiscard]] float snow_rate() const { return snow_rate_; }
    [[nodiscard]] const std::array<uint8_t, 4>& snow_colour() const { return snow_colour_; }
    [[nodiscard]] float camera_x() const { return camera_x_; }
    [[nodiscard]] float camera_z() const { return camera_z_; }
    bool& rain_on() { return rain_on_; }
    [[nodiscard]] bool rain_on() const { return rain_on_; }

private:
    float rain_height_{2000.0f}; // +0x04
    int rain_frames_{40};        // +0x08
    float rain_length_{100.0f};  // +0x0C
    float rain_rate_{0.0f};      // +0x10
    std::array<uint8_t, 4> rain_top_{};    // +0x14 RGBA
    std::array<uint8_t, 4> rain_bottom_{}; // +0x18
    float splash_rate_{0.0f};  // +0x1C
    int splash_life_{8};       // +0x20
    float splash_size_{16.0f}; // +0x24
    std::array<uint8_t, 4> splash_colour_{}; // +0x28
    int drops_{};                // +0x2C (the PC's)
    bool active_{false};         // +0x30
    std::array<uint8_t, 3> pad31_{};
    float snow_height_{500.0f}; // +0x34
    int snow_frames_{0xFE};     // +0x38
    float snow_size_{4.0f};     // +0x3C
    float snow_rate_{0.0f};     // +0x40
    std::array<uint8_t, 4> snow_colour_{}; // +0x44
    float camera_x_{};         // +0x48 where the camera was (the PC's)
    float camera_y_{};         // +0x4C
    float camera_z_{};         // +0x50
    std::array<uint8_t, 4> unknown54_{};   // +0x54
    bool rain_on_{true};       // +0x58
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(WeatherSystem, active_) == 0x30 && offsetof(WeatherSystem, snow_colour_) == 0x44 &&
                          offsetof(WeatherSystem, rain_on_) == 0x58,
                      "layout must match the original");
    }
};

} // namespace render
