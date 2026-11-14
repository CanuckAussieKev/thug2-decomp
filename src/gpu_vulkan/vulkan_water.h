#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - water shading on the Vulkan Gpu (docs/design/vulkan.md "Water"; [Graphics] rt_water - gpu_vulkan.h's
// WaterSettings). The game's water is flat textured sheets with scrolling coordinates, opaque or alpha-blended. With
// rt_water on, a draw is water when render_pc marked its material (engine/gfx/water_surfaces.h: a moving material
// whose first pass is the surface itself - render_pc/water_materials.cpp) and its program is one the per-pixel
// lighting can take (ff_key.h FragmentKey::water), it is drawn into the view's own rectangle, and it is opaque or blended
// by its alpha. Its fragment shader (water_shader.h) then works out, per pixel where the surface faces up (not a
// waterfall) and its texture isn't warm (not lava):
//   1. waves: a normal from four Gerstner waves about the wind (the level's swell: LevelWater - a harbour's lower and
//      shorter than the sea's - x rt_water_waves; each fading where a pixel spans a fifth of its length) and two layers
//      of a tiling ripple texture (made on the device at the first water draw: 256 x 256, 64 waves with whole cycles
//      across the tile, mostly along the wind - band-limited per mip level, so far water flattens instead of
//      shimmering) drifting with the wind (rt_water_wind) - all moving with the game's clock (pauses with the game; a
//      replay draws the same);
//   2. a reflection along the wave normal, weighted by Fresnel (Schlick, water's F0 0.02): a ray marched over the
//      depth behind the water in camera space (22 growing steps, the crossing halved 5 times; none where it leaves the
//      view or passes behind something), else the sky - as the screen shows it toward that direction (where nothing
//      was drawn), else the level's own sky captured (rt::sky_capture: the backdrop drawn into 3 x 2 faces once per sky
//      scene - asked for while rt_water is on), else the fog's colour;
//   3. refraction: what was drawn before the view's first water draw - its colour and its depth made linear, copied
//      once per view at that draw (multisampled targets resolved; the pass ended and opened again) - read at the pixel
//      moved by the wave normal's tilt in proportion to the water behind (none at the shore; not moved onto something in
//      front of the water). Opaque water (drawn among the opaque world, often first) is kept back and drawn once the
//      opaque world is finished (defer_water_draw), so its copy has the whole opaque world - its bed, and what it
//      reflects - without the water itself (the ray-traced effects at rt::opaque_view_drawn see its bed, not the water);
//   4. colour by depth: the light's path through the water (from the surface to what is behind, along the view) and
//      down to it absorbed per channel, exp(-sigma d) with sigma = 3 / visibility x (0.2 + 1.6 (1 - hue)) - the tint's
//      hue (rt_water_tint, auto: the game's colour there without the texture's detail) taking red first: shallow water
//      shows its bed nearly clear, deep water the scattered colour (the hue at the brightness the game gave the
//      surface - the bake, the pass colours) - and visibility the level's x rt_water_clarity; the texture's own detail
//      kept faintly (its texel over its mean, 35%);
//   5. foam where the water is shallow (within 24 units of its bed), in the ripples' drifting pattern, x rt_water_foam;
//   6. the sun's glitter: normalised Blinn-Phong (power 1500) of the scene's sun (rt::scene_sun, or the sun the game
//      shows) on the wave normals, Fresnel at the half vector, where the bake says the surface is sunlit; up to 6 with
//      rt_hdr (the bloom picks it up), 1 without.
// The moving surface ([Graphics] rt_water_geometry, docs/design/vulkan.md "Moving water"): render_pc splits the water's
// flat meshes into a grid as a level loads (render_pc/water_geometry.h) and says which draws are of a split mesh
// (water::current_waves); those draws' keys are FragmentKey::water kWaterMoving - the vertex shader moves each point by
// the same four Gerstner waves (height and the crests' sideways move, faded with the pixel's footprint, the grid's
// spacing and toward the shore by the copy's depth below it) and the fragment shader shades the rest point. Calm water
// (the swell's height 0) is drawn flat.
// Where the game drew any water the pixel's alpha becomes 1 (what is behind is in the colour already); where it drew
// none it stays 0. Draws that aren't water, and every draw with rt_water off, are as before: the keys have no water bit
// and nothing is pushed. With ray tracing the existing reflection pass leaves water materials to this (render_pc
// gives them no reflectivity while rt_water is on).
namespace render {
struct GpuStateBlock;
} // namespace render

