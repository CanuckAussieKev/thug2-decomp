#pragma once

#include <cstdint>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - (round PT-B) How every ray meets a surface: one classification of each drawn mesh, from its material's
// first pass as the game draws it (its blend mode, its texels' alpha, its fixed alpha) and the mesh's vertex alpha -
// never from what the object is. render_pc/rt_record.cpp gives each instance its flags from it (add_instance), so the
// acceleration structure's masks (gpu_vulkan/vulkan_rt_scene.cpp instance_of), the per-pixel passes' surfaces
// (rt_relit_world's lit_per_pixel), the relit bake's rays and the path tracer all take the same class; the light a ray
// takes through a seen-through surface is the one function every light ray calls (vulkan_rt_scene.h ray_light).
// docs/design/vulkan.md "Blended surfaces in every ray".
namespace render::rt {

enum class SurfaceClass : uint8_t {
    kOpaque,        // not blended, no alpha test: any hit ends a light ray
    kCutOut,        // its alpha test cuts holes: a hit where its drawn alpha passes the test (the blended cut-outs too)
    kBlendedOpaque, // drawn blended, but its drawn alpha is 1 everywhere - nothing behind it ever shows: opaque to every
                    // ray, lit per pixel as any opaque surface (Boston's brick facades, road markings)
    kSeenThrough,   // blended with real alpha, glass, a multiply: each ray through it takes hit_blend_transmittance's
                    // share of the light (its covered part blocks, glass reflects some, a filter tints)
    kAddsLight,     // adds to or takes from what is behind (blend modes 1 - 4, 9, 10: glows, light cards, grime): no
                    // surface for a light ray - light passes it whole; a path's ray meets it for its emission
};

// The class of a mesh drawn with `material` (render_pc's material_of: its flags kTransparent, kAlphaTested,
// kTextureAlpha; its first pass's blend mode and fixed alpha; its texels' least alpha) whose least vertex alpha is
// `least_vertex_alpha` (0 - 1, the colour word's alpha / 255; 1 for a mesh without vertex colours). The rule:
//   - not blended: kCutOut where it is alpha tested, else kOpaque;
//   - blend modes 1 - 4, 9, 10 (added): kAddsLight; 7, 8, 15 (a multiply): kSeenThrough;
//   - otherwise its least drawn alpha (the coverage hit_blend gives it, vulkan_rt_materials.h material_drawn_alpha)
//     decides - blend 5 the texel's alpha, x the vertex alpha doubled (the game's pixel shader: 2 x vertex x texel, held
//     to 1) unless the pass takes the texel's alone (kTextureAlpha); blend 6 the fixed alpha; the others 1: at least 1
//     at its least (min(1, 2 x least vertex alpha x least texel alpha) for blend 5) is kBlendedOpaque - nothing behind
//     it ever shows; else a blended cut-out (kAlphaTested) is kCutOut and the rest kSeenThrough.
// The least drawn alpha of blend 5 (1 for any other blend).
float least_drawn_alpha(const Material& material, float least_vertex_alpha);
SurfaceClass surface_class(const Material& material, float least_vertex_alpha);
// Whether the vertex alpha takes part in the material's drawn alpha (blend 5 without kTextureAlpha): a mesh drawn with
// it needs its least vertex alpha read; any other's is never asked.
bool vertex_alpha_counts(const Material& material);

// The instance flags (rt_scene.h InstanceFlag: kOpaque / kAlphaTested / kTransparent / kAddsLight, with kCastsShadow)
// an instance of the class takes - its kind (kWorld, kObject) and kSkinned are the caller's. A blended cut-out (its
// material kTransparent) keeps kTransparent beside kAlphaTested (a path's ray takes its coverage: hit_blend).
uint32_t surface_instance_flags(SurfaceClass surface, const Material& material);

// Its name for the logs ("opaque", "cut-out", "blended opaque", "seen through", "adds light").
const char* surface_class_name(SurfaceClass surface);

} // namespace render::rt
