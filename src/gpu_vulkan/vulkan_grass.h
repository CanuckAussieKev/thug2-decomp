#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/gfx/grass.h"
#include "engine/gfx/rt_light_field.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/wind.h"
#include "gpu_vulkan/gpu_vulkan.h"

// NOT ORIGINAL - grass (docs/design/vulkan.md "Grass"; [Graphics] grass - gpu_vulkan.h's GrassSettings): blades drawn
// over the level's grass ground. render_pc finds the grass in the collision and hands the field of blades over
// (engine/gfx/grass.h - this file's Sink); each viewport's pass runs at opaque_drawn (rt::ViewOrder::kGrass: after the
// opaque world, the objects and the sky and after the ray-traced shadows, occlusion, bounce and reflections; the
// screen-space effects aren't run over the blades themselves: a blade's depth is a few pixels wide, the normals they
// rebuild from it are noise). Before the fog and the tone mapping (world_drawn), which take the blades with the rest.
// The blades are never in the acceleration structures.
//
// (Round W2) A blade is lit by the one light, as a thin leaf of its material (engine/gfx/foliage_light.h): the light
// store round it (vulkan_light_window.h - Round O's probes: the sky's, the bounce's and the lamps' light), the sun's or
// the moon's light with its own traced visibility (a ray toward it from the blade's middle - ray tracing on; else seen),
// the canopy's shade by the blades above (Beer's law over the drawn blades' own leaf area), the material table's leaf
// (rt_material_table.h: a living leaf's albedo and transmittance - a dried blade the dried leaf's), reflected through
// rt_brdf.h on the face toward the eye and transmitted from the other. Nothing of the frame's colour is read.
//
// A pass, per viewport (its rectangle; split screen: each its own):
//   1. (the processor) the field's tiles near enough and in view chosen (plan_grass_draws): a tile at distance d (its
//      box's nearest point) draws its first blades - those whose rank is below grass_keep(d), the tile's blades being in
//      rank order -, with 3, 2 or 1 segments as it is near, middling or far; at most kGrassMostDrawn blades a view (the
//      share kept lowered for all when more);
//   2. (light, compute) each chosen blade shaped as the draw will shape it, its sun ray traced, its light at its root
//      and its tip worked out (grass_blade_light is the twin) - kept in a buffer of the blades' light;
//   3. (draw) the blades as triangle strips, one instance a blade, into the colour and depth (and the upscaler's motion,
//      when the view is upscaled), depth-tested; each blade shaped by grass_blade_shape (its twin on the processor):
//        - its height: the base (kGrassBaseHeight x grass_height) x its own (kGrassShortest - kGrassTallest) x the fade
//          (1 - smoothstep(kGrassFadeStart x distance, distance, d)) x its growth (0 - 1 as the share kept passes its
//          rank: (keep - rank) / kGrassGrowBand) - far blades sink into the ground;
//        - its bend (the tip's move along the ground): its own lean (up to kGrassLean of the height, across its face),
//          the wind's bend (round W: a blade a cantilever bent by the wind's drag - grass_wind_bend - in the wind at its
//          tip: the one wind's near the ground x the level's air there (vulkan_air.h) x its gusts, carried downwind
//          at the wind's speed), and each pusher within
//          its reach (and kGrassPushHeight of the root's height): kGrassPush x the height x smoothstep(1 - distance /
//          reach), straight away from it - at most kGrassMostBend of the height in all; the tip as high as the
//          length left lets it be (the blade keeps its length as it bends);
//        - its face: its own way turned kGrassFaceCamera of the way toward facing the camera (a blade seen edge-on
//          shows nothing); its width kGrassWidth at the root, never less than kGrassLeastPixels pixels, to nothing at
//          the tip, along a quadratic curve from the root, up, to the tip;
//        - its colour: its light at the root to its light at the tip along it.
// Every step is NaN-free.
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
// (round W) A blade's bend in the wind: a cantilever of length L under the wind's drag, q = 1/2 rho U^2 C_d w per unit of
// length, bends its tip d = q L^4 / (8 E I) - a share d / L = kGrassBendCoefficient U^2 L^3 (U m/s, L m), taken as
// d / L / sqrt(1 + (d / L)^2) (the large deflection's saturation). A leaf blade 4 mm wide and 0.3 mm thick (I = w t^3 /
// 12) of leaf tissue E 0.3 GPa (grass leaves' Young's modulus, Vincent 1982, J. Materials Science 17, "The mechanical
// design of grass"), drag C_d 1.2 (a long strip, partly turned with the flow), air 1.2 kg/m^3. U is the wind at the
// blade's tip in its own surface layer: the wind near the ground (wind.h kNearGround) brought down by the log profile
// over the grass's own roughness, z0 = 0.1 x its height (Brutsaert 1982, Evaporation into the Atmosphere: a crop's
// z0 about 0.1 - 0.13 of its height).
inline constexpr float kGrassLeafWidth = 0.004F;      // metres
inline constexpr float kGrassLeafThickness = 3.0e-4F; // metres
inline constexpr float kGrassLeafModulus = 3.0e8F;    // pascals
inline constexpr float kGrassLeafDrag = 1.2F;
inline constexpr float kAirDensity = 1.2F; // kg/m^3
inline constexpr float kGrassBendCoefficient =
    (0.5F * kAirDensity * kGrassLeafDrag * kGrassLeafWidth) /
    (8.0F * kGrassLeafModulus * (kGrassLeafWidth * kGrassLeafThickness * kGrassLeafThickness * kGrassLeafThickness / 12.0F));
