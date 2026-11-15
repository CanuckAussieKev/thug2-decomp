#pragma once

#include <array>
#include <cstdint>

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

struct MediumLight {
    bool have = false;      // a light (none indoors, or before the baked sun is fitted)
    bool clock = false;     // the clock's light (DayLight): its colour and size are used
    bool moonlit = false;   // the moon
    std::array<float, 3> toward{};                // normalised
    std::array<float, 3> colour{1.0F, 1.0F, 1.0F}; // the light now over the level's own sun's (display-referred)
    float radius_degrees = 0.0F; // its angular radius - how soft the shafts' edges are: the clock's (DayLight::
                                 // light_radius - the sun's, the larger moon's); 0, a point, without the clock
    double sun_elevation = 0.0;                   // degrees: the sun's (not the moon's) - the haze's conditions
};
// The light from a scene sun and a clock state (pure: the checks').
[[nodiscard]] MediumLight medium_light_from(const SceneSun& scene, const DayLight& day);
// The frame's (scene_sun(), day_light()).
[[nodiscard]] MediumLight medium_light();

// The haze is thicker round sunrise and sunset (the morning's mist, the evening's haze): x (1 + kHazeLowSunExtra x
// exp(-((e - kHazeLowSunPeak) / kHazeLowSunWidth)^2)), e the sun's elevation in degrees - 2.5 x at its peak, back to 1
// by day and deep in the night.
inline constexpr double kHazeLowSunExtra = 1.5;
inline constexpr double kHazeLowSunPeak = 2.0;    // degrees
inline constexpr double kHazeLowSunWidth = 8.0;   // degrees
[[nodiscard]] double haze_condition(double sun_elevation_degrees);

// Over water the air is wetter: a layer kWaterHazeHeight thick (thinning by e) over the water's surface whose extinction
// there is kWaterHazeExtra x the haze's at the ground. The march's view ray that ends on water (its material's
// reflection class rt::kReflectWater) goes through it.
inline constexpr double kWaterHazeExtra = 2.0;
inline constexpr double kWaterHazeHeight = 400.0; // world units

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
