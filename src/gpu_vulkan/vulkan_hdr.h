#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - HDR rendering, tone mapping and colour grading (docs/design/vulkan.md "HDR"; [Graphics] rt_hdr -
// gpu_vulkan.h's HdrSettings). Independent of ray tracing.
//
// With rt_hdr=1 (and a device that draws, blends, samples and blits RGBA16F - every desktop one) the targets the world
// is drawn into hold 16-bit floats (kHdrFormat): the back buffer - made so here, its format still reported to the game
// as the settings' 8-bit one -, and what render_pc asks for through rt::world_target_format (the full-size screen
// copies of the blur and the bloom, [Graphics] msaa's scene surface, the game view's picture). Draws into them aren't
// clipped at 1, so light added by the effects (R4 / R5's passes, one-bounce light at opaque_drawn, fog at world_drawn)
// keeps its range. Then, last at rt::world_view_drawn (rt::ViewOrder::kToneMapping), one full-screen pass over the
// viewport maps the bound colour target back to 0 - 1 in place: the target's viewport copied out, read back through
// the curve below and written over itself (colour only - alpha kept). The 2D is drawn after, unchanged; the present
// blits the float back buffer into the swapchain (the conversion clamps to 0 - 1); read-backs out of it (screenshots,
// the front buffer) and copies into 8-bit surfaces are converted (read_hdr_as_bgra8, resolve_for_copy).
//
// The curve, per pixel (the colours are the game's own, display-referred: 1 is the original's white):
//   1. not a number: 0; below 0: 0; above 65504: 65504
//   2. exposure:   c x 2^(rt_exposure / 2.2)      (stops of linear light, the colours taken as gamma 2.2)
//   3. saturation: L + (c - L) x rt_saturation, L = Rec. 709 luma; then at least 0
//   4. contrast:   0.5 x (c / 0.5)^rt_contrast   (a power about mid-grey)
//   5. the tone curve, per channel:
//      rt_tonemap=0 - the colour unchanged up to a knee k, then a soft exponential shoulder to the white point w:
//          y = c                              c <= k
//          y = 1 - d x exp(-(c - k) / d)      c > k, d = 1 - k
//        k is chosen so that w is shown within half a step of white (1 - d x exp(-(w - k) / d) = 1 - 0.5 / 255); the
//        slope is 1 at the knee (no visible crease). w = 1: no shoulder, everything above 1 clipped - exactly the
//        original's 8-bit targets. The default w = 1.25 puts the knee at ~0.91: what the original drew below it is
//        unchanged, its white shows as ~0.967 (8 steps darker), and values up to 1.25 keep their differences.
//      rt_tonemap=1 - ACES (Krzysztof Narkowicz's fit of the ACES reference rendering and output transforms):
//          x = c^2.2,  y = (x (2.51 x + 0.03)) / (x (2.43 x + 0.59) + 0.14) / that of w^2.2,  c' = y^(1 / 2.2)
//        a filmic look - deeper shadows, brighter mid-tones, w (1 - 8) shown as white.
//   6. the grade LUT (rt_grade_lut, a .cube 3D table): trilinear, inputs mapped through its DOMAIN_MIN / MAX
//   7. clamped to 0 - 1
// Multisampled targets: each sample through the curve, their mean written to all of the pixel's samples (so the
// resolve gives the mean of the mapped samples - edges smoothed after the curve, as they would be on a display); no
// sample-rate shading needed.
namespace render::vulkan {

struct Backend;
struct Image;

inline constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// The numbers the pass is given, worked out from the settings (also the check's reference).
struct ToneParameters {
    float scale = 1.0F;      // the exposure's factor
    float saturation = 1.0F;
    float contrast = 1.0F;
    uint32_t curve = 0;      // rt_tonemap
    float knee = 1.0F;       // curve 0: where the shoulder starts
    float room = 0.0F;       // curve 0: 1 - knee (0: clipped at 1)
    float aces_scale = 1.0F; // curve 1: 1 / the curve's value at the white point
};
[[nodiscard]] ToneParameters tone_parameters(const HdrSettings& settings);

// A 3D colour table (a .cube file): size^3 entries, red the fastest; inputs mapped from domain_min - domain_max.
struct GradeLut {
    uint32_t size = 0; // 0: none
    std::array<float, 3> domain_min{0.0F, 0.0F, 0.0F};
    std::array<float, 3> domain_max{1.0F, 1.0F, 1.0F};
    std::vector<std::array<float, 3>> entries;
};
// A .cube file's text read into `lut` (LUT_3D_SIZE 2 - 128, DOMAIN_MIN / MAX, TITLE and comments allowed; a 1D table
// refused); false with the reason.
bool parse_cube(std::string_view text, GradeLut* lut, std::string* error);

// The grading table used from now on, in place of rt_grade_lut's (the check's; set_hdr_settings reads the file's).
void set_hdr_lut(GradeLut lut);

// The curve on the processor (the pass's reference): an RGB colour of the target -> what the pass writes.
[[nodiscard]] std::array<float, 3> tone_map_reference(const ToneParameters& parameters, const GradeLut* lut,
                                                      std::array<float, 3> colour);

// Whether the device's targets are made floating point: rt_hdr=1 and the device draws, blends, samples and blits
// kHdrFormat (create_device_objects asks, before the back buffer is made; it then sets rt::set_hdr_colour).
[[nodiscard]] bool hdr_wanted(const Backend& state);

// The pass over the bound colour target's viewport (the listener's work at rt::world_view_drawn; also the check's).
// The caller holds the lock. False (nothing recorded): off, the bound target isn't kHdrFormat (an 8-bit picture - the
// UI's previews - is left alone), the pass can't be made (logged once).
bool record_tone_map_pass(Backend& state, const rt::ViewDrawn& view);
[[nodiscard]] uint64_t tone_map_passes();

// A level of a kHdrFormat image read back as B8G8R8A8 (each channel clamped to 0 - 1 and rounded to the nearest of 255
// steps, as the present's blit converts) - the back buffer's read-backs, whose surface says A8R8G8B8. The caller holds
// the lock and has finished the frame's work.
bool read_hdr_as_bgra8(Backend& state, Image* image, Extent extent, std::vector<uint8_t>* bgra);

// A multisampled image resolved into an image of its own format and size kept here (recorded in `commands`), for a
// copy into a surface of another format (a float back buffer into an 8-bit render target: the screenshot's); null when
// it can't be made.
Image* resolve_for_copy(Backend& state, VkCommandBuffer commands, Image* multisampled);

// The pass's objects released (the device's shutdown; the caller holds the lock).
void release_hdr(Backend& state);

} // namespace render::vulkan
