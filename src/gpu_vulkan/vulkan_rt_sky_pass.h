#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky_match.h"
#include "gpu_vulkan/vulkan_rt_sky.h"

// NOT ORIGINAL - a modern sky (docs/design/vulkan.md "The sky"; [Graphics] rt_sky, rt_sky_match, rt_clouds,
// rt_cloud_coverage, rt_cloud_speed, rt_cloud_quality, rt_cloud_shadows - gpu_vulkan.h's RtSkySettings). With rt_sky=1
// render_pc draws the level's sky dome as before, then tells this listener (rt::sky_view_drawn, ViewOrder::kSky), which
// draws over every pixel the world left empty (the depth's far value) - before the sky scene's transparent layers (kept
// only without clouds: painted cloud cards, haze bands, glows) and before every other effect:
//   0. the level's own sky, once per sky scene: render_pc's capture of the dome (rt_scene.h SkyCapture: six faces from
//      the origin) read back and analysed (engine/gfx/rt_sky_match.h: its clear sky's colours by band of elevation, its
//      clouds' share and colour) - and the atmosphere's model in the same bands (its sky-view table read back) - give the
//      gains that tint and scale the model toward the level's colours (x gain ^ rt_sky_match: 0 the model alone, 1 the
//      dome's band colours exactly; below the horizon the horizon's haze, never the dome's), the clouds' coverage (auto) and colour;
//   1. the atmosphere (vulkan_rt_sky.h: transmittance, Psi, the sky-view table with the gains) for the sun the player
//      sees (vulkan_rt_sky.h visible_sun: the game's SetSun; the baked sun without one; else 50 degrees up); the fog's
//      table follows it, so the aerial haze fades the far world toward this sky;
//   2. the clouds (rt_clouds; vulkan_rt_clouds.h's march at a quarter, half or full resolution - only texels with sky in
//      their block -, the start of each texel's samples jittered by interleaved gradient noise + the frame's golden-ratio
//      offset), drifting with the game clock (rt::ViewDrawn::game_seconds: a demo replays the same clouds);
//   3. a temporal pass: each texel's direction reprojected through the viewport's last camera (the clouds are far: a
//      direction is enough), the history read there (bilinear), clamped to this frame's 3 x 3 neighbourhood, blended
//      by kSkyHistoryBlend; restarted where the history had no sky or the view is new;
//   4. the composite (a full-screen triangle at the far plane, depth-tested against the world: every sample of the sky
//      and only those): the sky-view table toward the pixel x the clouds' transmittance + their light (upsampled from
//      the low-resolution history, its texels without sky left out), the clouds faded into the sky with distance
//      (aerial perspective), made display-referred (^ 1 / 2.2). An 8-bit target clips at 1; with rt_hdr=1 the sky's
//      light above 1 is kept for the tone mapping.
// Nothing here needs ray tracing.
namespace render::vulkan {

struct Backend;
struct Image;

inline constexpr float kSkyHistoryBlend = 0.1F;         // this frame's share of a texel with a history
inline constexpr double kCloudAerialDistance = 20000.0; // metres: the clouds fade into the sky by e over this
inline constexpr double kCloudSunScale = 1.0;           // the sun's light on the clouds x the sky's exposure
inline constexpr double kCloudAmbientTop = 1.0;         // the sky's light on the clouds' tops (x the zenith's)
inline constexpr double kCloudAmbientBottom = 0.6;      // and their bases (x the horizon's: its light and the ground's)
inline constexpr uint32_t kSkyViewSlots = 4;
inline constexpr std::size_t kSkyMatrixFloats = 16;
inline constexpr std::array<float, 3> kDefaultSkySun{0.36F, 0.77F, 0.53F}; // (50 degrees up: no sun anywhere)

// The depth usage the sky's passes need (sampled) - make_depth_surface.
[[nodiscard]] VkImageUsageFlags rt_sky_depth_usage(const Backend& state, VkFormat format);

// The passes for a viewport (the listener's work; the check's too). The caller holds the lock. False: skipped.
bool record_rt_sky_pass(Backend& state, const rt::ViewDrawn& view);
// The level's sky matched (step 0) and the atmosphere's tables made (1) for the frame's sun before any view is drawn
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

// What the level's sky was matched to (the last analysis) and what came of it.
struct RtSkyMatch {
    uint64_t capture_serial = 0;
    rt::SkyLook look;
    std::array<std::array<float, 3>, 4>
        model{};                // the model's band colours (exposed linear; ground, horizon, middle, zenith)
    AtmosphereInput atmosphere; // what the sky-view table is made with
    float coverage = 0.0F;      // the clouds' coverage used
    std::array<float, 3> cloud_gain{1.0F, 1.0F, 1.0F};
};
[[nodiscard]] RtSkyMatch rt_sky_match();

// The parameters the last pass was given (the check's reference).
struct RtSkyFrame {
    std::array<float, 3> sun{};
    std::array<float, 3> camera{};
    double camera_altitude = 0.0;   // metres
    std::array<double, 2> offset{}; // metres: the clouds' offset (the camera's place + the drift)
    std::array<float, 3> sun_light{};
    std::array<float, 3> ambient_bottom{};
    std::array<float, 3> ambient_top{};
    float coverage = 0.0F; // the model's amount (rt::cloud_amount of the match's coverage)
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
    std::array<float, 4> tint{}; // the clouds' gain
};
bool rt_sky_for_rays(Backend& state, const std::array<uint32_t, 4>& rect, RtSkyForRays* out);
// Its GLSL: the four bindings from `first_binding` (the sky view, the transmittance, the clouds' light and info) and
//   vec3 sky_for_ray(vec3 dir, mat4 to_pixel, vec4 sun, vec4 low, vec4 rect, vec4 tint)   exposed linear
[[nodiscard]] std::string rt_sky_rays_glsl(uint32_t first_binding);

void release_rt_sky(Backend& state);

} // namespace render::vulkan
