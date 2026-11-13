#pragma once

// NOT ORIGINAL - what a level's own sky looks like, read from its backdrop (rt_scene.h's SkyCapture: render_pc draws the
// sky scene's dome from the origin into six square faces once per level) - so the renderer's own sky ([Graphics] rt_sky:
// gpu_vulkan's vulkan_rt_sky_pass) keeps each level's mood: its colours toward the ground, the horizon, half way up and
// overhead, how much of it is cloud and what colour the clouds are.
//
// Each texel is a direction (its face's centre direction + the texel's place across and up) and a solid angle (a cube
// face's texel: 1 / (1 + a^2 + b^2)^1.5 of the face's centre one). A texel the backdrop drew nothing on (the clear colour
// exactly) is left out. Its cloudiness c is how grey it is: c = 1 - (saturation - kCloudGrey) / (kSkyBlue - kCloudGrey)
// clamped to 0 - 1, the saturation (max - min) / max of the colour - a painted sky's blue is saturated, its clouds and
// haze white or grey. In each band of elevation:
//   - the clear sky's colour: the texels' mean weighted by 1 - c (+ kClearFloor: a band of nothing but cloud still
//     gives its mean);
//   - everything's mean (clouds included);
//   - the share of the band the backdrop covers;
// over the upper sky (kCoverageLowest - kCoverageHighest degrees) the clouds' share (the mean c) and their colour (the
// mean weighted by c^2); and the mean luma above the horizon.
#include <array>
#include <cstdint>
#include <span>

namespace render::rt {

using Colour = std::array<float, 3>; // display-referred, 0 - 1

// The bands, low to high: just below the horizon (what the dome paints there: a sea, the far ground - analysed and logged,
// never matched: the renderer's sky shows the horizon's haze below it), the horizon (from just above it: a ring painted
// below it can't bleed in), half way up, overhead.
enum class SkyBand : uint8_t { kGround = 0, kHorizon = 1, kMiddle = 2, kZenith = 3 };
inline constexpr std::size_t kSkyBands = 4;
// Each band's elevations (degrees) - low, high - and the elevation its colour is placed at (the sky's gains are
// interpolated between these).
struct BandRange {
    double low = 0.0;
    double high = 0.0;
    double centre = 0.0;
};
inline constexpr std::array<BandRange, kSkyBands> kSkyBandRanges{{
    {.low = -8.0, .high = -0.5, .centre = -3.0},
    {.low = 0.5, .high = 10.0, .centre = 4.0},
    {.low = 15.0, .high = 45.0, .centre = 30.0},
    {.low = 55.0, .high = 90.0, .centre = 72.0},
}};
inline constexpr double kCloudGrey = 0.10; // saturation at or under this: all cloud
inline constexpr double kSkyBlue = 0.32;   // at or over this: all clear sky
inline constexpr double kClearFloor = 0.02;
inline constexpr double kCoverageLowest = 3.0; // degrees (the sky a player sees most)
inline constexpr double kCoverageHighest = 35.0;

// The capture as bytes: 3 x 2 faces of face_size, 4 bytes a texel, rows top down.
struct SkyCaptureTexels {
    std::span<const uint8_t> bytes;
    uint32_t face_size = 0;
    bool bgra = true;               // B, G, R, A in memory (Direct3D's A8R8G8B8); else R, G, B, A
    std::array<uint8_t, 3> clear{}; // the clear colour's bytes (R, G, B): texels of exactly it are left out
};

struct SkyLook {
    bool valid = false;
    std::array<Colour, kSkyBands> clear{};
    std::array<Colour, kSkyBands> all{};
    std::array<float, kSkyBands> covered{}; // the share of the band's solid angle the backdrop drew
    float coverage = 0.0F;                  // the clouds' share of the upper sky (0 - 1)
    Colour cloud{1.0F, 1.0F, 1.0F};         // the clouds' colour
    float brightness = 0.0F;                // mean luma (display) above the horizon
};

// A texel's direction (world, y up; normalised) and its solid angle relative to a face's centre texel.
struct CaptureTexel {
    std::array<double, 3> direction{};
    double weight = 0.0;
};
struct CaptureSpot {
    uint32_t face = 0;
    uint32_t x = 0; // in the face, from its left
    uint32_t y = 0; // from its top
};
[[nodiscard]] CaptureTexel capture_texel(const CaptureSpot& spot, uint32_t face_size);
// A colour's cloudiness (0: clear sky, 1: cloud).
[[nodiscard]] double cloudiness(const Colour& colour);

[[nodiscard]] SkyLook analyse_sky(const SkyCaptureTexels& capture);

} // namespace render::rt
