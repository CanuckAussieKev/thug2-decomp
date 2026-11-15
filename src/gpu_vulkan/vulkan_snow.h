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
//      reflections have shaded the ground, before the clutter and the grass): where the trails raised it, a grid over
//      the ground - a vertex a trail texel - lifted snow_depth over the sky map's ground and cut by the trails (the
//      layer's height: snow::layer_height's twin). Its colour is the shaded snow under it: a compute pass puts each
//      active texel's ground point on the screen and, where the depth there is the ground's, keeps the colour seen
//      (a toroidal image beside the trails': a point hidden this frame - under the skater - keeps its last colour), then
//      the layer is that colour x how the trail's own slope turns it to the light against the flat snow (the time of
//      day's light and sky, or the level's baked sun), its shadow from the trail's walls (a short march over the trail
//      heights toward the light) and its occlusion (a trail lower than round it); the packed snow in a trail darker and
//      glossier (a highlight from the light), the powder as the ground. Depth-tested and written, with the upscaler's
//      motion (the camera's).
//   3. The air (world_drawn, the same order: before the fog and the tone mapping): the spray (each particle thrown from
//      its start, snow::spray_place's twin, growing and fading over its life) and the falling snow (snowfall: flakes in
//      the world - a near box and a far one round the camera, carried by the level's wind, each a disc of its true size
//      on the screen widened by the lens's defocus, lit by the light now - rt::DayLight's sky and direct light, the
//      direct light traced to the flake, scattered forward - and the lamps round it - the light field's local light -,
//      hidden under roofs; their share of each pixel in the upscaler's reactive mask), then the haze of the snow too far
//      off to draw (a medium over the world by its depth).
// The trails' texture is a window of window_tiles(quality) tiles a side round the camera, each tile in the slot its
// place modulo the window: as the camera moves, the tiles that come in are copied into the slots of those that went
// (the trails kept on the processor for the whole session), the others stay - nothing is lost near the camera. A tile is
// copied again only when its version changed (a press, the filling in).
namespace render::vulkan {

struct Backend;
struct Buffer;
struct DrawCall;

inline constexpr uint32_t kSnowMostFlakes = 22000;
inline constexpr uint32_t kSnowNearFlakes = 9000;  // of them in the near box (the dense snow close by)
inline constexpr float kSnowFlakeBox = 1400.0F;    // the falling snow's box round the camera (world units a side)
inline constexpr float kSnowFlakeHeight = 900.0F;
inline constexpr float kSnowNearBox = 340.0F;
inline constexpr float kSnowNearHeight = 260.0F;
inline constexpr float kSnowFlakeLeast = 0.2F;     // a flake's radius (units: inches) from this ...
inline constexpr float kSnowFlakeSpread = 0.32F;   // ... to this more
inline constexpr float kSnowFlakeAlpha = 0.9F;     // a flake covering its whole disc
inline constexpr float kSnowLeastPixels = 0.75F;   // a flake drawn at least this radius (pixels), fainter by its share
inline constexpr float kSnowAperture = 0.45F;      // the lens's defocus: its aperture (units) ...
inline constexpr float kSnowFocus = 260.0F;        // ... focused this far when the camera's focus isn't known
inline constexpr float kSnowSkyReach = 4000.0F;    // the traced rays straight up: a surface or flake open this far
inline constexpr float kSnowSunReach = 6000.0F;    // a flake's ray toward the light
inline constexpr float kSnowHazeStart = 250.0F;    // the falling snow's haze: from here out ...
inline constexpr float kSnowHazeExtinction = 3.2e-4F; // ... this a unit (half the light through at about 2400 units)
inline constexpr float kSnowHazeSky = 9000.0F;     // the sky's distance through it
inline constexpr float kSnowLampCell = 128.0F;     // the lamps' grid round the camera (the light field's cells) ...
inline constexpr std::array<int, 3> kSnowLampCells{9, 4, 9}; // ... its cells (x, y, z)
inline constexpr float kSnowAlbedo = 0.86F;         // the snow's colour before its light (the game's white 1)
inline constexpr float kSnowPackedShade = 0.8F;     // a trail's packed snow against the powder
inline constexpr float kSnowNearTiles = 3.0F;       // tiles nearer than this many tiles: every texel a vertex; else every other
inline constexpr float kSnowEdgeNoise = 0.22F;      // the cover's edge moved by its noise (the up share)
inline constexpr float kSnowLift = 0.3F;            // the layer lifted this far (units) where active: a trail's floor
                                                    // never fights the ground drawn under it for the depth

// A draw's snow descriptors (the world's draws with FragmentKey::snow).
struct SnowDescriptors {
    bool on = false;
    VkDescriptorBufferInfo block{};
    VkDescriptorImageInfo sky{};
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE; // (kTrailSnowTraced: the frame's TLAS at kCelSceneBinding)
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
    bool last_traced_air = false; // the last air pass's flakes traced
    uint32_t last_lamp_cells = 0; // the last air pass's lamps' grid (0: no light field)
    uint32_t last_tiles = 0;      // the last layer pass's tiles drawn
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

// The objects released (the device's shutdown; the caller holds the lock).
void release_snow(Backend& state);

} // namespace render::vulkan
