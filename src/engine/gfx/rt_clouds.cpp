// NOT ORIGINAL - volumetric clouds: the model and its processor reference (rt_clouds.h). Every function here has a GLSL
// twin in gpu_vulkan/vulkan_rt_clouds.cpp - change both.
#include "engine/gfx/rt_clouds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_sky.h"

namespace render::rt {

namespace {

constexpr double kHalf = 0.5;
constexpr double kPerByte = 1.0 / 255.0;
constexpr double kUnit = 1.0 / 16777216.0; // 2^-24
constexpr uint32_t kUnitShift = 8;
constexpr std::size_t kChannels = 4;
// the hash's multipliers and shifts
constexpr uint32_t kHashX = 0x8DA6B343U;
constexpr uint32_t kHashY = 0xD8163841U;
constexpr uint32_t kHashZ = 0xCB1AB31FU;
constexpr uint32_t kHashSeed = 0x165667B1U;
constexpr uint32_t kHashMixFirst = 0x7FEB352DU;
constexpr uint32_t kHashMixSecond = 0x846CA68BU;
constexpr uint32_t kHashShiftWide = 16U;
constexpr uint32_t kHashShiftNarrow = 15U;
constexpr double kFarther = 9.0; // (farther than any feature point can be, squared)
constexpr int kCorners = 8;      // a cell's corners
constexpr int kPlaneCorners = 4;
constexpr uint32_t kGradientMask = 15U;
// the fade curve 6t^5 - 15t^4 + 10t^3
constexpr double kFadeFifth = 6.0;
constexpr double kFadeFourth = 15.0;
constexpr double kFadeThird = 10.0;
// the octaves' weights (each half the last) and frequencies
constexpr double kOctaveFirst = 0.625;
constexpr double kOctaveSecond = 0.25;
constexpr double kOctaveThird = 0.125;
constexpr double kOctaveFourth = 0.0625;
constexpr double kPerlinFirst = 0.5;
constexpr double kTwice = 2.0;
constexpr double kFourTimes = 4.0;
constexpr int kCells4 = 4;
constexpr int kCells8 = 8;
constexpr int kCells16 = 16;
constexpr int kCells32 = 32;
constexpr int kCells2 = 2;
// the seeds of each texture's noises
constexpr uint32_t kShapePerlinSeed = 11;
constexpr uint32_t kShapeWorleySeed = 21;
constexpr uint32_t kShapeGreenSeed = 31;
constexpr uint32_t kShapeBlueSeed = 41;
constexpr uint32_t kDetailRedSeed = 51;
constexpr uint32_t kDetailGreenSeed = 61;
constexpr uint32_t kDetailBlueSeed = 71;
constexpr uint32_t kWeatherSeed = 81;
constexpr uint32_t kKindSeed = 91;
constexpr double kPerlinStretch = 0.9; // Perlin's fractal widened toward 0 - 1
constexpr double kWeatherStretch = 1.1;
constexpr double kPhaseFloor = 1e-4;
constexpr double kWeightFloor = 1e-6;
// the height profile: the kind's top (kTopLowest - 1 of the layer), the base's soft edge, the top's
constexpr double kTopLowest = 0.45;
constexpr double kTopRange = 0.55;
constexpr double kBaseEdge = 0.12;
constexpr double kTopEdge = 0.4;
constexpr double kWispyFrom = 5.0; // (the detail inverts from billowy to wispy over the layer's lowest fifth)

// the gradients (the cube's 12 edges, four repeated)
constexpr std::array<std::array<int, 3>, 16> kGradients{{{1, 1, 0},
                                                         {-1, 1, 0},
                                                         {1, -1, 0},
                                                         {-1, -1, 0},
                                                         {1, 0, 1},
                                                         {-1, 0, 1},
                                                         {1, 0, -1},
                                                         {-1, 0, -1},
                                                         {0, 1, 1},
                                                         {0, -1, 1},
                                                         {0, 1, -1},
                                                         {0, -1, -1},
                                                         {1, 1, 0},
                                                         {-1, 1, 0},
                                                         {0, -1, 1},
                                                         {0, -1, -1}}};

double unit_of(uint32_t hash) { return static_cast<double>(hash >> kUnitShift) * kUnit; }

uint32_t wrapped(int value, int period) { return static_cast<uint32_t>(((value % period) + period) % period); }

double fade(double value)
{
    return value * value * value * ((value * ((value * kFadeFifth) - kFadeFourth)) + kFadeThird);
}

double lerp(double a, double b, double amount) { return a + ((b - a) * amount); }

double remap(double value, double low, double high, double to_low, double to_high)
{
    return to_low + ((value - low) * (to_high - to_low) / (high - low));
}

double saturate(double value) { return std::clamp(value, 0.0, 1.0); }

Vec3d scaled(const Vec3d& point, double by)
{
    return {core::at(point, 0) * by, core::at(point, 1) * by, core::at(point, 2) * by};
}

// Worley's fractal noise at `cells` (and twice, four times as many)
double worley_fbm(const Vec3d& point, int cells, uint32_t seed)
{
    return (kOctaveFirst * cloud_worley(scaled(point, cells), {.period = cells, .seed = seed})) +
           (kOctaveSecond * cloud_worley(scaled(point, kTwice * cells), {.period = 2 * cells, .seed = seed + 1})) +
           (kOctaveThird *
            cloud_worley(scaled(point, kFourTimes * cells), {.period = kCells4 * cells, .seed = seed + 2}));
}

double read_byte(const std::vector<uint8_t>& texels, std::size_t texel, std::size_t channel)
{
    return static_cast<double>(core::at(texels, (texel * kChannels) + channel)) * kPerByte;
}

double henyey_greenstein(double cosine, double asymmetry)
{
    const double denominator = std::max(1.0 + (asymmetry * asymmetry) - (2.0 * asymmetry * cosine), kPhaseFloor);
    return (1.0 - (asymmetry * asymmetry)) / (kFourTimes * std::numbers::pi * denominator * std::sqrt(denominator));
}

// where a ray from `origin` (from the planet's centre) along `direction` leaves the sphere of `radius` round the
// centre (-1: it never meets it ahead)
double leave_sphere(const Vec3d& origin, const Vec3d& direction, double radius)
{
    const double half_b = (core::at(origin, 0) * core::at(direction, 0)) +
                          (core::at(origin, 1) * core::at(direction, 1)) +
                          (core::at(origin, 2) * core::at(direction, 2));
    const double offset = (core::at(origin, 0) * core::at(origin, 0)) + (core::at(origin, 1) * core::at(origin, 1)) +
                          (core::at(origin, 2) * core::at(origin, 2)) - (radius * radius);
    const double inside = (half_b * half_b) - offset;
    if (inside < 0.0) {
        return -1.0;
    }
    const double far = -half_b + std::sqrt(inside);
    return far > 0.0 ? far : -1.0;
}

// whether the ray meets the ground ahead
bool meets_ground(const Vec3d& origin, const Vec3d& direction)
{
    const double half_b = (core::at(origin, 0) * core::at(direction, 0)) +
                          (core::at(origin, 1) * core::at(direction, 1)) +
                          (core::at(origin, 2) * core::at(direction, 2));
    const double offset = (core::at(origin, 0) * core::at(origin, 0)) + (core::at(origin, 1) * core::at(origin, 1)) +
                          (core::at(origin, 2) * core::at(origin, 2)) - (kGroundRadius * kGroundRadius);
    return half_b < 0.0 && ((half_b * half_b) - offset) >= 0.0;
}

} // namespace

uint32_t cloud_hash(uint32_t x, uint32_t y, uint32_t z, uint32_t seed)
{
    uint32_t hash = (x * kHashX) ^ (y * kHashY) ^ (z * kHashZ) ^ (seed * kHashSeed);
    hash ^= hash >> kHashShiftWide;
    hash *= kHashMixFirst;
    hash ^= hash >> kHashShiftNarrow;
    hash *= kHashMixSecond;
    hash ^= hash >> kHashShiftWide;
    return hash;
}

double cloud_worley(const Vec3d& point, const NoiseLattice& lattice)
{
    const int period = lattice.period;
    const uint32_t seed = lattice.seed;
    const std::array<int, 3> cell{static_cast<int>(std::floor(core::at(point, 0))),
                                  static_cast<int>(std::floor(core::at(point, 1))),
                                  static_cast<int>(std::floor(core::at(point, 2)))};
    const Vec3d inside{core::at(point, 0) - core::at(cell, 0), core::at(point, 1) - core::at(cell, 1),
                       core::at(point, 2) - core::at(cell, 2)};
    double best = kFarther;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const uint32_t wx = wrapped(core::at(cell, 0) + dx, period);
                const uint32_t wy = wrapped(core::at(cell, 1) + dy, period);
                const uint32_t wz = wrapped(core::at(cell, 2) + dz, period);
                const double fx = dx + unit_of(cloud_hash(wx, wy, wz, seed)) - core::at(inside, 0);
                const double fy = dy + unit_of(cloud_hash(wx, wy, wz, seed + 1)) - core::at(inside, 1);
                const double fz = dz + unit_of(cloud_hash(wx, wy, wz, seed + 2)) - core::at(inside, 2);
                best = std::min(best, (fx * fx) + (fy * fy) + (fz * fz));
            }
        }
    }
    return 1.0 - saturate(std::sqrt(best));
}

