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
// (The lamp solve check: a solved light is first put from the art's exposure into physical units - rt_relit.h
// kLeastExposureLamps: the bake's lamp-lit places are painted 10^2 - 10^3 x brighter than their lamps make them.)
//
// The luminous area (lamp_fixtures.cpp's table, per kind) is checked too: a candidate or a glow whose luminous area is no
// one fixture's of its kind is no lamp (a lit wall, a band of white paint, a haze card) - except a line whose luminous
// width is a fitting's: a row of fittings (a corridor's tubes, a sign's tubing) of any length.
// The kind from the lamp's own data: its colour (a saturated hue - a sign's or a neon tube's -, else its correlated
// colour temperature: sodium's ~2000 K, a filament's ~2800 K, white discharge / LED 3500 K+), its shape (a point, a
// line - a tube -, a panel and which way it faces) and where it is (roofed - an interior light - or outdoors).
//
// The figures (lamp_fixtures.cpp's table), checked against published sources (2026-11-17, the lamp solve check). A lamp's
// flux in lumens is the lamp makers' rated initial flux; a luminous surface's luminance L (cd/m^2, its mean) is from a
// published table where one exists, else derived as L = Phi / (pi A) from a rated flux and the lamp's own size (a
// Lambertian surface's exitance pi L over its area; a tube's area its surface, pi d l). The sources:
//   [F] City of Flagstaff (AZ) Zoning Code, Appendix 4 "Outdoor Lighting Reference Material": 4.040's lamp tables
//       (initial lumens by wattage) and 4.050 "Typical Lumen Outputs for 'Neon' Luminous Tube Lighting" (lm per foot by
//       colour, 12 / 15 mm tube at 30 / 60 mA);
//   [P] Philips / Signify lamp leaflets: MASTER SON-T PIA Plus (70 W 6 600 lm ... 400 W 55 800 lm; nominal CCT 2000 K,
//       1900 K at 50 W); CoreLine panel RC132V W60L60 (600 x 600 mm, 3 400 lm /830 - 3 600 lm /840);
//   [D] DIAL (DIALux) knowledge base, "Luminance L": fluorescent lamp 4 000 - 17 000, compact fluorescent 9 000 -
//       25 000, dull (frosted) incandescent 20 000 - 50 000 cd/m^2;
//   [W] Wikipedia "Orders of magnitude (luminance)" (its own sources: Mischler's lighting glossary, NVC Lighting, Hahn):
//       fluorescent lamp 12 000, frosted incandescent bulb 130 000, clear filament 7 x 10^6 cd/m^2;
//   [C] CIE 150:2003 (as tabled in Panasonic's sign-lighting guide): an illuminated sign's maximum mean luminance by
//       environmental zone - E1 50, E2 400, E3 800, E4 1 000 cd/m^2.
// Per kind (typical; range):
//   - high-pressure sodium street light: flux 9 500 lm (a 100 W clear HPS [F]); 2 250 (35 W [F]) - 55 800 (400 W PIA
//     Plus [P]); 2000 K [P]. Its lens's luminance derived: the lamp's flux x a luminaire's light output ratio over its
//     lens 0.01 - 0.5 m^2 (a cobra head's flat glass or refractor to a post-top lantern's panes): 9 500 x 0.7 / (pi 0.1)
//     = 21 000 cd/m^2; 1 000 - 1.2 x 10^6. (The ratio 0.7 is an estimate - no source found; enclosed road luminaires
//     send 0.6 - 0.8 of their lamp's flux.)
//   - white street light (metal halide, LED): flux 10 000 lm (a 140 W LED, 9 996 [F]); 3 400 (50 W pulse-start metal
//     halide, 63 W LED [F]) - 50 000 (450 W pulse-start metal halide [F]); 4000 K (an LED street light's or a /840
//     lamp's); the lens's luminance as above: 22 000; 1 500 - 1 x 10^6 (an LED array's optic).
//   - fluorescent tube: a T8 32 W's 3 100 lm [F] over 1.2 m x 26 mm (pi d l = 0.098 m^2): 10 000 cd/m^2 ([W] 12 000,
//     [D] 4 000 - 17 000); 4 000 [D] - 28 000 (a T5 HO 54 W's 5 000 lm over 1.15 m x 16 mm, derived); flux 1 190 (T12
//     21 W [F]) - 9 500 (T12 HO 110 W [F]); 4000 K (cool white); one tube's luminous area 0.005 - 0.6 m^2 (a 0.55 m T5 to
//     a 2.4 m T12 twin); a row of fittings' luminous width 0.015 (a bare T5, 16 mm) - 0.6 m (a 2 x 4 ft troffer), one
//     fitting at most 2.44 m long (an 8 ft T12 HO [F]).
//   - ceiling panel: a 600 x 600 LED panel's 3 600 lm over 0.36 m^2 [P]: 3 200 cd/m^2; 1 000 - 20 000 (derived: 1 000 -
//     10 000 lm panels over 0.05 - 1.5 m^2 - a troffer's prismatic lens brighter); flux 1 000 - 10 000; 4000 K [P].
//   - bulb or globe: a 60 W incandescent's 855 lm [F] over a 60 mm frosted sphere: 855 / (pi 4 pi 0.03^2) = 24 000 cd/m^2
//     ([D] 20 000 - 50 000); 5 000 (an opal globe, derived: 855 lm over a 0.2 m globe) - 200 000 ([W] 130 000 frosted);
//     flux 120 (15 W [F]) - 5 000 (250 W tungsten halogen [F]); 2800 K (an incandescent filament).
//   - neon / cold-cathode sign tube: a 15 mm clear red tube at 30 mA, 39 lm per foot [F] = 128 lm/m over pi d = 0.047
//     m^2/m: 870 cd/m^2; 45 (deep blue, 2 lm/ft [F]) - 18 000 (the brightest, 655 lm/ft [F], over a 12 mm tube); a sign's
//     tubing 1 - 10 m: flux 7 - 21 500, typically 3 m of red, 400 lm; its colour its own; a row's luminous width 0.006 -
//     0.15 m (one tube's diameter to a few strokes side by side - derived); one sign's tubing up to 10 m.
//   - back-lit sign or lit window: 400 cd/m^2 (E2 [C]); 50 (E1 [C]) - 1 000 (E4 [C]) - a lit room seen through a window,
//     500 lux on a 0.7 wall, 110 cd/m^2, within it; one sign's or window's area 0.02 - 20 m^2, so its flux pi L A: 3 -
//     63 000 lm, typically 1 m^2 at 400 cd/m^2, 1 300 lm; its colour its own.
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
    // (its luminous surface's area, m^2: one fixture's - the figures above. A point's or a panel's luminous area outside
    // its kind's is no one fixture of that kind; a line's is a row of fittings when its width is one's - below)
    float least_area = 0.0F;
    float most_area = 0.0F;
    // (one fixture's flux, lm: a typical one and the range lamp ratings give - the figures above. What a lamp with no
    // luminous area known - the level's own point lights, a card with no saturated core - and a solved lamp's flux in
    // physical units are checked against (x its fittings for a row); outside it its kind's typical fixture stands for it)
    float lumens = 0.0F;
    float least_lumens = 0.0F;
    float most_lumens = 0.0F;
    // (a line's: a row of fittings' luminous width, m - its area over its length; 0 for the kinds that are no line)
    float least_width = 0.0F;
    float most_width = 0.0F;
    float most_length = 0.0F; // (... and one fitting's longest, m: a row holds its length over this, at least 1)
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
