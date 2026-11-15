// NOT ORIGINAL - the volumetric light's model (rt_volume_light.h).
#include "engine/gfx/rt_volume_light.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "base/core/checked_index.h"

namespace render::rt {

namespace {

constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;
constexpr double kLeastDenominator = 1e-4;

double elevation_of(const std::array<float, 3>& toward)
{
    const double y = std::clamp(static_cast<double>(core::at(toward, 1)), -1.0, 1.0);
    return std::asin(y) * kDegreesPerRadian;
}

} // namespace

MediumLight medium_light_from(const SceneSun& scene, const DayLight& day)
{
    MediumLight light;
    if (!scene.found()) {
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

MediumLight medium_light() { return medium_light_from(scene_sun(), day_light()); }

double haze_condition(double sun_elevation_degrees)
{
    const double x = (sun_elevation_degrees - kHazeLowSunPeak) / kHazeLowSunWidth;
    return 1.0 + (kHazeLowSunExtra * std::exp(-x * x));
}

double henyey_greenstein(double cosine, double g)
{
    const double k = std::max(1.0 + (g * g) - (2.0 * g * cosine), kLeastDenominator);
    return (1.0 - (g * g)) / (k * std::sqrt(k));
}

double medium_phase(double cosine, double g)
{
    return (kMediumForwardShare * henyey_greenstein(cosine, g)) +
           ((1.0 - kMediumForwardShare) * henyey_greenstein(cosine, kMediumBackG));
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

} // namespace render::rt
