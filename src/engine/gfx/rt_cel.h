#pragma once

// NOT ORIGINAL - cel shading ([Graphics] rt_cel; docs/design/vulkan.md "Cel shading"): a rendering mode, not a filter.
// The light is put in bands where it is worked out - per pixel, in the draws' own shaders - and the rest follows:
//
//   - the world with the relit mode (rt_relit.h): its decomposition (the artists' tint, the sky's and bounce's light,
//     the sun's share and N.L, the lamps' residual) handed to the draws per vertex in a second vertex stream
//     (cel_stream_words below; render_pc/rt_relit_world writes it, the Vulkan Gpu binds it), and each pixel lit as
//         tint x (ambient_q x mix(shadow colour, 1, q) + sun x q) + residual_q
//     where q is N.L x the sun's visibility in `bands` steps (crisp, anti-aliased over a pixel), ambient_q the sky and
//     bounce light in whole stops and residual_q the lamps' light in quarter steps (hue kept in both). The visibility
//     is a ray traced per pixel toward the scene's sun (a hard shadow, no penumbra) when ray tracing is on, else the
//     vertex's own share of the sun's rays;
//   - the world without it (rt_relit off, a level that keeps its bake, a vertex the game animates): the baked vertex
//     colour's luminance in steps of a stop (2 / (bands - 1) stops), its hue kept, the darkest steps tinted by the
//     shadow colour;
//   - the skater, the peds and the objects (lit by lights with normals): each light's N.L in bands per pixel (the key
//     light also by the ray-traced visibility), the shadow colour on their ambient where the key light is off, and a
//     rim light band (1 - N.V past a threshold);
//   - outlines: an inverted hull drawn behind each lit object (thicker), lines from the depth over the world (thinner),
//     the ink the surface's own colour darkened;
//   - textures flattened (rt_cel_flatten): their colour mixed toward a blurrier mip level (alpha untouched);
//   - the sky (rt_sky): the atmosphere in flat bands of elevation and the clouds' cover and shading in bands.
// The ambient occlusion and the one-bounce light passes are left out while it is on (their soft gradients are what the
// mode takes away); the ray-traced shadows' own pass too when the draws trace their own.
//
// API-neutral: the settings, what render_pc publishes, the stream's layout and the bands' formulas (the processor's
// twins of the shaders' - the check's reference). Independent of rt_style (the toon filter) - both may be on.

#include <array>
#include <cstdint>
#include <string_view>

#include "engine/gfx/rt_relit.h"

