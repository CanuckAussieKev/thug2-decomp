// NOT ORIGINAL - the volumetric light's model (rt_volume_light.h).
#include "engine/gfx/rt_volume_light.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "base/core/checked_index.h"
#include "engine/gfx/rain.h"
#include "engine/gfx/snow.h"

namespace render::rt {

namespace {

constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;
constexpr double kLeastDenominator = 1e-4;
constexpr double kFourPi = 4.0 * std::numbers::pi;

double elevation_of(const std::array<float, 3>& toward)
{
    const double y = std::clamp(static_cast<double>(core::at(toward, 1)), -1.0, 1.0);
    return std::asin(y) * kDegreesPerRadian;
}

} // namespace

MediumLight medium_light_from(const SceneSun& scene, const SunRelight& relight, const DayLight& day)
{
    MediumLight light;
    // (the relit level's light alone: no relit data, no sunlit air - rt_volume_light.h; the console's `rt sun` - an
    // experiment, and the checks' sun - lights it whatever the level)
    if (!scene.found() || (!relight.relights && scene.origin != SunOrigin::kOverride)) {
        return light;
    }
    light.have = true;
    light.toward = scene.toward;
    light.sun_elevation = elevation_of(scene.toward);
    if (scene.origin == SunOrigin::kClock && day.on) {
        // the clock lights the level: its sun or moon, as strong and as coloured as it says
        light.clock = true;
        light.moonlit = day.moonlit;
        light.colour = day.light_colour;
        light.radius_degrees = day.light_radius;
        light.sun_elevation = elevation_of(day.sun);
    }
    return light;
}

MediumLight medium_light() { return medium_light_from(scene_sun(), sun_relight(), day_light()); }

namespace {
// (the frame's weather: render_pc writes it on the render thread before the views are drawn, the fog reads it there)
AirWeather& weather_now()
{
    static AirWeather weather;
    return weather;
}
} // namespace

void set_air_weather(const AirWeather& weather) { weather_now() = weather; }

AirWeather air_weather() { return weather_now(); }

double precipitation_extinction(const AirWeather& weather)
{
    const double rate = weather.rain_rate > 0.0 ? weather.rain_rate : (weather.rain ? rain::kModerateRate : 0.0);
    return (weather.snow ? snow::extinction(snow::kSnowfallRate) : 0.0) + rain::extinction(rate);
}

double level_fog_extinction(float start, float end)
{
    const double range = static_cast<double>(end) - static_cast<double>(start);
    return std::isfinite(range) && range > 0.0 ? kKoschmieder / range : 0.0;
}

std::array<float, 3> aerosol_albedo(const Aerosol& aerosol)
{
    std::array<float, 3> out{1.0F, 1.0F, 1.0F};
    for (std::size_t i = 0; i < 3; ++i) {
        const double t = core::at(aerosol.extinction, i);
        if (t > 0.0) {
            core::at(out, i) = static_cast<float>(std::clamp(core::at(aerosol.scattering, i) / t, 0.0, 1.0));
        }
    }
    return out;
}

StreetAir street_air(const Aerosol& aerosol, const AirWeather& weather)
{
    StreetAir air;
    constexpr std::size_t kGreen = 1;
    air.extinction = core::at(aerosol.extinction, kGreen) * aerosol_density(aerosol, 0.0) / kUnitsPerMetre;
    air.height = (aerosol.mist > 0.0 ? aerosol.mist_height : aerosol.height) * kUnitsPerMetre;
    const double falling = precipitation_extinction(weather);
    if (falling > 0.0) {
        air.snow = falling / kUnitsPerMetre;
        air.snow_height = kPrecipitationHeight * kUnitsPerMetre;
    }
    return air;
}

double henyey_greenstein(double cosine, double g)
{
    const double k = std::max(1.0 + (g * g) - (2.0 * g * cosine), kLeastDenominator);
    return (1.0 - (g * g)) / (k * std::sqrt(k));
}

double medium_phase(double cosine, double g) { return kFourPi * aerosol_phase(cosine, g); }

double god_ray_peak(double cosine, double g)
{
    const double edge = medium_phase(std::cos(kGodRayConeDegrees / kDegreesPerRadian), g);
    return std::max(medium_phase(cosine, g) - edge, 0.0);
}

double march_sun_phase(double cosine, double g, bool god_rays)
{
    return medium_phase(cosine, g) - (god_rays ? god_ray_peak(cosine, g) : 0.0);
}

double god_ray_open(double sky_share, double air_before)
{
    return 1.0 - ((1.0 - std::clamp(sky_share, 0.0, 1.0)) * std::clamp(air_before, 0.0, 1.0));
}

double air_share_before(double distance, double sigma, double transmittance)
{
    const double whole = 1.0 - std::clamp(transmittance, 0.0, 1.0);
    if (!(whole > 1e-9) || !(sigma > 0.0)) {
        return 0.0;
    }
    return std::clamp((1.0 - std::exp(-sigma * std::max(distance, 0.0))) / whole, 0.0, 1.0);
}

double air_map_half()
{
    constexpr double kMargin = 1.03;
    return kAirMapReach * kUnitsPerMetre * kMargin;
}

double cloud_map_fade(double rise)
{
    const double t = std::clamp((rise - kCloudMapFadeFrom) / (kCloudMapFadeTo - kCloudMapFadeFrom), 0.0, 1.0);
    return t * t * (3.0 - (2.0 * t));
}

std::array<double, 2> cloud_map_place(const std::array<double, 3>& at, const std::array<double, 3>& toward,
                                      double base_y)
{
    const double rise = std::max(core::at(toward, 1), kCloudMapLeastRise);
    const double along = (base_y - core::at(at, 1)) / rise;
    return {core::at(at, 0) + (core::at(toward, 0) * along), core::at(at, 2) + (core::at(toward, 2) * along)};
}

double medium_layer_depth(double u0, double dy, double s0, double s1, double inverse_height)
{
    double lo = s0;
    double hi = s1;
    double below = 0.0;
    constexpr double kLevel = 1e-12;
    if (inverse_height > 0.0) {
        if (std::abs(dy) < kLevel) {
            if (u0 <= 0.0) {
                below = s1 - s0;
                hi = lo;
            }
        } else {
            const double cut = std::clamp(-u0 / dy, s0, s1);
            if (dy > 0.0) {
                below = cut - s0;
                lo = cut;
            } else {
                below = s1 - cut;
                hi = cut;
            }
        }
    }
    double depth = below;
    if (hi > lo) {
        const double k = dy * inverse_height;
        constexpr double kThin = 1e-4;
        if (std::abs(k) * (hi - lo) < kThin) {
            depth += std::exp(-std::max(u0 + (dy * 0.5 * (lo + hi)), 0.0) * inverse_height) * (hi - lo);
        } else {
            depth += (std::exp(-std::max(u0 + (dy * lo), 0.0) * inverse_height) -
                      std::exp(-std::max(u0 + (dy * hi), 0.0) * inverse_height)) /
                     k;
        }
    }
    return depth;
}

std::array<float, 3> water_extinction(const std::array<float, 3>& hue, float clarity)
{
    constexpr float kVisibilities = 3.0F; // (e^-3: 5 % left at the clarity)
    constexpr float kLeast = 0.2F;
    constexpr float kByHue = 1.6F;
    std::array<float, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(out, i) = (kVisibilities / std::max(clarity, 1.0F)) * (kLeast + (kByHue * (1.0F - core::at(hue, i))));
    }
    return out;
}

std::array<float, 3> water_deep_reflectance(const std::array<float, 3>& hue)
{
    constexpr float kDeep = 0.65F;
    constexpr float kLeastLuma = 0.05F;
    std::array<float, 3> h2{};
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(h2, i) = core::at(hue, i) * core::at(hue, i);
    }
    const float luma = (0.2126F * core::at(h2, 0)) + (0.7152F * core::at(h2, 1)) + (0.0722F * core::at(h2, 2));
    const float scale = kDeep / std::max(luma, kLeastLuma);
    for (float& value : h2) {
        value *= scale;
    }
    return h2;
}

std::array<float, 3> water_scattering_albedo(const std::array<float, 3>& reflectance)
{
    constexpr float kMost = 0.999F;
    std::array<float, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const float r = std::clamp(core::at(reflectance, i), 0.0F, kMost);
        const float q = (1.0F - r) / (1.0F + r);
        core::at(out, i) = 1.0F - (q * q);
    }
    return out;
}

} // namespace render::rt
