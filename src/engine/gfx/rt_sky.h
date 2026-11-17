#pragma once

// NOT ORIGINAL - the sky and the sun's colour from an atmosphere (ray tracing's sun: docs/design/vulkan.md "The sun,
// its glare and the sky"; [Graphics] rt_sky_model). THUG2's levels bake their sunlight into vertex colours and give the
// sun no colour; the baked sun (rt_sun.h) gives only its direction. From its elevation this model gives what a real
// atmosphere would: the sun's colour after the air it crosses (warm when low, white when high) and the sky's light in
// any direction (blue overhead, pale toward the horizon, bright round the sun) - the fog's aerial perspective and the
// sun's glare are coloured by it.
//
// The model: single scattering in a spherical atmosphere (the ground 6360 km from the centre, its top 6420 km), the
// viewer kViewerAltitude above the ground.
//   - Rayleigh (the air): scattering kRayleigh per metre at sea level (red, green, blue), its density falling as
//     exp(-h / kRayleighHeight); phase 3 / (16 pi) (1 + mu^2).
//   - Mie (aerosols, haze - changed by the physical air): the weather's aerosol (Aerosol below - [Graphics] rt_fog_air,
//     a visibility): scattering and extinction per metre at the ground for red, green and blue, its density exp(-h /
//     height) + mist x exp(-h / mist_height) (the haze's layer and, in a mist, a low layer of droplets under it); phase
//     Cornette-Shanks with its g. The same coefficients light and dim the level's air (the fog pass's aerial march) -
//     one medium, from the ground to the atmosphere's top.
//   - Ozone (added by the low sun's sky): absorption only, kOzoneAbsorption per metre where it is densest, its density a
//     tent kOzoneHalfWidth either side of kOzoneCentre (Hillaire's EGSR 2020 values). Overhead it takes 3% of the green,
//     little; along a low sun's long path it takes the orange and green out of the light that reaches the far sky - the
//     twilight sky stays blue overhead and the anti-solar sky's band at sunset turns pink rather than yellow.
//   - The sun's transmittance from a point toward it: exp(-optical depth) through all three to the atmosphere's top (0
//     when the ground is in the way).
//   - The sky's radiance along a view ray (per unit of the sun's irradiance): the integral over the ray of
//     T(viewer -> x) x (Rayleigh's and Mie's scattering at x x their phase) x the sun's transmittance at x.
// Both are integrals along rays: SkySteps says how many segments (their lengths growing with the square of the
// distance: the air is densest near the viewer); the GPU's table (gpu_vulkan's vulkan_rt_sky) is the same integral with
// few steps, and this file's with many is its reference.
//
// Colours: the integrals are linear light. What the game's passes add is display-referred (the game's colours, 1 its
// white): a linear value v shows as v^(1 / kDisplayGamma). (Changed by the physical air:) the unit is a physical one -
// sky_exposure() is pi / E, E the horizontal irradiance under the reference day (the sun kReferenceElevation up in the
// clear weather's air: its direct light on the ground and the whole sky's, per unit of the sun's irradiance at the
// atmosphere's top): linear 1 is the radiance of a white diffuser lit by that day - the game's white, the colours the
// levels were painted and baked with under their daylight. The sky, the sun's disc (its irradiance over its solid angle)
// and the air's light are all radiances in that unit; nothing is matched to a target brightness.

#include <array>
#include <cstdint>
#include <vector>

#include "engine/gfx/rt_display.h" // kDisplayGamma: the frame's one transfer

