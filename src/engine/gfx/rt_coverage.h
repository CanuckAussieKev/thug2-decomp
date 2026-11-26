#pragma once

#include <cmath>
#include <cstdint>

// NOT ORIGINAL - round SKIN: a blended surface that writes depth, as the per-surface lighting keeps it (gpu_vulkan/
// vulkan_surface_light.h, the prepass). The game draws such a surface (a character's laces and headphones, a fence's soft
// edges, a sheet fading out) alpha-blended over what it drew first - and writes its depth. Under a prepass the world is
// shaded after every depth is known: the pixels such a surface keeps hide what lies behind them from the shading, so
// blending it over what is "behind" blends it over a pixel nothing drew (round V kept its pixels at least half opaque:
// for 0.5 <= alpha < 1 the colour under it was whatever the target held there - Training's skater, the shoes' and
// headphones' edges ~10^4 too bright, 61 % of the frame's light in 200 pixels). And its pixels under half opaque were
// drawn over the shaded surface behind without light inputs of their own (an object's then took its key light unshadowed).
//
// Its coverage is stochastic now (stochastic transparency - Enderton et al. 2010; UE's dithered opacity / masked materials
// under a temporal resolve): at each pixel, each frame, the surface is either wholly there - kept in the prepass, shaded as
// an opaque surface with its light inputs - or not there at all (its fragment discarded, the surface behind shaded as
// usual), with the probability of its alpha. The temporal resolve (the upscaler, Ray Reconstruction) averages the frames
// into the blend - what the path tracer's reference does with a blended surface at its first hit (a surface by its
// coverage). Without a jittered temporal resolve the threshold is fixed at a half (an alpha test - the masked material's
// own fallback). The rays' camera surface (vulkan_rt_scene.cpp drawn_kept) takes the same threshold at the same pixel, so
// the light inputs are made for the surface the raster kept.
namespace render::rt {

// The threshold without a temporal resolve: the pixel kept where its alpha is at least a half.
inline constexpr float kCoverageHalf = 0.5F;
// The frames the threshold's pattern cycles over.
inline constexpr uint32_t kCoverageFrames = 64;

// The seed a frame passes to the threshold (its serial within the cycle); -1: no temporal resolve (the half).
[[nodiscard]] inline float coverage_seed(uint64_t frame_serial, bool temporal)
{
    return temporal ? static_cast<float>(frame_serial % kCoverageFrames) : -1.0F;
}

// The threshold at pixel (x, y) - its integer coordinates - in frame `seed` of the cycle (0 - 1): interleaved gradient
// noise over the pixels (Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare" - each pixel's
// neighbours spread over 0 - 1), each frame moved on by the golden ratio's fraction (the R1 sequence, Roberts 2018: a
// pixel's thresholds over the frames spread evenly over 0 - 1 too).
[[nodiscard]] inline float coverage_noise(uint32_t x, uint32_t y, float seed)
{
    const float inner = (0.06711056F * static_cast<float>(x)) + (0.00583715F * static_cast<float>(y));
    const float spatial = 52.9829189F * (inner - std::floor(inner));
    const float moved = (spatial - std::floor(spatial)) + (0.618034F * seed);
    return moved - std::floor(moved);
}

// The least alpha the surface keeps at pixel (x, y) in a frame of `seed`: it is there where its alpha is above it.
[[nodiscard]] inline float coverage_least(uint32_t x, uint32_t y, float seed)
{
    return seed < 0.0F ? kCoverageHalf - 1.0e-6F : coverage_noise(x, y, seed);
}
[[nodiscard]] inline bool coverage_kept(float alpha, uint32_t x, uint32_t y, float seed)
{
    return alpha > coverage_least(x, y, seed);
}

// The same in GLSL (the raster's fragments - ff_shaders.cpp - and the rays' camera surface): `pixel` the integer
// coordinates (floor(gl_FragCoord.xy) / the compute pass's pixel).
inline constexpr const char* kCoverageGlsl = R"(
float coverage_noise(uvec2 pixel, float seed)
{
    float spatial = fract(52.9829189 * fract(0.06711056 * float(pixel.x) + 0.00583715 * float(pixel.y)));
    return fract(spatial + 0.618034 * seed);
}
float coverage_least(uvec2 pixel, float seed) { return seed < 0.0 ? 0.5 - 1.0e-6 : coverage_noise(pixel, seed); }
)";

} // namespace render::rt
