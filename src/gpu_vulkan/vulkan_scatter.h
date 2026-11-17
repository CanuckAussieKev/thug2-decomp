#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/gfx/rt_scene.h"
#include "engine/gfx/scatter.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_memory.h"

// NOT ORIGINAL - decals and clutter (docs/design/vulkan.md "Decals and clutter"; [Graphics] rt_decals, clutter -
// gpu_vulkan.h's ScatterSettings): marks and small props over the level's surfaces. render_pc reads the collision and
// the rails and hands the decals and items over (engine/gfx/scatter.h - this file's Sink). Independent of ray tracing;
// neither is ever in the acceleration structures, neither has collision.
//
// The decals' pass, per viewport, at opaque_drawn (rt::ViewOrder::kDecals: after the opaque world, the objects, the
// sky, the ray-traced shadows, occlusion, bounce and lamps; before the reflections, the grass, the fog and the tone
// mapping): the decals' triangles of the tiles in view, drawn into the colour alone (no
// depth attachment): each fragment reads the depth under it and is kept only where the scene's surface there is the
// decal's own (within kDecalMatchShare of its distance + kDecalMatchSlack - hidden decals and decals over nothing are
// dropped, so a mark never shows on the skater standing on it or through a wall); its colour multiplies the pixel's -
// mix(1, the atlas texel's rgb, its alpha x the decal's opacity x its fade) - so a stain darkens the lit ground as the
// ground's own colour would: in shadow, in the light, under a lamp. No distance fade or cut-off: a decal is drawn
// wherever it is seen, as any texture is - far away its atlas's mips resolve it (each mip the mean of the mark's
// darkening over its texels: alpha-weighted, so a far crack is the faint mean darkening of its patch, not a line that
// shimmers), sampled within its own cell at the mip it is read from, anisotropically as the game's textures are
// ([Graphics] anisotropy). While the ray-traced
// reflections run (and the target is single-sampled), the pass also draws a wet mask - each wet patch's darkening as its
// wetness, the most of any - which the reflections' trace reads: there the ground reflects as water does (kReflectWater:
// 2% face-on, a mirror at grazing angles), the puddle's share of it.
//
// The clutter's pass, per viewport, at opaque_drawn (rt::ViewOrder::kClutter, after the decals and reflections, before
// the grass): (round W2) a compute pass traces each chosen item's ray toward the sun or the moon (ray tracing on; else it
// is seen) and keeps its visibility; then the items drawn as instanced meshes (kMeshVertices each, the kind's - leaf,
// paper, can, weed, pebble), depth-tested and written, into the colour, depth (and the upscaler's motion). An item is lit
// by the one light (engine/gfx/foliage_light.h: the light store's probe at it - vulkan_light_window.h -, the sun with its
// traced visibility, rt_brdf.h's BRDF) with its own material from the table (rt_material_table.h: a fallen leaf a dried
// leaf, a weed a living one - both passing light through -, paper, a can's bare metal and its painted label, a pebble the
// gravel's albedo); its colour (a leaf's autumn colours, a weed's green, paper, a can's label) its albedo. Nothing of the
// frame is read. Leaves and weeds bend in the wind (round W: as the grass's
// blades - vulkan_grass.h grass_wind_bend - in the one wind x the level's air at them, vulkan_air.h). The level of detail: every item out to
// kClutterNearShare of the distance, then (near / d)^kClutterKeepPower of them (by rank); each shrinks into the ground
// from kClutterFadeStart of the distance to all of it.
namespace render::vulkan {

struct Backend;

inline constexpr float kDecalMatchShare = 0.003F;  // the depth under a decal fragment is its own within this share ...
inline constexpr float kDecalMatchSlack = 1.0F;    // ... + this (world units: a wall standing in a decal takes at most
                                                   // its foot's width of this, along the view, of it)
inline constexpr float kClutterDistance = 1600.0F; // world units (about 40 m): no item beyond
inline constexpr float kClutterNearShare = 0.25F;
inline constexpr float kClutterKeepPower = 1.5F;
inline constexpr float kClutterFadeStart = 0.7F;
inline constexpr uint32_t kClutterMostDrawn = 400000;

// What a view's decals and items are drawn with.
struct ScatterView {
    std::array<std::array<float, 4>, 4> view_projection{}; // D3D's row vectors: clip = (x, y, z, 1) x this
    std::array<float, 3> camera{};
    float keep_scale = 1.0F; // (lowered when a view has more than kClutterMostDrawn items)
    float seconds = 0.0F;    // the wind's clock
    float wind = 0.0F;       // (round W) the wind near the ground in the open now (m/s, wind.h kNearGround)
};
[[nodiscard]] ScatterView scatter_view(const rt::ViewDrawn& view);

// A draw: `count` decal vertices / items from `first`.
struct ScatterDraw {
    uint32_t first = 0;
    uint32_t count = 0;
};
// The view's decal draws (the tiles in view - the view's frustum, nothing else) and their vertex count.
[[nodiscard]] std::vector<ScatterDraw> plan_decal_draws(const scatter::Scatter& made, const ScatterView& view,
                                                        uint32_t* vertices);
// The view's clutter draws (`view.keep_scale` lowered when too many) and their item count.
[[nodiscard]] std::vector<ScatterDraw> plan_clutter_draws(const scatter::Scatter& made, ScatterView* view,
                                                          uint32_t* items);
// The share of the items kept at a distance, and the size left there.
[[nodiscard]] float clutter_keep(const ScatterView& view, float distance);
[[nodiscard]] float clutter_fade(float distance);
// A decal vertex's weight (the vertex shader's twin, without the snow and the rain's reach): its opacity - the same at
// every distance.
[[nodiscard]] float decal_weight(const scatter::DecalVertex& vertex);
// The decals' atlas's mip `level` (1 the first below it) from its first level (RGBA8 rows, `width` x `height`): each
// texel the alpha-weighted mean of the 2^level square under it (rgb the mean of rgb x alpha over the mean alpha; alpha
// its mean), made from the first level so the rounding is a level's own - the mark the pass multiplies in
// (mix(1, rgb, alpha)) averaged as the light it takes away. A cell's texels never mix with another's (cells are
// kDecalTile, a power of two).
[[nodiscard]] std::vector<uint8_t> decal_atlas_mip(std::span<const uint8_t> texels, uint32_t width, uint32_t height,
                                                   uint32_t level);

// The passes over the bound colour and depth targets for a view (the listeners' work; also the check's). The caller
// holds the lock. False (nothing recorded): off, nothing to draw, the targets can't be used, the pass can't be made
// (logged once).
bool record_decal_pass(Backend& state, const rt::ViewDrawn& view);
bool record_clutter_pass(Backend& state, const rt::ViewDrawn& view);
[[nodiscard]] uint64_t decal_passes();
[[nodiscard]] uint64_t clutter_passes();
[[nodiscard]] uint32_t decal_last_vertices();
[[nodiscard]] uint32_t clutter_last_items();

// (the puddles) The wet mask the decals' pass drew this frame for the viewport `rect` - R8, the puddles' share 0 - 1 at
// each pixel, in the colour target's pixels, SHADER_READ_ONLY_OPTIMAL - or null: none drawn (no wet patch in view, the
// reflections off, a multisampled target). The reflections' trace reads it (vulkan_rt_reflections.h): a puddle reflects
// as water. The caller holds the lock.
[[nodiscard]] const Image* scatter_wet_mask(const Backend& state, const std::array<uint32_t, 4>& rect);
[[nodiscard]] uint64_t wet_masks_drawn();
// (the check's) The wet mask read back (R8, the colour target's size; the frame's work finished first). False: none.
bool read_wet_mask(Backend& state, std::vector<uint8_t>* texels, uint32_t* width);

// The Sink render_pc hands the decals and clutter to (installed while either is on; the check uses it directly).
scatter::Sink& scatter_sink();

// The pass's objects released (the device's shutdown; the caller holds the lock).
void release_scatter(Backend& state);

} // namespace render::vulkan