namespace render::rt {

using Rgb = std::array<double, 3>;

inline constexpr double kGroundRadius = 6360.0e3; // metres
inline constexpr double kTopRadius = 6420.0e3;
inline constexpr double kViewerAltitude = 100.0;
inline constexpr Rgb kRayleigh{5.802e-6, 13.558e-6, 33.1e-6};
inline constexpr double kRayleighHeight = 8000.0;
inline constexpr Rgb kOzoneAbsorption{0.650e-6, 1.881e-6, 0.085e-6};
inline constexpr double kOzoneCentre = 25000.0;    // metres above the ground
inline constexpr double kOzoneHalfWidth = 15000.0; // (none under 10 km or over 40 km)
inline constexpr double kReferenceElevation = 0.78539816339744831; // (45 degrees: the unit's day - sky_exposure)

// --- (added by the physical air) the weather's aerosol --------------------------------------------------------------
// The haze in the air is the weather's: [Graphics] rt_fog_air picks a visibility (the meteorological range at the
// ground - Koschmieder: the distance at which a black object's contrast against the horizon falls to 2%, 3.912 / the
// extinction at 550 nm). The aerosol's extinction at the ground is what the visibility leaves after the air's own
// (Rayleigh's green); across the spectrum it follows Angstrom's law (lambda / 550 nm)^-alpha at the model's 680 / 550 /
// 440 nm. Its single-scattering albedo and phase (Cornette-Shanks g) are the aerosol type's:
//   - clear: 40 km - a clear day's continental aerosol over a 1.2 km layer (optical depth about 0.10 at 550 nm),
//     alpha 1.3, albedo 0.9, g 0.8;
//   - hazy: 10 km - a hazy summer day (optical depth about 0.45), alpha 1.0, albedo 0.9, g 0.8;
//   - misty: 2 km at the ground - the clear day's haze with a mist of droplets in a layer kMistHeight deep under it
//     (droplets: alpha 0.3 - nearly grey -, albedo 0.97).
// The sky model, the sun's light and the level's air (the fog pass) all read the one Aerosol (set_aerosol: the settings
// put it there); a path tracer would take the same coefficients.
enum class Weather : uint8_t { kClear, kHazy, kMisty };
struct Aerosol {
    Rgb scattering{};          // per metre at the ground (the haze's layer)
    Rgb extinction{};          // per metre at the ground (scattering / albedo)
    double height = 1200.0;    // metres: the haze's layer's scale height
    double mist = 0.0;         // the mist's density at the ground over the haze layer's (0: none)
    double mist_height = 100.0; // metres
    double g = 0.8;            // Cornette-Shanks asymmetry
    bool operator==(const Aerosol&) const = default;
};
inline constexpr double kKoschmieder = 3.912;
inline constexpr double kClearVisibility = 40000.0; // metres
inline constexpr double kHazyVisibility = 10000.0;
inline constexpr double kMistyVisibility = 2000.0;
inline constexpr double kHazeHeight = 1200.0;  // metres
inline constexpr double kMistHeight = 100.0;   // metres
[[nodiscard]] Aerosol aerosol_for(Weather weather);
// The ini's spelling: "clear", "hazy", "misty" ("0" / "1" / "2"; an older ini's "cinematic" is hazy); anything else
// clear.
[[nodiscard]] Weather parse_weather(const char* text);
[[nodiscard]] const char* weather_name(Weather weather);
// The frame's weather and its aerosol (thread-safe; the clear weather's until set). set_weather puts its aerosol in
// place; set_aerosol another (the checks').
void set_weather(Weather weather);
[[nodiscard]] Weather weather();
void set_aerosol(const Aerosol& aerosol);
[[nodiscard]] Aerosol aerosol();
// The aerosol's density at a height (metres above the ground) over its density at the ground's haze layer: exp(-h /
// height) + mist exp(-h / mist_height).
[[nodiscard]] double aerosol_density(const Aerosol& aerosol, double height);
// Cornette-Shanks' phase (per steradian) and Rayleigh's.
[[nodiscard]] double aerosol_phase(double cosine, double g);
[[nodiscard]] double rayleigh_phase(double cosine);

// How finely the integrals are taken: segments along a view ray, and along each sample's ray toward the sun.
inline constexpr uint32_t kTableViewSteps = 32; // (the device's table: vulkan_rt_sky)
inline constexpr uint32_t kTableLightSteps = 16;
struct SkySteps {
    uint32_t view = kTableViewSteps;
    uint32_t light = kTableLightSteps;
};
inline constexpr SkySteps kReferenceSteps{.view = 1024, .light = 512};

// A direction as (x, y, z), y up. The model's sun at `elevation` (radians above the horizon) lies along +x.
using Dir = std::array<double, 3>;
[[nodiscard]] Dir sun_direction_at(double elevation);

// The sun's light reaching the viewer (each channel 0 - 1, the top of the atmosphere's 1) with the sun at `elevation`
// (the frame's aerosol, or the one given).
[[nodiscard]] Rgb sun_transmittance(double elevation, uint32_t steps);
[[nodiscard]] Rgb sun_transmittance(double elevation, uint32_t steps, const Aerosol& air);
// The sky's radiance toward `view` (normalised) with the sun along `sun` (normalised), per unit of the sun's irradiance,
// linear, not exposed - single scattering.
[[nodiscard]] Rgb sky_radiance(const Dir& view, const Dir& sun, const SkySteps& steps);
[[nodiscard]] Rgb sky_radiance(const Dir& view, const Dir& sun, const SkySteps& steps, const Aerosol& air);
// The unit's factor (see above): pi / the reference day's horizontal irradiance (the clear weather's air, every order
// of scattering; worked out once). Radiances per unit of the sun's irradiance x this are the game's linear colours.
[[nodiscard]] double sky_exposure();
// (added by the physical air) The sun: its angular radius (0.53 degrees across) and solid angle - its disc's mean
// radiance is its irradiance over this.
inline constexpr double kSunAngularRadius = 0.004654; // radians
[[nodiscard]] double sun_solid_angle();
// Its disc as drawn (the sky pass, or the sun's pass over a painted sky): limb-darkened, I(r) = I(0) (1 - u (1 -
// sqrt(1 - (r / R)^2))) with u = kSunLimbDarkening (the visible band's), its mean over the disc (1 - u / 3) I(0) = its
// irradiance (the transmittance at `elevation`, the frame's aerosol) over its solid angle - in the unit (x
// sky_exposure). The centre's radiance, linear (0 under the horizon).
inline constexpr double kSunLimbDarkening = 0.6;
[[nodiscard]] Rgb sun_disc_radiance(double elevation);

// The sun's colour as the game's passes use it (display-referred: the transmittance relative to the sun overhead, to
// 1 / kDisplayGamma, its brightest channel 1): (1, 1, 1) overhead, warmer toward the horizon. `steps` as sun_transmittance.
[[nodiscard]] std::array<float, 3> sun_colour_at(double elevation, uint32_t steps = kReferenceSteps.light);

// The GPU's table of the sky (vulkan_rt_sky): kSkyLutWidth x kSkyLutHeight entries of exposed linear radiance - across,
// the view's azimuth from the sun's (0 - pi), down, its elevation e from kSkyLutLowest to straight up, spaced by
// sqrt((e - lowest) / (pi / 2 - lowest)) (finer near the horizon) - then one more row: entry 0 the sun's transmittance
// (linear, relative to the top of the atmosphere), entry 1 the sun's colour (sun_colour_at's), entry 2 the zenith's
// radiance (exposed).
inline constexpr uint32_t kSkyLutWidth = 64;
inline constexpr uint32_t kSkyLutHeight = 32;
inline constexpr double kSkyLutLowest = -0.1; // radians
// The view direction (model space, the sun along +x) of a table entry's centre.
struct SkyLutEntry {
    uint32_t column = 0;
    uint32_t row = 0;
};
[[nodiscard]] Dir sky_lut_direction(const SkyLutEntry& entry);

// --- (added by the sky: [Graphics] rt_sky) the same atmosphere with light scattered many times ----------------------
// Hillaire's method ("A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020): the light
// scattered twice or more is, at any point, the sun's irradiance there times a transfer Psi(altitude, the sun's
// zenith cosine) - the second order's light L2 (every direction round the point, scattered once on the way with an
// even phase, the ground's light at the end - an albedo of kGroundAlbedo) over 1 - f (f: the share of light a point
// sends back to itself the same way): Psi = L2 / (1 - f), a geometric series of the orders. The sky then adds, along
// each view ray, each point's scattering coefficient x Psi to the single scattering above. The device keeps three
// tables (gpu_vulkan's vulkan_rt_sky): the transmittance (Bruneton's mapping), Psi, and the sky seen from the viewer
// (kSkyViewWidth x kSkyViewHeight: across, the azimuth from the sun's - u = sqrt(azimuth / pi), finer toward the sun;
// down, the elevation - finer toward the horizon, below it too). This file's integrals are their reference.
inline constexpr double kGroundAlbedo = 0.3;
inline constexpr uint32_t kTransmittanceWidth = 256;
inline constexpr uint32_t kTransmittanceHeight = 64;
inline constexpr uint32_t kMultipleWidth = 32;  // across: the sun's zenith cosine, -1 to 1
inline constexpr uint32_t kMultipleHeight = 32; // down: the altitude, the ground to the top
inline constexpr uint32_t kSkyViewWidth = 192;
inline constexpr uint32_t kSkyViewHeight = 108;
inline constexpr uint32_t kMultipleDirectionsSide = 8; // (8 x 8 directions round a point)
inline constexpr uint32_t kMultipleSteps = 20;         // segments along each
inline constexpr uint32_t kSkyViewSteps = 32;          // segments along a view ray (rt_sky's spacing)

// A point of the atmosphere seen as the transmittance table sees it: its distance from the centre and the cosine of
// a direction's zenith angle there.
struct RadiusCosine {
    double radius = 0.0;
    double cosine = 0.0;
};
// The transmittance table's texel (u, v in 0 - 1, texel centres at (i + 0.5) / size) <-> (r, mu); Bruneton's mapping.
[[nodiscard]] RadiusCosine transmittance_lut_point(double u, double v);
[[nodiscard]] std::array<double, 2> transmittance_lut_uv(const RadiusCosine& point);
// The transmittance from a point toward the atmosphere's top (the ground not considered), integrated with `steps`.
[[nodiscard]] Rgb transmittance_to_top(const RadiusCosine& point, uint32_t steps);
// Psi at an altitude (metres) with the sun's zenith cosine: kMultipleDirectionsSide^2 directions x `steps.steps`
// segments, the sun's transmittance integrated with `steps.light` (the device reads its table instead).
struct AtmospherePoint {
    double altitude = 0.0; // metres above the ground
    double cos_sun = 1.0;  // the sun's zenith cosine there
};
struct MultipleSteps {
    uint32_t steps = kMultipleSteps;
    uint32_t light = kTableLightSteps;
};
[[nodiscard]] Rgb multiple_scattering(const AtmospherePoint& place, const MultipleSteps& steps_given);
[[nodiscard]] Rgb multiple_scattering(const AtmospherePoint& place, const MultipleSteps& steps_given, const Aerosol& air);
// The table of Psi (kMultipleWidth x kMultipleHeight, row by row) and a bilinear read of it - what sky_radiance_ms
// adds (the device's table is the same integral).
using MultipleTable = std::vector<Rgb>;
[[nodiscard]] MultipleTable multiple_table(const MultipleSteps& steps);
[[nodiscard]] Rgb multiple_from_table(const MultipleTable& table, const AtmospherePoint& place);
// The sky's radiance toward `view` with the sun along `sun` with every order (single scattering as sky_radiance, plus
// Psi from `table`; below the horizon the ground's light at the ray's end, lit by the sun with kGroundAlbedo), per unit
// of the sun's irradiance, linear, not exposed.
[[nodiscard]] Rgb sky_radiance_ms(const Dir& view, const Dir& sun, const SkySteps& steps, const MultipleTable& table);
[[nodiscard]] Rgb sky_radiance_ms(const Dir& view, const Dir& sun, const SkySteps& steps, const MultipleTable& table,
                                  const Aerosol& air);
// (added by the physical air) The light on a level surface with the sun `elevation` (radians) up, per unit of the sun's
// irradiance, luminance (Rec. 709): its direct light (transmittance x sine) and the whole sky's (every order, cosine-
// weighted), in `air`. The unit's day (sky_exposure) is this at kReferenceElevation in the clear weather's air.
struct GroundLight {
    double direct = 0.0;
    double sky = 0.0;
};
[[nodiscard]] GroundLight ground_light(double elevation, const Aerosol& air);
// The sky-view table's texel centre (column, row) <-> its view direction (model space: the sun along +x).
[[nodiscard]] Dir sky_view_direction(const std::array<double, 2>& uv);
[[nodiscard]] std::array<double, 2> sky_view_uv(const Dir& view);

} // namespace render::rt
