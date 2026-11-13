#pragma once

#include <cstdint>

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
    float f0 = 0.04F;
};
// What `block`'s draw with `key` gets (all 0 with rt_pbr off): call with the backend's lock held.
[[nodiscard]] SurfaceDraw surface_draw(const GpuStateBlock& block, const ShaderKey& key);
// The draw's SurfaceUniforms (`vertex` is its VertexUniforms: the lights in camera space).
void fill_surface_uniforms(const GpuStateBlock& block, const VertexUniforms& vertex, const SurfaceDraw& draw,
                           SurfaceUniforms* uniforms);
// `colour`'s map, if it has one (null otherwise; for the material table and other effects).
[[nodiscard]] Texture* surface_map_for(const Texture* colour);
// `colour` is being destroyed: its map taken out of the table, for the caller to release (null: none).
[[nodiscard]] Texture* take_surface_map(const Texture* colour);
// The maps attached since the device was made (the log; checks).
[[nodiscard]] uint64_t surface_maps_attached();

} // namespace render::vulkan