double cloud_perlin(const Vec3d& point, const NoiseLattice& lattice)
{
    const int period = lattice.period;
    const uint32_t seed = lattice.seed;
    const std::array<int, 3> cell{static_cast<int>(std::floor(core::at(point, 0))),
                                  static_cast<int>(std::floor(core::at(point, 1))),
                                  static_cast<int>(std::floor(core::at(point, 2)))};
    const Vec3d inside{core::at(point, 0) - core::at(cell, 0), core::at(point, 1) - core::at(cell, 1),
                       core::at(point, 2) - core::at(cell, 2)};
    std::array<double, kCorners> corner{};
    for (int i = 0; i < kCorners; ++i) {
        const int ox = i & 1;
        const int oy = (i >> 1) & 1;
        const int oz = (i >> 2) & 1;
        const uint32_t hash =
            cloud_hash(wrapped(core::at(cell, 0) + ox, period), wrapped(core::at(cell, 1) + oy, period),
                       wrapped(core::at(cell, 2) + oz, period), seed);
        const std::array<int, 3>& gradient = core::at(kGradients, hash & kGradientMask);
        core::at(corner, i) = (core::at(gradient, 0) * (core::at(inside, 0) - ox)) +
                              (core::at(gradient, 1) * (core::at(inside, 1) - oy)) +
                              (core::at(gradient, 2) * (core::at(inside, 2) - oz));
    }
    const double ux = fade(core::at(inside, 0));
    const double uy = fade(core::at(inside, 1));
    const double uz = fade(core::at(inside, 2));
    const double x00 = lerp(core::at(corner, 0), core::at(corner, 1), ux);
    const double x10 = lerp(core::at(corner, 2), core::at(corner, 3), ux);
    const double x01 = lerp(core::at(corner, 4), core::at(corner, 5), ux);
    const double x11 = lerp(core::at(corner, 6), core::at(corner, 7), ux);
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz);
}

