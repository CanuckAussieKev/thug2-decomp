#pragma once

#include <cstdint>
#include <span>

#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/ff_uniforms.h"

// NOT ORIGINAL - per-pixel lighting and surface maps on the Vulkan Gpu ([Graphics] rt_pbr, off by default;
// docs/design/vulkan.md "Per-pixel lighting and surface maps"). With it on:
//   - the surface maps made offline beside the texture dictionaries (formats/surface_map_format.h) are taken: this
//     installs engine/gfx/surface_maps.h's SurfaceMaps, render_pc attaches each map to its texture as the dictionary
//     loads, and a map is kept as a texture of its own (A8R8G8B8, its mips made here: normals averaged and
//     renormalised, the roughness widened where the normals spread - Toksvig) until its texture is let go of;
//   - a draw whose program ends with the vertex colour (ff_key.h kSurface*: kMaterial, kTextureVertexX4,
//     kTextureConstantAlpha) is lit per pixel when its first texture has a map (the world: the bake relit by the
//     map's normal toward the bake's sun and the sky; objects: their lights per pixel), or - without a map - when it
//     is lit by lights and has normals (objects: Direct3D 9's vertex lighting per pixel). ff_shaders.cpp writes the
//     shaders (its "per-pixel lighting" comment has the formulas); SurfaceUniforms (ff_uniforms.h) is their block.
// With it off nothing here runs: no maps are read and every key's surface bits stay 0 - the shaders are the same as
// without this file.
//
// Characters (added - [Graphics] rt_characters, RtPbrSettings::characters; docs/design/vulkan.md "Characters"): the
// character models' maps (tools/pbr_maps --models; surface_map_format kFlagModel) are used only with it on - off, such
// a draw is exactly as with no map. On, a model's map lit by the draw's lights gets its class's shading bits (ff_key.h
// kDetailSkin / kDetailCloth / kDetailHair, DetailUniforms::character) instead of the detail layer or relief.
//
// Hooks for the other work streams:
//   - the relit mode: ff_key.h kSurfaceRelit (reserved) - a key with it and kSurfaceLighting gets the per-pixel normal
//     and specular from here; its own light replaces the bake where ff_shaders.cpp's surface_lighting builds
//     surface_v0, and SurfaceUniforms::relit is its vector;
//   - tessellation / displacement: a map's alpha is the height (0.5: the surface), bound at kSurfaceMapBinding for a
//     key with kSurfaceMap, and in R3's material table as MaterialRecord::surface_texture (vulkan_rt_materials.h) with
//     surface_map_for() below for anything else that has the colour texture;
//   - ray-traced effects (reflections, GI): surface_map_for() and the material table's roughness / F0 / surface_texture.
namespace render {
struct GpuStateBlock;
} // namespace render

namespace render::vulkan {

struct Texture;
struct Backend;

// (RtPbrSettings, set_rt_pbr_settings: gpu_vulkan.h)

// A draw's per-pixel lighting: the key's surface bits, and the map to bind (kSurfaceMap).
struct SurfaceDraw {
    uint8_t flags = 0;
    Texture* map = nullptr;
    float f0 = kRtPbrF0;
    // (added by the detail materials - rt_detail / rt_delight, vulkan_detail.h) the key's detail bits, the kind's detail
    // texture (kDetailMaterial) and the block
    uint8_t detail = 0;
    Texture* detail_map = nullptr;
    DetailUniforms detail_uniforms{};
};
// What `block`'s draw with `key` gets (all 0 with rt_pbr off): call with the backend's lock held.
[[nodiscard]] SurfaceDraw surface_draw(const GpuStateBlock& block, const ShaderKey& key);
// The draw's SurfaceUniforms (`vertex` is its VertexUniforms: the lights in camera space; `frame` the frame's serial:
// the sun is looked up once a frame). The draw thread only.
void fill_surface_uniforms(const GpuStateBlock& block, const VertexUniforms& vertex, const SurfaceDraw& draw, uint64_t frame,
                           SurfaceUniforms* uniforms);
// `colour`'s map, if it has one (null otherwise; for the material table and other effects).
[[nodiscard]] Texture* surface_map_for(const Texture* colour);
// The F0 of `colour`'s map's class (0: no map) - or, with rt_detail, its surface kind's (vulkan_detail.h).
[[nodiscard]] float surface_map_f0(const Texture* colour);
// (added for the surface reflections - vulkan_rt_reflections.h) How glossy `colour`'s surface is by its map: its surface
// kind's (detail_material_format::Kind - the library's, else its class's) share of a mirror's reflection, 0 - 1 -
// metal, glazed tiles and polished floors, glass and water whole, polished stone half, wood and paint less; asphalt,
// concrete, brick, plaster, ground, plants and cloth none. 0 without a map, for a character
// model's map, or with rt_pbr off - its F0 and its kind's roughness (the detail library's, and how far it replaces the
// map's: 0 without the library) beside it. False: no map used.
struct SurfaceGloss {
    float gloss = 0.0F;
    float f0 = 0.0F;
    float kind_roughness = 0.0F;
    float kind_roughness_weight = 0.0F;
};
[[nodiscard]] bool surface_map_gloss(const Texture* colour, SurfaceGloss* out);
// The gloss of a surface kind / of a map's class (surface_map_gloss's tables).
[[nodiscard]] float kind_gloss(uint8_t kind);
[[nodiscard]] float class_gloss(uint8_t material_class);
// `colour` is being destroyed: its map taken out of the table, for the caller to release (null: none).
[[nodiscard]] Texture* take_surface_map(const Texture* colour);
// The maps attached since the device was made (the log; checks).
[[nodiscard]] uint64_t surface_maps_attached();

// --- (added by surface relief - [Graphics] rt_relief; docs/design/vulkan.md "Surface relief") ---------------------------
// Parallax occlusion mapping on a surface-mapped draw (ff_key.h kDetailRelief, DetailUniforms::relief): its depth is the
// map's own - the generator made its normals from its height as (-dh/du k, -dh/dv k, 1), so the height's full range spans
// k of the map's texels; k (the texture's bump: its class's, its level's terrains and texel size - tools/pbr_maps
// surfaces.ts) is read back from the map when it is attached (relief_depth_of_map) - x the surface kind's weight
// (relief_weight: brick and stone whole, none on smooth asphalt, concrete, metal, glass, water...) x rt_relief_strength.

// k of a map's texels (surface_map_format.h's: width x height x 4 bytes): the least-squares ratio of its normals' slopes
// to its height's Sobel gradients (wrapping, as the generator's); 0 for a flat map, at most 16.
[[nodiscard]] float relief_depth_of_map(std::span<const uint8_t> texels, uint32_t width, uint32_t height);
// The relief's weight for a surface kind (detail_material_format::Kind), 0 - 1.
[[nodiscard]] float relief_weight(uint8_t kind);

} // namespace render::vulkan
