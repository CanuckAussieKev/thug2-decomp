#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - the art styles: the toon filter and the passes the other looks share (docs/design/vulkan.md "Art
// style"; [Graphics] rt_style - gpu_vulkan.h's
// StyleSettings). A post-process over each viewport's world after everything else at world_drawn (rt::ViewOrder::kStyle:
// after the tone mapping and the upscaler - the colour the display's 0 - 1, at the output's resolution), before the 2D:
// the HUD, the menus and text are never filtered. Independent of ray tracing, HDR and the upscaler.
//
// The passes, per viewport (its rectangle in the target; split screen: each its own, nothing read across its edge):
//   0. the viewport's colour copied out (resolved first when multisampled: the filter sees the pixel's mean);
//   1. (prep, compute) the depth made linear: each depth pixel's distance along the view (the projection's w; R32F;
//      sample 0 of a multisampled depth); the depth's far value (1: nothing drawn - the sky) marked kStyleSky;
//   2. (down, compute) the colour at half resolution: each 2 x 2 block's mean (clamped to 0 - 1; not a number: 0);
//   3. (smooth, compute) the generalized Kuwahara filter at half resolution (radius r half-resolution pixels from
//      rt_style_smooth, scaled with the output's height: r = round(smooth x height / 720 / 2), 1 - 8; 0: no smoothing):
//      the four overlapping (r + 1) x (r + 1) quadrants around the pixel, each's mean m_k and variance s_k (summed over
//      red, green and blue), mixed with weights 1 / (1 + (255^2 s_k)^4) - the flattest quadrant wins smoothly. Texture
//      detail flattens into painted fields; an edge between two fields stays (the quadrants on one side of it win);
//   4. (composite, a full-screen triangle over the viewport) per output pixel:
//      a. the smoothed colour brought back to full resolution edge-aware: the four half-resolution texels around it,
//         bilinear weights times exp(-kStyleGuide x |texel - the pixel's own colour|^2) (a joint bilateral upsample:
//         the pixel takes the fields on its own side of an edge); their sum below kStyleWeightFloor: plain bilinear;
//      b. colour simplification in OKLab (the colours taken as gamma 2.2): lightness in rt_style_bands bands (0: none)
//         - each band's middle value, soft edges kStyleBandSoftness of a band wide, kStyleBandHold of the lightness
//         kept as it was (the darkest shades not crushed) -, hue kept, the chroma times
//         rt_style_saturation;
//      c. outlines from the linear depth z at the pixel (its depth pixel: the depth's rectangle scaled to the
//         colour's - the upscaler's lower resolution) and h depth pixels left / right / up / down (h from
//         rt_style_outline_width, scaled with the depth's height: max(1, round(width x height / 720))): per axis
//         s = 1 - z (1/z_a + 1/z_b) / 2 (0 on any plane - 1/z is affine across a flat surface on the screen; above 0 where
//         the pixel stands out toward the camera: the near side of a silhouette, a ridge; below where it is recessed: a
//         valley, the far side of a silhouette), v = |s| x f / h (f the focal length in depth pixels: a crease's
//         measure of its change of slope, independent of resolution and distance). Per axis an edge
//         smoothstep(kStyleCreaseLow, kStyleCreaseHigh, v), 0 where s < -kStyleFarSide (the far side of a step: the
//         line stays on the object in front), times kStyleCreaseWeight unless s > kStyleStepSide (a silhouette: full);
//         the larger axis' taken, faded with the distance (1 - smoothstep(kStyleFadeStart, kStyleFadeEnd, z)) and times
//         rt_style_outlines. The sky (z = kStyleSky) has none. The line's colour: the toon colour's OKLab lightness x
//         kStyleLineLightness, its chroma x kStyleLineChroma - a dark, tinted ink, never pure black;
//      d. the toon colour back to gamma 2.2 and clamped to 0 - 1, mixed with the pixel's own by rt_style_strength.
//   Alpha is kept. Every step is NaN-free (inputs clamped first).
// style_reference below is the composite's twin on the processor (the check's reference).
//
// The other looks (rt_style comic, sketch, noir, thermal - vulkan_style_looks.h) share these passes and differ in the
// composite: comic and sketch add an edge pass (step 4c's edge per depth pixel into an R32F image, which their ink
// widens), thermal and noir read the half-resolution colour unsmoothed. vhs, retro and watercolour
// are names only so far: logged once as not built in, nothing drawn.
namespace render::vulkan {

struct Backend;

inline constexpr float kStyleSky = 1.0e9F;           // the linear depth of a pixel nothing was drawn at
inline constexpr float kStyleGuide = 22.0F;          // the upsample's colour weight: exp(-k |difference|^2)
inline constexpr float kStyleWeightFloor = 1.0e-4F;
inline constexpr float kStyleBandSoftness = 0.25F;   // (of a band)
inline constexpr float kStyleBandHold = 0.3F;        // the share of the lightness kept as it was (the shading under the bands)
inline constexpr float kStyleKuwaharaScale = 65025.0F; // 255^2: the variance in steps of 8-bit colour
inline constexpr float kStyleCreaseLow = 0.18F;
inline constexpr float kStyleCreaseHigh = 0.45F;
inline constexpr float kStyleFarSide = 0.02F;
inline constexpr float kStyleStepSide = 0.02F;
inline constexpr float kStyleCreaseWeight = 0.75F;
inline constexpr float kStyleFadeStart = 2500.0F;   // world units
inline constexpr float kStyleFadeEnd = 9000.0F;
inline constexpr float kStyleLineLightness = 0.2F;
inline constexpr float kStyleLineChroma = 1.3F;
inline constexpr float kStyleBaseHeight = 720.0F;    // the height the smoothing radius and the line width are given for
inline constexpr int kStyleMostRadius = 8;           // (half-resolution pixels)

// What the passes are given for a viewport, worked out from the settings and the view (also the reference's input).
struct StyleParameters {
    ArtStyle style = ArtStyle::kToon;
    int radius = 0;              // the Kuwahara filter's (half-resolution pixels; 0: no smoothing)
    float bands = 0.0F;          // 0: no banding
    float saturation = 1.0F;
    float strength = 1.0F;
    float outlines = 0.0F;       // 0: none
    int spacing = 1;             // h: the outline's neighbours, in depth pixels
    float focal = 1.0F;          // f: the projection's vertical scale x the depth's height / 2 (depth pixels)
    std::array<float, 4> projection{}; // P22, P32, P23, P33 (D3D's row-vector layout): the linear depth's
    std::array<float, 2> depth_range{0.0F, 1.0F};
    // (the looks' - vulkan_style_looks.h; output pixels scale with the picture's height / 720)
    int ink = 0;                 // comic, sketch: how far a line reaches round its edge (depth pixels; 0: the edge alone)
    int ring = 1;                // thermal: the "stands in front" test's ring (depth pixels); noir: the surroundings'
                                 // ring (half-resolution pixels)
    float halftone = 0.0F;       // comic: the dot screen's spacing (output pixels; 0: none)
    float hatch = 1.0F;          // sketch: the strokes' spacing (output pixels)
    float wash = 0.0F;           // sketch
    float paper = 0.0F;          // comic, sketch
    float grain = 0.0F;          // comic, sketch, noir, thermal
    float vignette = 0.0F;       // noir, thermal
    float contrast = 1.0F;       // noir
    float focus = 0.0F;          // thermal
    float cell = 1.0F;           // the grain's and the paper fibre's cell (output pixels: height / 720, at least 1)
    std::array<float, 3> dark{};  // noir's colours, as the shader has them (8 bits a channel)
    std::array<float, 3> light{};
    uint32_t seed = 0;           // the frame's (the grain and the noise change with it; 0 - 65535)
};
// The heights (pixels) the radius and the line width scale with: the colour's viewport, the depth's.
struct StyleHeights {
    uint32_t output = 0;
    uint32_t depth = 0;
};
[[nodiscard]] StyleParameters style_parameters(const StyleSettings& settings, const rt::ViewDrawn& view,
                                               const StyleHeights& heights);
// The parameters of the last pass recorded (its frame's seed among them; the check's reference input).
[[nodiscard]] StyleParameters style_last_parameters();
// The settings while the backend's lock is held (the camera effects' grade: a style's own vignette and grain).
[[nodiscard]] const StyleSettings& style_settings_locked();
// Whether a style has passes here (toon, comic, sketch, noir, thermal; the others log "not built in" and draw nothing).
[[nodiscard]] bool style_built(ArtStyle style);

// The linear depth of a depth value (pass 1's).
[[nodiscard]] float style_linear_depth(const StyleParameters& parameters, float depth);

// The whole filter on the processor (the check's reference) over a viewport of `size`: `colour` its pixels (RGB, row by
// row), `depth` the linear depth over the same pixels (null or empty: no outlines) - the depth at the colour's
// resolution -, `smoothed` (not null) the half-resolution smoothed colour to use instead of the twin's (the pass's own,
// read back: the composite alone compared). Returns the pixels as the composite writes them.
using StyleRgb = std::array<float, 3>;
struct StyleImages {
    const std::vector<StyleRgb>* colour = nullptr;
    const std::vector<float>* depth = nullptr;
    const std::vector<StyleRgb>* smoothed = nullptr;
};
[[nodiscard]] std::vector<StyleRgb> style_reference(const StyleParameters& parameters, const StyleImages& images,
                                                    Extent size);
// The pieces (also the check's): the half-resolution image (of a `size` picture), smoothed (`size` the half image's);
// and one pixel's toon colour without outlines.
[[nodiscard]] std::vector<StyleRgb> style_half(const std::vector<StyleRgb>& colour, Extent size);
[[nodiscard]] std::vector<StyleRgb> style_kuwahara(const std::vector<StyleRgb>& half, Extent size, int radius);
[[nodiscard]] StyleRgb style_simplify(const StyleParameters& parameters, StyleRgb colour);
// The edge pass's twin: each depth pixel's edge (step 4c before rt_style_outlines; `depth` of `size`).
[[nodiscard]] std::vector<float> style_edges(const StyleParameters& parameters, const std::vector<float>& depth,
                                             Extent size);

// The passes over the bound colour target for a view (the listener's work; also the check's). The caller holds the
// lock. False (nothing recorded): off, strength 0, the target can't be read, the passes can't be made (logged once).
bool record_style_pass(Backend& state, const rt::ViewDrawn& view);
[[nodiscard]] uint64_t style_passes();
// Whether the last pass drew outlines (a samplable depth bound).
[[nodiscard]] bool style_outlined();
// The last pass's linear depth read back (R32F, the depth image's whole size); false when there is none. The caller
// holds the lock and has finished the frame's work.
bool read_style_depth(Backend& state, std::vector<float>* depth, Extent* size);
// The last pass's smoothed half-resolution colour read back (RGBA16F: four 16-bit floats a texel, the image's whole
// size); false when there is none. As read_style_depth.
bool read_style_smoothed(Backend& state, std::vector<uint16_t>* halves, Extent* size);
// The last edge pass's output read back (R32F, the depth image's whole size); as read_style_depth.
bool read_style_edges(Backend& state, std::vector<float>* edges, Extent* size);

// The usage a depth surface of `format` gets besides being an attachment: VK_IMAGE_USAGE_SAMPLED_BIT whenever the
// format can be sampled (the style is turned on while the game runs: the depth surfaces made before must be readable).
[[nodiscard]] VkImageUsageFlags style_depth_usage(const Backend& state, VkFormat format);

// The passes' objects released (the device's shutdown; the caller holds the lock).
void release_style(Backend& state);

} // namespace render::vulkan
