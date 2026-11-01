// The engine's weather: see weather.h.

#include "engine/gfx/weather.h"

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/script/param_struct.h"

namespace render {

namespace {

float float_parameter(script::Struct* params, float fallback)
{
    float value = fallback;
    params->fetch_float(0u, &value, 0);
    return value;
}
int integer_parameter(script::Struct* params, int fallback)
{
    int value = fallback;
    params->fetch_int_value(0u, &value, 0);
    return value;
}
int current_frames(int frames)
{
    if (frames < 0)
        frames = 0;
    if (frames > 0xFE)
        frames = 0xFE;
    return frames;
}
void change_colour(uint8_t* colour, int value) { std::memcpy(colour, &value, 4); }

// A blend mode's name (the unnamed checksum) and fixed alpha (0x20) handed to one of the blend-mode setters.
bool pass_blend_mode(WeatherSystem* weather, script::Struct* params,
                     void (WeatherSystem::*set_blend_mode)(uint32_t, uint32_t))
{
    uint32_t name = 0;
    if (!params->read_hash_value(0u, &name, 0))
        return false;
    int fixed_alpha = 0x20;
    params->fetch_int_value(0u, &fixed_alpha, 0);
    (weather->*set_blend_mode)(name, static_cast<uint32_t>(fixed_alpha));
    return true;
}

} // namespace

// 0x004A14C0
WeatherSystem::WeatherSystem()
{
    std::memset(rain_bottom_, 0xFF, 4);
    std::memset(rain_top_, 0, 4);
    std::memset(splash_colour_, 0x80, 4);
    std::memset(snow_colour_, 0x80, 4);
}

// 0x004A1020
void WeatherSystem::tick_grid() { debug::printf("STUB: plat_update_grid\n"); }

// 0x004A1030
void WeatherSystem::tick_frame(float) { debug::printf("STUB: plat_process\n"); }

// 0x004A1040
void WeatherSystem::render() { debug::printf("STUB: plat_render\n"); }

// 0x004A1050
void WeatherSystem::set_rain_blend_mode(uint32_t, uint32_t) { debug::printf("STUB: plat_set_rain_blend_mode\n"); }

// 0x004A1060
void WeatherSystem::set_splash_blend_mode(uint32_t, uint32_t) { debug::printf("STUB: plat_set_splash_blend_mode\n"); }

// 0x004A1070
void WeatherSystem::set_snow_blend_mode(uint32_t, uint32_t) { debug::printf("STUB: plat_set_snow_blend_mode\n"); }

// 0x004A10B0
bool WeatherSystem::script_set_rain_height(script::Struct* params, script::Script*)
{
    rain_height_ = 2000.0f;
    params->fetch_float(0u, &rain_height_, 0);
    return true;
}

// 0x004A10D0
bool WeatherSystem::script_set_rain_frames(script::Struct* params, script::Script*)
{
    rain_frames_ = 40;
    params->fetch_int_value(0u, &rain_frames_, 0);
    rain_frames_ = current_frames(rain_frames_);
    return true;
}

// 0x004A1110
bool WeatherSystem::script_set_rain_length(script::Struct* params, script::Script*)
{
    rain_length_ = 100.0f;
    params->fetch_float(0u, &rain_length_, 0);
    return true;
}

// 0x004A1130
bool WeatherSystem::script_set_rain_blend_mode(script::Struct* params, script::Script*)
{
    return pass_blend_mode(this, params, &WeatherSystem::set_rain_blend_mode);
}

// 0x004A1190
bool WeatherSystem::script_set_rain_rate(script::Struct* params, script::Script*)
{
    rain_rate_ = 0.0f;
    params->fetch_float(0u, &rain_rate_, 0);
    return true;
}

// 0x004A11B0
bool WeatherSystem::script_set_rain_color(script::Struct* params, script::Script*)
{
    int colour = 0;
    params->fetch_int_value("top", &colour, 0);
    change_colour(rain_top_, colour);
    colour = -1;
    params->fetch_int_value("bottom", &colour, 0);
    change_colour(rain_bottom_, colour);
    return true;
}

// 0x004A1240
bool WeatherSystem::script_set_splash_rate(script::Struct* params, script::Script*)
{
    splash_rate_ = 0.0f;
    params->fetch_float(0u, &splash_rate_, 0);
    return true;
}

// 0x004A1260
bool WeatherSystem::script_set_splash_life(script::Struct* params, script::Script*)
{
    splash_life_ = 8; // (not kept within 0-254)
    params->fetch_int_value(0u, &splash_life_, 0);
    return true;
}

// 0x004A1280
bool WeatherSystem::script_set_splash_size(script::Struct* params, script::Script*)
{
    splash_size_ = 16.0f;
    params->fetch_float(0u, &splash_size_, 0);
    return true;
}

// 0x004A12A0
bool WeatherSystem::script_set_splash_color(script::Struct* params, script::Script*)
{
    change_colour(splash_colour_, integer_parameter(params, static_cast<int>(0x80808080)));
    return true;
}

// 0x004A12F0
bool WeatherSystem::script_set_splash_blend_mode(script::Struct* params, script::Script*)
{
    return pass_blend_mode(this, params, &WeatherSystem::set_splash_blend_mode);
}

// 0x004A1350
bool WeatherSystem::script_set_snow_height(script::Struct* params, script::Script*)
{
    snow_height_ = 500.0f;
    params->fetch_float(0u, &snow_height_, 0);
    return true;
}

// 0x004A1370
bool WeatherSystem::script_set_snow_frames(script::Struct* params, script::Script*)
{
    snow_frames_ = 0xFE;
    params->fetch_int_value(0u, &snow_frames_, 0);
    snow_frames_ = current_frames(snow_frames_);
    return true;
}

// 0x004A13B0
bool WeatherSystem::script_set_snow_size(script::Struct* params, script::Script*)
{
    snow_size_ = 4.0f;
    params->fetch_float(0u, &snow_size_, 0);
    return true;
}

// 0x004A13D0
bool WeatherSystem::script_set_snow_blend_mode(script::Struct* params, script::Script*)
{
    return pass_blend_mode(this, params, &WeatherSystem::set_snow_blend_mode);
}

// 0x004A1430
bool WeatherSystem::script_set_snow_rate(script::Struct* params, script::Script*)
{
    snow_rate_ = float_parameter(params, 0.0f);
    return true;
}

// 0x004A1450
bool WeatherSystem::script_set_snow_color(script::Struct* params, script::Script*)
{
    change_colour(snow_colour_, integer_parameter(params, static_cast<int>(0x80808080)));
    return true;
}

// 0x004A14A0
bool WeatherSystem::script_set_snow_active(script::Struct*, script::Script*)
{
    rain_on_ = false;
    return true;
}

// 0x004A14B0
bool WeatherSystem::script_set_rain_active(script::Struct*, script::Script*)
{
    rain_on_ = true;
    return true;
}

} // namespace render
