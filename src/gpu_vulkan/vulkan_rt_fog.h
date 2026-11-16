#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - volumetric fog and light shafts (docs/design/vulkan.md "Volumetric fog"; [Graphics] rt_fog - gpu_vulkan.h's
// RtFogSettings). After a viewport's whole world is drawn (rt::world_view_drawn, ViewOrder::kFog: after the transparent
// passes, the particles and the weather, before tone mapping and the 2D), compute passes over images of a quarter of the
// viewport's size on each axis (half at rt_fog_quality 2: RtFogSettings::scale), then one full-screen pass over the
// colour target:
//   1. march: each texel takes one pixel of its 4 x 4 (2 x 2) block - a different one each frame, in ordered-dither
//      order - and follows the view ray from the camera to the depth's surface (the sky: to the fog's end) through
//      `steps` segments, closer together near the eye (b t^2). The fog is the level's own
//      (rt::ViewDrawn::fog): the game's linear range fog f(d) = (end - d) / (end - start) is a medium whose extinction is
//      1 / (end - d) between start and end - integrated exactly over each segment, so its transmittance T is the game's
//      f (to the power rt_fog_density), whatever the step count - times the height falloff exp(-(y - camera y) / H)
//      (rt_fog_height = H; 0: even). At one point in each segment (jittered: interleaved gradient noise + the frame's
//      golden-ratio offset) a ray query toward the sun (the level's baked sun - engine/gfx/rt_sun.h, as R4's shadows
//      use it, or the console's `rt sun` override; masks 0x01 world + 0x02 objects, the first hit ends it) says whether
//      that point is lit. The light scattered there toward the eye:
//          L = fog colour x (a + (1 - a) x lit x phase x tint)   (a = 0.5: the share the sky and the surroundings give)
//      phase = the weather's aerosol's (Cornette-Shanks, rt_sky.h), x 4 pi (1 on average over the sphere: an unshadowed
//      fog looks as the game's does), tint = 1 (the bake gives the sun no colour; the fog's carries the hue). No sun (the
//      fit not run yet, or a level whose bake shows none - indoors, the Foundry): L = the fog colour - no shafts.
//      Accumulated front to back: in += T (1 - e^-tau) L, T *= e^-tau (stopped once T is under 1e-4). (Changed by the
//      street air, for its cost:) the steps go in pairs, one point of each pair - the first or the second by turns,
//      frame to frame - tracing the light's shadow ray and the picked lamp's for both; the lamps a view ray never comes
//      within reach of left out of its march.
//      The street air (engine/gfx/rt_volume_light.h street_air - outdoors, where a sun or moon lights the level) is a
//      second medium along every ray of the level's own fog: the weather's aerosol near the ground (one layer) and the
//      falling rain or snow, its extinction added to the fog's, its light kFogStreetAirLight per unit of light (the level
//      fog's display-referred light), mixed with the fog's by their shares of each segment's optical depth.
//   (added by the distinct shafts) 0. the air's sun map (rt_volume_light.h air_map_half) - a square of rt_fog_quality's
//      512 / 1024 / 1536 texels across the light's way round the eye (kAirMapReach each way), each the distance down the
//      light's way to the first thing its ray hits; the march reads it at every step inside the square (a bilinear
//      compare, the light's disc's penumbra) instead of a ray each other step - the near air's shadows sharp.
//   2. temporal: the texel's point reprojected through the viewport's camera of the frame before; the history read there
//      (bilinear over the texels whose distance agrees), clamped to this frame's 3 x 3 neighbourhood (changed by the
//      distinct shafts: its mean +- kFogHistorySpread deviations, inside its least and most), blended with this
//      frame's by 1 / its length (up to kFogMaxHistory);
//   (added by the god rays - rt_volume_light.h god_ray_peak; rt_fog_god_rays) the march leaves out the phase's forward
//      peak and adds up its potential (raw info's a); 2a. what each texel of half the viewport's pixels puts in the
//      light's way (its sky share, the distance of what stands there); 2b. three radial passes toward the light's place
//      on the screen (10 taps over 1/100, 1/10 and the whole of the way) - the composite adds the peak x the potential x
//      what is open along the line (the air before what stands on it shadowed by it), the light's colour, the setting.
//   3. composite (full resolution, a blend): colour = colour x T + in - the low-resolution result upsampled with
//      weights falling off with the difference between each texel's distance and the pixel's own (depth-aware); every
//      sample of a multisampled target the pixel's value, alpha kept. (Changed by the physical air:) the aerial march's
//      light is linear radiance - its composite reads a copy of the colour and writes (colour^2.2 x T + in)^(1/2.2).
// Modes (rt_fog_mode):
//   0 replace (the default): the level's own fog is turned off in the draws (rt_fog_replaces_level_fog; the Vulkan
//     Gpu's fixed-function emulation) and the pass draws it instead - the same colour, distances and transmittance, but
//     darker where the world shadows the fog from the sun and brighter looking into the sun. Its limit: the transparent
//     passes and the particles don't write depth, so they get the fog of the surface behind them.
//   1 shafts: the level's own fog kept as the game draws it; only the sunlit part of the light, L = fog colour x (1 - a)
//     x lit x phase x tint x 0.5, is added (T = 1: nothing taken away).
// The sky (the depth's far value - the sky dome, or the fog-coloured clear with distance clipping) is never dimmed: it
//   gets the sunlit light only, x 0.1 (the shafts against the sky, darker gaps where the world blocks the sun) - except
//   in the sky model's aerial haze (below), which hazes it as the world at the far plane.
// No fog in the level (ViewFog::on false, or end <= start - THUG2's levels draw fog only where a script asks for it, or
//   with [Graphics] DistanceClipping=1 and Fog=1): with rt_fog_haze=1 (the default) and a sun, a thin haze is drawn in
//   shafts mode - colour kFogHazeColour, from the camera to kFogHazeEnd - only its sunlit light added (the shafts, the
//   glow toward the sun; nothing dimmed, the shade left as the level lit it); else the pass is skipped and nothing
//   changes.
// Without rt_hdr the light added is clamped to 1.
// The histories are kept per viewport (its rectangle and target size): up to kFogViewSlots (split screen's two), the
// least recently drawn dropped; a viewport not drawn in the frame before starts again.
namespace render::vulkan {

struct Backend;

inline constexpr uint32_t kFogMaxHistory = 12; // frames
// (added by the distinct shafts) the history held within this many deviations of its 3 x 3 neighbours' mean (step 2)
inline constexpr float kFogHistorySpread = 1.5F;
inline constexpr uint32_t kFogViewSlots = 4;
inline constexpr uint32_t kFogMask = 0x01 | 0x02; // (world, objects: what shadows the fog)
// (added by the volumetric light) with the material table and the frame's geometry the cut-outs too, by their alpha
// (vulkan_rt_scene.h cutout_rays_glsl: light through foliage, fences and grates - the one lighting path's test)
inline constexpr uint32_t kFogCutoutMask = 0x01 | 0x02 | 0x04;
inline constexpr uint32_t kFogMostLamps = 32; // the lamps' block's room (rt_fog_quality's most)
inline constexpr float kFogAmbientShare = 0.5F;   // a above
inline constexpr float kFogSkyShare = 0.1F;
inline constexpr float kFogShaftShare = 0.5F;       // mode 1's
inline constexpr float kFogSunDistance = 100000.0F; // how far a ray toward the sun looks (world units)
inline constexpr float kFogHazeEnd = 4000.0F;       // the haze's end (world units: its linear "fog" from the camera)
inline constexpr std::array<float, 3> kFogHazeColour{0.8F, 0.8F, 0.8F};
// (added by the sun's glare; rewritten by the physical air) The atmosphere's air - aerial perspective (rt_sky_model=1
// and rt_fog_haze=1 in a level without fog, with a sun): the medium the sky is made of, at the level's ground (the
// baked sun's ground, rt::BakedSun::ground; the camera's height when unknown): the air's molecules (Rayleigh's
// scattering, rt_sky.h kRayleigh, thinning over kRayleighHeight) and the weather's aerosol (rt_sky.h Aerosol: its haze
// layer and any mist under it, their scattering, extinction and Cornette-Shanks phase), with the falling rain or snow
// (rt_volume_light.h) - followed to kFogAerialFar (closed form along each segment). Its light is the single scattering
// of the light (its irradiance in the sky's unit, its visibility traced) through both phases, every higher order as the
// sky's own (Psi at the ground: the scattering x the atmosphere's multiple-scattering transfer), and the lamps' - linear
// radiance, composited in linear light. Nothing caps it or tunes it toward a look: toward a low sun the air is as
// bright as its forward scattering makes it, the aureole the same phase the sky's own aureole is. The sky's pixels: where
// the atmosphere is drawn (rt_sky=1) nothing - its sky-view table holds the whole ray; over the level's painted sky dome
// (its horizon the far world past the level's meshes) marched as a surface at kFogAerialFar.
// A path tracer would read the same: sigma_s / sigma_t per band from kRayleigh and the Aerosol, the two phases, the
// sun's irradiance (rt_sky.h sun_transmittance x the unit) and the sky's radiance from the sky-view table.
inline constexpr float kFogAerialFar = 40000.0F;
// The level's own fog's street air: its light per unit of light (display-referred - the level fog path's light; the
// atmosphere's air above is linear radiance).
inline constexpr float kFogStreetAirLight = 0.3025F;

// The scene the rays are traced through (R2's TLAS; VK_NULL_HANDLE: none - the pass is then skipped, logged once). The
// same contract as rt_shadows_set_scene (vulkan_rt_shadows.h). Thread: any (it takes the backend's lock) -
// rt_fog_set_scene_locked when the caller already holds it.
void rt_fog_set_scene(VkAccelerationStructureKHR tlas);
void rt_fog_set_scene_locked(VkAccelerationStructureKHR tlas);

// The usage a depth surface of `format` gets besides being an attachment: VK_IMAGE_USAGE_SAMPLED_BIT when rt_fog is on,
// ray tracing is enabled and the format can be sampled, else none (make_depth_surface).
[[nodiscard]] VkImageUsageFlags rt_fog_depth_usage(const Backend& state, VkFormat format);

// Whether the draws leave the level's own fog out (rt_fog on in mode 0 and the last view's pass recorded): the Vulkan
// Gpu's draw state reads it for every draw. Thread: any.
[[nodiscard]] bool rt_fog_replaces_level_fog();

// The passes over the device's bound targets for a viewport (the listener's work, also called by the check program).
// The caller holds the lock. Returns whether they were recorded (false: skipped - off, no fog, no scene, the targets
// can't be read; the reason logged once).
bool record_rt_fog_pass(Backend& state, const rt::ViewDrawn& view);

// The passes recorded (for the check and the log).
[[nodiscard]] uint64_t rt_fog_passes();

// The passes' time on the device, from timestamps written before and after each (read back a few passes later, when
// the device has written them): how many were timed and their total. Every 240 timed passes the log gets their average
// ("rt_fog: the passes take ... ms on the device"). The caller holds the lock.
struct RtFogTiming {
    uint64_t samples = 0;
    double milliseconds = 0.0;
};
[[nodiscard]] RtFogTiming rt_fog_timing();

// Every viewport's history forgotten (the next pass starts again, the frame index back to 0). The caller holds the lock.
void rt_fog_reset_history(Backend& state);

// For the check: one of the low-resolution images of the viewport last drawn with `rect` read back (the frame's work
// finished first): width x height texels of 4 floats.
enum class RtFogImage : uint8_t {
    kRaw,     // step 1: rgb the light scattered toward the eye, a the transmittance
    kRawInfo, // step 1: r the distance marched to (-1: the sky), g the full-resolution pixel's x, b its y (viewport's)
    kHistory, // step 2: as kRaw
    kHistoryInfo, // step 2: r the distance, g the history's length
    // (added by the god rays) step 2b's last radial pass (half the viewport's pixels): r the light's surroundings open
    // along the texel's line to the light x the peak's weight, g the weight (r / g: what is open); none when no rays ran
    kRays,
};
struct RtFogTexels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::array<float, 4>> texels;
};
bool read_rt_fog_image(Backend& state, const std::array<uint32_t, 4>& rect, RtFogImage which, RtFogTexels* out);

// The passes' objects and the histories released (the device's shutdown; the caller holds the lock).
void release_rt_fog(Backend& state);

} // namespace render::vulkan