inline constexpr float kGrassRoughnessShare = 0.1F;
// The bend's share of a blade `height_m` tall in the wind `near_ground` m/s (at wind.h kNearGround, its gust in, x the
// air's share there) - the twin of the shader's grass_wind_bend.
[[nodiscard]] float grass_wind_bend(float height_m, float near_ground);
// Its GLSL twin, `float grass_wind_bend(float height_m, float near_ground)` - also the clutter's weeds' and leaves'.
[[nodiscard]] std::string grass_bend_glsl();
inline constexpr float kGrassPush = 1.4F;
inline constexpr float kGrassPushHeight = 72.0F;
inline constexpr float kGrassMostBend = 0.92F;
inline constexpr float kGrassFaceCamera = 0.6F;
// (round W2) a blade's albedo and transmittance its kind's x (1 +- this) by its own hash: leaves of one sward differ
// (Gates et al. 1965's samples spread about 10% about their mean at the green peak)
inline constexpr float kGrassShadeSpread = 0.1F;
inline constexpr float kGrassUnitsPerMetre = 39.37F;
inline constexpr uint32_t kGrassMostDrawn = 1500000;

// What a view's blades are shaped with (the settings and the view; also the reference's input).
struct GrassView {
    std::array<std::array<float, 4>, 4> view_projection{}; // D3D's row vectors: clip = (x, y, z, 1) x this
    std::array<float, 3> camera{};
    float distance = 0.0F;   // world units: where the blades have sunk
    float height = 0.0F;     // the base height x grass_height
    float pixel = 0.0F;      // world units a pixel spans at a distance of 1
    float keep_scale = 1.0F; // (lowered when a view has more than kGrassMostDrawn blades)
    float seconds = 0.0F;    // the wind's clock (the game's)
    float wind = 0.0F;       // (round W) the wind near the ground in the open now (m/s at wind.h kNearGround, no gust)
    float leaf_area = 0.0F;  // (round W2) the drawn blades' leaf area a ground area (their count x a blade's area)
};
[[nodiscard]] GrassView grass_view(const GrassSettings& settings, const rt::ViewDrawn& view, uint32_t height_pixels);

// The share of the blades kept at distance d, and the height left there.
[[nodiscard]] float grass_keep(const GrassView& view, float distance);
[[nodiscard]] float grass_fade(const GrassView& view, float distance);

// (round W2) The leaf area the field's blades make a ground area at `density` and `height` (GrassSettings').
[[nodiscard]] float grass_leaf_area(float density, float height);

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

// (round W2) A blade's light as the light pass works it out (its twin): the radiance it sends toward the camera at its
// root and at its tip (linear, the frame's unit) for the light store's probe at its middle, the direct light (toward,
// per unit of N.L) and its visibility `seen`.
struct GrassLight {
    rt::ProbeL1 probe;
    std::array<float, 3> toward{0.0F, 1.0F, 0.0F};
    std::array<float, 3> sun{};
    float seen = 1.0F;
};
struct GrassLit {
    std::array<float, 3> root{};
    std::array<float, 3> tip{};
};
[[nodiscard]] GrassLit grass_blade_light(const grass::Blade& blade, const GrassShape& shape, const GrassView& view,
                                         const GrassLight& light);

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
// (round W2) The blades' light read back as the last light pass left it: each blade's root and tip radiance and its sun
// visibility.
struct GrassLightRead {
    std::array<float, 3> root{};
    std::array<float, 3> tip{};
    float seen = 0.0F;
};
bool read_grass_colours(Backend& state, std::vector<GrassLightRead>* colours);

// The Sink render_pc hands the field and the pushers to (installed while grass=1; the check uses it directly).
grass::Sink& grass_sink();
// The pushers given last (the check's reference).
[[nodiscard]] std::vector<grass::Pusher> grass_pushers();

// The pass's objects released (the device's shutdown; the caller holds the lock).
void release_grass(Backend& state);

} // namespace render::vulkan
