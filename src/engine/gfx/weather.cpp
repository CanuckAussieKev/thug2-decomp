// The engine's weather: see weather.h.

#include "engine/gfx/weather.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/script/param_struct.h"

namespace render {

constexpr int kMaxFrames = 0xFE; // a drop's frames fit a byte below the free marker 0xFF
constexpr int kDefaultFixedAlpha = 0x20;
constexpr int kFullByte = 0xFF;
constexpr int kNeutralByte = 0x80;
constexpr float kRainHeightDefault = 2000.0F;
constexpr int kRainFramesDefault = 40;
constexpr int kSplashLifeDefault = 8;
constexpr float kSplashSizeDefault = 16.0F;
constexpr uint32_t kNeutralColour = 0x80808080;
constexpr float kSnowHeightDefault = 500.0F;
constexpr float kSnowSizeDefault = 4.0F;

namespace {

float float_parameter(script::Struct* params, float fallback)
{
    float value = fallback;
    params->fetch_float(0U, &value, 0);
    return value;
}
int integer_parameter(script::Struct* params, int fallback)
{
    int value = fallback;
    params->fetch_int_value(0U, &value, 0);
    return value;
}
int current_frames(int frames)
{
    frames = std::max(frames, 0);
    frames = std::min(frames, kMaxFrames);
    return frames;
}
void change_colour(uint8_t* colour, int value) { std::memcpy(colour, &value, 4); }

// A blend mode's name (the unnamed checksum) and fixed alpha (0x20) handed to one of the blend-mode setters.
bool pass_blend_mode(WeatherSystem* weather, script::Struct* params,
                     void (WeatherSystem::*set_blend_mode)(uint32_t, uint32_t))
{
    uint32_t name = 0;
    if (!params->read_hash_value(0U, &name, 0)) {
        return false;
    }
    int fixed_alpha = kDefaultFixedAlpha;
    params->fetch_int_value(0U, &fixed_alpha, 0);
    (weather->*set_blend_mode)(name, static_cast<uint32_t>(fixed_alpha));
    return true;
}

} // namespace

// 0x004A14C0
WeatherSystem::WeatherSystem()
{
    std::memset(rain_bottom_.data(), kFullByte, 4);
    std::memset(rain_top_.data(), 0, 4);
    std::memset(splash_colour_.data(), kNeutralByte, 4);
    std::memset(snow_colour_.data(), kNeutralByte, 4);
}

// 0x004A1020
void WeatherSystem::tick_grid() { debug::printf("STUB: plat_update_grid\n"); }

// 0x004A1030
void WeatherSystem::tick_frame(float /*unused*/) { debug::printf("STUB: plat_process\n"); }

// 0x004A1040
void WeatherSystem::render() { debug::printf("STUB: plat_render\n"); }

// 0x004A1050
void WeatherSystem::set_rain_blend_mode(uint32_t /*unused*/, uint32_t /*unused*/)
{
    debug::printf("STUB: plat_set_rain_blend_mode\n");
}

// 0x004A1060
void WeatherSystem::set_splash_blend_mode(uint32_t /*unused*/, uint32_t /*unused*/)
{
    debug::printf("STUB: plat_set_splash_blend_mode\n");
}

// 0x004A1070
void WeatherSystem::set_snow_blend_mode(uint32_t /*unused*/, uint32_t /*unused*/)
{
    debug::printf("STUB: plat_set_snow_blend_mode\n");
}

// 0x004A10B0
bool WeatherSystem::script_set_rain_height(script::Struct* params, script::Script* /*script*/)
{
    rain_height_ = kRainHeightDefault;
    params->fetch_float(0U, &rain_height_, 0);
    return true;
}

// 0x004A10D0
bool WeatherSystem::script_set_rain_frames(script::Struct* params, script::Script* /*script*/)
{
    rain_frames_ = kRainFramesDefault;
    params->fetch_int_value(0U, &rain_frames_, 0);
    rain_frames_ = current_frames(rain_frames_);
    return true;
}

// 0x004A1110
bool WeatherSystem::script_set_rain_length(script::Struct* params, script::Script* /*script*/)
{
    rain_length_ = 100.0F;
    params->fetch_float(0U, &rain_length_, 0);
    return true;
}

// 0x004A1130
bool WeatherSystem::script_set_rain_blend_mode(script::Struct* params, script::Script* /*script*/)
{
    return pass_blend_mode(this, params, &WeatherSystem::set_rain_blend_mode);
}

// 0x004A1190
bool WeatherSystem::script_set_rain_rate(script::Struct* params, script::Script* /*script*/)
{
    rain_rate_ = 0.0F;
    params->fetch_float(0U, &rain_rate_, 0);
    return true;
}

// 0x004A11B0
bool WeatherSystem::script_set_rain_color(script::Struct* params, script::Script* /*script*/)
{
    int colour = 0;
    params->fetch_int_value("top", &colour, 0);
    change_colour(rain_top_.data(), colour);
    colour = -1;
    params->fetch_int_value("bottom", &colour, 0);
    change_colour(rain_bottom_.data(), colour);
    return true;
}

// 0x004A1240
bool WeatherSystem::script_set_splash_rate(script::Struct* params, script::Script* /*script*/)
{
    splash_rate_ = 0.0F;
    params->fetch_float(0U, &splash_rate_, 0);
    return true;
}

// 0x004A1260
bool WeatherSystem::script_set_splash_life(script::Struct* params, script::Script* /*script*/)
{
    splash_life_ = kSplashLifeDefault; // (not kept within 0-254)
    params->fetch_int_value(0U, &splash_life_, 0);
    return true;
}

// 0x004A1280
bool WeatherSystem::script_set_splash_size(script::Struct* params, script::Script* /*script*/)
{
    splash_size_ = kSplashSizeDefault;
    params->fetch_float(0U, &splash_size_, 0);
    return true;
}

// 0x004A12A0
bool WeatherSystem::script_set_splash_color(script::Struct* params, script::Script* /*script*/)
{
    change_colour(splash_colour_.data(), integer_parameter(params, static_cast<int>(kNeutralColour)));
    return true;
}

// 0x004A12F0
bool WeatherSystem::script_set_splash_blend_mode(script::Struct* params, script::Script* /*script*/)
{
    return pass_blend_mode(this, params, &WeatherSystem::set_splash_blend_mode);
}

// 0x004A1350
bool WeatherSystem::script_set_snow_height(script::Struct* params, script::Script* /*script*/)
{
    snow_height_ = kSnowHeightDefault;
    params->fetch_float(0U, &snow_height_, 0);
    return true;
}

// 0x004A1370
bool WeatherSystem::script_set_snow_frames(script::Struct* params, script::Script* /*script*/)
{
    snow_frames_ = kMaxFrames;
    params->fetch_int_value(0U, &snow_frames_, 0);
    snow_frames_ = current_frames(snow_frames_);
    return true;
}

// 0x004A13B0
bool WeatherSystem::script_set_snow_size(script::Struct* params, script::Script* /*script*/)
{
    snow_size_ = kSnowSizeDefault;
    params->fetch_float(0U, &snow_size_, 0);
    return true;
}

// 0x004A13D0
bool WeatherSystem::script_set_snow_blend_mode(script::Struct* params, script::Script* /*script*/)
{
    return pass_blend_mode(this, params, &WeatherSystem::set_snow_blend_mode);
}

// 0x004A1430
bool WeatherSystem::script_set_snow_rate(script::Struct* params, script::Script* /*script*/)
{
    snow_rate_ = float_parameter(params, 0.0F);
    return true;
}

// 0x004A1450
bool WeatherSystem::script_set_snow_color(script::Struct* params, script::Script* /*script*/)
{
    change_colour(snow_colour_.data(), integer_parameter(params, static_cast<int>(kNeutralColour)));
    return true;
}

// 0x004A14A0
bool WeatherSystem::script_set_snow_active(script::Struct* /*params*/, script::Script* /*script*/)
{
    rain_on_ = false;
    return true;
}

// 0x004A14B0
bool WeatherSystem::script_set_rain_active(script::Struct* /*params*/, script::Script* /*script*/)
{
    rain_on_ = true;
    return true;
}

} // namespace render
