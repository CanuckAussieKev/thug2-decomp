#pragma once

#include <array>
#include <cstdint>
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
// mapping): the decals' triangles of the tiles within the decal distance and in view, drawn into the colour alone (no
// depth attachment): each fragment reads the depth under it and is kept only where the scene's surface there is the
// decal's own (within kDecalMatchShare of its distance + kDecalMatchSlack - hidden decals and decals over nothing are
// dropped, so a mark never shows on the skater standing on it or through a wall); its colour multiplies the pixel's -
// mix(1, the atlas texel's rgb, its alpha x the decal's opacity x its fade) - so a stain darkens the lit ground as the
// ground's own colour would: in shadow, in the light, under a lamp. The fade: 1 to kDecalFadeStart of the decal's own
// distance (its size's), then down to 0 at it (and at the decal distance, kDecalDistance). While the ray-traced
// reflections run (and the target is single-sampled), the pass also draws a wet mask - each wet patch's darkening as its
// wetness, the most of any - which the reflections' trace reads: there the ground reflects as water does (kReflectWater:
// 2% face-on, a mirror at grazing angles), the puddle's share of it.
//
// The clutter's pass, per viewport, at opaque_drawn (rt::ViewOrder::kClutter, after the decals and reflections, before
// the grass): like the grass's (vulkan_grass.h) - the viewport's colour copied out; a compute pass puts each chosen
// item's root on the screen and, where the depth there is the root's own, keeps the colour there (the ground's light
// under it) in a buffer, so a root hidden this frame keeps the last seen; then the items drawn as instanced meshes
// (kMeshVertices each, the kind's - leaf, paper, can, weed, pebble), depth-tested and written, into the colour, depth
// (and the upscaler's motion). An item's colour: pebbles the ground's own colour (+-20%); the rest their own (a leaf's
// autumn colours, a weed's green, paper, a can's label) x the ground's light (its luminance over kClutterGroundAlbedo) x
// the mesh's shading. Leaves and weeds sway in the wind (their height's share). The level of detail: every item out to
// kClutterNearShare of the distance, then (near / d)^kClutterKeepPower of them (by rank); each shrinks into the ground
// from kClutterFadeStart of the distance to all of it.
namespace render::vulkan {

struct Backend;

inline constexpr float kDecalDistance = 3200.0F;   // world units (about 80 m): no decal beyond
inline constexpr float kDecalFadeStart = 0.6F;     // (of a decal's own distance: it fades from here)
inline constexpr float kDecalMatchShare = 0.003F;  // the depth under a decal fragment is its own within this share ...
inline constexpr float kDecalMatchSlack = 1.0F;    // ... + this (world units: a wall standing in a decal takes at most
                                                   // its foot's width of this, along the view, of it)
inline constexpr float kClutterDistance = 1600.0F; // world units (about 40 m): no item beyond
inline constexpr float kClutterNearShare = 0.25F;
inline constexpr float kClutterKeepPower = 1.5F;
inline constexpr float kClutterFadeStart = 0.7F;
inline constexpr float kClutterGroundAlbedo = 0.4F;
inline constexpr float kClutterLearn = 0.34F;
inline constexpr float kClutterMatchShare = 0.04F;
inline constexpr float kClutterMatchSlack = 8.0F;
inline constexpr uint32_t kClutterMostDrawn = 400000;

// What a view's decals and items are drawn with.
struct ScatterView {
    std::array<std::array<float, 4>, 4> view_projection{}; // D3D's row vectors: clip = (x, y, z, 1) x this
    std::array<float, 3> camera{};
    float keep_scale = 1.0F; // (lowered when a view has more than kClutterMostDrawn items)
    float seconds = 0.0F;    // the wind's clock
};
[[nodiscard]] ScatterView scatter_view(const rt::ViewDrawn& view);

// A draw: `count` decal vertices / items from `first`.
struct ScatterDraw {
    uint32_t first = 0;
    uint32_t count = 0;
};
// The view's decal draws (the tiles nearer than kDecalDistance and in view) and their vertex count.
[[nodiscard]] std::vector<ScatterDraw> plan_decal_draws(const scatter::Scatter& made, const ScatterView& view,
                                                        uint32_t* vertices);
// The view's clutter draws (`view.keep_scale` lowered when too many) and their item count.
[[nodiscard]] std::vector<ScatterDraw> plan_clutter_draws(const scatter::Scatter& made, ScatterView* view,
                                                          uint32_t* items);
// The share of the items kept at a distance, and the size left there.
[[nodiscard]] float clutter_keep(const ScatterView& view, float distance);
[[nodiscard]] float clutter_fade(float distance);
// A decal vertex's fade seen from `camera` (the vertex shader's twin: its own distance - its packed byte x
// kDecalFadeUnits, at most kDecalDistance - and kDecalFadeStart of it).
[[nodiscard]] float decal_fade(const scatter::DecalVertex& vertex, const std::array<float, 3>& camera);

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