namespace render::vulkan {

struct Backend;
struct DrawCall;

inline constexpr uint32_t kWaterWavesSize = 256;   // the ripple texture's side
inline constexpr uint32_t kWaterWavesLevels = 9;   // 256 down to 1
inline constexpr uint32_t kWaterWaveCount = 64;    // the waves summed into it
inline constexpr float kWaterDetail = 0.35F;       // the texture's detail kept
inline constexpr float kWaterRefraction = 0.12F;   // the refraction's shift per unit of tilt and water behind
inline constexpr float kWaterGlitterHdr = 6.0F;    // the glitter's peak with rt_hdr
inline constexpr float kWaterGlitterSdr = 1.0F;    // ... without

// A water draw's descriptors (pushed with the stages' at kWaterUniformsBinding - kWaterWavesBinding).
struct WaterDescriptors {
    bool on = false;
    VkDescriptorBufferInfo block{};
    VkDescriptorImageInfo scene{};
    VkDescriptorImageInfo depth{};
    VkDescriptorImageInfo sky{};
    VkDescriptorImageInfo waves{};
};

// Whether `block`'s draw can be water before its key is looked at (rt_water on, render_pc's current material water
// with this texture on stage 0 - or the draw replayed below -, a 3D draw opaque or blended by its alpha into the view's
// rectangle). The caller holds the lock.
[[nodiscard]] bool water_candidate(const Backend& state, const GpuStateBlock& block);
// An opaque water draw before the view's opaque world is finished (the sea drawn among the level's first meshes - what
// is behind it and what it reflects not drawn yet): kept, not drawn, and drawn as water at rt::opaque_view_drawn (its
// state and buffers live for the frame) over the copy of the whole opaque world without it. True when kept.
bool defer_water_draw(Backend& state, const GpuStateBlock& block, const DrawCall& call);
// A draw's key with the water bit and its descriptors where it is water (`water` left off otherwise: the key as it
// was); false when the draw is kept back (defer_water_draw - the caller draws nothing now).
bool water_key(Backend& state, const GpuStateBlock& block, const DrawCall& call, ShaderKey* shaders, WaterDescriptors* water);
// A water draw made ready: the copy behind the water made (the view's first water draw: the pass ended), the ripples made
// (the first ever), the view's block in the ring; its descriptors. False: drawn as before (logged once why).
bool prepare_water_draw(Backend& state, const GpuStateBlock& block, WaterDescriptors* out);

// The view's block as the shader reads it, from the settings, the level's water and a view (also the check's).
struct WaterView {
    rt::ViewDrawn view;
    Extent target;          // the colour target's size
    bool copied = false;    // the copy behind the water is there
    bool sky_bound = false; // the level's sky capture is bound
};
[[nodiscard]] WaterUniforms water_uniforms(const WaterSettings& settings, const WaterView& view);

// The ripple texture's level `level` on the processor (the check's reference): kWaterWavesSize >> level texels a side,
// four floats each (slope along x, slope along z, height 0 - 1, the foam's pattern 0 - 1).
[[nodiscard]] std::vector<std::array<float, 4>> water_waves_reference(uint32_t level);

// Counts for the log and the checks.
struct WaterStats {
    uint64_t draws = 0;  // water draws shaded
    uint64_t copies = 0; // views whose colour and depth were copied
    uint64_t refused = 0; // candidates drawn as before (no view, a target that can't be read...)
    uint64_t replayed = 0; // opaque water draws drawn again as water after the opaque world
    uint64_t moving = 0;   // water draws whose surface the swell moved (rt_water_geometry)
};
[[nodiscard]] WaterStats water_stats();

// For the check: the copy's linear depth and the ripple texture's level read back (the frame's work finished first; the
// caller holds the lock). False: none made.
bool read_water_depth(Backend& state, std::vector<float>* depth, Extent* size);
bool read_water_waves(Backend& state, uint32_t level, std::vector<uint16_t>* halves, Extent* size);

// The objects released (the device's shutdown; the caller holds the lock).
void release_water(Backend& state);

} // namespace render::vulkan
