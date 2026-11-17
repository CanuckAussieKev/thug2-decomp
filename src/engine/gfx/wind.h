#pragma once

// NOT ORIGINAL - the level's wind (docs/design/vulkan.md "Snow", "Grass"): one wind every effect that moves with it
// reads - the grass's sway, the falling snow's drift. API-neutral, constant over a level (the game has no wind of its own):
// a direction over the ground and a strength that gusts on a slow clock of its own.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "engine/gfx/rt_sky.h"

namespace render::wind {

inline constexpr std::array<float, 2> kDirection{0.8F, 0.6F}; // downwind (x, z; unit length)
inline constexpr float kSpeed = 22.0F;        // world units a second at the wind's mean (about 2 km/h of drift)
inline constexpr float kGustShare = 0.45F;    // the gusts: the speed between (1 - this) and (1 + this) of the mean
inline constexpr float kGustSlow = 0.13F;     // the gusts' two waves (radians a second)
inline constexpr float kGustFast = 0.41F;

// The wind's strength at `seconds` on its clock, over its mean (the shaders' twin: vulkan_snow.cpp's wind_at).
[[nodiscard]] inline float gust(float seconds)
{
    constexpr float kSlowShare = 0.65F;
    constexpr float kFastShare = 0.35F;
    return 1.0F + (kGustShare * ((kSlowShare * std::sin(seconds * kGustSlow)) + (kFastShare * std::sin((seconds * kGustFast) + 1.3F))));
}

// (added by the physical waves) The weather's wind 10 m above open ground (metres a second - the meteorologists' standard
// height), blowing along kDirection: what raises the water's waves (water_waves.h; each body sees it x how open it is to
// the sky). [Graphics] rt_wind: a speed, or auto - the air's weather's (rt_sky.h Weather, rt_fog_air): clear air with a
// gentle breeze (5 m/s, Beaufort 3: a common daytime wind over land), hazy air with a light one (3 m/s: haze gathers in
// air slow to mix), mist with the air nearly still (1.5 m/s: mist forms only in winds under about 2 - 3 m/s).
// (The grass's sway and the snow's drift - kSpeed - are the wind near the ground among the level's things; not tied to
// this yet.)
inline constexpr double kClearWind = 5.0;
inline constexpr double kHazyWind = 3.0;
inline constexpr double kMistyWind = 1.5;
inline constexpr double kWindAuto = -1.0;
inline constexpr double kMostWind = 40.0;
[[nodiscard]] inline double weather_wind(rt::Weather weather)
{
    switch (weather) {
    case rt::Weather::kHazy:
        return kHazyWind;
    case rt::Weather::kMisty:
        return kMistyWind;
    case rt::Weather::kClear:
        break;
    }
    return kClearWind;
}
// The setting (kWindAuto: the weather's) and the wind now.
[[nodiscard]] inline std::atomic<double>& wind_setting()
{
    static std::atomic<double> setting{kWindAuto};
    return setting;
}
inline void set_wind_setting(double metres_per_second)
{
    wind_setting().store(std::isfinite(metres_per_second) && metres_per_second >= 0.0 ? std::min(metres_per_second, kMostWind)
                                                                                     : kWindAuto,
                         std::memory_order_relaxed);
}
[[nodiscard]] inline double speed()
{
    const double setting = wind_setting().load(std::memory_order_relaxed);
    return setting >= 0.0 ? setting : weather_wind(rt::weather());
}

} // namespace render::wind
