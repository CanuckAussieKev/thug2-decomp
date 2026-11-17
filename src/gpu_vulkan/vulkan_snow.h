#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/gpu_vulkan.h"

// NOT ORIGINAL - snow (docs/design/vulkan.md "Snow"; [Graphics] snow - gpu_vulkan.h's SnowSettings): the Vulkan Gpu's
// side of engine/gfx/snow.h. What render_pc reads of the world - the level's sky map, the trails pressed each simulation
// step, the spray - is drawn in three places:
//   1. The cover (the world's draws): a draw render_pc marks as the ground or a scene object (snow::current_ground) on one
//      of the per-pixel lighting's programs gets FragmentKey::snow - its albedo blended toward the snow's by how much it
//      faces up and whether the sky map says it is open to the sky (ff_shaders.cpp kSnowBlock; SnowUniforms per view at
//      kSnowUniformsBinding, the sky map at kSnowSkyBinding). It is lit as the surface is - the bake, the time of day's
//      relit light, the per-pixel lighting, the traced shadows after: the one lighting path.
//   2. The layer (opaque_drawn, rt::ViewOrder::kSnow: after the ray-traced shadows, occlusion, bounce, lamps and
//      reflections have shaded the ground, before the clutter and the grass): the snow's own surface over all the open
//      ground round the camera - a camera-centred grid of levels (kSnowGridQuads cells a side, each level's cell twice
//      the last's from the trails' texel, out to kSnowLayerReach; a level's vertices near its edge move onto the next
//      level's grid, the inner level drawn where it is), each vertex the snow's top there (snow::surface_at's twin: the
//      sky map's ground + snow_depth x its cover, banked against what stands higher round it), faded to the flat cover
//      toward the reach, cut by the trails (snow::layer_height's twin); no triangle across a step the snow doesn't
//      bridge (a wall, a roof's edge); TRACED, none where a ray straight up meets something drawn (as the cover). Its
//      colour is the shaded ground under it: a compute pass puts each texel of the trails' window's ground point on the
//      screen (moved onto the drawn ground by the depth there) and, where the depth there is the ground's, keeps the
//      colour seen (a toroidal image beside the trails': a point hidden this frame keeps its last colour; beyond the
//      window, what is behind the layer's pixel); then turns it by its
//      own shape against the flat snow in the light now (the banks' normal and occlusion from the vertices, the trails'
//      relief, the trail walls' shadow - a short march over their heights toward the light), the packed snow in a trail
//      darker and glossier. Depth-tested and written, with the upscaler's motion (the camera's).
//   3. The air (world_drawn, the same order: before the fog and the tone mapping): the spray (each particle thrown from
//      its start, snow::spray_place's twin, growing and fading over its life) and the falling snow (snowfall: flakes in
//      the world - a near box and a far one round the camera, each flake's place wrapped into its box in the world's own
//      frame so the camera moves through them (flake_place is the shader's twin), carried by the level's wind and its
//      gusts' eddies, each a disc of its true size on the screen (most small, a few big) widened by the lens's defocus,
//      drawn as a streak along its own move on the screen over the shutter's share of the frame, lit by the light now -
//      rt::DayLight's sky and direct light, the direct light traced to the flake, scattered forward - or more where the
//      snow under it was lit more (the lamps: the gather's colours), and from below by that snow; hidden under roofs;
//      with the upscaler's motion, a second draw writes each flake's own motion where it covers enough of the pixel, and
//      their share of each pixel is the reactive mask), then the haze of the snow too far off to draw (a medium over the
//      world by its depth).
// The trails' texture is a window of window_tiles(quality) tiles a side round the camera, each tile in the slot its
// place modulo the window: as the camera moves, the tiles that come in are copied into the slots of those that went
// (the trails kept on the processor for the whole session), the others stay - nothing is lost near the camera. A tile is
// copied again only when its version changed (a press, the filling in).
namespace render::vulkan {

struct Backend;
struct Buffer;
struct DrawCall;

inline constexpr uint32_t kSnowMostFlakes = 22000;
inline constexpr uint32_t kSnowNearFlakes = 12000; // of them in the near box (the snow close by: parallax, streaks)
inline constexpr float kSnowFlakeBox = 1400.0F;    // the falling snow's box round the camera (world units a side)
inline constexpr float kSnowFlakeHeight = 900.0F;
inline constexpr float kSnowNearBox = 440.0F;
inline constexpr float kSnowNearHeight = 320.0F;
inline constexpr float kSnowFlakeLeast = 0.25F;    // a flake's radius (units: inches) from this ...
inline constexpr float kSnowFlakeSpread = 0.6F;    // ... to this more (most flakes small: the spread x a share squared)
inline constexpr float kSnowFlakeAlpha = 0.9F;     // a flake covering its whole disc
inline constexpr float kSnowLeastPixels = 0.75F;   // a flake drawn at least this radius (pixels), fainter by its share
inline constexpr float kSnowAperture = 0.15F;      // the lens's defocus: its aperture (units: an eye's, not a camera's) ...
inline constexpr float kSnowFocus = 260.0F;        // ... focused this far when the camera's focus isn't known
inline constexpr float kSnowNearClear = 40.0F;     // no flake drawn nearer the camera than this (faded in from 0.4 of it)
inline constexpr float kSnowShutter = 0.5F;        // a flake's streak: its move over this share of the frame before ...
inline constexpr float kSnowMostStreak = 16.0F;    // ... at most this long (pixels)
inline constexpr float kSnowMotionShare = 0.3F;    // a flake covering this share of a pixel or more: the pixel's motion is its
// (round W2) The eddies the flakes ride (swirl_at): rolls of wavelength kSnowSwirlLength across the wind and
// kSnowRollLength along it and up, frozen turbulence carried downwind at the wind's speed. Their swing is the
// turbulence's: a pattern of wavelength L passing at the wind's speed U moves the air at its amplitude A x 2 pi U / L, which
// is sqrt(2) sigma for a turbulence of standard deviation sigma = I U (wind.h turbulence_intensity, the lateral and
// vertical shares) - A = sqrt(2) I L / (2 pi), whatever the wind; and they turn over at sqrt(2) sigma / (L / 2 pi)
// radians a second (none in still air).
inline constexpr float kSnowSwirlLength = 260.0F; // units (the rolls across the wind: 6.6 m)
inline constexpr float kSnowSwirlDepth = 340.0F;  // units (their width across it)
inline constexpr float kSnowRollLength = 200.0F;  // units (the rolls along the wind and up: 5 m)
[[nodiscard]] float snow_swirl_amplitude(); // units
[[nodiscard]] float snow_roll_amplitude();  // units
// Their turning rates per unit of the wind's speed (radians a second per unit a second).
[[nodiscard]] float snow_swirl_rate();
[[nodiscard]] float snow_roll_rate();
inline constexpr float kSnowSkyReach = 4000.0F;    // the traced rays straight up: a surface or flake open this far
inline constexpr float kSnowSunReach = 6000.0F;    // a flake's ray toward the light
inline constexpr float kSnowHazeStart = 250.0F;    // the falling snow's haze (snow::extinction - the one value): from here out
inline constexpr float kSnowHazeSky = 9000.0F;     // the sky's distance through it
// (round W) the snow's albedo: the material table's fresh snow (rt_material_table.h - Warren 1982); a trail's packed
// snow is the table's packed snow, lit through the same light model (rt_brdf.h)
inline constexpr float kSnowAlbedo = 0.86F;
static_assert(kSnowAlbedo == 0.5F * (0.8F + 0.92F), "the table's fresh snow");
// (round W) the flakes' phase function (Henyey-Greenstein): ice crystals and aggregates scatter forward, asymmetry
// about 0.75 (Liou 2002, An Introduction to Atmospheric Radiation, ice crystals' g 0.75 - 0.85 in the visible)
inline constexpr float kSnowPhaseG = 0.75F;
inline constexpr uint32_t kSnowGridQuads = 128;     // the layer: a level's cells a side (a level twice the last's cell) ...
inline constexpr float kSnowLayerReach = 1500.0F;   // ... as many levels as reach this far (units: its height faded out
                                                    // from 0.6 of it - the flat cover beyond)
inline constexpr std::size_t kSnowMostLevels = 7;
inline constexpr float kSnowStepCut = 10.0F;        // a step the snow doesn't bridge (units, at least the depth) ...
inline constexpr float kSnowStepSlope = 0.8F;       // ... over a slope this steep across a grid cell: no layer across
inline constexpr float kSnowEdgeNoise = 0.22F;      // the cover's edge moved by its noise (the up share)
inline constexpr float kSnowLift = 0.3F;            // the layer lifted this far (units) where active: a trail's floor
                                                    // never fights the ground drawn under it for the depth

// A draw's snow descriptors (the world's draws with FragmentKey::snow).
struct SnowDescriptors {
    bool on = false;
    VkDescriptorBufferInfo block{};
    VkDescriptorImageInfo sky{};
    bool map_on = false;          // (round W - kTrailSnowTraced: the precipitation occlusion map at kWetMapBinding)
    VkDescriptorImageInfo map{};
    Buffer* stream = nullptr; // (kTrailSnowTint: the relit world's stream, bound as the cel shading's)
};
// The draw's key with the snow's cover where it takes it (snow on, the sky map on the device, a draw render_pc marked as
// the ground, one of the per-pixel lighting's programs, not water), and its descriptors. The caller holds the lock.
void snow_key(Backend& state, const GpuStateBlock& block, const DrawCall& call, ShaderKey* shaders, SnowDescriptors* out);

// The snow on the ground as another pass reads it - the decals' (vulkan_scatter.h: the marks the scatter put on the
// collision's faces lie under the snow, faded by its cover there). `on` false: snow off, or no sky map on the device.
// The sky map's cells (nearest): each its top's height (the low 16 bits over [range.x, range.x + range.y], 0 none) and
// the cover there (the high 16 bits: snow::slope_cover of the face) - a point no lower than the top less map.w is that
// face. The caller holds the lock.
struct SnowGround {
    bool on = false;
    VkDescriptorImageInfo sky{};
    std::array<float, 4> map{};   // x, z: the first cell's corner; z: a cell's size; w: the slack (units)
    std::array<float, 4> range{}; // x: the heights' low; y: their span; z, w: the map's columns and rows
};
[[nodiscard]] SnowGround snow_ground(Backend& state);

// The listener's work (also the check's). The caller holds the lock. False: nothing recorded (off, no sky map, nothing
// to draw, the targets can't take it, the pass can't be made - logged once).
bool record_snow_layer(Backend& state, const rt::ViewDrawn& view);
bool record_snow_air(Backend& state, const rt::ViewDrawn& view);
// What the view begins with: the per-view block, the sky map and the trails' window brought up to date.
void begin_snow_view(Backend& state, const rt::ViewDrawn& view);

struct SnowStats {
    uint64_t layer_passes = 0;
    uint64_t air_passes = 0;
    uint64_t covered_draws = 0;   // world draws given the cover
    uint64_t traced_draws = 0;    // ... of them traced (kTrailSnowTraced)
    uint64_t haze_passes = 0;
    uint64_t flake_motion_passes = 0; // the flakes' own motion drawn (a view with the upscaler's motion)
    bool last_traced_air = false; // the last air pass's flakes traced
    uint32_t last_tiles = 0;      // the last layer pass's tiles gathered (the trails' window)
    uint32_t last_levels = 0;     // ... and its grid's levels drawn
    bool last_traced_layer = false; // ... its rays straight up traced
    uint32_t last_spray = 0;      // the last air pass's spray particles
    uint32_t last_flakes = 0;     // ... and flakes
    uint64_t tiles_copied = 0;    // tiles copied into the window (a new tile in a slot, a changed one)
    uint64_t slots_cleared = 0;   // slots emptied (no tile there)
    std::array<int, 2> window{};  // the window's first tile (x, z)
    int window_size = 0;          // tiles a side
};
[[nodiscard]] SnowStats snow_stats();

// The trails' window on the device read back (the check's): RGBA8 texels, window_size x 64 a side, slot order (texel
// (x, z) of the world at (x mod N, z mod N)). The caller holds the lock; the frame's work is finished.
bool read_snow_window(Backend& state, std::vector<uint32_t>* texels, uint32_t* side);

// A falling flake's place (the flake vertex shader's twin - the check's): the snow's clock, the wind's drift and its
// strength now, and how far each moved since the frame before (seconds, at most a tenth; units).
struct FlakeClock {
    double clock = 0.0;
    double drift = 0.0;
    float gust = 1.0F;
    double since = 0.0;
    double drift_since = 0.0;
};
// The clock the last frame's flakes were drawn at (the caller holds the lock).
[[nodiscard]] FlakeClock flake_clock();
struct FlakePlace {
    std::array<float, 3> world{}; // where flake `index` is (round `camera`)
    std::array<float, 3> moved{}; // its own move since the frame before (the fall, the wind, its flutter, the eddies)
    float radius = 0.0F;          // its size (units)
    bool near = false;            // one of the near box's
};
[[nodiscard]] FlakePlace flake_place(uint32_t index, const FlakeClock& clock, const std::array<float, 3>& camera);

// (round W) The layer's light - its fragment shader's twin (the check's), traced: `ground` the snow-covered ground's
// radiance as the world drew it (the frame's linear light), `sun` the direct light per unit of N.L toward `l`, its
// visibility from the ground (`ground_seen`) and from the layer (`layer_seen`), `shape` the ground's normal, `n` the
// layer's, `v` toward the eye, `ao` the layer's hollows' shade, `packed` 0 - 1. Lit through rt_brdf.h with the material
// table's snow: the ground's light less its sunlight round the layer as round an even hemisphere, the sun by its own
// visibility.
struct SnowLayerInput {
    std::array<float, 3> ground{};
    std::array<float, 3> sun{};
    std::array<float, 3> l{0.0F, 1.0F, 0.0F};
    float ground_seen = 1.0F;
    float layer_seen = 1.0F;
    std::array<float, 3> shape{0.0F, 1.0F, 0.0F};
    std::array<float, 3> n{0.0F, 1.0F, 0.0F};
    std::array<float, 3> v{0.0F, 1.0F, 0.0F};
    float ao = 1.0F;
    float packed = 0.0F;
};
[[nodiscard]] std::array<float, 3> snow_layer_light(const SnowLayerInput& in);

// The objects released (the device's shutdown; the caller holds the lock).
void release_snow(Backend& state);

} // namespace render::vulkan
