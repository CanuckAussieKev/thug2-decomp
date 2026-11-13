#pragma once

#include <array>
#include <vector>

#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_style.h"

// NOT ORIGINAL - the art styles beside the toon filter ([Graphics] rt_style comic, sketch, noir, thermal; docs/design/
// vulkan.md "Art style"). Each is a composite of its own over vulkan_style's passes (the viewport's colour copied out,
// the linear depth, the half-resolution colour and its Kuwahara smoothing, and - comic, sketch - the edge pass: step 4c's
// edge per depth pixel). Per output pixel (`local`: its place in the viewport; a "cell" is max(1, round(height / 720))
// pixels; every pattern is anchored to the viewport's pixels - nothing in it moves with the camera, so nothing shimmers;
// the noir grain and the thermal noise change with the frame's seed, the paper and the hatching never do):
//   ink (comic, sketch): the edge image widened - the largest e x clamp(e (k + 1) - d, 0, 1) over the depth pixels d <=
//     k + 0.5 away (k = the look's reach): a strong edge reaches k pixels, a weak one fewer (the lines taper where an
//     edge fades), never darker than its own e; times rt_style_outlines.
//   comic: the colour smoothed (rt_style_smooth), its OKLab lightness L in rt_style_bands hard-edged bands (soft over
//     kComicSoftness of a band), the chroma x rt_style_saturation x kComicChroma; halftone dots where the unbanded L is
//     below kDotsFrom - a screen at 45 degrees, its spacing rt_style_comic_halftone (720-line pixels), each dot's
//     radius spacing x kDotSize x sqrt(darkness) - in a darker shade of the fill; the paper's tint and fibre
//     (rt_style_paper, rt_style_grain); black ink (k: rt_style_outline_width x 1.5).
//   sketch: the smoothed colour's lightness made a tone; four hatching layers (55, 145, 10, 100 degrees, each turned by
//     a slow noise - the regions' jitter), each fading in below its threshold, their strokes rt_style_sketch_hatch
//     apart, thicker where darker, with pressure and gaps along them; graphite outlines (k: rt_style_outline_width x
//     0.5), grainy; on paper (rt_style_paper's tint, rt_style_grain's fibre) washed with a pale copy of the colour
//     (rt_style_sketch_wash).
//   noir: the lightness through a tone curve (rt_style_noir_contrast about 0.5, then smoothstep - the blacks crushed),
//     film grain per cell and frame (rt_style_grain; strongest in the mid-tones), a vignette (rt_style_vignette), then
//     the two colours mixed by it (rt_style_noir_dark - rt_style_noir_light).
//   thermal: heat from the half-resolution colour's brightness (blurred: bilinear), the nearness (kThermalNear -
//     kThermalFar units) and how far the pixel stands in front of its surroundings (1 - z x the mean of 1/z on a ring
//     of kThermalRing 720-line pixels, and on one three times as wide - the larger; the rings' points on the sky left
//     out: 0 on any plane - a floor, a wall - and along a skyline, high on a skater, a ped, a car, a pole in front of
//     what is behind them; x rt_style_thermal_focus); the sky cold; sensor noise per cell, per line and per frame (rt_style_grain), a
//     vignette; a heat palette black - blue - purple - red - orange - yellow - white.
//   Every look mixes over the original by rt_style_strength; alpha kept; NaN-free (inputs clamped first).
namespace render::vulkan {

inline constexpr float kComicInkReach = 1.5F;   // the ink's reach k: the outline width (depth pixels) x this
inline constexpr float kSketchInkReach = 0.5F;
inline constexpr int kMostInk = 4;
inline constexpr float kComicSoftness = 0.04F;  // (of a band, either side of its edge)
inline constexpr float kLeastPattern = 3.0F;    // the halftone's and hatching's least spacing (output pixels)
inline constexpr float kThermalRing = 24.0F;    // (720-line pixels)
inline constexpr float kThermalNear = 150.0F;   // (world units: warmest nearer than this)
inline constexpr float kThermalFar = 3000.0F;   // (no warmth from nearness past this)

// Whether a look reads the linear depth; draws the edge pass's ink; draws lines at all.
[[nodiscard]] bool look_reads_depth(ArtStyle look);
[[nodiscard]] bool look_draws_ink(ArtStyle look);
[[nodiscard]] bool look_draws_lines(ArtStyle look);

// The look's parameters from the settings (style_parameters' last step).
void set_look_parameters(const StyleSettings& settings, const StyleHeights& heights, StyleParameters* parameters);

// The push constants' look fields (vulkan_style.cpp's block): the band edge's softness (tone.y) and look_a - look_d.
struct LookConstants {
    float softness = 0.0F;
    std::array<float, 4> a{};
    std::array<float, 4> b{};
    std::array<float, 4> c{};
    std::array<float, 4> d{};
};
[[nodiscard]] LookConstants look_constants(const StyleParameters& parameters);

// The looks' GLSL: what they share (after vulkan_style.cpp's composite pieces), and one look's main.
[[nodiscard]] const char* style_look_shared_glsl();
[[nodiscard]] const char* style_look_body(ArtStyle look);

// A look's twin on the processor (style_reference's for every look but toon): `images.depth` the linear depth at the
// colour's resolution (null or empty: none), `images.smoothed` as style_reference's.
[[nodiscard]] std::vector<StyleRgb> style_look_reference(const StyleParameters& parameters, const StyleImages& images,
                                                         Extent size);

} // namespace render::vulkan
