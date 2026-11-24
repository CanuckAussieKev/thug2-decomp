#pragma once

// NOT ORIGINAL - (round SUBPIX) a surface's emission map: what a relit surface's light store's emission is multiplied by
// where it is drawn, read by every renderer through this one definition - the draws (gpu_vulkan/ff_shaders.cpp's relit
// branch), the rays (vulkan_rt_scene.cpp store_emission: the path tracer, ReSTIR DI's targets, ReSTIR GI's and the
// reflections' cone-filtered hits, the light list's emitter powers) and the relit bake (render_pc/rt_record.cpp
// emission_texels: the area a lamp's flux is spread over).
//
// Level 0 is the surface's first texture, texel for texel: the texel's colour (linear) where the lamp-texel test lets it
// emit (rt_lights.h lamp_texel_coded with the mesh's code - its texture's floor; 0: every texel), 0 elsewhere. Each
// further level is the mean of the level above over its footprint (a box filter: flux conserved at every level, as the
// emissive mips of shipping engines are made - the mean of the emission, never the emission of the mean). Before, both
// renderers ran the lamp-texel test on the FILTERED texel (the raster at its mip, the rays at the ray cone's level):
// wherever a footprint held lamp texels among darker ones the filtered texel failed the test and the surface sent nothing
// - the flux the bake conserves at level 0 lost at distance and in every cone-filtered hit.
//
// The test reads the texel alone (its colour and its own alpha): a texture-space map can't hold the vertex's alpha or the
// material's colour, so neither enters the emission (the bake conserves the flux under the same definition: L is the
// lamp's flux over pi x the drawn area x this map's mean - the material's colour would only have tinted it).
#include <array>
#include <cstdint>
#include <vector>

#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/texel_decode.h"

namespace render {
class Gpu;
}

namespace render::rt {

// One texel's emission factor (linear, 0 - 1 a channel): the texel's linear colour where the coded lamp-texel test passes
// on it (its colour and its own alpha), else 0. `texel` display-referred bytes, R G B A.
[[nodiscard]] std::array<float, 3> emission_texel(const std::array<uint8_t, 4>& texel, uint32_t code);

// One level of an emission map: width x height texels, rows top to bottom.
struct EmissionLevel {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::array<float, 3>> texels;
};

// The whole map of `image` under `code`: level 0 emission_texel of each texel, then every level down to 1 x 1, each half
// the size above (rounded down, at least 1 - the Vulkan chain) and each texel the mean of the texels of the level above
// its footprint covers. An empty image: one white texel's map (an untextured surface draws white).
[[nodiscard]] std::vector<EmissionLevel> emission_map(const texel::Image& image, uint32_t code);

// A level's mean (the whole map's level 0 mean - every level's, the filter conserving it).
[[nodiscard]] std::array<float, 3> emission_mean(const EmissionLevel& level);

// The map as a texture of `gpu`'s: four halves a texel (R G B linear, 1), every level written - what the renderers sample
// (gpu_value::kFormatA16B16G16R16F). Null when it can't be made; the caller releases it.
[[nodiscard]] GpuTexture* emission_map_texture(Gpu& gpu, const std::vector<EmissionLevel>& levels);

// (round SUBPIX - emitters smaller than a pixel) Under Ray Reconstruction a lamp's surface drawn over a pixel or two is
// hit by the raster's one sample in some of its jitter phases only - a pixel lit at its whole luminance one frame in
// several, which Ray Reconstruction takes for a firefly and drops (round NOBIAS2: a 1-pixel emitter kept 0.31, one lit
// one frame in four 0.04). An emitting triangle's directly seen emission is therefore split by its drawn area in the
// view's pixels: the share emission_splat_share gives is splatted - the triangle's whole flux toward the camera spread
// over the pixel footprint round its true place, every frame, into Ray Reconstruction's transparency layer (the stars'
// technique, round NIGHTSKY: flux-conserving points the upscaler keeps; the layer is upscaled, not denoised) - and the
// draws keep the rest. One function decides the split for both (the raster from its fragment's barycentric derivatives,
// the splat from the triangle's projected corners): their shares add to 1.
inline constexpr float kSplatWholeAtMost = 2.0F; // (drawn pixels: a triangle this small or smaller is splatted whole)
inline constexpr float kSplatNoneAtLeast = 8.0F; // (... this large or larger drawn alone - a 3 x 3 pixel patch)
[[nodiscard]] float emission_splat_share(float drawn_pixels);
// The GLSL twin: `float emission_splat_share(float drawn_pixels)`.
[[nodiscard]] const char* emission_splat_glsl();

} // namespace render::rt