std::array<double, 4> cloud_shape_texel(uint32_t x, uint32_t y, uint32_t z)
{
    const double size = kCloudShapeSize;
    const Vec3d point{(x + kHalf) / size, (y + kHalf) / size, (z + kHalf) / size};
    const double perlin =
        (cloud_perlin(scaled(point, kCells4), {.period = kCells4, .seed = kShapePerlinSeed}) * kPerlinFirst) +
        (cloud_perlin(scaled(point, kCells8), {.period = kCells8, .seed = kShapePerlinSeed + 1}) * kOctaveSecond) +
        (cloud_perlin(scaled(point, kCells16), {.period = kCells16, .seed = kShapePerlinSeed + 2}) * kOctaveThird);
    const double perlin01 = saturate((perlin * kPerlinStretch) + kHalf);
    const double worley = worley_fbm(point, kCells4, kShapeWorleySeed);
    return {saturate(remap(perlin01, worley - 1.0, 1.0, 0.0, 1.0)), worley, worley_fbm(point, kCells8, kShapeGreenSeed),
            worley_fbm(point, kCells16, kShapeBlueSeed)};
}

std::array<double, 4> cloud_detail_texel(uint32_t x, uint32_t y, uint32_t z)
{
    const double size = kCloudDetailSize;
    const Vec3d point{(x + kHalf) / size, (y + kHalf) / size, (z + kHalf) / size};
    return {worley_fbm(point, kCells4, kDetailRedSeed), worley_fbm(point, kCells8, kDetailGreenSeed),
            worley_fbm(point, kCells16, kDetailBlueSeed), 1.0};
}

