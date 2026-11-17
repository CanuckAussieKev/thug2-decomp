#pragma once

// NOT ORIGINAL - the one wind (docs/design/vulkan.md "Round W: the air and the wind"): every effect that moves with the
// wind reads it here and nowhere else - the water's waves, the grass's sway, the clutter's, the falling snow's drift
// and eddies, the rain's slant and its drops' drift, the clouds' drift. Render-side only: the simulation never reads it.
//
// The wind is the weather's (the game has none of its own): a speed 10 m above open ground (the meteorologists'
// standard height) blowing along kDirection, with gusts on a slow clock of its own. Where a thing is it is
//   - brought to the thing's height by the neutral logarithmic profile over the town's roughness (kRoughness);
//   - times the share of the free stream the air reaches there with (engine/gfx/air_volume.h: the level's air transport
//     from the open sky - 0 in a closed room, falling off from a doorway, 1 in the open);
// so nothing has a wind constant of its own.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <numbers>

#include "engine/gfx/air_volume.h"
#include "engine/gfx/rt_sky.h"

namespace render::wind {

inline constexpr std::array<float, 2> kDirection{0.8F, 0.6F}; // downwind near the ground (x, z; unit length)
inline constexpr double kUnitsPerMetre = 39.37;               // (the world's units are inches)

// --- the weather's wind ----------------------------------------------------------------------------------------------
// [Graphics] rt_wind: a speed, or auto - the air's weather's (rt_sky.h Weather, rt_fog_air): clear air with a gentle
// breeze (5 m/s, Beaufort 3: a common daytime wind over land), hazy air with a light one (3 m/s: haze gathers in air slow
// to mix), mist with the air nearly still (1.5 m/s: mist forms only in winds under about 2 - 3 m/s).
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
// The setting (kWindAuto: the weather's) and the wind now (m/s, 10 m up in the open).
[[nodiscard]] inline std::atomic<double>& wind_setting()
{
    static std::atomic<double> setting{kWindAuto};
    return setting;
}
inline void set_wind_setting(double metres_per_second)
{
    wind_setting().store(std::isfinite(metres_per_second) && metres_per_second >= 0.0
                             ? std::min(metres_per_second, kMostWind)
                             : kWindAuto,
                         std::memory_order_relaxed);
}
[[nodiscard]] inline double speed()
{
    const double setting = wind_setting().load(std::memory_order_relaxed);
    return setting >= 0.0 ? setting : weather_wind(rt::weather());
}

// --- the wind's profile with height ----------------------------------------------------------------------------------
// The neutral logarithmic profile u(z) = u(10 m) ln(z / z0) / ln(10 / z0) over a town's scattered buildings (Davenport's
// roughness classification, "very rough": z0 0.5 m); none under z0. What is near the ground - grass, clutter, falling
// snow and rain round the eye - takes it at kNearGround (inside the town's canopy the profile no longer holds: 2 m, round
// the eye, is where it is still meaningful and what a person feels).
inline constexpr double kRoughness = 0.5;  // metres
inline constexpr double kReference = 10.0; // metres
inline constexpr double kNearGround = 2.0; // metres
[[nodiscard]] inline double profile(double wind_10m, double height)
{
    if (!(height > kRoughness)) {
        return 0.0;
    }
    return std::max(wind_10m, 0.0) * std::log(height / kRoughness) / std::log(kReference / kRoughness);
}
[[nodiscard]] inline double speed_at_height(double height) { return profile(speed(), height); }
// The wind near the ground in the open, world units a second.
[[nodiscard]] inline double near_ground_units() { return speed_at_height(kNearGround) * kUnitsPerMetre; }

// --- aloft (the clouds) ----------------------------------------------------------------------------------------------
// The boundary layer's top (kBoundaryLayer): the profile up to it, the wind constant above (the free atmosphere's -
// near the geostrophic wind), turned from the ground's by the Ekman spiral's turning across the boundary layer over land
// (kEkmanTurn: 20 - 40 degrees over land - Holton, An Introduction to Dynamic Meteorology, 5.3).
inline constexpr double kBoundaryLayer = 1000.0; // metres
inline constexpr double kEkmanTurn = 30.0;       // degrees
[[nodiscard]] inline double aloft_speed() { return profile(speed(), kBoundaryLayer); }
[[nodiscard]] inline std::array<double, 2> aloft_direction()
{
    const double turn = kEkmanTurn * std::numbers::pi / 180.0;
    const double x = static_cast<double>(kDirection[0]);
    const double z = static_cast<double>(kDirection[1]);
    return {(x * std::cos(turn)) - (z * std::sin(turn)), (x * std::sin(turn)) + (z * std::cos(turn))};
}

// --- the gusts -------------------------------------------------------------------------------------------------------
// The wind's strength over its mean: two slow waves whose spread is the turbulence's - the longitudinal turbulence
// intensity over the town's roughness, I = 1 / ln(z / z0) (EN 1991-1-4 4.4, k_I 1, at its least height for such ground,
// 10 m: 0.33); a wave of amplitude sqrt(2) I has the standard deviation I. Their periods (48 s and 15 s) are in the
// spectrum's energetic range near the ground (tens of seconds).
inline constexpr float kGustSlow = 0.13F; // radians a second
inline constexpr float kGustFast = 0.41F;
[[nodiscard]] inline float gust_share()
{
    return static_cast<float>(std::numbers::sqrt2 / std::log(kReference / kRoughness));
}
// (round W2) The turbulence itself: the longitudinal intensity sigma_u / U (the same I as the gusts'), and the lateral
// and vertical components' shares of it over rough ground in the surface layer - sigma_v / sigma_u ~ 0.75 and
// sigma_w / sigma_u ~ 0.5 (Panofsky and Dutton 1984, Atmospheric Turbulence, 7.1: sigma_u, sigma_v, sigma_w about 2.4,
// 1.9 and 1.25 friction velocities in neutral air).
inline constexpr float kLateralTurbulence = 0.75F;
inline constexpr float kVerticalTurbulence = 0.5F;
[[nodiscard]] inline float turbulence_intensity()
{
    return static_cast<float>(1.0 / std::log(kReference / kRoughness));
}

// The wind's strength at `seconds` on its clock, over its mean (the shaders' twin: wind_glsl's wind_gust).
[[nodiscard]] inline float gust(float seconds)
{
    constexpr float kSlowShare = 0.65F;
    constexpr float kFastShare = 0.35F;
    return 1.0F + (gust_share() * ((kSlowShare * std::sin(seconds * kGustSlow)) +
                                   (kFastShare * std::sin((seconds * kGustFast) + 1.3F))));
}

// --- the wind at a place ---------------------------------------------------------------------------------------------
struct Air {
    float speed = 0.0F;   // m/s (the mean, not the gust)
    float shelter = 1.0F; // the share of the free stream reaching here (air_volume.h)
    float sky = 1.0F;     // the share of the sky's fall reaching here
};
// The wind at `point` (world units) `height` metres above the ground under it.
[[nodiscard]] inline Air air_at(const air::Vec3& point, double height)
{
    const air::Sample sample = air::sample_at(point);
    return {.speed = static_cast<float>(speed_at_height(height) * static_cast<double>(sample.wind)),
            .shelter = sample.wind,
            .sky = sample.sky};
}

// The GLSL twin of gust(): `float wind_gust(float seconds)` (WIND_GUST_SHARE defined from gust_share()).
[[nodiscard]] inline const char* wind_glsl()
{
    return R"(
// (engine/gfx/wind.h wind_glsl: the gusts - gust()'s twin)
float wind_gust(float seconds)
{
    return 1.0 + WIND_GUST_SHARE * (0.65 * sin(seconds * 0.13) + 0.35 * sin(seconds * 0.41 + 1.3));
}
)";
}

} // namespace render::wind
