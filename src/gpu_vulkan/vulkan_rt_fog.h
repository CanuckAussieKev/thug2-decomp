#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_volume_light.h"

// NOT ORIGINAL - volumetric fog and light shafts (docs/design/vulkan.md "Volumetric fog"; [Graphics] rt_fog - gpu_vulkan.h's
// RtFogSettings). (Round FOG - rewritten as froxel volumetric fog: Wronski 2014 "Volumetric Fog" (AC4), Hillaire 2015
// "Physically Based and Unified Volumetric Rendering in Frostbite", UE4/5's volumetric fog; the per-pixel march and its
// ReLAX-style histories retired - their few light samples a frame bubbled and crawled near small bright lights.)
// After a viewport's whole world is drawn (rt::world_view_drawn, ViewOrder::kFog: after the transparent passes, the
// particles and the weather, before tone mapping, the upscaler and the 2D):
//   0. the air's sun map (rt_volume_light.h air_map_half) - a square of rt_fog_quality's 512 / 1024 / 1536 texels across
//      the light's way round the eye, each the distance down the light's way to the first thing its ray hits; the
//      froxels read it inside the square (a bilinear compare, the light's disc's penumbra) instead of tracing.
//   1. the froxels' light: a camera-aligned grid - a column per RtFogSettings::scale x scale pixels of the viewport,
//      RtFogSettings::steps slices deep, slice x's near face at n ((far / n + 1)^(x / slices) - 1) from the eye
//      (exponential; far the fog's end - the level's, the haze's - or the aerial air's reach, n = far / 400). Each froxel
//      takes RtFogSettings::samples points a frame, each at a random place across the froxel and stratified along it
//      (the frame's golden-ratio offset), and at each: the sun's or the moon's share (its shadow ray toward a point of its
//      disc - masks 0x01 world + 0x02 objects + 0x08 the drawn-apart, the cut-outs by their alpha -, or the air's sun
//      map; the clouds' shadow map), the local lights' light toward the eye (the light list's local group by resampled
//      importance sampling - every lamp and emitting surface by power, the same lights ReSTIR DI and GI sample;
//      vulkan_light_list.h) and the sky's open share (a ray toward the upper hemisphere: the shade's light gets in only
//      through what is open). Then the history: the froxel's centre reprojected into the grid of the frame before, its
//      light read there trilinearly and blended exponentially (1 / (frames + 1) while young, then 1 / kFogMaxHistory) -
//      Frostbite's and UE's temporal reprojection. Two volumes: (the lamps' light, the sun's share), (the lamps' light
//      with Rayleigh's phase, the sky's share).
//   2. the integration: each column front to back along its ray (Hillaire's energy-conserving form: a slice's light S x
//      (1 - e^-tau) / sigma_t, each medium's optical depth exact over the slice). The media are the level's own
//      (rt::ViewDrawn::fog): (round UNITS) an even medium of Koschmieder's extinction for the game's distances
//      (engine/gfx/rt_volume_light.h level_fog_extinction: kKoschmieder / (end - start) per world unit, from `start` on;
//      x rt_fog_density) times the height falloff exp(-(y - camera y) / H) (rt_fog_height); the street air near the
//      ground (rt_volume_light.h street_air - the weather's aerosol, the falling rain or snow); or, in a level without
//      fog, the atmosphere's air (below). The level's fog is the weather's aerosol made denser: its albedo and phase
//      (Cornette-Shanks, rt_sky.h), lit in the global unit x the frame's pre-exposure by the light's irradiance, the
//      lamps and the open sky - linear radiance, single scattering (as Unreal's and Frostbite's volumetric fog: the
//      sky's light as an ambient term, no multiple scattering). A level without physical light (round FW: no current
//      relit data - the clock alone isn't) keeps the game's fog colour as its own light, and its lamps (the found
//      emitters at display brightness) light no air. Under the time of day's clock with physical light the level's fog
//      is the original's time of day states' (a stand-in for the night's dark and the day's haze) - no medium: the air
//      is the weather's alone, as in a level without fog (below), and the game's fog stays out of the draws. (Before: fog colour x (0.5 + 0.5 x (sun + lamps)) taken as display-referred and decoded
//      with the display's 2.2 power - a lamp's pre-exposed light in the air raised to its 2.2th power.) Each slice keeps what lies before its far face
//      (the light, the transmittance), its own source and the god rays' potentials; the level fog's sky pixels their
//      own line (a share of the sunlit light, nothing dimmed).
//   (the god rays - rt_volume_light.h god_ray_peak; rt_fog_god_rays) the froxels leave out the phase's forward peak and
//      add up its potential; 2a. what each texel of half the viewport's pixels puts in the light's way; 2b. three radial
//      passes toward the light's place on the screen - the composite adds the peak x the potential x what is open along
//      the line, no more than the froxels' own shadow allows.
//   3. the composite (full resolution, a blend): each pixel reads the volume at its depth - the integral before the
//      slice holding it (bilinear across the columns), then the slice's source from its near face to the pixel's depth,
//      its optical depth exact along the pixel's own ray: colour x T + in. Every sample of a multisampled target the
//      pixel's value, alpha kept. Ray Reconstruction's transparency layer, where it is drawn, gets the same at the front
//      layer's depth (vulkan_front_layer.h): its light x T + its coverage x in. The aerial air's light is linear
//      radiance; (round UNITS) the level fog's too.
// Modes (rt_fog_mode):
//   0 replace (the default): the level's own fog is turned off in the draws (rt_fog_replaces_level_fog; the Vulkan
//     Gpu's fixed-function emulation) and the pass draws it instead - the same colour, distances and transmittance, but
//     darker where the world shadows the fog from the sun and brighter looking into the sun. Without Ray Reconstruction's
//     transparency layer the transparent passes and the particles get the fog of the surface behind them.
//   1 shafts: the level's own fog kept as the game draws it; only the sunlit part of the light, x 0.5, is added (T = 1).
// The sky (the depth's far value) is never dimmed: it gets the sunlit light only, x 0.1 - except in the sky model's
//   aerial air, which hazes it as the world at the far plane.
// No fog in the level (ViewFog::on false, or end <= start): with rt_fog_haze=1 (the default) and a sun, the haze is
//   drawn in shafts mode - (round UNITS) the weather's street air alone, the grid to kFogHazeEnd; with the sky model the atmosphere's
//   air (aerial perspective, below); else the pass is skipped and nothing changes.
// The light added is never clamped: the frame is linear light.
// The froxels' history is kept per viewport (its rectangle and target size): up to kFogViewSlots (split screen's two),
// the least recently drawn dropped; a viewport not drawn in the frame before, or a camera cut, starts again.
namespace render::vulkan {

struct Backend;

// (round FOG) the froxels' history: blended by 1 / its frames up to this (an exponential history of weight 1 - 1 /
// kFogMaxHistory) - (round BUDGET) 32 frames (0.97; was 20 - 0.95): the froxels take a third of the samples they did a
// frame (gpu_vulkan.h kRtFogQualitySamples), the history holds them longer
inline constexpr uint32_t kFogMaxHistory = 32; // frames
// a history that starts again is seeded with this many frames of the froxels' light (vulkan_rt_history.h seed_frames)
inline constexpr uint32_t kFogSeedFrames = 8;
inline constexpr uint32_t kFogViewSlots = 4;
// (world, objects: what shadows the fog; round PG: what is drawn apart - the clutter, particles, billboards)
inline constexpr uint32_t kFogMask = 0x01 | 0x02 | 0x08;
// (added by the volumetric light) with the material table and the frame's geometry the cut-outs too, by their alpha
// (vulkan_rt_scene.h cutout_rays_glsl: light through foliage, fences and grates - the one lighting path's test)
inline constexpr uint32_t kFogCutoutMask = 0x01 | 0x02 | 0x04 | 0x08;
// (round WF) the most candidates a sample resamples from the light list (rt_fog_quality's most)
inline constexpr uint32_t kFogMostCandidates = 8;
inline constexpr float kFogSkyShare = 0.1F;
inline constexpr float kFogShaftShare = 0.5F;       // mode 1's
inline constexpr float kFogSunDistance = 100000.0F; // how far a ray toward the sun looks (world units)
inline constexpr float kFogHazeEnd = 4000.0F; // the haze's grid reach (world units; round UNITS: its air the street air)
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

// The scene the rays are traced through (R2's TLAS; VK_NULL_HANDLE: none - the pass is then skipped, logged once). The
// same contract as rt_shadows_set_scene (vulkan_rt_shadows.h). Thread: any (it takes the backend's lock) -
// rt_fog_set_scene_locked when the caller already holds it.
void rt_fog_set_scene(VkAccelerationStructureKHR tlas);
void rt_fog_set_scene_locked(VkAccelerationStructureKHR tlas);
// (round W) rt_fog on - the caller holds the lock (the snow's haze steps aside while the fog's march takes the falling snow).
[[nodiscard]] bool rt_fog_on_locked(const Backend& state);

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
// (round OM - the checks') The light list's candidates from the presampled pool (true, the default) or picked at each
// point as before it (the same distribution: a check of the pool's records against the list's own).
void set_rt_fog_light_pool(bool on);

// (round WF - path tracing, stage 7) The local lights the last pass's air was lit by: the light list's lamps (vulkan_light_list.h
// restir_local_lights - its order), its emitting triangles' count, the scale on their light (the frame's pre-exposure) and
// the frame (0: none - no material table, or nothing listed). For the checks and the log. The caller holds the lock.
struct RtFogLights {
    std::vector<rt::Emitter> lamps;
    std::size_t triangles = 0;
    float scale = 0.0F;
    uint64_t frame = 0;
};
[[nodiscard]] RtFogLights rt_fog_lights_locked(const Backend& state);
// (round MEDIUM) The air the last pass marched (rt_volume_light.h AirMedium; no layers: no pass, or none) - what the
// reference path tracer traces its paths through. The caller holds the lock.
[[nodiscard]] rt::AirMedium rt_fog_medium_locked(const Backend& state);

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

// For the check: one of the images of the viewport last drawn with `rect` read back (the frame's work finished first):
// width x height texels of 4 floats. (Round FOG) The resolve's images hold a texel per froxel column: the fog the
// column's middle pixel sees, read from the volume as the composite reads it (without the god rays' peak).
enum class RtFogImage : uint8_t {
    kRaw,         // the resolve: rgb the light scattered toward the eye, a the transmittance (the colour kept by)
    kRawInfo,     // r the distance to the pixel's surface (-1: the sky), g the pixel's x, b its y (viewport's), a the
                  // god rays' potential
    kHistory,     // as kRaw (there is one result: the froxels' history is in their light)
    kHistoryInfo, // r the distance, g the frames the froxels' history holds (with this one), b, a the pixel
    // (added by the god rays) step 2b's last radial pass (half the viewport's pixels): r the light's surroundings open
    // along the texel's line to the light x the peak's weight, g the weight (r / g: what is open); none when no rays ran
    kRays,
    kRawLit, // (round W2) r the god rays' potential under the froxels' own shadow
    // (round FOG) the volumes, their slices side by side (8 a row): the froxels' light after its history (rgb the lamps',
    // a the sun's share seen), and the integral before each slice's far face (rgb the light, a the transmittance)
    kLight,
    kIntegrated,
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
