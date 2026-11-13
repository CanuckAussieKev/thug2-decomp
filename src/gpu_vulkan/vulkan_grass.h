#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/gfx/grass.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"

// NOT ORIGINAL - grass (docs/design/vulkan.md "Grass"; [Graphics] grass - gpu_vulkan.h's GrassSettings): blades drawn
// over the level's grass ground. render_pc finds the grass in the collision and hands the field of blades over
// (engine/gfx/grass.h - this file's Sink); each viewport's pass runs at opaque_drawn (rt::ViewOrder::kGrass: after the
// opaque world, the objects and the sky and after the ray-traced shadows, occlusion, bounce and reflections have shaded
// the ground - a blade takes the ground's shaded colour at its root, so it is in the shadow its root is in; the
// screen-space effects aren't run over the blades themselves: a blade's depth is a few pixels wide, the normals they
// rebuild from it are noise and darkened every blade). Before the fog and the tone mapping (world_drawn), which take
// the blades with the rest. Independent of ray tracing; the blades are never in the acceleration structures.
//
// A pass, per viewport (its rectangle; split screen: each its own):
//   1. (the processor) the field's tiles near enough and in view chosen (plan_grass_draws): a tile at distance d (its
//      box's nearest point) draws its first blades - those whose rank is below grass_keep(d), the tile's blades being in
//      rank order -, with 3, 2 or 1 segments as it is near, middling or far; at most kGrassMostDrawn blades a view (the
//      share kept lowered for all when more);
//   2. the viewport's colour copied out (resolved first when multisampled): the ground as drawn - its texture, the
//      bake's light and shadow under it, the per-pixel lighting, the fog;
//   3. (gather, compute) each chosen blade's root put on the screen: where the depth there is the root's own (within
//      kGrassMatchShare of its distance + kGrassMatchSlack), the colour there is the blade's - kept in a buffer of the
//      blades' colours, so a root hidden this frame (under the skater) keeps the last colour seen; each sighting adds
//      kGrassLearn to how well the blade is known (a blade never seen isn't drawn: it grows in over a few frames);
//   4. (draw) the blades as triangle strips, one instance a blade, into the colour and depth (and the upscaler's motion,
//      when the view is upscaled), depth-tested; each blade shaped by grass_blade_shape (its twin on the processor):
//        - its height: the base (kGrassBaseHeight x grass_height) x its own (kGrassShortest - kGrassTallest) x the fade
//          (1 - smoothstep(kGrassFadeStart x distance, distance, d)) x how well it is known x its growth (0 - 1 as the
//          share kept passes its rank: (keep - rank) / kGrassGrowBand) - far blades sink into the ground they are the
//          colour of;
//        - its bend (the tip's move along the ground): its own lean (up to kGrassLean of the height, across its face),
//          the wind's sway (kGrassWind of the height, downwind, two slow waves over the ground), and each pusher within
//          its reach (and kGrassPushHeight of the root's height): kGrassPush x the height x smoothstep(1 - distance /
//          reach), straight away from it - at most kGrassMostBend of the height in all; the tip as high as the
//          length left lets it be (the blade keeps its length as it bends);
//        - its face: its own way turned kGrassFaceCamera of the way toward facing the camera (a blade seen edge-on
//          shows nothing); its width kGrassWidth at the root, never less than kGrassLeastPixels pixels, to nothing at
//          the tip, along a quadratic curve from the root, up, to the tip;
//        - its colour: the ground's under it x kGrassRootShade at the root to kGrassTipShade at the tip (the root's
//          contact darkening, the tip catching the light) x its own shade (within kGrassShadeSpread).
// Every step is NaN-free (the colours read clamped first).
namespace render::vulkan {

struct Backend;

inline constexpr float kGrassBaseHeight = 6.0F; // world units (inches) at grass_height 1
inline constexpr float kGrassShortest = 0.55F;  // a blade's own height: this - kGrassTallest of the base
inline constexpr float kGrassTallest = 1.45F;
inline constexpr float kGrassWidth = 0.45F; // at the root (world units)
inline constexpr float kGrassLeastPixels = 0.9F;
inline constexpr float kGrassNearShare = 0.2F; // every blade kept out to this share of the distance ...
inline constexpr float kGrassKeepPower = 1.5F; // ... then (near / d)^power of them
inline constexpr float kGrassGrowBand = 0.08F; // (a share of the ranks: a blade grows in over it)
inline constexpr float kGrassFadeStart = 0.7F; // the blades sink from this share of the distance to all of it
inline constexpr float kGrassLean = 0.35F;
inline constexpr float kGrassWind = 0.2F;
inline constexpr float kGrassPush = 1.4F;
inline constexpr float kGrassPushHeight = 72.0F;
inline constexpr float kGrassMostBend = 0.92F;
inline constexpr float kGrassFaceCamera = 0.6F;
inline constexpr float kGrassRootShade = 0.75F;
inline constexpr float kGrassTipShade = 1.12F;
inline constexpr float kGrassShadeSpread = 0.1F;
inline constexpr float kGrassMatchShare = 0.04F;
inline constexpr float kGrassMatchSlack = 8.0F;
inline constexpr float kGrassLearn = 0.34F;
inline constexpr float kGrassUnitsPerMetre = 39.37F;
inline constexpr uint32_t kGrassMostDrawn = 1500000;
inline constexpr std::array<float, 2> kGrassWindDirection{0.8F, 0.6F}; // (x, z)

// What a view's blades are shaped with (the settings and the view; also the reference's input).
struct GrassView {
    std::array<std::array<float, 4>, 4> view_projection{}; // D3D's row vectors: clip = (x, y, z, 1) x this
    std::array<float, 3> camera{};
    float distance = 0.0F;   // world units: where the blades have sunk
    float height = 0.0F;     // the base height x grass_height
    float pixel = 0.0F;      // world units a pixel spans at a distance of 1
    float keep_scale = 1.0F; // (lowered when a view has more than kGrassMostDrawn blades)
    float seconds = 0.0F;    // the wind's clock (the game's)
};
[[nodiscard]] GrassView grass_view(const GrassSettings& settings, const rt::ViewDrawn& view, uint32_t height_pixels);

// The share of the blades kept at distance d, and the height left there.
[[nodiscard]] float grass_keep(const GrassView& view, float distance);
[[nodiscard]] float grass_fade(const GrassView& view, float distance);

// A draw: `count` blades from `first` (one or more whole tiles' first blades), each with `segments`.
struct GrassDraw {
    uint32_t first = 0;
    uint32_t count = 0;
    uint32_t segments = 0;
};
// The view's draws (`view.keep_scale` lowered when the view has too many blades) and how many blades they draw.
[[nodiscard]] std::vector<GrassDraw> plan_grass_draws(const grass::Field& field, GrassView* view, uint32_t* blades);

// A blade as the vertex shader shapes it: the curve from the root through `control` to the tip, its height, its width
// at the root (the curve's middle line), the way across its face, its own shade (0 - 1).
struct GrassShape {
    std::array<float, 3> root{};
    std::array<float, 3> control{};
    std::array<float, 3> tip{};
    std::array<float, 3> across{};
    float height = 0.0F;
    float width = 0.0F;
    float shade = 0.0F;
};
[[nodiscard]] GrassShape grass_blade_shape(const grass::Blade& blade, const GrassView& view,
                                           std::span<const grass::Pusher> pushers, float known);

// The pass over the bound colour and depth targets for a view (the listener's work; also the check's). The caller holds
// the lock. False (nothing recorded): off, no field, no blade in view, the targets can't be used, the pass can't be made
// (logged once).
bool record_grass_pass(Backend& state, const rt::ViewDrawn& view);
[[nodiscard]] uint64_t grass_passes();
[[nodiscard]] uint32_t grass_last_blades(); // the last pass's blades drawn (before the shader's own fading)
[[nodiscard]] uint32_t grass_last_draws();

// The installed field's blades [first, first + count) shaped on the device for `view` as the last pass's pushers push
// them, each fully known (the check's comparison): x, y, z the tip, w the height. The caller holds the lock; the frame's
// work is finished. False when there is no field.
bool probe_grass(Backend& state, const rt::ViewDrawn& view, uint32_t first, uint32_t count,
                 std::vector<std::array<float, 4>>* tips);
// The blades' colours read back: r, g, b and how well each is known.
bool read_grass_colours(Backend& state, std::vector<std::array<float, 4>>* colours);

// The Sink render_pc hands the field and the pushers to (installed while grass=1; the check uses it directly).
grass::Sink& grass_sink();
// The pushers given last (the check's reference).
[[nodiscard]] std::vector<grass::Pusher> grass_pushers();

// The pass's objects released (the device's shutdown; the caller holds the lock).
void release_grass(Backend& state);

} // namespace render::vulkan
