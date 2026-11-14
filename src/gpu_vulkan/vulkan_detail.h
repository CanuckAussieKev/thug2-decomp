#pragma once

#include <cstdint>
#include <span>

#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/gpu_vulkan.h"

// NOT ORIGINAL - detail materials and de-lighting on the Vulkan Gpu ([Graphics] rt_detail, rt_delight; off by default;
// docs/design/vulkan.md "Detail materials and de-lighting"). Both act on the draws the per-pixel lighting (rt_pbr,
// vulkan_surface.h) lights with a surface map:
//   - rt_detail: each surface-mapped texture's surface kind (asphalt, smooth or rough concrete, brick, stone, metal,
//     wood, dirt, grass, tiles, plaster, gravel) from the detail-material library (formats/detail_material_format.h:
//     thug2-decomp-materials.t2dm in the game folder, made by tools/detail_materials from CC0 scans and the levels'
//     collision terrain), else from its surface map's class. The kind gives a tiling high-resolution detail layer - the
//     albedo's luminance modulated, its normal and roughness blended into the map's, fading with its texel density so
//     far away the original look is untouched - and a real material: roughness, F0 (the per-pixel highlight's and the
//     ray-traced passes' through surface_map_f0), metalness (the highlight tinted by the texel);
//   - rt_delight: the light painted into the colour textures (shadows, occlusion, highlights) flattened toward the
//     texture's mean luminance by its low-frequency luminance, in the shader - the player's files are not changed.
// ff_shaders.cpp writes the shaders (its "detail layer" comment has the formulas); DetailUniforms (ff_uniforms.h) is
// their block. With both off nothing here runs and every key's detail byte stays 0.
namespace render::vulkan {

struct Texture;

// The detail of a draw: the key's detail bits, the kind's texture (kDetailMaterial), its F0 (< 0: the map's) and the
// block.
struct DetailDraw {
    uint8_t flags = 0;
    Texture* texture = nullptr;
    float f0 = -1.0F;
    DetailUniforms uniforms{};
};

// What a draw of a colour texture with a surface map gets: the texture's checksum and content hash (its first level's)
// and its map's class. All 0 / null with rt_detail and rt_delight off. Takes the detail state's own lock.
[[nodiscard]] DetailDraw detail_draw(uint32_t checksum, uint64_t content_hash, uint8_t material_class);
// Changes whenever the settings or the library change (callers cache detail_draw's answers by it).
[[nodiscard]] uint64_t detail_generation();
// The library read and its textures made when rt_detail is on and they aren't (Gpu calls: the thread that makes
// textures - the dictionaries' loader, the settings' change).
void ensure_detail_library();
// The library from a file's bytes (formats/detail_material_format.h) in place of the one before, its kinds' textures
// made (Gpu calls); false (and none) if it isn't one. ensure_detail_library() reads the game folder's; the checks hand
// theirs over.
[[nodiscard]] bool load_detail_library(std::span<const uint8_t> file);
// The library's file name in the game folder.
inline constexpr const char* kDetailLibraryFile = "thug2-decomp-materials.t2dm";
// The kinds' textures made so far (the log; checks).
[[nodiscard]] uint32_t detail_textures_made();

} // namespace render::vulkan
