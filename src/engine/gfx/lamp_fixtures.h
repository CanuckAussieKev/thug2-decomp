#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// NOT ORIGINAL - (round L) a lamp's light from a physical model of its fixture, where the relit bake can't measure it
// (docs/design/vulkan.md "Round L: lamps by physics").
//
// The bake tells a lamp's light from its pool in the bake's own colours (rt_relit.h solve_relit_lamps) - only when the
// lamp was on in the bake and outshone the rest. A street lamp in an afternoon bake was off: nothing to measure. Its
// light is then what a real fixture of its kind sends: the emitter's own luminous area A (its emissive triangles - a
// lamp head's lens, a tube, a panel, a light card's saturated core) x the luminance of its kind's luminous surface L,
// a Lambertian emitter's flux Phi = pi L A. A solved light whose surface luminance Phi / (pi A) is outside its kind's
// physical range is a failed measurement (a pool's size can't be told from its brightness alone): the model replaces it.
//
// The luminous area (lamp_fixtures.cpp's table, per kind) is checked too: a candidate or a glow whose luminous area is no
// one fixture's of its kind is no lamp (a lit wall, a band of white paint, a haze card).
// The kind from the lamp's own data: its colour (a saturated hue - a sign's or a neon tube's -, else its correlated
// colour temperature: sodium's ~2000 K, a filament's ~2800 K, white discharge / LED 3500 K+), its shape (a point, a
// line - a tube -, a panel and which way it faces) and where it is (roofed - an interior light - or outdoors).
//
// The luminances (cd/m^2, the luminous surface's mean) - typical values and ranges. Written from the sources' published
// figures as remembered (no network access to re-check page numbers; to be verified like round M's material table):
//   - high-pressure sodium street lights (cobra heads, lanterns): SON / HPS lamps of 70 - 400 W give 6 000 - 55 000 lm
//     (lamp makers' catalogues, IEC 60662 ratings); a luminaire sends ~0.75 of it through a 0.05 - 0.2 m^2 refractor or
//     flat glass: 10^4 - 10^5 cd/m^2 over the lens (the bare arc tube 10^6 - 10^7);
//   - white street / area lights (metal halide, mercury, LED): 5 000 - 40 000 lm through similar optics; LED arrays'
//     lenses up to 10^6 cd/m^2 (IES RP-8 / the CIE 115 road-lighting context; makers' photometry);
//   - fluorescent tubes: T12 ~ 7 000, T8 ~ 11 000, T5 HO ~ 20 000 cd/m^2 (IES Lighting Handbook lamp tables; Philips
//     TL-D / TL5 datasheets);
//   - ceiling panels (troffers with opal or prismatic diffusers, LED panels): 1 000 - 20 000 cd/m^2; an office LED
//     panel 3 600 lm over 0.36 m^2 ~ 3 200 cd/m^2; EN 12464-1's glare limits 1 500 - 3 000 cd/m^2 above 65 degrees;
//   - bulbs (incandescent / compact, frosted or opal): a frosted 60 W bulb's 800 lm over a 60 mm sphere -
//     Phi / (pi 4 pi r^2) ~ 2.3 x 10^4 cd/m^2; opal globes 5 000 - 20 000; clear filaments ~ 10^6 - 10^7;
//   - neon and cold-cathode sign tubes: ~ 1 000 - 3 000 cd/m^2 for red neon, a few hundred for the argon-mercury
//     blues and greens (sign-industry figures);
//   - back-lit signs and lit windows: CIE 150's limits for illuminated signs 400 - 1 000 cd/m^2 by zone; a lit room seen
//     through a window ~ 50 - 500 cd/m^2.
namespace render::rt {

enum class LampFixture : uint8_t {
    kUnknown = 0,
    kSodiumStreet = 1, // high-pressure sodium street / area light
    kWhiteStreet = 2,  // metal halide, mercury or LED street / area light
    kTube = 3,         // a fluorescent tube
    kPanel = 4,        // a ceiling panel - a troffer, an LED panel
    kBulb = 5,         // an incandescent / compact bulb or a globe lantern
    kNeon = 6,         // a neon / cold-cathode sign tube (a saturated colour)
    kSign = 7,         // a back-lit sign or a lit window (a vertical panel)
};
inline constexpr std::size_t kLampFixtures = 8;

struct FixtureValues {
    std::string_view name;
    float luminance = 0.0F; // cd/m^2, typical
    float least = 0.0F;     // ... the kind's physical range
    float most = 0.0F;
    float kelvin = 0.0F;    // its light's colour temperature (0: its colour is its own - a sign's, a neon's)
    // (its luminous surface's area, m^2: one fixture's - a street light's lens 0.01 - 0.5 (IES cobra heads, post-top
    // lanterns), a tube or a twin-tube fitting 0.005 - 0.6 (T5 - T12, 0.6 - 2.4 m), a ceiling panel 0.05 - 1.5 (a 2 x 4 ft
    // troffer 0.74), a bulb or globe 0.002 - 0.3 (an A19's 0.011, a 40 cm globe's 0.5 / 2 seen), a sign's tubing up to
    // 2, a back-lit sign or a shop window up to 20. A luminous area outside its kind's is no one fixture of that kind.)
    float least_area = 0.0F;
    float most_area = 0.0F;
};
[[nodiscard]] const FixtureValues& fixture_values(LampFixture fixture);
[[nodiscard]] std::string_view fixture_name(LampFixture fixture);

// What the lamp's own data says of it.
struct FixtureEvidence {
    uint8_t shape = 0;                     // rt_lights.h EmitterShape (point 0, panel 1, line 2)
    std::array<float, 3> axis{0.0F, 1.0F, 0.0F}; // a panel's normal, a line's direction
    std::array<float, 3> colour{};         // its light's colour (linear; any scale)
    bool indoors = false;                  // roofed: its surfaces see no sky (an interior light)
};
// A colour's correlated colour temperature (McCamy's cubic over linear sRGB; 0: too far from a light's or dark) and
// whether it is saturated - not a light's colour: its chromaticity more than kLightDuv from the Planckian locus at its
// colour temperature (CIE 1960 uv; ANSI C78.377 holds white lamps within 0.006 of it, a high-pressure sodium lamp sits
// about +0.01 above it - a sign's or a neon's colour is far off it: a magenta card ~0.1 below), or no colour temperature
// at all. (A channel ratio can't tell: a sodium lamp's own blue is under a tenth of its red.)
inline constexpr float kLightDuv = 0.03F;
[[nodiscard]] float colour_kelvin(const std::array<float, 3>& colour);
[[nodiscard]] bool saturated_colour(const std::array<float, 3>& colour);
// The rules: a saturated colour - a neon tube (a point or a line) or a sign (a panel); a vertical panel - a sign or a lit
// window; indoors - a line a tube, a panel a ceiling panel, a point a bulb; outdoors - a line a tube (a canopy's), else by
// the colour temperature: under 2500 K sodium, under 3500 K a bulb or globe lantern, else a white street light.
inline constexpr float kVerticalPanel = 0.5F; // a panel whose normal's |y| is under this faces sideways
inline constexpr float kSodiumKelvin = 2500.0F;
inline constexpr float kFilamentKelvin = 3500.0F;
[[nodiscard]] LampFixture fixture_of(const FixtureEvidence& evidence);
// A blackbody's colour at `kelvin` as linear sRGB with luma 1 (the Planckian locus by Kim et al. 2002's cubic splines
// in CIE 1931 xy, 1667 - 25 000 K; Y 1).
[[nodiscard]] std::array<float, 3> blackbody_colour(float kelvin);

} // namespace render::rt
