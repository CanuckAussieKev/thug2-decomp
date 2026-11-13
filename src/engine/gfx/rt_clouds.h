#pragma once

// NOT ORIGINAL - volumetric clouds ([Graphics] rt_clouds with rt_sky; gpu_vulkan's vulkan_rt_clouds draws them): the
// model, and its processor reference - the device's noise and march are these functions in GLSL (vulkan_rt_clouds.cpp
// keeps the two side by side), so the check can march the same rays on both.
//
// The layer: a shell round the planet (rt_sky.h's radii) from kCloudBase to kCloudTop above the ground. What is in it:
//   - the noise, made at start-up (no assets): a 128^3 shape texture (r: Perlin-Worley - Perlin's fractal noise widened
//     by Worley's; g, b, a: Worley's fractal noise at 4, 8 and 16 cells), a 32^3 detail texture (Worley's at 4, 8, 16)
//     and a 256^2 weather map (r: where clouds may be - Perlin's fractal noise; g: their kind - low and flat to tall),
//     all tiling, from an integer hash: the same bits on the processor and the device;
//   - the density at a point: the weather's coverage wc = clamp(2 coverage - 1 + weather.r) (coverage 0: none, 0.5:
//     the weather's patches, 1: overcast), a height profile for the kind (soft at the base, ending at a height the kind
//     sets), the shape's Perlin-Worley eroded by its Worley fractal, then the coverage's threshold, then the detail's
//     Worley eroding the edges (billowy below, wispy above);
//   - the light at a point: the sun's (the atmosphere's transmittance at that altitude) through the cloud toward it
//     (kCloudLightSamples steps, growing), Beer's law with a multiple-scattering approximation (Wrenninge's octaves: each
//     a share kOctaveShare of the last, its extinction x kOctaveExtinction, its phase's asymmetry x kOctaveAsymmetry), a
//     phase of two Henyey-Greenstein lobes (forward - the silver lining toward the sun - and back), a powder term (the
//     dark edges of thick cloud facing away from the light), and the sky's light (the atmosphere's: its horizon colour
//     at the base, its zenith's at the top);
//   - the march: from the camera through the layer (none below the horizon: the ground is in the way), `steps` segments
//     to kCloudFar at most, a jittered sample in each; light scattered and transmittance integrated per segment in closed
//     form (Hillaire's energy-conserving step); stops once the transmittance is under kCloudOpaque.
// Units: metres; the game's units are kGameUnitsPerMetre to the metre. Light in the sky table's exposed linear units.
#include <array>
#include <cstdint>
#include <vector>

