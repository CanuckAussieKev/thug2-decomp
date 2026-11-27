#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "gpu_vulkan/vulkan_style_film.h"

// NOT ORIGINAL - (round FA) the lighting's debug views ([debug] rt_debug_view, live: `set rt_debug_view <name>` on the
// console or the automation channel; docs/design/vulkan.md "The lighting's debug views"). As Unreal Engine 5's Lumen /
// path tracer visualisations and RTXDI's debug outputs do, one term of the frame's light at a time: every other term's
// producer writes nothing into the frame, so the picture - shaded, denoised by Ray Reconstruction, tone mapped as
// always - is that term alone (and the surfaces' own emission, which the shading adds and no producer carries). The
// exposure is held where the whole frame's meter had it when a view was chosen, so a term looks as bright as it is in
// the whole picture. Which term flickers is then measured with the term alone in the frame (tools/game/speck_count.ts on
// a captured series), or seen live with `flicker`.
//
// The terms and their producers (a view lit per surface, ReSTIR DI and GI - the path the game takes under DLSS RR):
//   lamps          ReSTIR DI's diffuse: the lamps' and emitters' direct light on the albedo (vulkan_restir_di.h)
//   lamps_specular ReSTIR DI's specular: the same samples through the surfaces' specular lobe
//   gi             ReSTIR GI's diffuse from surfaces: the bounce, with the radiance cache's further bounces
//   gi_specular    ReSTIR GI's specular from surfaces (its own GGX ray a pixel)
//   sky            the sun's or the moon's direct light (the shadows' visibility) and GI's samples that saw the sky
//   sun            (round PANES) the sun's or the moon's direct light alone: the disc's visibility the shading takes
//                  (`sky` less the sky's light - which of the two lights a pixel)
//   reflections    the reflections pass: the game's shine, glass and mirrors - its rays' hits lit by the light list
//   fog            the volumetric fog's in-scattered light (the light list's lamps and the sun in the air)
//   front          the front layer: the nearest see-through surface's light (vulkan_front_layer.h)
//   emission       none of the above: what the surfaces emit themselves (lamp heads, signs, lit windows)
//   flicker        the whole frame as a heat map of each pixel's temporal standard deviation over the last
//                  kFlickerFrames frames (display luma 0-255: black 0, red, yellow, white at kFlickerFull and more)
namespace render::vulkan {

struct Backend;

enum class DebugView : uint8_t {
    kOff,
    kLamps,
    kLampsSpecular,
    kGi,
    kGiSpecular,
    kSky,
    kReflections,
    kFog,
    kFront,
    kEmission,
    kFlicker,
    kSun, // (round PANES)
};

// The terms a view lets into the frame (bits).
namespace debug_term {
inline constexpr uint32_t kLampsDiffuse = 1U << 0U;
inline constexpr uint32_t kLampsSpecular = 1U << 1U;
inline constexpr uint32_t kGiDiffuse = 1U << 2U;
inline constexpr uint32_t kGiSpecular = 1U << 3U;
inline constexpr uint32_t kSky = 1U << 4U;
inline constexpr uint32_t kReflections = 1U << 5U;
inline constexpr uint32_t kFog = 1U << 6U;
inline constexpr uint32_t kFront = 1U << 7U;
inline constexpr uint32_t kSun = 1U << 8U; // (round PANES) the disc's direct light (the visibility); `sky` lets it in too
inline constexpr uint32_t kAll = 0x1FFU;
} // namespace debug_term

inline constexpr uint32_t kFlickerFrames = 60;   // the flicker view's window (a running mean and variance over it)
inline constexpr float kFlickerFull = 16.0F;     // its heat map's white: a standard deviation of 16 / 255 luma

// Its name ("off", "lamps", ...); and back (false: not one).
[[nodiscard]] const char* debug_view_name(DebugView view);
[[nodiscard]] bool parse_debug_view(std::string_view text, DebugView* view);
// Every name, " | " between them (the console's help).
[[nodiscard]] std::string debug_view_names();
// The terms `view` lets in: every one off and with `flicker`, the one named, none with `emission`.
[[nodiscard]] uint32_t debug_view_terms(DebugView view);

// The view now ([debug] rt_debug_view - the camera effects' settings set it: set_camera_fx_settings; any thread), and its
// terms - what every producer reads.
[[nodiscard]] DebugView debug_view();
[[nodiscard]] uint32_t debug_terms();
// The view put to use (logged when it changes); off gives the exposure back to the meter.
void set_debug_view(DebugView view);

// The view's frame kept (the caller holds the lock; once a frame, at a view's world_drawn): the exposure held at the
// whole frame's while a term view is on, given back when it goes; the change logged.
void keep_debug_view(Backend& state);
// (`flicker`) The heat map over the view's picture (after the grade, before frame generation), its history started
// again when the view starts or the picture's size changes. False: nothing drawn (another view, or the pass couldn't
// be made - logged once).
// `target` the view's picture (the camera effects' listener at rt::ViewOrder::kDebugView plans it).
bool record_flicker_view(Backend& state, const FilmTarget& target);
void release_debug_view(Backend& state);

} // namespace render::vulkan
