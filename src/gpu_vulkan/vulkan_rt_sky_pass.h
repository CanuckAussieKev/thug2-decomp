#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_rt_sky.h"

// NOT ORIGINAL - a modern sky (docs/design/vulkan.md "The sky"; [Graphics] rt_sky, rt_clouds,
// rt_cloud_coverage, rt_cloud_quality, rt_cloud_shadows - gpu_vulkan.h's RtSkySettings). With rt_sky=1
// render_pc draws the level's sky dome as before, then tells this listener (rt::sky_view_drawn, ViewOrder::kSky), which
// draws over every pixel the world left empty (the depth's far value) - before the sky scene's transparent layers (kept
// only without clouds: painted cloud cards, haze bands, glows) and before every other effect:
//   0. the level's own sky, once per sky scene: a new capture of the dome (rt_scene.h SkyCapture) starts the clouds'
//      histories again. Nothing of the painted dome is used: the sky and the clouds are the atmosphere's at every time
//      of day (its colours matched in before - rt_sky_match, retired - kept a midday blue at dawn), and the clouds'
//      cover is the weather's (rt_clouds.h cloud_coverage - round S2; its clouds' share read from the dome before);
//   1. the atmosphere (vulkan_rt_sky.h: transmittance, Psi, the sky-view table - the model alone) for the sun the player
//      sees (vulkan_rt_sky.h visible_sun: the time of day's - rt::DayLight -, the game's SetSun, the baked sun; else
//      50 degrees up), the night sky's light added at night; the fog's table follows it, so the aerial haze fades the
//      far world toward this sky;
//   2. the clouds (rt_clouds; vulkan_rt_clouds.h's march at a quarter, half or full resolution - only texels with sky in
//      their block -, the start of each texel's samples jittered by interleaved gradient noise + the frame's golden-ratio
//      offset), drifting with the game clock (rt::ViewDrawn::game_seconds: a demo replays the same clouds); lit by the
//      sun through the air to the layer (the atmosphere's transmittance there: golden when low) and by the drawn sky's
//      own light (rt_clouds.h cloud_sky_light, read from the sky-view table: the sky as it is now);
//   3. a temporal pass: each texel's direction reprojected through the viewport's last camera (the clouds are far: a
//      direction is enough), the history read there (bilinear), clamped to this frame's 3 x 3 neighbourhood, blended
//      by kSkyHistoryBlend; restarted where the history had no sky or the view is new;
//   4. the composite (a full-screen triangle at the far plane, depth-tested against the world: every sample of the sky
//      and only those): the sky-view table toward the pixel x the clouds' transmittance + their light (upsampled from
//      the low-resolution history, its texels without sky left out), the clouds faded into the sky with distance
//      (aerial perspective), written as linear light (round F: the frame is linear; nothing clipped - the output
//      encodes once).
// Nothing here needs ray tracing.
namespace render::vulkan {

struct Backend;
struct Image;

inline constexpr float kSkyHistoryBlend = 0.1F;         // this frame's share of a texel with a history
inline constexpr double kCloudAerialDistance = 20000.0; // metres: the clouds fade into the sky by e over this
inline constexpr double kCloudSunScale = 1.0;           // the sun's light on the clouds x the sky's exposure
inline constexpr uint32_t kSkyViewSlots = 4;
inline constexpr std::size_t kSkyMatrixFloats = 16;
inline constexpr std::array<float, 3> kDefaultSkySun{0.36F, 0.77F, 0.53F}; // (50 degrees up: no sun anywhere)

// The depth usage the sky's passes need (sampled) - make_depth_surface.
[[nodiscard]] VkImageUsageFlags rt_sky_depth_usage(const Backend& state, VkFormat format);

// The passes for a viewport (the listener's work; the check's too). The caller holds the lock. False: skipped.
bool record_rt_sky_pass(Backend& state, const rt::ViewDrawn& view);
// The level's sky read (step 0) and the atmosphere's tables made (1) for the frame's sun before any view is drawn
// (VulkanRayTracing::prepare_frame, each frame; without ray tracing the first view's pass does it, as before): a new
// level's first frame - shown over the loading screen's last picture - has its sky, and what reads the drawn sky's
// colours (the fog's table, the one-bounce light) the new level's, not the last one's for a frame. The caller holds
// the lock.
void prepare_rt_sky(Backend& state);
// A new level's (a new capture's) or a new viewport's clouds: their history seeded in their first frame - the march
// and the temporal pass run kSkySeedFrames times with the next frames' jitter - so they show converged, nothing
// shimmering into place.
inline constexpr uint32_t kSkySeedFrames = 12;
[[nodiscard]] uint64_t rt_sky_passes();
// (round S2) The clouds' histories start again (seeded in their next pass) - a lighting cut (rt_daylight.h DayLight::cut):
// they hold the light before it, at its pre-exposure. The caller holds the lock.
void rt_sky_reset_history();

// The level's sky: its capture's serial, the clouds' coverage used (the weather's) and the atmosphere's input.
struct RtSkyLevel {
    uint64_t capture_serial = 0;
    AtmosphereInput atmosphere; // what the sky-view table is made with
    float coverage = 0.0F;      // the clouds' coverage used
};
[[nodiscard]] RtSkyLevel rt_sky_level();

// The parameters the last pass was given (the check's reference).
struct RtSkyFrame {
    std::array<float, 3> sun{};
    std::array<float, 3> camera{};
    double camera_altitude = 0.0;   // metres
    std::array<double, 2> offset{}; // metres: the clouds' offset (the camera's place + the drift)
    std::array<float, 3> sun_light{};
    std::array<float, 3> sky_glow{}; // added to the sky's light on the clouds (rt::CloudLight::moon_glow); the sky's own
                                     // is the sky-view table's (rt_clouds.h cloud_sky_light)
    float coverage = 0.0F; // the model's amount (rt::cloud_amount of the level's coverage)
    uint32_t steps = 0;
    uint32_t scale = 1;
    float jitter_offset = 0.0F;
    std::array<float, kSkyMatrixFloats> to_world{}; // column-major: the target's pixel (x, y, depth, 1) -> world
    bool history = false;
};
[[nodiscard]] RtSkyFrame rt_sky_frame();

// For the check: the viewport's images read back (the frame's work finished first): the march's (rgb light, a
// transmittance; b: x distance, y 1 where marched) and the history's.
enum class RtSkyImage : uint8_t { kMarch, kMarchInfo, kHistory, kHistoryInfo };
struct RtSkyTexels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::array<float, 4>> texels;
};
bool read_rt_sky_image(Backend& state, const std::array<uint32_t, 4>& rect, RtSkyImage which, RtSkyTexels* out);