namespace render::rt {

inline constexpr uint32_t kCloudShapeSize = 128;
inline constexpr uint32_t kCloudDetailSize = 32;
inline constexpr uint32_t kCloudWeatherSize = 256;
inline constexpr double kCloudBase = 1500.0; // metres above the ground
inline constexpr double kCloudTop = 4000.0;
inline constexpr double kCloudShapeScale = 9000.0;   // metres the shape texture covers before it repeats
inline constexpr double kCloudDetailScale = 1100.0;
inline constexpr double kCloudWeatherScale = 42000.0;
inline constexpr double kCloudDensity = 0.03;        // extinction per metre at density 1
inline constexpr double kCloudFar = 60000.0;         // metres along a ray at most
inline constexpr double kCloudLightStep = 70.0;      // the first light step (metres); each next x kCloudLightGrowth
inline constexpr double kCloudLightGrowth = 1.7;
inline constexpr uint32_t kCloudLightSamples = 6;
inline constexpr double kCloudForwardG = 0.8;
inline constexpr double kCloudBackG = -0.3;
inline constexpr double kCloudForwardShare = 0.7;
inline constexpr double kOctaveShare = 0.5;
inline constexpr double kOctaveExtinction = 0.5;
inline constexpr double kOctaveAsymmetry = 0.5;
inline constexpr uint32_t kCloudOctaves = 3;
inline constexpr double kCloudPowderLength = 60.0; // metres: the powder term's depth
inline constexpr double kCloudPowderShare = 0.5;
inline constexpr double kCloudOpaque = 0.01;
inline constexpr double kGameUnitsPerMetre = 39.37; // (the game's units are inches)
inline constexpr std::array<double, 2> kCloudWind{0.94, 0.34}; // the way the clouds drift (x, z; normalised)
inline constexpr double kCloudWindSpeed = 12.0;                // metres a second at rt_cloud_speed 1

// --- the noise --------------------------------------------------------------------------------------------------------
// A 32-bit integer hash of a lattice point and a seed (the device's is the same operations on uint).
[[nodiscard]] uint32_t cloud_hash(uint32_t x, uint32_t y, uint32_t z, uint32_t seed);
// Worley noise (1 at a feature point, 0 a cell away or more) and Perlin's gradient noise (about -1 - 1) at p (in cells),
// tiling every `period` cells.
using Vec3d = std::array<double, 3>;
[[nodiscard]] double cloud_worley(const Vec3d& p, int period, uint32_t seed);
[[nodiscard]] double cloud_perlin(const Vec3d& p, int period, uint32_t seed);
// The textures' texels (their 0 - 1 values before the device rounds them to bytes): the shape's (x, y, z of 0 -
// kCloudShapeSize), the detail's, the weather map's (z unused).
[[nodiscard]] std::array<double, 4> cloud_shape_texel(uint32_t x, uint32_t y, uint32_t z);
[[nodiscard]] std::array<double, 4> cloud_detail_texel(uint32_t x, uint32_t y, uint32_t z);
[[nodiscard]] std::array<double, 4> cloud_weather_texel(uint32_t x, uint32_t y);

// The textures as the device holds them (RGBA bytes, x fastest, then y, then z) and filtered reads (trilinear /
// bilinear, wrapping), as the device's samplers.
struct CloudNoise {
    std::vector<uint8_t> shape;   // kCloudShapeSize^3 x 4
    std::vector<uint8_t> detail;  // kCloudDetailSize^3 x 4
    std::vector<uint8_t> weather; // kCloudWeatherSize^2 x 4
};
[[nodiscard]] std::array<double, 4> sample_volume(const std::vector<uint8_t>& texels, uint32_t size, const Vec3d& uvw);
[[nodiscard]] std::array<double, 4> sample_plane(const std::vector<uint8_t>& texels, uint32_t size, double u, double v);

// The density (0 - 1) at a point of cloud space (metres: x and z across - the drift and the camera's place included -,
// y the altitude above the ground), with the layer's coverage (0 - 1), its detail or not.
[[nodiscard]] double cloud_density(const CloudNoise& noise, const Vec3d& point, double coverage, bool detail);

// One ray's march.
struct CloudRay {
    double camera_altitude = 0.0;          // metres above the ground
    std::array<double, 2> offset{};        // metres: the camera's place across and the drift
    Vec3d direction{};                     // normalised, y up
    Vec3d to_sun{};                        // normalised
    double coverage = 0.5;
    double density_scale = 1.0;            // x kCloudDensity
    uint32_t steps = 64;
    double jitter = 0.5;                   // where in each segment its sample is (0 - 1)
    std::array<double, 3> sun_light{};     // the sun's light at the layer (exposed linear: its colour after the air)
    std::array<double, 3> ambient_bottom{}; // the sky's light at the layer's base and top
    std::array<double, 3> ambient_top{};
};
struct CloudResult {
    std::array<double, 3> light{}; // scattered toward the camera
    double transmittance = 1.0;
    double distance = -1.0;         // metres: the light's weighted distance (-1: no cloud on the way)
    bool marched = false;           // false: the ray never enters the layer (it meets the ground)
};
[[nodiscard]] CloudResult march_clouds(const CloudNoise& noise, const CloudRay& ray);

// The phase of the two lobes at an asymmetry scale (1 for the first octave).
[[nodiscard]] double cloud_phase(double cosine, double asymmetry_scale);

} // namespace render::rt