std::array<double, 4> cloud_weather_texel(const std::array<uint32_t, 2>& texel)
{
    const uint32_t x = core::at(texel, 0);
    const uint32_t y = core::at(texel, 1);
    const double size = kCloudWeatherSize;
    const double u = (x + kHalf) / size;
    const double v = (y + kHalf) / size;
    const auto at = [u, v](double cells) { return Vec3d{u * cells, v * cells, kHalf}; };
    const double fbm = (cloud_perlin(at(kCells4), {.period = kCells4, .seed = kWeatherSeed}) * kPerlinFirst) +
                       (cloud_perlin(at(kCells8), {.period = kCells8, .seed = kWeatherSeed + 1}) * kOctaveSecond) +
                       (cloud_perlin(at(kCells16), {.period = kCells16, .seed = kWeatherSeed + 2}) * kOctaveThird) +
                       (cloud_perlin(at(kCells32), {.period = kCells32, .seed = kWeatherSeed + 3}) * kOctaveFourth);
    return {saturate((fbm * kWeatherStretch) + kHalf),
            saturate((cloud_perlin(at(kCells2), {.period = kCells2, .seed = kKindSeed}) * kPerlinStretch) + kHalf), 0.0,
            1.0};
}

std::array<double, 4> sample_volume(const std::vector<uint8_t>& texels, uint32_t size, const Vec3d& uvw)
{
    std::array<double, 4> out{};
    const auto count = static_cast<int>(size);
    std::array<int, 3> low{};
    Vec3d frac{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double at = (core::at(uvw, axis) * size) - kHalf;
        const double floor = std::floor(at);
        core::at(low, axis) = static_cast<int>(floor);
        core::at(frac, axis) = at - floor;
    }
    for (int i = 0; i < kCorners; ++i) {
        const int ox = i & 1;
        const int oy = (i >> 1) & 1;
        const int oz = (i >> 2) & 1;
        const double w = (ox != 0 ? core::at(frac, 0) : 1.0 - core::at(frac, 0)) *
                         (oy != 0 ? core::at(frac, 1) : 1.0 - core::at(frac, 1)) *
                         (oz != 0 ? core::at(frac, 2) : 1.0 - core::at(frac, 2));
        const std::size_t texel = (((static_cast<std::size_t>(wrapped(core::at(low, 2) + oz, count)) * size) +
                                    wrapped(core::at(low, 1) + oy, count)) *
                                   size) +
                                  wrapped(core::at(low, 0) + ox, count);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            core::at(out, channel) += w * read_byte(texels, texel, channel);
        }
    }
    return out;
}

