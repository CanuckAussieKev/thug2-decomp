#pragma once

#include <array>
#include <cstdint>

#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"

// NOT ORIGINAL - the volumetric light's model (docs/design/vulkan.md "Volumetric fog and light shafts"; the Vulkan
// backend's gpu_vulkan/vulkan_rt_fog draws it, vulkan_rt_sky_pass makes the clouds' shadow map it reads). API-neutral
// and pure but for medium_light(), which reads the frame's published state.
//
// One light lights the air: the scene's sun (rt_scene.h scene_sun - the one every effect lights by) and, while the clock
// lights the level (its origin kClock), the clock's state for it (rt_daylight.h DayLight: the sun by day, the moon at
// night - its colour and strength over the level's own sun's, its angular size). Without the clock the light is the
// level's own: colour 1, a point (as the shafts were before the clock). The air itself is the atmosphere's (changed by
// the physical air): the weather's aerosol and the air's own molecules (rt_sky.h Aerosol, kRayleigh) - the same
// coefficients the sky is made of - with what falls through it (rain, snow); the device's march and the checks'
// processor march are the same model.
namespace render::rt {

// The light is the relit level's: the air is lit by a sun only where the relit mode relights the level (rt_scene.h
// SunRelight::relights - its offline data read, the mode on, the level not opted out), so the air's light is the light
// the level's surfaces show. A level without relit data has none: its fog is its own look (the game's colour whole, the
// lamps' light in it), no sunlit air. (The console's `rt sun` - an override, an experiment - lights the air whatever
// the level.)
struct MediumLight {
    bool have = false;      // a light (none indoors, on a level the relit mode doesn't relight, or before its sun is read)
    bool clock = false;     // the clock's light (DayLight): its colour and size are used
    bool moonlit = false;   // the moon
    std::array<float, 3> toward{};                // normalised
    std::array<float, 3> colour{1.0F, 1.0F, 1.0F}; // the light now over the level's own sun's (display-referred)
    float radius_degrees = 0.0F; // its angular radius - how soft the shafts' edges are: the clock's (DayLight::
                                 // light_radius - the sun's, the larger moon's); 0, a point, without the clock
    double sun_elevation = 0.0;                   // degrees: the sun's (not the moon's)
};
// The light from a scene sun, the relit mode's word on the level and a clock state (pure: the checks').
[[nodiscard]] MediumLight medium_light_from(const SceneSun& scene, const SunRelight& relight, const DayLight& day);
// The frame's (scene_sun(), sun_relight(), day_light()).
[[nodiscard]] MediumLight medium_light();

inline constexpr double kUnitsPerMetre = 39.37; // (the world's units are inches)

// --- (changed by the physical air) what falls through the air: the weather's precipitation ---------------------------
// The haze, the mist and the air's molecules are the atmosphere's (rt_sky.h - the weather picked by rt_fog_air); falling
// rain and snow add a grey extinction near the ground (their drops and flakes scatter forward as the aerosol does),
// thinning by e every kPrecipitationHeight. (round W) The snow's is its flakes' (engine/gfx/snow.h extinction - Gunn and
// Marshall's flakes at the snowfall's rate: 740 m at the moderate 1 mm/h). (changed by the rain) The rain's is
// its drops' (engine/gfx/rain.h extinction - Marshall-Palmer's drops at the rate, geometric optics): [Graphics] rain's
// rate, or the game's own scripted rain taken as moderate (rain::kModerateRate: about 4.5 km).
inline constexpr double kPrecipitationHeight = 60.0; // metres
// What the weather puts in the air: render_pc publishes it each frame from the game's weather, the snow's and the rain's
// settings.
struct AirWeather {
    bool rain = false;       // the game's own rain (its weather scripts)
    bool snow = false;
    double rain_rate = 0.0;  // (added by the rain) [Graphics] rain: mm/h (0: none)
    bool operator==(const AirWeather&) const = default;
};
void set_air_weather(const AirWeather& weather);
[[nodiscard]] AirWeather air_weather();
// The precipitation's extinction per metre at the ground (0: none).
[[nodiscard]] double precipitation_extinction(const AirWeather& weather);

// The air near the ground as one layer (the level's own fog's march - its game fog's light is display-referred, the
// air in it a single exponential layer): the aerosol's extinction at the ground (green) and the height it thins over
// (the mist's where there is one, else the haze's), the precipitation's - per world unit, heights in world units.
struct StreetAir {
    double extinction = 0.0; // at its ground
    double height = 0.0;
    double snow = 0.0;       // (the precipitation: rain or snow)
    double snow_height = 0.0;
};
[[nodiscard]] StreetAir street_air(const Aerosol& aerosol, const AirWeather& weather);

// The air's phase function x 4 pi (1 on average over the sphere): the aerosol's - Cornette-Shanks with its g (rt_sky.h
// aerosol_phase), the one phase the sky is made with.
[[nodiscard]] double henyey_greenstein(double cosine, double g); // x 4 pi
[[nodiscard]] double medium_phase(double cosine, double g);

// --- (added by the god rays) the forward peak's shadows, seen on the screen: [Graphics] rt_fog_god_rays -------------
// Looking toward a low sun through branches, past a tower, the air shows streaks radiating from the sun: the light its
// forward lobe scatters toward the eye, cut by everything between the eye's lines of sight and the sun. That light
// comes from within a few degrees of the sun, and its shadows are as fine as the branches seen against the sky - finer
// than the march's quarter-resolution texels and its steps. So the shadowing of the light the phase gathers near the
// light's way - its peak, the aerosol's phase over its value kGodRayConeDegrees from the light - is resolved at the
// screen's resolution instead (changed by the physical air: an estimate of the same integral, nothing added):
//   - the march (vulkan_rt_fog step 1) scatters the light with march_sun_phase - the phase without the peak - and adds
//     up, beside it, the peak's light unshadowed per unit of the peak's phase (its potential: the aerosol's light per
//     unit of light x what reaches the eye, x the clouds' shadow - the geometry's shadows are the screen's);
//   - the screen (steps 2a / 2b, half the viewport's resolution): along the line from each pixel toward the light's place
//     on the screen, the share of it that is sky (the depth's far value: cut-outs' gaps are sky) and the mean distance of
//     what stands on it - in three radial passes of kGodRayTaps taps each (kGodRayTaps^3 points: the same every frame);
//   - every point of the view ray toward the light projects onto that same line (epipolar), so what stands on it
//     shadows the air between it and the eye: open = 1 - (1 - sky share) x the share of the peak's air lying before
//     what stands there (god_ray_open; air_share_before - the medium's own: a uniform air's light within d of the eye,
//     (1 - e^(-sigma d)) / (1 - T));
//   - the composite adds peak(view) x potential x open x the light. open is at most 1: unshadowed the light is the
//     march's own peak, moved, not made (the checks' energy case); rt_fog_god_rays is on or off.
inline constexpr double kGodRayConeDegrees = 30.0;
inline constexpr int kGodRayTaps = 10; // per radial pass (three passes)
// The peak (x 4 pi): max(medium_phase(cosine, g) - medium_phase(cos kGodRayConeDegrees, g), 0).
[[nodiscard]] double god_ray_peak(double cosine, double g);
// The phase the march scatters the sun's or moon's light with: medium_phase, less the peak while the god rays take it.
[[nodiscard]] double march_sun_phase(double cosine, double g, bool god_rays);
// What of the peak's light gets through along a line toward the light: its sky share and the share of the peak's air
// lying before what stands on it.
[[nodiscard]] double god_ray_open(double sky_share, double air_before);
// The share of a uniform medium's light along a view ray that lies within `distance` of the eye: extinction `sigma` (per
// world unit), the whole ray's transmittance `transmittance`.
[[nodiscard]] double air_share_before(double distance, double sigma, double transmittance);

// (added by the distinct shafts; changed by the physical air) The air's sun map: where the air round the eye is in the
// sun, made each frame from the light's side - rays traced down the light's way through a square of texels
// (rt_fog_quality's: 512 / 1024 / 1536 a side) across the plane square to it, round the eye's place in it (snapped to
// whole texels: shadows don't swim as the camera moves), each texel the distance to the first thing its ray hits (the
// cut-outs by their alpha - the one lighting path's test). A point of the air is lit where it lies before that distance;
// a texel's penumbra is the light's disc's. Every step of the march inside the square reads it instead of a ray each
// other step - the near air's shadows sharp. It is how the visibility term is sampled, not part of the medium: it
// covers kAirMapReach round the eye, beyond it the march traces rays. The rays start kAirMapLift world units up the
// light's way.
inline constexpr double kAirMapReach = 100.0; // metres
inline constexpr double kAirMapLift = 100000.0; // world units (the fog's light rays' reach)
[[nodiscard]] double air_map_half(); // its half side in world units

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
