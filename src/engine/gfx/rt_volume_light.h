#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the volumetric light's model (docs/design/vulkan.md "Volumetric fog and light shafts"; the Vulkan
// backend's gpu_vulkan/vulkan_rt_fog draws it, vulkan_rt_sky_pass makes the clouds' shadow map it reads). API-neutral
// and pure but for medium_light(), which reads the frame's published state.
//
// One light lights the air: the scene's sun (rt_scene.h scene_sun - the one every effect lights by) and, while the clock
// lights the level (its origin kClock), the clock's state for it (rt_daylight.h DayLight: the sun by day, the moon at
// night - its colour and strength over the level's own sun's, its angular size). Without the clock the light is the
// level's own: colour 1, a point (as the shafts were before the clock). The air's own conditions - thicker at dawn and dusk, over water - and its
// phase function are here too, so the device's march and the checks' processor march are the same model.
namespace render::rt {

//
// (changed by the cinematic air) The light is the relit level's: the air is lit by a sun only where the relit mode
// relights the level (rt_scene.h SunRelight::relights - its offline data read, the mode on, the level not opted out), so
// the air's light is the light the level's surfaces show. A level without relit data has none: its fog is its own look
// (the game's colour whole, the lamps' light in it), no sunlit air, no street air. (The console's `rt sun` - an override,
// an experiment - lights the air whatever the level.)
struct MediumLight {
    bool have = false;      // a light (none indoors, on a level the relit mode doesn't relight, or before its sun is read)
    bool clock = false;     // the clock's light (DayLight): its colour and size are used
    bool moonlit = false;   // the moon
    std::array<float, 3> toward{};                // normalised
    std::array<float, 3> colour{1.0F, 1.0F, 1.0F}; // the light now over the level's own sun's (display-referred)
    float radius_degrees = 0.0F; // its angular radius - how soft the shafts' edges are: the clock's (DayLight::
                                 // light_radius - the sun's, the larger moon's); 0, a point, without the clock
    double sun_elevation = 0.0;                   // degrees: the sun's (not the moon's) - the haze's conditions
};
// The light from a scene sun, the relit mode's word on the level and a clock state (pure: the checks').
[[nodiscard]] MediumLight medium_light_from(const SceneSun& scene, const SunRelight& relight, const DayLight& day);
// The frame's (scene_sun(), sun_relight(), day_light()).
[[nodiscard]] MediumLight medium_light();

// The haze is thicker round sunrise and sunset (the morning's mist, the evening's haze): x (1 + kHazeLowSunExtra x
// exp(-((e - kHazeLowSunPeak) / kHazeLowSunWidth)^2)), e the sun's elevation in degrees - 2.5 x at its peak, back to 1
// by day and deep in the night.
inline constexpr double kHazeLowSunExtra = 1.5;
inline constexpr double kHazeLowSunPeak = 2.0;    // degrees
inline constexpr double kHazeLowSunWidth = 8.0;   // degrees
[[nodiscard]] double haze_condition(double sun_elevation_degrees);

// Over water the air is wetter: a layer kWaterHazeHeight thick (thinning by e) over the water's surface whose extinction
// there is kWaterHazeExtra x the haze's (and the street air's, below) at the ground. The march's view ray that ends on
// water (its material's reflection class rt::kReflectWater) goes through it.
inline constexpr double kWaterHazeExtra = 2.0;
inline constexpr double kWaterHazeExtraAir = kWaterHazeExtra;
inline constexpr double kWaterHazeHeight = 400.0; // world units

// (added by the street air) The air at street level. The aerial haze above is the atmosphere's - a few kilometres
// thick, so thin near the ground that a street's 50 metres hold an optical depth of a few hundredths: no light shafts
// between buildings, no cones under lamps. What makes them in a real street is the air's own lowest layer - aerosols,
// moisture and dust held near the ground (the boundary layer's bottom; a mist in the morning and at night, when the
// ground cools the air over it). The street air is that layer:
//   - its extinction at the level's ground: kAirClearDay per metre (a clear day's city air, ~13 km of visibility), x
//     air_condition (the time of day and the weather), as the look (AirLook, below) shapes it;
//   - thinning by e every kAirHeight metres above the ground (kAirNightHeight at night: the cooled ground's mist lies
//     low, the lamps' cones down to the street in it, the lamps' heads near its top) - as the haze, as thick as there
//     below it: a horizontal view a few metres up crosses it whole, a view up the buildings soon leaves it - the far
//     world seen over the streets isn't hazed by it;
//   - falling snow (the game's own snow, or [Graphics] snow with snowfall): kAirSnow per metre more, thinning over
//     kAirSnowHeight metres (snowflakes scatter as the haze's droplets do: forward);
//   - over water the wetter layer above (x kWaterHazeExtraAir).
// Its light is the haze's: the light's (sun, moon, lamps) x the phase x kHazeSunlit's share, its shade the sky's (or the
// level's fog colour where the level has fog). Lit like every other medium by the one light (medium_light).
inline constexpr double kUnitsPerMetre = 39.37; // (the world's units are inches)
inline constexpr double kAirClearDay = 3.0e-4;  // per metre
inline constexpr double kAirHeight = 8.0;       // metres
inline constexpr double kAirNightHeight = 4.0;  // metres: a night's mist lies lower (as night does: air_night)
// air_condition: x (1 + kAirLowSunExtra x exp(-((e - kAirLowSunPeak) / kAirLowSunWidth)^2) + kAirNightExtra x night
// + kAirRainExtra x rain) - e the sun's elevation in degrees, night 0 above kAirNightFrom rising smoothly to 1 at
// kAirNightTo (the ground's radiative cooling: a night's mist), rain 1 while it rains (wet air)
inline constexpr double kAirLowSunExtra = 7.0; // round sunrise and sunset: 8 x a clear day (~1.6 km of visibility)
inline constexpr double kAirLowSunPeak = 3.0;  // degrees (the clock's dawn and dusk: 8 up)
inline constexpr double kAirLowSunWidth = 10.0; // degrees
inline constexpr double kAirNightExtra = 11.0; // a night's mist: 12 x a clear day (~1.1 km), shallower (below)
inline constexpr double kAirNightFrom = -3.0;  // degrees
inline constexpr double kAirNightTo = -12.0;   // degrees
inline constexpr double kAirRainExtra = 3.0;
inline constexpr double kAirSnow = 1.2e-3;      // per metre (light snowfall: ~3 km of visibility)
inline constexpr double kAirSnowHeight = 60.0;  // metres

// --- (added by the cinematic air) the air's look: [Graphics] rt_fog_air, Options "Atmosphere" -----------------------
// The model above is the physical one, and physically a street's air is thin: beams between buildings show only looking
// toward a low sun, a street lamp's cone seen from the side not at all. Games (and films - a smoke machine on every set)
// show light in the air far more than a real street does: a beam that reads across a street needs air so thick that the
// same air, followed a few hundred metres, whites out the far world. The look is that departure, explicit and in steps,
// the way games' volumetric fog does it (a volume round the camera, its scattering set apart from its thickness, each
// light's own share in it):
//   - kClear: the physical air, exactly the model above (every factor 1, the phase medium_phase's, everywhere);
//   - kCinematic (the default): the beams and cones of a modern game -
//       a volume round the eye: the street air whole to `range` / 2 from the camera, gone by `range` (smoothly) - the
//       beams and cones near the player, the far world seen through the aerial haze alone, so it doesn't milk out;
//       denser (`density`), never thinner than `least` x a clear day's (a thin base by day), a night's mist `night` x;
//       scattering more than it takes away: the sun's (or moon's) light it scatters x `glow`, the lamps' x `lamp_glow`
//       (a film lights its practicals for the haze; a lamp's few hundred lumens over a sun's hundred thousand), what it
//       dims by its extinction alone - not energy-conserving, by design;
//       its phase given an even lobe (`side`, over medium_phase's: x 4 pi, 1 the light an even medium would scatter):
//       the lit air shows from the side, not only looking into the light - beams across the street, cones from the
//       pavement;
//       its shade's light (the sky's, the fog's ambient) x `shade`: the air out of the light stays clear, the beams
//       stand out against it;
//   - kMisty: a misty morning - thick at any hour, a wider volume, the shade's light whole.
// Only the street air takes the look: the aerial haze (the atmosphere's - the far world's), the level's own fog and the
// falling snow keep their own. kNone: no street air (the checks' cases of the other media alone).
enum class AirLook : uint8_t { kNone, kClear, kCinematic, kMisty };
struct AirArt {
    double density = 1.0;   // x the street air's extinction
    double least = 0.0;     // its air_condition at least (x a clear day's)
    double night = 1.0;     // x the night's mist (kAirNightExtra)
    double side = 0.0;      // the even lobe added to its phase (x 4 pi)
    double shade = 1.0;     // x its shade's light
    double glow = 1.0;      // x the sun's or moon's light it scatters
    double lamp_glow = 1.0; // x the lamps' light it scatters
    double range = 0.0;     // metres: the volume round the eye (0: everywhere)
};
[[nodiscard]] AirArt air_art(AirLook look);
// The ini's spelling: "clear", "cinematic", "misty" (and "0" / "1" / "2"); anything else the default.
[[nodiscard]] AirLook parse_air_look(std::string_view text);
[[nodiscard]] const char* air_look_name(AirLook look);
// The street air's phase (x 4 pi) for a look's side lobe: medium_phase + side.
[[nodiscard]] double air_phase(double cosine, double g, const AirArt& art);
// How much of the street air there is `distance` world units from the eye: 1 to range / 2, 0 from range (smoothstep).
[[nodiscard]] double air_reach(double distance, const AirArt& art);

// What the weather puts in the air: render_pc publishes it each frame from the game's weather and the snow's settings.
struct AirWeather {
    bool rain = false;
    bool snow = false;
    bool operator==(const AirWeather&) const = default;
};
void set_air_weather(const AirWeather& weather);
[[nodiscard]] AirWeather air_weather();

// (the look's `night` x the night's mist; at least its `least`)
[[nodiscard]] double air_condition(double sun_elevation_degrees, bool rain, const AirArt& art = {});
// How much it is night for the air (0 by day, 1 from kAirNightTo down; smooth between): its mist's share.
[[nodiscard]] double air_night(double sun_elevation_degrees);

// The street air for a light (its sun's elevation), the weather and the look (kNone: none): extinctions per world unit,
// heights in world units. Falling snow is the snow's whatever the look.
struct StreetAir {
    double extinction = 0.0; // at its ground
    double height = 0.0;
    double snow = 0.0;
    double snow_height = 0.0;
};
[[nodiscard]] StreetAir street_air(double sun_elevation_degrees, const AirWeather& weather, AirLook look);

// The air's phase function x 4 pi (1 on average over the sphere): strong forward scattering, as haze's droplets do (Mie),
// two Henyey-Greenstein lobes - kMediumForwardShare forward with g (rt_fog_anisotropy), the rest back with
// kMediumBackG. Looking toward the light the shafts stand out; looking away they are faint.
inline constexpr double kMediumForwardShare = 0.85;
inline constexpr double kMediumBackG = -0.3;
[[nodiscard]] double henyey_greenstein(double cosine, double g); // x 4 pi
[[nodiscard]] double medium_phase(double cosine, double g);

// The sunlit haze's brightness under the level's own sun (display-referred, x the light's colour and the phase): about
// the day sky's near the horizon - an unshaded haze lit from the side looks as bright as the sky behind it.
inline constexpr float kHazeSunlit = 0.55F;

// The clouds' shadow map (vulkan_rt_sky_pass makes it, the fog's march and the clouds' shadows on the ground read it):
// the light's transmittance through the cloud layer, kCloudMapSize texels a side over 2 x kCloudMapHalf metres of the
// cloud base's plane, centred where the camera's way toward the light meets it (snapped to whole texels: the map doesn't
// swim as the camera moves), kCloudMapSamples densities along the light through the layer. A point at `at` reads the
// texel where its way toward the light meets the base. The light no lower than kCloudMapLeastRise (its height's sine);
// the shadows fade in between kCloudMapFadeFrom and kCloudMapFadeTo of it (a low light's shadows lie too far along).
inline constexpr uint32_t kCloudMapSize = 256;
inline constexpr double kCloudMapHalf = 1600.0; // metres
inline constexpr uint32_t kCloudMapSamples = 8;
inline constexpr double kCloudMapLeastRise = 0.08;
inline constexpr double kCloudMapFadeFrom = 0.02;
inline constexpr double kCloudMapFadeTo = 0.15;
[[nodiscard]] double cloud_map_fade(double rise);
// Where a point's way toward the light meets the cloud base (world x, z): `base_y` the base's world height.
[[nodiscard]] std::array<double, 2> cloud_map_place(const std::array<double, 3>& at,
                                                    const std::array<double, 3>& toward, double base_y);

} // namespace render::rt
