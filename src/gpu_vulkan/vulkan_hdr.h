#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - the linear frame, its exposure, tone mapping and colour grading (docs/design/vulkan.md "HDR" and "A
// linear, scene-referred frame" - round F; gpu_vulkan.h's HdrSettings). Independent of ray tracing.
//
// The game's Vulkan frame (HdrSettings::on - always, rt_hdr retired; a device that draws, blends, samples and blits
// RGBA16F - every desktop one) is linear, scene-referred light in the sky's unit (rt_sky.h: 1 a white diffuser under the
// reference day) in 16-bit floats (kHdrFormat): the back buffer - made so here, its format still reported to the game
// as the settings' 8-bit one -, and what render_pc asks for through rt::world_target_format (the full-size screen
// copies of the blur and the bloom, [Graphics] msaa's scene surface, the game view's picture). From a view's beginning
// to this pass (Backend::linear_phase) the game's draws decode their display-referred colours to linear light at the
// fragment's output (ff_shaders.cpp fragment_linear_output, engine/gfx/rt_display.h), so every blend composes light;
// the effects add and multiply linear light; nothing is clipped. Then, at rt::world_view_drawn
// (rt::ViewOrder::kToneMapping: after the upscaler, the camera's blur and depth of field and the lens, before the art
// styles and the grade), one full-screen pass over the viewport (where it is shown, once scaled up) maps the bound
// colour target to the display in place - the one encode: the target's viewport copied out, read back through the
// curve below and written over itself (colour only - alpha kept). The 2D is drawn after in the display's colours, as
// the original's; the present blits the float back buffer into the swapchain (the conversion clamps to 0 - 1);
// read-backs out of it (screenshots, the front buffer) and copies into 8-bit surfaces are converted
// (read_hdr_as_bgra8, resolve_for_copy).
//
// The curve, per pixel (the colour linear light; the display's preferences act on its encoded value):
//   1. not a number: 0; below 0: 0; above 65504: 65504
//   2. exposure:   c x 2^rt_exposure x the meter's 2^stops (below), in linear light
//   3. the display's encoding: c^(1 / 2.2) (rt_display.h display_from_linear) - the original's white 1 shows as 1
//   4. saturation: L + (c - L) x rt_saturation, L = Rec. 709 luma; then at least 0
//   5. contrast:   0.5 x (c / 0.5)^rt_contrast   (a power about mid-grey)
//   6. the tone curve, per channel:
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
//   7. the grade LUT (rt_grade_lut, a .cube 3D table): trilinear, inputs mapped through its DOMAIN_MIN / MAX
//   8. clamped to 0 - 1
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
    // (round S) the frame's pre-exposure the colour carries
    float pre_exposure = 1.0F;
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

// (round L - a camera, not an eye: the user, 2026-11-17) What is shown is what a camera records: no rod vision (no
// scotopic desaturation, no Purkinje blue - round S's eye step is gone), its colours the sensor's at every light level.
// White balance: a fixed daylight white (D65 - the frame's linear Rec. 709 primaries' own white, as a camera set to
// "daylight"): no adaptation - moonlight (red 1.17 / blue 0.73 of the sun's: rt_daylight.h moon_irradiance) records
// slightly warm, sodium lamps orange, as a daylight-balanced sensor shows them.
// The curve on the processor (the pass's reference): an RGB colour of the target -> what the pass writes.
[[nodiscard]] std::array<float, 3> tone_map_reference(const ToneParameters& parameters, const GradeLut* lut,
                                                      std::array<float, 3> colour);