// The passes' time on the device (timestamps; logged every 240 as "rt_sky: ..."): frames timed, milliseconds of the
// clouds' march, the temporal pass and the composite.
struct RtSkyTiming {
    uint64_t samples = 0;
    std::array<double, 3> milliseconds{};
};
[[nodiscard]] RtSkyTiming rt_sky_timing();

// The reflections' misses (vulkan_rt_reflections.cpp, with rt_sky): what a ray that leaves the world sees - the drawn
// sky toward it (the sky-view table) and the clouds this frame's pass drew for the viewport where the ray's direction
// falls inside it (their history, read as the composite reads it, faded out toward the viewport's edges; outside: the
// sky alone). False: no sky to show (rt_sky off, or not drawn yet) - the misses keep the game's environment map.
struct RtSkyForRays {
    VkDescriptorImageInfo sky_view{}; // combined image samplers
    VkDescriptorImageInfo transmittance{};
    VkDescriptorImageInfo cloud_light{}; // storage images (GENERAL); without clouds the sky-view table's (unread)
    VkDescriptorImageInfo cloud_info{};
    std::array<float, kSkyMatrixFloats> to_pixel{}; // column-major: a world direction (w 0) -> the target's pixel
    std::array<float, 4> sun{};                     // xyz toward the drawn sky's sun; w 1: clouds
    std::array<float, 4> low{};  // the clouds' images: x, y their size, z pixels per texel; w the aerial distance (m)
    std::array<float, 4> rect{}; // the viewport (the target's pixels)
};
bool rt_sky_for_rays(Backend& state, const std::array<uint32_t, 4>& rect, RtSkyForRays* out);
// Its GLSL: the four bindings from `first_binding` (the sky view, the transmittance, the clouds' light and info) and
//   vec3 sky_for_ray(vec3 dir, mat4 to_pixel, vec4 sun, vec4 low, vec4 rect)   exposed linear
[[nodiscard]] std::string rt_sky_rays_glsl(uint32_t first_binding);

// (added by the volumetric light - engine/gfx/rt_volume_light.h) The clouds' shadow map: the light's transmittance
// through the cloud layer (rt::medium_light: the sun, or the moon at night), made at each viewport's opaque_drawn while
// the clouds are drawn and rt_cloud_shadows is on - one map, read by both: the shadows on the ground and the shafts
// through the clouds' gaps in the air (changed by the cinematic air: the fog's march no longer asks for it alone - the
// air shaded by clouds the ground isn't was two lights; one light for both).
struct CloudShadowMap {
    VkDescriptorImageInfo image{}; // a combined image sampler (linear, clamped; GENERAL): r the transmittance
    std::array<float, 4> place{};  // x, z its centre (world), y the cloud base's height (world), w 1 / its width
    std::array<float, 4> light{};  // xyz toward the light, w the shadow's share kept (fading out with a low light)
};
// The map made in this frame (for the viewport drawing now: its opaque_drawn came before). False: none (no clouds, no
// light, not wanted). The caller holds the lock.
bool cloud_shadow_map(const Backend& state, CloudShadowMap* out);
// The GLSL that reads it (after the caller's own set 0 bindings; the sampler at set 0, `binding`):
//   float cloud_map_kept(vec3 at, vec4 place, vec4 light) - the share of the light that gets through the clouds to `at`
[[nodiscard]] std::string cloud_map_glsl(uint32_t binding);

// How many maps were made (the checks').
[[nodiscard]] uint64_t cloud_maps_made();
// For the check: the map last made read back (the frame's work finished first): kCloudMapSize^2 transmittances, row by
// row, and where it lies. The caller holds the lock.
bool read_cloud_shadow_map(Backend& state, std::vector<float>* texels, CloudShadowMap* made);

void release_rt_sky(Backend& state);

} // namespace render::vulkan