std::array<double, 4> sample_plane(const std::vector<uint8_t>& texels, uint32_t size, const std::array<double, 2>& uv)
{
    const double u = core::at(uv, 0);
    const double v = core::at(uv, 1);
    std::array<double, 4> out{};
    const auto count = static_cast<int>(size);
    const double ax = (u * size) - kHalf;
    const double ay = (v * size) - kHalf;
    const double fx = ax - std::floor(ax);
    const double fy = ay - std::floor(ay);
    const auto lx = static_cast<int>(std::floor(ax));
    const auto ly = static_cast<int>(std::floor(ay));
    for (int i = 0; i < kPlaneCorners; ++i) {
        const int ox = i & 1;
        const int oy = (i >> 1) & 1;
        const double w = (ox != 0 ? fx : 1.0 - fx) * (oy != 0 ? fy : 1.0 - fy);
        const std::size_t texel = (static_cast<std::size_t>(wrapped(ly + oy, count)) * size) + wrapped(lx + ox, count);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            core::at(out, channel) += w * read_byte(texels, texel, channel);
        }
    }
    return out;
}

double cloud_density(const CloudNoise& noise, const Vec3d& point, double coverage, bool detail)
{
    const double height = (core::at(point, 1) - kCloudBase) / (kCloudTop - kCloudBase);
    if (height <= 0.0 || height >= 1.0) {
        return 0.0;
    }
    const std::array<double, 4> weather =
        sample_plane(noise.weather, kCloudWeatherSize,
                     {core::at(point, 0) / kCloudWeatherScale, core::at(point, 2) / kCloudWeatherScale});
    const double cover = saturate((core::at(weather, 0) - (1.0 - coverage)) * kCoverEdge);
    if (cover <= 0.0) {
        return 0.0;
    }
    const double top = kTopLowest + (kTopRange * core::at(weather, 1));
    const double profile = saturate(height / kBaseEdge) * saturate((top - height) / (kTopEdge * top));
    if (profile <= 0.0) {
        return 0.0;
    }
    const std::array<double, 4> shape =
        sample_volume(noise.shape, kCloudShapeSize, scaled(point, 1.0 / kCloudShapeScale));
    const double fbm = (core::at(shape, 1) * kOctaveFirst) + (core::at(shape, 2) * kOctaveSecond) +
                       (core::at(shape, 3) * kOctaveThird);
    const double base = saturate(remap(core::at(shape, 0), fbm - 1.0, 1.0, 0.0, 1.0)) * profile;
    // (the profile again after the threshold: tops and bases rounded off, not cut)
    double density = saturate(remap(base, 1.0 - (kCoverThreshold * cover), 1.0, 0.0, 1.0)) * profile;
    if (detail && density > 0.0) {
        const std::array<double, 4> fine =
            sample_volume(noise.detail, kCloudDetailSize, scaled(point, 1.0 / kCloudDetailScale));
        const double detail_fbm = (core::at(fine, 0) * kOctaveFirst) + (core::at(fine, 1) * kOctaveSecond) +
                                  (core::at(fine, 2) * kOctaveThird);
        const double erode = lerp(detail_fbm, 1.0 - detail_fbm, saturate(height * kWispyFrom));
        density = saturate(remap(density, erode * kDetailErosion, 1.0, 0.0, 1.0));
    }
    return density;
}

double cloud_phase(double cosine, double asymmetry_scale)
{
    return (kCloudForwardShare * henyey_greenstein(cosine, kCloudForwardG * asymmetry_scale)) +
           ((1.0 - kCloudForwardShare) * henyey_greenstein(cosine, kCloudBackG * asymmetry_scale));
}

