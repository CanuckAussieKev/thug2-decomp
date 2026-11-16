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

double haze_condition(double sun_elevation_degrees)
{
    const double x = (sun_elevation_degrees - kHazeLowSunPeak) / kHazeLowSunWidth;
    return 1.0 + (kHazeLowSunExtra * std::exp(-x * x));
}

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

double air_night(double sun_elevation_degrees)
{
    const double night =
        std::clamp((kAirNightFrom - sun_elevation_degrees) / (kAirNightFrom - kAirNightTo), 0.0, 1.0);
    return night * night * (3.0 - (2.0 * night));
}

double air_condition(double sun_elevation_degrees, bool rain, const AirArt& art)
{
    const double low = (sun_elevation_degrees - kAirLowSunPeak) / kAirLowSunWidth;
    const double made = 1.0 + (kAirLowSunExtra * std::exp(-low * low)) +
                        (kAirNightExtra * art.night * air_night(sun_elevation_degrees)) + (rain ? kAirRainExtra : 0.0);
    return std::max(made, art.least);
}

AirArt air_art(AirLook look)
{
    // (the numbers: docs/design/vulkan.md "The air's look" - the cinematic step's from the check's street and lamp
    // cases: a beam across a street at dusk and a lamp's cone from the side well over the shade, the far view along a
    // street at dusk not whited out)
    switch (look) {
    case AirLook::kClear:
        return {};
    case AirLook::kCinematic:
        return {.density = 2.0,
                .least = 1.0,
                .night = 1.5,
                .side = 6.0,
                .shade = 0.2,
                .glow = 3.0,
                .lamp_glow = 5.0,
                .range = 60.0,
                .along = 4.0,
                .deep = 4.0};
    case AirLook::kMisty:
        return {.density = 5.0,
                .least = 4.0,
                .night = 1.5,
                .side = 1.5,
                .shade = 1.0,
                .glow = 1.0,
                .lamp_glow = 3.0,
                .range = 150.0};
    case AirLook::kNone:
        break;
    }
    return {.density = 0.0};
}

AirLook parse_air_look(std::string_view text)
{
    if (text == "clear" || text == "0") {
        return AirLook::kClear;
    }
    if (text == "misty" || text == "2") {
        return AirLook::kMisty;
    }
    return AirLook::kCinematic;
}

const char* air_look_name(AirLook look)
{
    switch (look) {
    case AirLook::kNone:
        return "none";
    case AirLook::kClear:
        return "clear";
    case AirLook::kCinematic:
        return "cinematic";
    case AirLook::kMisty:
        return "misty";
    }
    return "?";
}

StreetAir street_air(double sun_elevation_degrees, const AirWeather& weather, AirLook look)
{
    StreetAir air;
    const AirArt art = air_art(look);
    if (!(art.density > 0.0)) {
        return air;
    }
    air.extinction =
        kAirClearDay * air_condition(sun_elevation_degrees, weather.rain, art) * art.density / kUnitsPerMetre;
    const double night = air_night(sun_elevation_degrees);
    // (the look's depth by day: a night's mist lies low whatever the look - the lamps' heads near its top)
    air.height = ((kAirHeight * art.deep) + ((kAirNightHeight - (kAirHeight * art.deep)) * night)) * kUnitsPerMetre;
    if (weather.snow) {
        air.snow = kAirSnow / kUnitsPerMetre;
        air.snow_height = kAirSnowHeight * kUnitsPerMetre;
    }
    return air;
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

double air_phase(double cosine, double g, const AirArt& art) { return medium_phase(cosine, g) + art.side; }

double air_stretch(double cosine, const AirArt& art)
{
    const double c = std::max(cosine, 0.0);
    return 1.0 + ((art.along - 1.0) * c * c);
}

double air_reach(double distance, const AirArt& art, double cosine)
{
    if (!(art.range > 0.0)) {
        return 1.0;
    }
    const double range = art.range * kUnitsPerMetre * air_stretch(cosine, art);
    const double t = std::clamp((distance - (0.5 * range)) / (0.5 * range), 0.0, 1.0);
    return 1.0 - (t * t * (3.0 - (2.0 * t)));
}

double air_map_half(const AirArt& art)
{
    if (!(art.range > 0.0)) {
        return 0.0;
    }
    // (the farthest a point of the volume lies from the eye across the light's way: range x stretch x sine, over the
    // angles - a small search; at least the range)
    constexpr int kAngles = 90;
    double widest = 1.0;
    for (int i = 0; i <= kAngles; ++i) {
        const double angle = 0.5 * std::numbers::pi * static_cast<double>(i) / kAngles;
        widest = std::max(widest, air_stretch(std::cos(angle), art) * std::sin(angle));
    }
    constexpr double kMargin = 1.03;
    return art.range * kUnitsPerMetre * widest * kMargin;
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
