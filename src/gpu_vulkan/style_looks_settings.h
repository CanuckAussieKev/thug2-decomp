#pragma once

#include <cstdint>

// NOT ORIGINAL - the vhs, retro and watercolour art styles' settings ([Graphics] rt_style and rt_style_<style>_*), apart
// from their passes (vulkan_style_looks.h) so that the settings' readers see no Vulkan header.
namespace render::vulkan {

// rt_style's places of these styles in its name list (off, toon, comic, sketch, vhs, retro, watercolour, noir, thermal).
inline constexpr unsigned kLookVhs = 4;
inline constexpr unsigned kLookRetro = 5;
inline constexpr unsigned kLookWatercolour = 6;
[[nodiscard]] constexpr bool is_look(unsigned style)
{
    return style == kLookVhs || style == kLookRetro || style == kLookWatercolour;
}

// The knobs (their defaults; the ranges style_*_constants clamps them to).
inline constexpr float kVhsFisheye = 0.35F;   // 0 - 1
inline constexpr float kVhsScanlines = 0.5F;  // 0 - 1
inline constexpr float kVhsSaturation = 0.7F; // 0 - 2
struct VhsSettings {
    float fisheye = kVhsFisheye;       // rt_style_vhs_fisheye: the barrel distortion (0: none)
    float bleed = 1.0F;                // rt_style_vhs_bleed: the chroma's smear and fringe (0 - 2)
    float wobble = 1.0F;               // rt_style_vhs_wobble: line jitter, bend and the tracking band's tear (0 - 2)
    float noise = 1.0F;                // rt_style_vhs_noise: the grain (0 - 2)
    float scanlines = kVhsScanlines;   // rt_style_vhs_scanlines
    float saturation = kVhsSaturation; // rt_style_vhs_saturation: the chroma x this
    bool overlay = true;               // rt_style_vhs_overlay: REC and the tape counter
};
inline constexpr float kRetroLines = 240.0F; // 60 - 720
inline constexpr float kRetroBits = 5.0F;    // 2 - 8 a channel
struct RetroSettings {
    float lines = kRetroLines; // rt_style_retro_lines: the low-resolution picture's rows
    float bits = kRetroBits;   // rt_style_retro_bits
    float dither = 1.0F;       // rt_style_retro_dither (0 - 1)
    float snap = 1.0F;         // rt_style_retro_snap: how far the vertices move to the grid (0 - 1)
    float affine = 1.0F;       // rt_style_retro_affine: 0 perspective-correct, 1 affine (0 - 1)
};
inline constexpr float kWatercolourPaper = 0.6F;    // 0 - 1
inline constexpr float kWatercolourOutlines = 0.35F; // 0 - 1
struct WatercolourSettings {
    float bleed = 1.0F;                    // rt_style_watercolour_bleed: the blur's reach (0 - 2; 0: the half image)
    float flow = 1.0F;                     // rt_style_watercolour_flow: the paint moved off its place (0 - 2)
    float granulation = 1.0F;              // rt_style_watercolour_granulation (0 - 2)
    float edges = 1.0F;                    // rt_style_watercolour_edges: the wet edges' darkening (0 - 2)
    float paper = kWatercolourPaper;       // rt_style_watercolour_paper: highlights washed out to the paper
    float outlines = kWatercolourOutlines; // rt_style_watercolour_outlines: the graphite line
};
struct LookSettings {
    unsigned style = 0; // rt_style (as vulkan_style.h's StyleSettings::style): the retro geometry is on with kLookRetro
    VhsSettings vhs;
    RetroSettings retro;
    WatercolourSettings watercolour;
};
void set_look_settings(const LookSettings& settings);
[[nodiscard]] LookSettings look_settings();

} // namespace render::vulkan
