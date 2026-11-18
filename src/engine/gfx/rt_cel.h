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
    // (path tracing, stage 1) the lighting's sky light on a level surface (the global unit, physical - rt_relit.h
    // RelitLighting::sky): what a path tracer's open sky gives where it has no sky table
    std::array<float, 3> sky{};
};
void set_cel_world(const CelWorld& world);
[[nodiscard]] CelWorld cel_world();
// A number that changes whenever it is set (the draws look it up again).
[[nodiscard]] uint64_t cel_world_serial();

// --- the stream (render_pc writes it; the Vulkan Gpu binds it at the draws' binding 2) ------------------------------
// (added by the light energy audit: the stream is the relit world's light as written - its parts - and the ray-traced
// passes read it too, through the geometry table: gpu_vulkan/vulkan_rt_scene.h hit_world_light. Each part of a relit
// world pixel's light then has one owner - docs/design/vulkan.md "One lighting path", "Each light counted once".)
// (lighting round A) It is the relit world's light store: the game's vertex buffers are never written - every world
// draw from a relit mesh reads word 5 in place of its vertex colour (its alpha the game's), and the ray-traced passes
// replace their own part of it per pixel.
// (round L - a physical light store, engine/gfx/light_store.h) Eight words a vertex. The light words are RGB9E5: light in
// the global unit, physical - NOT pre-exposed (the draws and the ray-traced passes multiply by the frame's pre-exposure
// where they read it: a pre-exposure change rewrites nothing); the tint and the flags are D3DCOLORs (A8R8G8B8):
//   0: the sky's and bounce's light (light);
//   1: the tint - the surface's colour (rgb, x 128, at most 2), the vertex's share of the sun's rays (a, x 255): the
//      visibility its colour was written with (D3DCOLOR);
//   2: the lamps' light on it (tint x L x the lamps on - light);
//   3: the direct light's colour and strength the colour was written with (light: the lighting state's sun per N.L);
//   4: the sky's part of word 0's light (light - the rest is the bounce);
//   5: the light the world is drawn with - tint x (word 0 + word 3 x N.L x word 1's a) + word 2 + word 7 (light: rt_relit.h
//      relit_colour; kRelitColourLocation);
//   6: the flags (D3DCOLOR): N.L toward the sun (red, x 255), the vertex's sky share K (green, x 255), 255 in blue: its
//      emission is a lamp drawn in a texture (shown on the lamp texels alone - word 5 leaves it out), and in alpha 255
//      the vertex has the stream, 128 the mesh is hidden (the bake's shadow drawn as geometry, an original light card),
//      0 none (an animated vertex - the draw keeps the game's colour, the cel shader bands it);
//   7: the vertex's own emission - a lamp's lit surface shows this radiance (light: rt_relit.h RelitVertex::emit).
// Before round L: six 8-bit words of pre-exposed light x 128 - a lamp's head held at 2 (about 1.8 cd/m^2 at a night's
// pre-exposure) and every pre-exposure change a full relight.
// The cel and surface draws read words 0 - 2 (kCelStreamAttributes), 6 and 7; every relit draw words 5 and 6.
inline constexpr uint32_t kCelStreamWords = 8;
inline constexpr uint32_t kCelColourWord = 5;
inline constexpr uint32_t kCelFlagsWord = 6;
inline constexpr uint32_t kCelEmitWord = 7;
// (word 6's alpha) the vertex has the stream / its mesh is hidden (the draw's colour transparent black: the shader
// replaces the whole colour with nothing)
inline constexpr uint32_t kCelHasStream = 0xFF000000U;
inline constexpr uint32_t kCelHiddenFlags = 0x80000000U;
inline constexpr uint32_t kCelStreamAttributes = 3;
inline constexpr uint32_t kCelStreamBytes = kCelStreamWords * 4;
using CelWords = std::array<uint32_t, kCelStreamWords>;
[[nodiscard]] CelWords cel_stream_words(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
inline constexpr CelWords kCelNoStream{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
// A hidden mesh's vertex (its flags kCelHiddenFlags, nothing else).
inline constexpr CelWords kCelHiddenStream{0U, 0U, 0U, 0U, 0U, 0U, kCelHiddenFlags, 0U};
// (added by the light energy audit) A vertex's stream read back (light in the global unit, physical) - the processor's
// twin of gpu_vulkan/vulkan_rt_scene.h hit_world_light at a vertex (which multiplies by the frame's pre-exposure);
// `valid` false for kCelNoStream (word 6's alpha 0).
struct StreamParts {
    std::array<float, 3> ambient{};
    float n_dot_l = 0.0F;
    std::array<float, 3> tint{};
    float seen = 0.0F;
    std::array<float, 3> lamps{};
    std::array<float, 3> sun{};
    std::array<float, 3> sky{};
    float sky_share = 0.0F;
    std::array<float, 3> colour{};
    std::array<float, 3> emit{};
    bool valid = false;
    bool hidden = false;
};
[[nodiscard]] StreamParts stream_parts(const CelWords& words);
// The light its colour shows with the sun's visibility `seen` (the twin of the GLSL world_light_with, the sky as it is):
// tint x (ambient + sun x N.L x seen) + lamps + emit.
[[nodiscard]] std::array<float, 3> stream_light(const StreamParts& parts, float seen);

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
