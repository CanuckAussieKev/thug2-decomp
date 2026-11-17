#pragma once

// NOT ORIGINAL - rain ([Graphics] rain: a rain rate in mm/h; docs/design/vulkan.md "Rain"): the weather's rain as one
// physical state every consumer reads. API-neutral and pure but for the state at the end (the settings, the clock and
// the water lying on the ground, stepped on the simulation's steps - visual only: nothing here is read by the game).
//
// One input, the rain rate R (mm of water an hour on level ground), gives everything else:
//   - the drops' sizes: Marshall and Palmer (1948) - N(D) = N0 e^(-Lambda D), N0 = 8000 m^-3 mm^-1,
//     Lambda = 4.1 R^-0.21 mm^-1 - between kLeastDiameter and kMostDiameter (smaller: mist, larger: they break up);
//   - their fall speed: Atlas, Srivastava and Sekhon (1973)'s fit to Gunn and Kinzer's measurements,
//     v(D) = 9.65 - 10.3 e^(-0.6 D) m/s (D in mm);
//   - how many there are a cubic metre (the integral of N), how many strike a square metre a second (of N v), and the
//     rate they carry back (6 pi 1e-4 the integral of D^3 v N - the check's: Marshall-Palmer with Atlas's speeds gives
//     back the rate it was made from within a few per cent);
//   - the air's extinction from them: geometric optics (drops thousands of wavelengths across: an extinction efficiency
//     of 2 - half refraction and reflection, half diffraction), 2 x pi/4 the integral of D^2 N - what the volumetric
//     fog's march takes for the falling rain (rt_volume_light.h precipitation_extinction);
//   - their slant: the wind (engine/gfx/wind.h - the weather's 10 m wind) brought down to the drops' height by the
//     neutral log profile over a town's roughness, against the median-volume drop's fall speed;
//   - the cloud: rain falls from a deck covering the sky (nimbostratus: overcast by its definition) - cloud_cover;
//   - the water on the ground (Water, stepped): stored up to a paved surface's depression storage while it rains, dried
//     by evaporation after; from it the pores' filling, the film on slopes (Nusselt's film draining a plane under the
//     rain's supply), and puddles where the ground's small relief dips under the stored water's level;
//   - a wet material from a dry one (wet_material): the water's Fresnel layer over the surface and Lekner and Dorf
//     (1988)'s darkening - light trapped by total internal reflection inside the water returns to the surface and is
//     absorbed again - by how much of the surface the water holds.
// The GLSL twin of the surface's part is surface_glsl (the world shader's and the wet mask's - one function).

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "engine/gfx/rt_brdf.h"

