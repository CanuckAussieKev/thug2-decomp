#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - volumetric fog and light shafts (docs/design/vulkan.md "Volumetric fog"; [Graphics] rt_fog - gpu_vulkan.h's
// RtFogSettings). After a viewport's whole world is drawn (rt::world_view_drawn, ViewOrder::kFog: after the transparent
// passes, the particles and the weather, before tone mapping and the 2D), compute passes over images of half the
// viewport's size, then one full-screen pass over the colour target:
//   1. march: each texel takes one pixel of its 2 x 2 block (a different one each frame) and follows the view ray from
//      the camera to the depth's surface (the sky: to the fog's end) through `steps` segments. The fog is the level's own
//      (rt::ViewDrawn::fog): the game's linear range fog f(d) = (end - d) / (end - start) is a medium whose extinction is
//      1 / (end - d) between start and end - integrated exactly over each segment, so its transmittance T is the game's
//      f (to the power rt_fog_density), whatever the step count - times the height falloff exp(-(y - camera y) / H)
//      (rt_fog_height = H; 0: even). At one point in each segment (jittered: interleaved gradient noise + the frame's
//      golden-ratio offset) a ray query toward the sun (the level's baked sun - engine/gfx/rt_sun.h, as R4's shadows
//      use it, or the console's `rt sun` override; masks 0x01 world + 0x02 objects, the first hit ends it) says whether
//      that point is lit. The light scattered there toward the eye:
//          L = fog colour x (a + (1 - a) x lit x phase x tint)   (a = 0.5: the share the sky and the surroundings give)
//      phase = Henyey-Greenstein with g = rt_fog_anisotropy, x 4 pi (1 on average over the sphere: an unshadowed fog
//      looks as the game's does), tint = 1 (the bake gives the sun no colour; the fog's carries the hue). No sun (the
//      fit not run yet, or a level whose bake shows none - indoors, the Foundry): L = the fog colour - no shafts. Accumulated front to back: in += T (1 - e^-tau) L, T *= e^-tau;
//   2. temporal: the texel's point reprojected through the viewport's camera of the frame before; the history read there
//      (bilinear over the texels whose distance agrees), clamped to this frame's 3 x 3 neighbourhood, blended with this
//      frame's by 1 / its length (up to kFogMaxHistory);
//   3. composite (full resolution, a blend): colour = colour x T + in - the low-resolution result upsampled with
//      weights falling off with the difference between each texel's distance and the pixel's own (depth-aware); every
//      sample of a multisampled target the pixel's value, alpha kept.
// Modes (rt_fog_mode):
//   0 replace (the default): the level's own fog is turned off in the draws (rt_fog_replaces_level_fog; the Vulkan
//     Gpu's fixed-function emulation) and the pass draws it instead - the same colour, distances and transmittance, but
//     darker where the world shadows the fog from the sun and brighter looking into the sun. Its limit: the transparent
//     passes and the particles don't write depth, so they get the fog of the surface behind them.
//   1 shafts: the level's own fog kept as the game draws it; only the sunlit part of the light, L = fog colour x (1 - a)
//     x lit x phase x tint x 0.5, is added (T = 1: nothing taken away).
// The sky (the depth's far value - the sky dome, or the fog-coloured clear with distance clipping) is never dimmed: it
//   gets the sunlit light only, x 0.1 (the shafts against the sky, darker gaps where the world blocks the sun).
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

inline constexpr uint32_t kFogScale = 2;       // the march's resolution: 1 / this of the viewport's on each axis
inline constexpr uint32_t kFogMaxHistory = 12; // frames
inline constexpr uint32_t kFogViewSlots = 4;
inline constexpr uint32_t kFogMask = 0x01 | 0x02; // (world, objects: what shadows the fog)
inline constexpr float kFogAmbientShare = 0.5F;   // a above
inline constexpr float kFogSkyShare = 0.1F;
inline constexpr float kFogShaftShare = 0.5F;       // mode 1's
inline constexpr float kFogSunDistance = 100000.0F; // how far a ray toward the sun looks (world units)
inline constexpr float kFogHazeEnd = 4000.0F;       // the haze's end (world units: its linear "fog" from the camera)
inline constexpr std::array<float, 3> kFogHazeColour{0.8F, 0.8F, 0.8F};
// (added by the sun's glare) The sky model's haze - aerial perspective (rt_sky_model=1 and rt_fog_haze=1 in a level
// without fog, with a sun): a medium of its own instead of the thin haze's added light. Its extinction is
// kFogAerialExtinction x rt_fog_haze_density per world unit at the level's ground (rt::BakedSun::ground; the camera's
// height when unknown), thinning by e every rt_fog_haze_height above it, followed to kFogAerialFar (closed form along
// each segment); x rt_fog_density as the level's fog. Its light at a point:
//     sky x a  +  sun x luma(sky) x (1 - a) x kFogAerialSunGain x lit x phase
// sky the model's sky toward the ray's direction (no lower than kFogAerialLowest above the horizon: the colour the far
// world fades to), sun the model's sun colour, a = kFogAerialSkyShare (the shade's share: lit haze is several times the
// shaded's - beams between buildings), phase two Henyey-Greenstein lobes (kFogAerialForwardShare of g =
// kFogAerialForwardG, the rest g = kFogAerialBroadG) x 4 pi. Composited as the replaced fog: colour x T + light. The sky
// is never dimmed (only its sunlit light x kFogSkyShare added).
inline constexpr float kFogAerialExtinction = 1.0F / 12000.0F;
inline constexpr float kFogAerialFar = 40000.0F;
inline constexpr float kFogAerialSkyShare = 0.45F;
inline constexpr float kFogAerialSunGain = 1.2F; // (sun x luma(sky) x (1 - a) x this: lit haze from the side ~0.9 of the sky)
inline constexpr float kFogAerialForwardShare = 0.4F;
inline constexpr float kFogAerialForwardG = 0.6F;
inline constexpr float kFogAerialBroadG = 0.2F;
inline constexpr float kFogAerialLowest = 0.03F; // radians

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