// (added by the low sun's glare; rewritten by the physical air) The metering - the camera's exposure, the one
// controller (rt_exposure_adapt: how many stops it may move either way; 0: off). The frame is linear light in the sky's
// unit (rt_sky.h: 1 a white diffuser under the reference day; round F). A camera meters
// the light of the view and sets its exposure so the view's average lands where the sensor's calibration puts it:
//   - the meter: kMeterGrid x kMeterGrid samples over the viewport (meter_place), their linear luminance (Rec. 709 luma of
//     the colour as drawn - the user's rt_exposure is an offset after it, not metered), each at most
//     kMeterBrightest (the sun's disc, a few pixels at thousands, doesn't swing it), weighted toward the centre
//     (meter_weight: a Gaussian kMeterCentre of the viewport wide), their weighted geometric mean (the log average - a
//     camera's average metering: a small bright part of a view counts for its share, a sky or a low sun's glow for
//     what it covers);
//   - the target: the reflected-light meter's calibration (ISO 2720, K = 12.5, the saturation-based exposure of a
//     sensor of speed S: H = 1 / (1.2 x 2^EV100), EV100 = log2(L x S / K)) puts a metered average L at L x 100 / 12.5 /
//     (1.2 x 8 x L) of the sensor's white - kMeterKey, about 0.104 -, scaled by the eye's adaptation (meter_key: the
//     key over its daylight value - the scene key a photographer exposes a dim scene darker by (Krawczyk et al.'s
//     1.03 - 2 / (2 + log10(L + 1)) of the absolute luminance - the unit calibrated by the sun's illuminance at the
//     atmosphere's top, kSolarIlluminance: a dim night is not brought up to day); stops = log2(key / L) - (round L) within
//     the camera's range (camera_stops_range: the exposure values its aperture, shutter and sensitivity reach) and
//     +-rt_exposure_adapt (meter_target_stops) - both ways: a bright view down, a dark one up;
//   - over time as a camera's auto exposure: kMeterDarkenSeconds toward darker, kMeterBrightenSeconds back (1 - 1/e of
//     the way; meter_adapt), a new viewport starting at its target;
//   - the factor on the light: 2^stops, x rt_exposure's 2^stops.
// The device keeps it: a compute pass over the viewport's copy writes the slot, the curve's pass reads it - no read-back.
// A path tracer's camera takes the same: its radiance x 2^stops, then this curve.
inline constexpr uint32_t kMeterSide = 16;    // (the group's side)
inline constexpr uint32_t kMeterEach = 4;     // (each invocation's samples a side)
inline constexpr uint32_t kMeterGrid = kMeterSide * kMeterEach;
inline constexpr float kMeterKey = 0.104F;    // the metered average's place (of the sensor's white): 100 / 12.5 / 9.6
inline constexpr float kMeterBrightest = 16.0F;
inline constexpr float kMeterLeast = 1e-4F;   // (a sample's luminance at least: the log average's floor)
inline constexpr double kSolarIlluminance = rt::kSolarIlluminance; // (lux: the unit's calibration - rt_sky.h)
inline constexpr float kMeterCentre = 0.3F;
// (round L) the camera's range - the exposure values (EV100 = log2(N^2 / t) - log2(S / 100)) its settings reach: from
// kCameraLeastEv100 - f/1.4, 1/30 s (a 30 fps frame's longest shutter), ISO 102 400 (a low-light video camera's top native
// sensitivity, e.g. Sony's alpha 7S III): log2(1.96 x 30) - log2(1024) = -4.1 - to kCameraMostEv100 - f/16, 1/8000 s,
// ISO 100: log2(256 x 8000) = 21.0. A scene darker than the least is recorded darker (the meter can't open further), one
// brighter than the most brighter. The EV a factor stands at: the reflected-light calibration's (ISO 2720, K 12.5) -
// the luminance it puts at kMeterKey, L, is EV100 = log2(L x 100 / K) (camera_ev100). rt_exposure_adapt: on / off (off -
// a fixed camera at the reference day's exposure); rt_exposure the player's exposure compensation after it.
inline constexpr float kCameraLeastEv100 = -4.1F;
inline constexpr float kCameraMostEv100 = 21.0F;
inline constexpr float kMeterMostStops = 40.0F;
[[nodiscard]] float camera_ev100(float stops);
// The stops (least, most) the camera's range allows.
[[nodiscard]] std::array<float, 2> camera_stops_range();
inline constexpr float kMeterDarkenSeconds = 0.4F;
inline constexpr float kMeterBrightenSeconds = 1.2F;
inline constexpr uint32_t kMeterSlots = 4;
// The readout's luminance histogram (the log's percentiles - what a scene's parts measure: the moonlit ground, the
// lamps' pools, the lamps themselves): kMeterBins bins of 1 / kMeterBinsPerStop stop from 2^kMeterLowestStop cd/m^2
// (2^-24 - 2^24: starlight's floor to a bulb's filament).
inline constexpr int kMeterBinsPerStop = 4;
inline constexpr int kMeterLowestStop = -24;
inline constexpr int kMeterBins = 48 * kMeterBinsPerStop;
// A sample's place in the viewport (0 - 1 across, down) for grid index (i, j); the pixel is rect origin + floor(place x
// size).
[[nodiscard]] std::array<float, 2> meter_place(uint32_t i, uint32_t j);
[[nodiscard]] float meter_weight(const std::array<float, 2>& place);
// A colour (linear light, already x the exposure's factor) as the meter counts it.
[[nodiscard]] float meter_light(const std::array<float, 3>& colour);
// The key for a metered average (linear): kMeterKey x the eye's adaptation over its daylight value.
[[nodiscard]] float meter_key(float mean);
[[nodiscard]] float meter_target_stops(float mean, float most_stops);
[[nodiscard]] float meter_adapt(float now_stops, float target_stops, float seconds);
// The meter's state of the viewport `rect` of the target (read back: the check's) - false when it has none.
struct MeterReading {
    float factor = 1.0F;
    float stops = 0.0F;
    float mean = 0.0F;
    float target = 0.0F;
    // (the readout's luminance - cd/m^2: the samples' median, 95th and 99.5th percentiles, each its histogram bin's
    // middle; w (round L) the EV100 the camera stands at)
    std::array<float, 4> luminance{};
};
bool read_meter(Backend& state, const std::array<uint32_t, 4>& rect, MeterReading* reading);

// (path tracing, stage 1 - P5) The manual mode: the camera set by hand to `ev100` - no metering, no adaptation; the light
// x exposure_at_ev100 (the factor the meter's slot would hold at that EV: 2^(camera_ev100(0) - ev100), the same ISO 2720
// calibration), then rt_exposure's offset and the curve as always. What a path tracer's reference and the realtime frame
// are compared at. Logged (with the EV100) when it changes. The caller of either holds the lock or none (an atomic state).
void set_manual_ev100(bool on, float ev100);
struct ManualExposure {
    bool on = false;
    float ev100 = 0.0F;
};
[[nodiscard]] ManualExposure manual_exposure();
[[nodiscard]] float exposure_at_ev100(float ev100);
// The EV100 the last metered viewport's camera stood at (the meter's readout, a frame or two behind - the factor it
// held: camera_ev100 of its stops); false before any (no meter, or none read yet). The caller holds the lock.
bool last_metered_ev100(Backend& state, float* ev100);
// (path tracing, stage 1) The curve's parameters and grading table in use (the pass's - null: no table), for a picture
// mapped on the processor as the pass maps the frame (tone_map_reference). The caller holds the lock.
[[nodiscard]] ToneParameters tone_parameters_locked();
[[nodiscard]] const GradeLut* grade_lut_locked();

// Whether the device's targets are made floating point - the linear frame: HdrSettings::on (the game's: always) and the
// device draws, blends, samples and blits kHdrFormat (create_device_objects asks, before the back buffer is made; it
// then sets rt::set_hdr_colour).
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