namespace render::rain {

inline constexpr double kUnitsPerMetre = 39.37; // (the world's units are inches)

// --- the settings ---------------------------------------------------------------------------------------------------
// The rate (mm/h on level ground): the American Meteorological Society's classes - light up to 2.5, moderate to 7.6,
// heavy above; a downpour (a convective burst) 30 and more.
inline constexpr double kLightRate = 1.0;
inline constexpr double kModerateRate = 4.0;
inline constexpr double kHeavyRate = 12.0;
inline constexpr double kDownpourRate = 40.0;
inline constexpr double kMostRate = 150.0;
struct Settings {
    double rate = 0.0; // 0: no rain
    bool operator==(const Settings&) const = default;
};
void set_settings(const Settings& settings); // (cleaned: a rate outside 0 - kMostRate clamped, not finite 0)
[[nodiscard]] Settings settings();

// --- the drops --------------------------------------------------------------------------------------------------------
inline constexpr double kMarshallPalmerN0 = 8000.0; // m^-3 mm^-1
inline constexpr double kMarshallPalmerScale = 4.1; // Lambda = this x R^kMarshallPalmerPower (mm^-1)
inline constexpr double kMarshallPalmerPower = -0.21;
inline constexpr double kLeastDiameter = 0.1;                        // mm
inline constexpr double kMostDiameter = 6.0;                         // mm
[[nodiscard]] double size_slope(double rate);                        // Lambda (mm^-1)
[[nodiscard]] double size_density(double rate, double diameter);     // N(D) (m^-3 mm^-1)
[[nodiscard]] double number_density(double rate);                    // drops a cubic metre
[[nodiscard]] double number_density_over(double rate, double least); // ... of them at least `least` mm across
// The diameter (mm) at share `u` (0 - 1) of the drops (the distribution's inverse: the particles' sizes).
[[nodiscard]] double diameter_at(double rate, double u);
[[nodiscard]] double median_volume_diameter(double rate); // D0 = 3.67 / Lambda (mm)
[[nodiscard]] double fall_speed(double diameter);         // m/s (Atlas et al. 1973)
[[nodiscard]] double rate_from_drops(double rate);        // mm/h carried by the distribution (the check's)
[[nodiscard]] double extinction(double rate);             // per metre
[[nodiscard]] double impact_rate(double rate, double least = kLeastDiameter); // drops a square metre a second

// --- the wind on them -------------------------------------------------------------------------------------------------
inline constexpr double kRoughnessLength = 0.5; // metres: a town's scattered buildings (Davenport's "very rough")
inline constexpr double kDropHeight = 2.0;      // metres: where the rain is seen - round the eye
// The 10 m wind (m/s) at `height` metres by the neutral log profile.
[[nodiscard]] double wind_at(double wind_10m, double height);
// How the rain falls now: the median-volume drop's fall speed against the wind at kDropHeight (`wind_10m` m/s blowing
// along `downwind`, x and z, unit length). `direction` the way a drop moves (world, y down negative, unit length);
// `speed` its speed (world units a second); `slant` its sideways move a unit of fall (x, z).
struct Fall {
    std::array<double, 3> direction{0.0, -1.0, 0.0};
    double speed = 0.0;
    std::array<double, 2> slant{};
};
[[nodiscard]] Fall fall(double rate, double wind_10m, const std::array<double, 2>& downwind);

// --- the sky ----------------------------------------------------------------------------------------------------------
// The cloud deck's coverage the rain implies (0 - 1): steady rain falls from nimbostratus, a deck over the whole sky.
[[nodiscard]] double cloud_cover(double rate);

// --- the water on the ground ------------------------------------------------------------------------------------------
inline constexpr double kDepressionStorage = 1.0; // mm: what a paved surface holds before it runs off (0.5 - 1.5)
inline constexpr double kPoreStorage = 0.2;       // mm: what fills a porous surface's pores and texture first
inline constexpr double kEvaporation = 0.3;       // mm/h: a wet pavement's drying with no rain falling
inline constexpr double kFilmWhole = 0.1;         // mm: a film this thick covers the surface's grain
inline constexpr double kFlowLength = 1.0;        // metres: how far the rain on a slope runs before it leaves it
inline constexpr double kWaterViscosity = 1.0e-3; // Pa s
inline constexpr double kWaterDensity = 1000.0;   // kg m^-3
inline constexpr double kGravity = 9.81;          // m s^-2
inline constexpr double kReliefDeviation = 3.0;   // mm: a pavement's small unevenness (its standard deviation) ...
inline constexpr double kReliefLength = 1.2;      // ... over about this many metres (the relief noise's scale)
inline constexpr double kTextureDepth = 0.7;      // mm: a pavement's mean texture depth (ISO 13473-1; asphalt 0.5 - 1.5)
// The steady film (mm) on a plane sloping at `sine` with `rate` falling on it (Nusselt: h = (3 mu q L / rho g sin)^1/3),
// at most `stored` (the water there is): on level ground (sine 0) the water stored.
[[nodiscard]] double film_thickness(double rate, double sine, double stored);
// The ground's relief (in units of kReliefDeviation) at a place (x, z, world units): the GLSL twin's relief_at - a
// smooth noise of mean 0 over kReliefLength. Its distribution is measured on the processor once (relief_level): the
// level the water stands at over the relief's mean where `stored` mm lie on level ground on average.
[[nodiscard]] float relief_at(float x, float z);
[[nodiscard]] double relief_level(double stored); // in units of kReliefDeviation (the relief's own)
[[nodiscard]] double puddle_cover(double stored); // the share of level ground under standing water
// The relief's root-mean-square slope: the slope the film drains over on level ground, between the puddles.
[[nodiscard]] double relief_slope();
// The share of a surface a film `film` mm thick makes flat - a mirror's water surface -: where it rises over the surface's
// texture (its depth, kTextureDepth, from a uniform texture 0 - 2 x the mean); a puddle is flat whole.
[[nodiscard]] double flat_share(double film);
// What the surface shader takes, worked out once a frame (the twin's inputs).
struct Ground {
    double stored = 0.0;    // mm on level open ground
    double pores = 0.0;     // 0 - 1: a porous surface's pores filled
    double level = -8.0;    // the puddles' water level (relief units); far below: none
    double film_rate = 0.0; // mm/h: the rain feeding the film on slopes (0 when it stopped: the films drain away)
};
[[nodiscard]] Ground ground_of(double stored, double rate);

// --- the wet material -------------------------------------------------------------------------------------------------
inline constexpr double kWaterIndex = 1.333;
[[nodiscard]] double water_f0();                  // ((n - 1) / (n + 1))^2
[[nodiscard]] double water_outside_reflectance(); // the hemispherical (cosine-weighted) Fresnel from the air
[[nodiscard]] double water_inside_reflectance();  // ... from inside the water (total internal reflection)
// Lekner and Dorf: a surface of albedo `albedo` under water: (1 - Re) a (1 - ri) / (1 - a ri).
[[nodiscard]] double wet_albedo(double albedo);
inline constexpr double kWaterRoughness = 0.05; // a still water surface's (GGX alpha's root; ripples add theirs)
struct Material {
    std::array<double, 3> albedo{}; // linear
    double roughness = 0.5;
    double f0 = 0.04;
    double porosity = 0.0; // 0 - 1: how much water the surface takes in (asphalt, concrete: 1; metal, glass: 0)
};
// Wetness at a point: `pores` 0 - 1 its pores filled, `film` 0 - 1 a film over it, `puddle` 0 - 1 standing water.
struct Wetness {
    double pores = 0.0;
    double film = 0.0;   // the film's share (darkening): whole from kFilmWhole
    double puddle = 0.0;
    double flat = 0.0;   // the flat water's share (flat_share): the film's mirror
};
// The material wet: the share under water (the pores by the porosity, the film, the puddle) darkened by wet_albedo,
// its roughness toward the water's where the water lies flat (a puddle, a film over the texture), its F0 the water's there.
[[nodiscard]] Material wet_material(const Material& dry, const Wetness& wet);
// The same change on the one light model's material inputs (engine/gfx/rt_brdf.h - round M's BRDF): the seam every
// shading path joins - its albedo darkened, its roughness and F0 the water's where the water lies flat; `porosity` the
// surface's (the material table's when it has one; porosity_of its roughness until then).
[[nodiscard]] rt::brdf::Material wet_brdf_material(const rt::brdf::Material& dry, double porosity, const Wetness& wet);
// (the check's) The wet material's hemispherical albedo for light from straight above: the water's Fresnel plus what
// comes back out of the surface under it - at most 1.
[[nodiscard]] double wet_reflectance(double albedo, double water_share);
// The share of a surface (0 - 1) its porosity, from the roughness: the seam the material table will fill (Round M:
// a material's own porosity) - until then the rough surfaces (asphalt, concrete, stone, wood) are porous, the smooth
// ones (metal, glass, glazed tiles, paint) not.
[[nodiscard]] double porosity_of(double roughness);

// --- the rings on standing water ---------------------------------------------------------------------------------------
// A drop striking water sends out a ring of capillary waves at about their slowest speed, dying within a fraction of a
// second. The rings are drawn on kRippleLayers jittered grids (rain_ripple): each cell one ring a life at a new place
// each time, the cells as small as gives the rate of the drops kRippleLeast mm and over striking a square metre.
inline constexpr double kRippleSpeed = 0.23;  // m/s: the slowest capillary-gravity wave
inline constexpr double kRippleLife = 0.4;    // s
inline constexpr double kRippleWidth = 0.008; // m: a ring's crest
inline constexpr double kRippleSlope = 0.25;  // a new ring's steepest slope
inline constexpr double kRippleLeast = 1.0;   // mm
inline constexpr int kRippleLayers = 3;
struct Ripples {
    double cell = 0.0; // world units (0: none)
    double life = kRippleLife;
    int layers = kRippleLayers;
};
[[nodiscard]] Ripples ripples_of(double rate);

// The GLSL twin of the surface's part (needs RAIN_* defines from glsl_constants and a `rain` uniform block with
// ground (x stored, y pores, z level, w film rate) - see vulkan_rain.cpp): rain_relief, rain_film, rain_wet_albedo,
// rain_porosity, rain_wetness; and rain_ripple(xz, ripple) - the rings' slope (d height / d x, d z) at a place, `ripple`
// x the cell (units), y the layers, z the rain's clock (s), w the life (s); it reads no block (the water's shader too).
[[nodiscard]] const char* surface_glsl();
// The rings' GLSL (rain_mix, rain_ripple): before surface_glsl, or alone (the water's shader).
[[nodiscard]] const char* ripple_glsl();
// The constants the GLSL twin reads (#define lines).
[[nodiscard]] const char* glsl_constants();

// --- the state, stepped (visual only) --------------------------------------------------------------------------------
// One simulation step of `seconds` (render_pc: snow_world's step): the rain's clock moves on, the stored water fills
// toward kDepressionStorage at the rate (less the drops that run off) or dries by kEvaporation.
void step(double seconds);
[[nodiscard]] double clock();
[[nodiscard]] double stored();
// (the checks') a stored depth set; the clock and the store cleared
void set_stored(double millimetres);
void reset();

} // namespace render::rain
