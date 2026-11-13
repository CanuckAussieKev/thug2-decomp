#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/style_looks_settings.h"
#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - three more art styles beside the toon filter (vulkan_style.h; docs/design/vulkan.md "Art style"):
// [Graphics] rt_style=vhs, retro or watercolour and their rt_style_<style>_* knobs (rt_style_strength mixes each over the
// original as it does the toon's). Each is a post-process over each viewport's world at the toon's place
// (rt::ViewOrder::kStyle: after the tone mapping and the upscaler, before the 2D - the HUD, the menus and text are never
// filtered), its own rectangle in the target (split screen: nothing read across a viewport's edge), alpha kept, every
// step NaN-free. Independent of ray tracing, HDR and the upscaler. vulkan_style.cpp's pass hands a view to
// record_look_pass when rt_style is one of these.
//
// vhs (vulkan_style_vhs.cpp) - a 90s camcorder skate video, one full-screen pass. Per output pixel (s: a tape line,
//   the viewport's height / 480 pixels):
//   1. tape wobble: the pixel's line shifted sideways by a per-line jitter (a hash of the line and the frame - 30 a
//      second of game time), a slow bend down the picture and, inside a tracking band rolling down the picture now and
//      then (one 9-second slot in ten has none; a hash of the slot), a tear;
//   2. the fisheye: the centred position's radius (1 at the corners) r^2 -> the source at c x (1 + k r^2) / (1 + k e^2),
//      e^2 the left / right edges' middles' (barrel distortion: those stay put, the middle is magnified, the corners
//      reach past the picture - dark), the corners darkened further (a vignette);
//   3. the source read along the line: luma through a 5-tap soft kernel, the chroma (YIQ's I and Q) through a wide 7-tap
//      one moved to the right (the colour bleeds and fringes after the edges); the chroma x rt_style_vhs_saturation;
//   4. luma lifted and squeezed (the tape's grey blacks), grain per tape cell (stronger in the band), back to RGB;
//   5. the on-screen display (rt_style_vhs_overlay): a blinking red dot and REC at the top left, SP and a tape counter
//      (h:mm:ss of game time) at the bottom - a 5 x 7 dot font of our own, white over a dark shadow, moved with the
//      tape's wobble but not the lens;
//   6. scanlines (darker between a tape's lines; faded out below 2 pixels a line), the vignette.
//   Everything that moves follows the game clock (rt::ViewDrawn::game_seconds): a paused game holds it, a demo's video
//   export renders it frame for frame as the run plays.
// retro (vulkan_style_retro.cpp) - a 32-bit console's look:
//   1. the geometry (ff_shaders.cpp, VertexKey::retro - a key bit only while a view's world is drawn with this style
//      on, so every other shader stays as it was): each 3D vertex snapped toward the low-resolution grid's corners and
//      the attributes interpolated linearly on the screen (affine texture mapping; the depth stays exact). The ray-traced
//      effects keep working - they read this depth;
//   2. one full-screen pass: the viewport cut into rt_style_retro_lines rows of square cells (the columns rounded to the
//      viewport's aspect), each cell the source's pixel at its middle (point-sampled), each channel brought to
//      rt_style_retro_bits bits (5: 15-bit colour) through a 4 x 4 ordered dither per cell.
// watercolour (vulkan_style_watercolour.cpp) - a wash painting on paper; three passes:
//   1. (compute) the colour at half resolution (each 2 x 2 block's mean);
//   2. (compute) the paint: an edge-aware (bilateral) 5 x 5 blur of it, the taps rt_style_watercolour_bleed apart;
//   3. (full-screen) per pixel: the paint read at a place moved by a flow field (value noise fixed to the screen - the
//      paper doesn't move, nothing shimmers), darkened where its lightness changes (a wet edge: the pigment's density
//      up), granulated by the paper's texture (value noise at two scales), the highlights washed out to the paper's
//      colour, and a light graphite line where the paint's edges are (read from a place moved the other way - drawn by
//      hand, not traced).
// Each style's processor twin (*_reference below) is the check's reference: it takes the same LookConstants the pass
// is given.
namespace render::vulkan {

struct Backend;
struct Image;


// What a pass is given: the viewport's rectangle in the target and the style's values (each style's own layout - its
// file's header comment); also the reference's input. 128 bytes: the push constants as the shaders declare them.
inline constexpr std::size_t kLookVectors = 6; // (a - f)
struct LookConstants {
    std::array<int32_t, 4> rect{}; // x, y, width, height
    std::array<int32_t, 4> info{};
    std::array<std::array<float, 4>, kLookVectors> values{};
};

// The style's constants for a viewport of `shown` (x, y, width, height) - the vhs's at game time `seconds`.
[[nodiscard]] LookConstants vhs_constants(const VhsSettings& settings, float strength,
                                          const std::array<uint32_t, 4>& shown, double seconds);
[[nodiscard]] LookConstants retro_constants(const RetroSettings& settings, float strength,
                                            const std::array<uint32_t, 4>& shown);
[[nodiscard]] LookConstants watercolour_constants(const WatercolourSettings& settings, float strength,
                                                  const std::array<uint32_t, 4>& shown);

// The processor twins over a viewport's pixels (RGB, row by row, `size` the constants' width and height): the pixels as
// the pass writes them. The watercolour's `paint` (not null): the pass's own half-resolution paint (read back) used
// instead of the twin's - the composite alone compared.
using LookRgb = std::array<float, 3>;
[[nodiscard]] std::vector<LookRgb> vhs_reference(const LookConstants& constants, const std::vector<LookRgb>& colour);
[[nodiscard]] std::vector<LookRgb> retro_reference(const LookConstants& constants, const std::vector<LookRgb>& colour);
[[nodiscard]] std::vector<LookRgb> watercolour_paint(const LookConstants& constants, const std::vector<LookRgb>& colour);
[[nodiscard]] std::vector<LookRgb> watercolour_reference(const LookConstants& constants, const std::vector<LookRgb>& colour,
                                                         const std::vector<LookRgb>* paint);

// The colour's place for a view: the target's image and the viewport's rectangle in it (vulkan_style.cpp's plan).
struct LookTarget {
    Image* colour = nullptr;
    std::array<uint32_t, 4> shown{};
};
// The style's passes over the target (the caller holds the lock; vulkan_style.cpp's record_style_pass). False (nothing
// recorded): not one of these styles, strength 0, the passes can't be made (logged once).
bool record_look_pass(Backend& state, const rt::ViewDrawn& view, const StyleSettings& settings, const LookTarget& target);
[[nodiscard]] uint64_t look_passes();
// The last watercolour pass's paint read back (RGBA16F: four 16-bit floats a texel, the image's whole size); false when
// there is none. The caller holds the lock and has finished the frame's work.
bool read_look_paint(Backend& state, std::vector<uint16_t>* halves, Extent* size);

// The retro geometry: whether the draws now take VertexKey::retro (the style is retro and a view's world is being drawn:
// from its view_begins to its world_drawn), and VertexUniforms::retro for a viewport of `width` x `height`.
[[nodiscard]] bool retro_geometry();
// VertexKey::retro for a draw of vertex format `fvf` now: 1 while retro_geometry() and not pre-transformed, else 0.
[[nodiscard]] uint8_t retro_key_bit(uint32_t fvf);
[[nodiscard]] std::array<float, 4> retro_vertex_uniform(Extent viewport);
// The grid's cells across and down a viewport (the post pass's and the snapping's).
[[nodiscard]] std::array<int32_t, 2> retro_cells(float lines, Extent viewport);

// The passes' objects released (the device's shutdown; the caller holds the lock).
void release_looks(Backend& state);

} // namespace render::vulkan