CloudResult march_clouds(const CloudNoise& noise, const CloudRay& ray)
{
    CloudResult result;
    const Vec3d origin{0.0, kGroundRadius + ray.camera_altitude, 0.0};
    const Vec3d& way = ray.direction;
    if (meets_ground(origin, way)) {
        return result;
    }
    const double start = leave_sphere(origin, way, kGroundRadius + kCloudBase);
    double end = leave_sphere(origin, way, kGroundRadius + kCloudTop);
    if (start < 0.0 || end <= start) {
        return result;
    }
    result.marched = true;
    end = std::min(end, start + kCloudFar);
    const uint32_t steps = std::max(1U, ray.steps);
    const double span = (end - start) / steps;
    const double cosine = (core::at(way, 0) * core::at(ray.to_sun, 0)) + (core::at(way, 1) * core::at(ray.to_sun, 1)) +
                          (core::at(way, 2) * core::at(ray.to_sun, 2));
    std::array<double, kCloudOctaves> phases{};
    double asymmetry = 1.0;
    for (std::size_t k = 0; k < kCloudOctaves; ++k) {
        core::at(phases, k) = cloud_phase(cosine, asymmetry);
        asymmetry *= kOctaveAsymmetry;
    }
    const double scale = kCloudDensity * ray.density_scale;
    double transmittance = 1.0;
    double weighted = 0.0;
    double weight = 0.0;
    for (uint32_t i = 0; i < steps && transmittance >= kCloudOpaque; ++i) {
        const double along_ray = start + ((i + ray.jitter) * span);
        const Vec3d at{core::at(origin, 0) + (core::at(way, 0) * along_ray),
                       core::at(origin, 1) + (core::at(way, 1) * along_ray),
                       core::at(origin, 2) + (core::at(way, 2) * along_ray)};
        const double altitude = std::sqrt((core::at(at, 0) * core::at(at, 0)) + (core::at(at, 1) * core::at(at, 1)) +
                                          (core::at(at, 2) * core::at(at, 2))) -
                                kGroundRadius;
        const Vec3d point{core::at(at, 0) + core::at(ray.offset, 0), altitude,
                          core::at(at, 2) + core::at(ray.offset, 1)};
        const double density = cloud_density(noise, point, ray.coverage, true);
        if (density <= 0.0) {
            continue;
        }
        const double sigma = density * scale;
        // the sun's way through the cloud
        double depth = 0.0;
        double along = 0.0;
        double length = kCloudLightStep;
        for (uint32_t j = 0; j < kCloudLightSamples; ++j) {
            const double to_sample = along + (kHalf * length);
            const Vec3d light_at{core::at(point, 0) + (core::at(ray.to_sun, 0) * to_sample),
                                 core::at(point, 1) + (core::at(ray.to_sun, 1) * to_sample),
                                 core::at(point, 2) + (core::at(ray.to_sun, 2) * to_sample)};
            depth += cloud_density(noise, light_at, ray.coverage, false) * scale * length;
            along += length;
            length *= kCloudLightGrowth;
        }
        double lit = 0.0;
        double share = 1.0;
        double extinction = 1.0;
        for (std::size_t k = 0; k < kCloudOctaves; ++k) {
            lit += share * core::at(phases, k) * std::exp(-extinction * depth);
            share *= kOctaveShare;
            extinction *= kOctaveExtinction;
        }
        const double powder = lerp(1.0, 1.0 - std::exp(-2.0 * sigma * kCloudPowderLength), kCloudPowderShare);
        const double height = saturate((altitude - kCloudBase) / (kCloudTop - kCloudBase));
        const double kept = std::exp(-sigma * span);
        const double integral = (1.0 - kept); // (S - S kept) / sigma with S = sigma x the light
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const double ambient =
                lerp(core::at(ray.ambient_bottom, channel), core::at(ray.ambient_top, channel), height);
            const double source = (core::at(ray.sun_light, channel) * lit * powder) + ambient;
            core::at(result.light, channel) += transmittance * source * integral;
        }
        weighted += transmittance * (1.0 - kept) * along_ray;
        weight += transmittance * (1.0 - kept);
        transmittance *= kept;
    }
    result.transmittance = transmittance;
    result.distance = weight > kWeightFloor ? weighted / weight : -1.0;
    return result;
}

} // namespace render::rt
