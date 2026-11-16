#pragma once

#include <array>

// NOT ORIGINAL - the water's waves from physics (docs/design/vulkan.md "Water"; the backend's water shading -
// gpu_vulkan/vulkan_water.h - draws them). API-neutral, no Vulkan: the processor's half, the checks' reference.
//
// A water body's waves are the wind's: their height and length grow with the wind over the water, with the FETCH (how far
// the wind has blown over the water - the distance to the upwind shore) and are held down by the DEPTH. One empirical
// law covers all three: Young and Verhagen's finite-depth growth curves (Coastal Engineering 29, 1996 - Lake George),
//   epsilon = 3.64e-3 [tanh A1 tanh(B1 / tanh A1)]^1.74,   A1 = 0.493 delta^0.75,  B1 = 3.13e-3 chi^0.57
//   nu      = 0.133   [tanh A2 tanh(B2 / tanh A2)]^-0.37,  A2 = 0.331 delta^1.01,  B2 = 5.215e-4 chi^0.73
// with epsilon = g^2 E / U^4 (E the surface's variance: the significant height Hs = 4 sqrt(E)), nu = f_p U / g (the
// spectrum's peak frequency), delta = g d / U^2 (the depth), chi = g F / U^2 (the fetch), U the wind 10 m up. In deep water
// and over a long fetch they become the fully developed sea (Hs 0.24 U^2 / g, Pierson-Moskowitz's), over a short one
// JONSWAP's fetch law (Hs proportional to the square root of the fetch). Waves never stand higher than 0.78 of the depth
// (they break). Below kOnsetWind the wind raises no waves at all (Kahma and Donelan 1988): the surface is still.
//
// The wind over the water is the weather's (engine/gfx/wind.h speed()) x how open the water is to the sky (its sky
// visibility from the level's collision: water_bodies.h) - a puddle under a roof sees none. The spectrum's shape is
// JONSWAP's (gamma 3.3); the shader's four Gerstner waves stand for its peak (wavelengths 1, 0.63, 0.41, 0.27 x the peak's),
// each with the energy of its band, and its ripple texture for the shorter waves - their mean square slope the spectrum's
// equilibrium tail over the texture's band (alpha ln(f2 / f1), alpha JONSWAP's Phillips constant 0.076 chi^-0.22).
// Whitecaps cover the share Monahan and O'Muircheartaigh measured (1980: 3.84e-6 U^3.41).
namespace render::water {

inline constexpr double kGravity = 9.81;            // m/s^2
inline constexpr double kSurfaceTension = 7.4e-5;   // m^3/s^2 (sigma / rho: water's capillary waves)
inline constexpr double kOnsetWind = 1.0;           // m/s 10 m up: below it no wind waves grow
inline constexpr double kBreaking = 0.78;           // the highest wave over the depth (McCowan)
inline constexpr double kOpenFetch = 1.0e7;         // metres: water open to the horizon (a fully developed sea)
inline constexpr double kDeepWater = 1.0e4;         // metres: no bed found below the water
inline constexpr double kUnitsPerMetre = 39.37;     // (the world's units are inches)
inline constexpr std::array<double, 4> kSwellLengths{1.0, 0.63, 0.41, 0.27}; // the four waves' lengths x the peak's
// The ripple texture's band (the shader's two layers: a 263-unit tile with 2 - 24 cycles, a 97-unit one): world units.
inline constexpr double kRippleLongest = 131.5;
inline constexpr double kRippleShortest = 4.0;
// The ripple texture's slope RMS (both axes) as the shader mixes its two layers (0.55 a + 0.45 b of 0.22 each).
inline constexpr double kRippleTextureSlope = 0.156;

// The wind sea at a place: what the growth law gives.
struct WindSea {
    double wind = 0.0;       // the wind over the water (m/s 10 m up)
    double height = 0.0;     // the significant wave height Hs (metres)
    double frequency = 0.0;  // the spectrum's peak (Hz; 0: still)
    double wavelength = 0.0; // the peak's wavelength at the depth (metres)
};
// `wind` m/s over the water, `fetch` metres to the upwind shore, `depth` metres to the bed.
[[nodiscard]] WindSea wind_sea(double wind, double fetch, double depth);

// The angular frequency of a wave of wavenumber k (rad/m) on water `depth` deep: gravity and capillarity,
// omega^2 = (g k + T k^3) tanh(k d) - and the wavenumber of a frequency (its inverse).
[[nodiscard]] double omega_of(double k, double depth);
[[nodiscard]] double wavenumber_of(double omega, double depth);

// JONSWAP's spectrum shape at frequency f for a peak fp (variance per Hz, unnormalised: f^-5 exp(-5/4 (fp/f)^4) x
// gamma^r) and its integral over [f0, f1] (numerical).
[[nodiscard]] double jonswap_shape(double f, double fp);
[[nodiscard]] double jonswap_shape_integral(double f0, double f1, double fp);

// What the shader's waves take from a wind sea (world units): the four Gerstner waves' slopes (amplitude x k) and phase
// speeds, the peak's wavelength, the significant height, the ripple texture's slope scale and the whitecaps' share.
struct SeaWaves {
    std::array<float, 4> slopes{};
    std::array<float, 4> speeds{}; // units a second
    float wavelength = 0.0F;       // the peak's (units)
    float height = 0.0F;           // Hs (units)
    float ripples = 0.0F;          // x the ripple texture's slope
    float whitecaps = 0.0F;        // 0 - 1: the crests' foam at full strength from 1% of the surface white
};
[[nodiscard]] SeaWaves sea_waves(const WindSea& sea, double fetch, double depth);

} // namespace render::water
