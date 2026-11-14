#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <volk.h>

#include "gpu_vulkan/gpu_vulkan.h"

// NOT ORIGINAL - the sky model on the device (docs/design/vulkan.md "The sun, its glare and the sky"; engine/gfx/rt_sky.h
// is the model and its reference). One small compute pass writes the sky's table (rt::kSkyLutWidth x kSkyLutHeight + 1
// row, 16-bit floats) whenever the sun's direction changes: each entry the single-scattering integral of the model's
// air and haze along its view direction with the sun at the level's sun's elevation (32 segments, 16 toward the sun:
// rt_sky.h's integral with few steps), exposed (rt::sky_exposure()); the last row the sun's transmittance, its colour
// and the zenith's light. The fog's aerial perspective reads it (the sky's colour toward any direction); the sun's pass
// colours its glare from the sun's colour.
//
// The sun: the console's `rt sun` override, else the level's baked sun (rt_sun.h; none indoors - then there is no
// table and nothing uses one). Its colour: [Graphics] rt_sun_colour when given, else the model's (rt::sun_colour_at -
// warm when low, white when high). rt::set_sun_light publishes it (with rt_sky_model=1 or rt_sun_colour set) for the
// other effects - the one-bounce light tints its sun by it.
namespace render::vulkan {

struct Backend;
struct Image;

// The settings (vulkan_rt_sun.cpp's set_rt_sun_settings writes them; the caller of either holds the lock).
RtSunSettings& sun_settings_held();

// The sun this frame (normalised, toward it): the scene's (rt_scene.h scene_sun - changed by the scene's sun: the
// console's `rt sun`, else rt_sun_source's, every effect's) - asking for the baked sun's fit while any of the sun's
// effects is on. False: none.
bool frame_sun(std::array<float, 3>* toward);

// The sun the player sees (the disc, the glare and the rays are drawn there): the scene's (frame_sun) - so the disc is
// where the shadows and the light come from -, else (no scene sun: indoors, or the fit not made yet) where the game
// shows its sun (rt::game_sun), lighting nothing. False: none. (rt_sun_place is retired.)
bool visible_sun(std::array<float, 3>* toward);
// The sun's colour for `toward` alone (rt_sun_colour or the model's; cheap, kept for the last direction).
[[nodiscard]] std::array<float, 3> sun_colour_for(const std::array<float, 3>& toward);

// The sun's colour (display-referred, its brightest channel 1) and the sky's overhead for `toward` (set or the model's).
struct SkyColours {
    std::array<float, 3> sun{1.0F, 1.0F, 1.0F};
    std::array<float, 3> zenith{};
};
[[nodiscard]] SkyColours sky_colours(const std::array<float, 3>& toward);

// The table brought up to date for `toward` (recorded in `commands`, outside a render pass, when the direction changed
// or there was no table; a barrier after it for the shaders that read it). The caller holds the lock. False: it can't
// be made (logged once) - the callers draw without it.
bool update_sky(Backend& state, VkCommandBuffer commands, const std::array<float, 3>& toward);
// The table to be made again at the next update_sky (the settings changed), or forgotten for a frame with no sun (the
// other effects told there is no sun light: rt::set_sun_light off). The caller holds the lock.
void invalidate_sky();
void sky_without_sun();
// The table as a combined image sampler (linear, clamped; GENERAL layout) - valid after update_sky returned true.
[[nodiscard]] VkDescriptorImageInfo sky_table_descriptor();
// The GLSL a shader reading the table includes: `u_sky` at (set 0, `binding`), and
//   vec3 sky_linear(vec3 dir, vec3 to_sun)   the exposed linear radiance toward dir (world space, y up)
//   vec3 sky_display(vec3 dir, vec3 to_sun)  the same as the game's colours (^ 1 / 2.2)
[[nodiscard]] std::string sky_table_glsl(uint32_t binding);

// The table read back (the check's): (kSkyLutHeight + 1) x kSkyLutWidth texels of 4 floats, row by row. The caller holds
// the lock (the frame's work is finished first).
bool read_sky_table(Backend& state, std::vector<std::array<float, 4>>* texels);
// How many times the table was written (the check's).
[[nodiscard]] uint64_t sky_table_updates();
// The table and its pass released (the device's shutdown; the caller holds the lock).
void release_sky(Backend& state);

// A 16-bit float's value (the read-backs').
[[nodiscard]] float half_to_float(uint16_t half);

// --- (added by the sky: [Graphics] rt_sky) the atmosphere with every order of scattering ------------------------------
// Three tables of rt_sky.h's multiple-scattering model (16-bit floats, GENERAL layout): the transmittance (256 x 64,
// made once), Psi (32 x 32, once) and the sky seen from the viewer (192 x 108, exposed linear radiance), made again
// whenever its input changes: the drawn sky's sun, the gains that match it to the level (each band's colour multiplied
// - the horizon's, half way up's, overhead's, interpolated between their elevations); below the horizon the horizon's haze
// giving way to the model's far ground (never the level's dome: what it paints down there is left out of the match).
// The bands' centres (radians: 4, 30 and 72 degrees - rt_sky_match.h kSkyBandRanges), where each gain holds.
inline constexpr float kGainHorizon = 0.07F;
inline constexpr float kGainMiddle = 0.52F;
inline constexpr float kGainZenith = 1.26F;
// Below the horizon: the haze's elevation (radians, just above the horizon - the model's viewer is kViewerAltitude up,
// its own horizon 0.3 degrees down), how far down (radians) the model's ground comes in, and how much of it at most.
inline constexpr double kHazeElevation = 0.01;
inline constexpr double kBelowFade = 0.2;
inline constexpr double kBelowShare = 0.7;
inline constexpr double kNightHorizon = 1.5; // (added by the time of day) the night sky at the horizon over overhead
struct AtmosphereInput {
    std::array<float, 3> sun{0.0F, 1.0F, 0.0F}; // toward the drawn sky's sun (world, normalised)
    std::array<std::array<float, 3>, 3> gains{{{1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}}};
    std::array<float, 3> gain_elevations{kGainHorizon, kGainMiddle, kGainZenith}; // radians
    // (added by the time of day - engine/gfx/rt_daylight.h DayLight::night_sky) the night sky's light overhead, added
    // after the gains (exposed linear; kNightHorizon times it at the horizon and below); 0 by day and without the clock
    std::array<float, 3> night{};
    bool operator==(const AtmosphereInput&) const = default;
};
// The tables brought up to date (recorded outside a pass, a barrier after). The caller holds the lock. False: they can't
// be made (logged once).
bool update_atmosphere(Backend& state, VkCommandBuffer commands, const AtmosphereInput& input);
// While true, the fog's table (update_sky) is made from the sky-view table - the haze fades the far world toward the sky
// drawn, about its sun (the table's last row, entry 3, w 1).
void set_table_follows_sky_view(bool follow);
enum class AtmosphereTable : uint8_t { kTransmittance, kMultiple, kSkyView };
[[nodiscard]] VkDescriptorImageInfo atmosphere_descriptor(AtmosphereTable which);
void atmosphere_used(uint64_t serial); // (the tables read by a submission)
// The GLSL a shader reading the tables includes: u_transmittance and u_sky_view at (set 0, the bindings), and
//   vec3 atmosphere_transmittance(float r, float mu)  toward the top from radius r (metres), zenith cosine mu
//   vec3 sky_view_linear(vec3 dir, vec3 sun)          the drawn sky toward dir (world), exposed linear
[[nodiscard]] std::string atmosphere_glsl(uint32_t transmittance_binding, uint32_t sky_view_binding);
struct AtmosphereTexels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::array<float, 4>> texels;
};
bool read_atmosphere_table(Backend& state, AtmosphereTable which, AtmosphereTexels* out);
[[nodiscard]] uint64_t atmosphere_generation();
void release_atmosphere(Backend& state);

} // namespace render::vulkan