namespace render::rt {

inline constexpr uint32_t kCelLeastBands = 2;
inline constexpr uint32_t kCelMostBands = 4;
inline constexpr float kCelBandBias = 0.15F;       // (a band's step taken a little early: the lit side wins ties)
inline constexpr float kCelAmbientStops = 1.0F;    // the sky's and bounce's light in steps of this many stops
inline constexpr float kCelResidualStep = 0.25F;   // the lamps' light in steps of this (light units: 1 the bake's 128)
inline constexpr float kCelRimThreshold = 0.62F;   // 1 - N.V past this: the rim
inline constexpr float kCelShadowLow = 0.3F;       // the fallback's shadow tint from here (light units) ...
inline constexpr float kCelShadowHigh = 0.55F;     // ... fading out by here
inline constexpr float kCelInkLightness = 0.28F;   // the outlines' ink: the surface's colour this bright ...
inline constexpr float kCelInkChroma = 1.25F;      // ... and this saturated
inline constexpr float kCelFlattenBias = 2.5F;     // rt_cel_flatten 1: the colour mixed fully toward this many mips up
inline constexpr float kCelHullPixels = 2.5F;      // an object's outline (rt_cel_outline_width 1) at 720 lines
inline constexpr float kCelWorldLinePixels = 1.0F; // the world's lines
inline constexpr std::array<float, 3> kCelShadowColour{0.62F, 0.66F, 0.86F}; // (the default: a cool blue, not grey)
inline constexpr float kCelRim = 0.5F;
inline constexpr float kCelFlatten = 0.5F;
inline constexpr float kCelMostRim = 2.0F;
inline constexpr float kCelMostOutlineWidth = 4.0F;

struct CelSettings {
    bool on = false;
    uint32_t bands = 3; // 2 - 4: the steps of the sun's light (the shadow band among them)
    std::array<float, 3> shadow_colour = kCelShadowColour; // the shadow band's tint
    float rim = kCelRim;        // the rim light's strength (0: none)
    float outline_width = 1.0F; // the outlines' width (0: none; 1: the default thickness)
    float flatten = kCelFlatten; // how far the textures' colour goes toward a blurrier mip (0 - 1)
};
void set_cel_settings(const CelSettings& settings);
[[nodiscard]] CelSettings cel_settings();
// Whether the mode is on (no lock: asked per draw).
[[nodiscard]] bool cel_shading_on();
// A number that changes whenever the settings do (render_pc writes its streams again).
[[nodiscard]] uint64_t cel_settings_serial();
// rt_cel_shadow_colour's text: "r,g,b" (each 0 - 1, or 0 - 255 when any is above 1) or "#rrggbb". False: not one.
bool parse_cel_colour(std::string_view text, std::array<float, 3>* out);

// What render_pc publishes about the relit world (rt_relit_world): whether the world's draws carry the stream, and the
// relit model's sun light (a x the sun's colour, light units) the stream's N.L is multiplied by.
struct CelWorld {
    bool relit = false;
    std::array<float, 3> sun{};
    std::array<float, 3> toward{0.0F, 1.0F, 0.0F}; // the sun the stream's N.L was taken toward
};
void set_cel_world(const CelWorld& world);
[[nodiscard]] CelWorld cel_world();
// A number that changes whenever it is set (the draws look it up again).
[[nodiscard]] uint64_t cel_world_serial();

// --- the stream (render_pc writes it; the Vulkan Gpu binds it at the draws' binding 2) ------------------------------
// Three D3DCOLOR words a vertex (A8R8G8B8 - the shader reads them as the vertex colour, rgba 0 - 1):
//   0: the sky's and bounce's light (rgb, light x 128 - the bake's scale), N.L toward the sun (a, x 255);
//   1: the tint x the level's scale (rgb, x 128, at most 2), the vertex's share of the sun's rays (a, x 255);
//   2: the lamps' residual (rgb, x 128), 255 in a: the vertex has the stream (0: the shader bands the vertex colour).
inline constexpr uint32_t kCelStreamWords = 3;
inline constexpr uint32_t kCelStreamBytes = kCelStreamWords * 4;
using CelWords = std::array<uint32_t, kCelStreamWords>;
[[nodiscard]] CelWords cel_stream_words(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
inline constexpr CelWords kCelNoStream{0U, 0U, 0U};

// --- the bands (the shaders' formulas on the processor) ---------------------------------------------------------------
// An anti-aliased edge's half width, in steps (0: a hard edge - the twins' default; the shaders' comes from the
// derivatives).
struct CelEdge {
    float width = 0.0F;
};
// Rounding to whole steps, the edge at each half step.
[[nodiscard]] float cel_round(float steps, CelEdge edge);
// The sun's light in bands: N.L x visibility (0 - 1) to 0 .. 1 in bands - 1 steps.
[[nodiscard]] float cel_sun_band(float lit, CelEdge edge, uint32_t bands);
// A light's luminance in whole steps of `stops` stops (its hue kept by the caller: the factor returned).
[[nodiscard]] float cel_log_factor(float value, CelEdge edge, float stops);
// One world pixel lit from the stream (light units in and out; v0 = the result / 2). `visible` the sun's visibility.
struct CelStreamPixel {
    std::array<float, 3> ambient{};
    float n_dot_l = 0.0F;
    std::array<float, 3> tint{};
    float share = 0.0F;
    std::array<float, 3> residual{};
};
[[nodiscard]] std::array<float, 3> cel_world_light(const CelSettings& settings, const std::array<float, 3>& sun,
                                                   const CelStreamPixel& pixel, float visible, CelEdge edge);
// One world pixel without the stream: the bake (light units) banded.
[[nodiscard]] std::array<float, 3> cel_bake_light(const CelSettings& settings, const std::array<float, 3>& bake, CelEdge edge);
// The bake's steps for `bands` (stops).
[[nodiscard]] float cel_bake_stops(uint32_t bands);
// The outlines' ink for a colour (0 - 1).
[[nodiscard]] std::array<float, 3> cel_ink(const std::array<float, 3>& colour);

} // namespace render::rt
