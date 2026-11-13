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

double fade(double t) { return t * t * t * ((t * ((t * 6.0) - 15.0)) + 10.0); }

double lerp(double a, double b, double t) { return a + ((b - a) * t); }

double remap(double value, double low, double high, double to_low, double to_high)
{
    return to_low + ((value - low) * (to_high - to_low) / (high - low));
}

double saturate(double value) { return std::clamp(value, 0.0, 1.0); }

Vec3d scaled(const Vec3d& p, double by) { return {p[0] * by, p[1] * by, p[2] * by}; }

// Worley's fractal noise at `cells` (and twice, four times as many)
double worley_fbm(const Vec3d& p, int cells, uint32_t seed)
{
    return (0.625 * cloud_worley(scaled(p, cells), cells, seed)) +
           (0.25 * cloud_worley(scaled(p, 2.0 * cells), 2 * cells, seed + 1)) +
           (0.125 * cloud_worley(scaled(p, 4.0 * cells), 4 * cells, seed + 2));
}

double read_byte(const std::vector<uint8_t>& texels, std::size_t texel, std::size_t channel)
{
    return static_cast<double>(core::at(texels, (texel * kChannels) + channel)) * kPerByte;
}

double henyey_greenstein(double cosine, double g)
{
    const double k = std::max(1.0 + (g * g) - (2.0 * g * cosine), 1e-4);
    return (1.0 - (g * g)) / (4.0 * std::numbers::pi * k * std::sqrt(k));
}

// where a ray from `origin` (from the planet's centre) along `direction` leaves the sphere of `radius` round the
// centre (-1: it never meets it ahead)
double leave_sphere(const Vec3d& origin, const Vec3d& direction, double radius)
{
    const double b = (origin[0] * direction[0]) + (origin[1] * direction[1]) + (origin[2] * direction[2]);
    const double c = (origin[0] * origin[0]) + (origin[1] * origin[1]) + (origin[2] * origin[2]) - (radius * radius);
    const double d = (b * b) - c;
    if (d < 0.0) {
        return -1.0;
    }
    const double far = -b + std::sqrt(d);
    return far > 0.0 ? far : -1.0;
}

// whether the ray meets the ground ahead
bool meets_ground(const Vec3d& origin, const Vec3d& direction)
{
    const double b = (origin[0] * direction[0]) + (origin[1] * direction[1]) + (origin[2] * direction[2]);
    const double c = (origin[0] * origin[0]) + (origin[1] * origin[1]) + (origin[2] * origin[2]) -
                     (kGroundRadius * kGroundRadius);
    return b < 0.0 && ((b * b) - c) >= 0.0;
}

} // namespace

uint32_t cloud_hash(uint32_t x, uint32_t y, uint32_t z, uint32_t seed)
{
    uint32_t h = (x * 0x8DA6B343U) ^ (y * 0xD8163841U) ^ (z * 0xCB1AB31FU) ^ (seed * 0x165667B1U);
    h ^= h >> 16U;
    h *= 0x7FEB352DU;
    h ^= h >> 15U;
    h *= 0x846CA68BU;
    h ^= h >> 16U;
    return h;
}

double cloud_worley(const Vec3d& p, int period, uint32_t seed)
{
    const std::array<int, 3> cell{static_cast<int>(std::floor(p[0])), static_cast<int>(std::floor(p[1])),
                                  static_cast<int>(std::floor(p[2]))};
    const Vec3d inside{p[0] - cell[0], p[1] - cell[1], p[2] - cell[2]};
    double best = 9.0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const uint32_t wx = wrapped(cell[0] + dx, period);
                const uint32_t wy = wrapped(cell[1] + dy, period);
                const uint32_t wz = wrapped(cell[2] + dz, period);
                const double fx = dx + unit_of(cloud_hash(wx, wy, wz, seed)) - inside[0];
                const double fy = dy + unit_of(cloud_hash(wx, wy, wz, seed + 1)) - inside[1];
                const double fz = dz + unit_of(cloud_hash(wx, wy, wz, seed + 2)) - inside[2];
                best = std::min(best, (fx * fx) + (fy * fy) + (fz * fz));
            }
        }
    }
    return 1.0 - saturate(std::sqrt(best));
}

double cloud_perlin(const Vec3d& p, int period, uint32_t seed)
{
    const std::array<int, 3> cell{static_cast<int>(std::floor(p[0])), static_cast<int>(std::floor(p[1])),
                                  static_cast<int>(std::floor(p[2]))};
    const Vec3d f{p[0] - cell[0], p[1] - cell[1], p[2] - cell[2]};
    std::array<double, 8> corner{};
    for (int i = 0; i < 8; ++i) {
        const int ox = i & 1;
        const int oy = (i >> 1) & 1;
        const int oz = (i >> 2) & 1;
        const uint32_t h = cloud_hash(wrapped(cell[0] + ox, period), wrapped(cell[1] + oy, period),
                                      wrapped(cell[2] + oz, period), seed);
        const std::array<int, 3>& g = core::at(kGradients, h & 15U);
        core::at(corner, i) = (g[0] * (f[0] - ox)) + (g[1] * (f[1] - oy)) + (g[2] * (f[2] - oz));
    }
    const double ux = fade(f[0]);
    const double uy = fade(f[1]);
    const double uz = fade(f[2]);
    const double x00 = lerp(corner[0], corner[1], ux);
    const double x10 = lerp(corner[2], corner[3], ux);
    const double x01 = lerp(corner[4], corner[5], ux);
    const double x11 = lerp(corner[6], corner[7], ux);
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz);
}

std::array<double, 4> cloud_shape_texel(uint32_t x, uint32_t y, uint32_t z)
{
    const double size = kCloudShapeSize;
    const Vec3d p{(x + kHalf) / size, (y + kHalf) / size, (z + kHalf) / size};
    const double perlin = (cloud_perlin(scaled(p, 4.0), 4, 11) * 0.5) + (cloud_perlin(scaled(p, 8.0), 8, 12) * 0.25) +
                          (cloud_perlin(scaled(p, 16.0), 16, 13) * 0.125);
    const double perlin01 = saturate((perlin * 0.9) + 0.5);
    const double worley = worley_fbm(p, 4, 21);
    return {saturate(remap(perlin01, worley - 1.0, 1.0, 0.0, 1.0)), worley, worley_fbm(p, 8, 31),
            worley_fbm(p, 16, 41)};
}

std::array<double, 4> cloud_detail_texel(uint32_t x, uint32_t y, uint32_t z)
{
    const double size = kCloudDetailSize;
    const Vec3d p{(x + kHalf) / size, (y + kHalf) / size, (z + kHalf) / size};
    return {worley_fbm(p, 4, 51), worley_fbm(p, 8, 61), worley_fbm(p, 16, 71), 1.0};
}

std::array<double, 4> cloud_weather_texel(uint32_t x, uint32_t y)
{
    const double size = kCloudWeatherSize;
    const double u = (x + kHalf) / size;
    const double v = (y + kHalf) / size;
    const auto at = [u, v](double cells) { return Vec3d{u * cells, v * cells, kHalf}; };
    const double fbm = (cloud_perlin(at(4.0), 4, 81) * 0.5) + (cloud_perlin(at(8.0), 8, 82) * 0.25) +
                       (cloud_perlin(at(16.0), 16, 83) * 0.125) + (cloud_perlin(at(32.0), 32, 84) * 0.0625);
    return {saturate((fbm * 1.1) + 0.5), saturate((cloud_perlin(at(2.0), 2, 91) * 0.9) + 0.5), 0.0, 1.0};
}

std::array<double, 4> sample_volume(const std::vector<uint8_t>& texels, uint32_t size, const Vec3d& uvw)
{
    std::array<double, 4> out{};
    const auto n = static_cast<int>(size);
    std::array<int, 3> low{};
    Vec3d frac{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double at = (uvw[axis] * size) - kHalf;
        const double floor = std::floor(at);
        core::at(low, axis) = static_cast<int>(floor);
        core::at(frac, axis) = at - floor;
    }
    for (int i = 0; i < 8; ++i) {
        const int ox = i & 1;
        const int oy = (i >> 1) & 1;
        const int oz = (i >> 2) & 1;
        const double w = (ox != 0 ? frac[0] : 1.0 - frac[0]) * (oy != 0 ? frac[1] : 1.0 - frac[1]) *
                         (oz != 0 ? frac[2] : 1.0 - frac[2]);
        const std::size_t texel = (((static_cast<std::size_t>(wrapped(low[2] + oz, n)) * size) + wrapped(low[1] + oy, n)) * size) +
                                  wrapped(low[0] + ox, n);
        for (std::size_t c = 0; c < kChannels; ++c) {
            core::at(out, c) += w * read_byte(texels, texel, c);
        }
    }
    return out;
}

std::array<double, 4> sample_plane(const std::vector<uint8_t>& texels, uint32_t size, double u, double v)
{
    std::array<double, 4> out{};
    const auto n = static_cast<int>(size);
    const double ax = (u * size) - kHalf;
    const double ay = (v * size) - kHalf;
    const double fx = ax - std::floor(ax);
    const double fy = ay - std::floor(ay);
    const auto lx = static_cast<int>(std::floor(ax));
    const auto ly = static_cast<int>(std::floor(ay));
    for (int i = 0; i < 4; ++i) {
        const int ox = i & 1;
        const int oy = (i >> 1) & 1;
        const double w = (ox != 0 ? fx : 1.0 - fx) * (oy != 0 ? fy : 1.0 - fy);
        const std::size_t texel = (static_cast<std::size_t>(wrapped(ly + oy, n)) * size) + wrapped(lx + ox, n);
        for (std::size_t c = 0; c < kChannels; ++c) {
            core::at(out, c) += w * read_byte(texels, texel, c);
        }
    }
    return out;
}

double cloud_density(const CloudNoise& noise, const Vec3d& point, double coverage, bool detail)
{
    const double height = (point[1] - kCloudBase) / (kCloudTop - kCloudBase);
    if (height <= 0.0 || height >= 1.0) {
        return 0.0;
    }
    const std::array<double, 4> weather =
        sample_plane(noise.weather, kCloudWeatherSize, point[0] / kCloudWeatherScale, point[2] / kCloudWeatherScale);
    const double cover = saturate((2.0 * coverage) - 1.0 + weather[0]);
    if (cover <= 0.0) {
        return 0.0;
    }
    const double top = 0.45 + (0.55 * weather[1]);
    const double profile = saturate(height / 0.12) * saturate((top - height) / (0.4 * top));
    if (profile <= 0.0) {
        return 0.0;
    }
    const std::array<double, 4> shape = sample_volume(noise.shape, kCloudShapeSize, scaled(point, 1.0 / kCloudShapeScale));
    const double fbm = (shape[1] * 0.625) + (shape[2] * 0.25) + (shape[3] * 0.125);
    const double base = saturate(remap(shape[0], fbm - 1.0, 1.0, 0.0, 1.0)) * profile;
    double density = saturate(remap(base, 1.0 - cover, 1.0, 0.0, 1.0)) * cover;
    if (detail && density > 0.0) {
        const std::array<double, 4> fine =
            sample_volume(noise.detail, kCloudDetailSize, scaled(point, 1.0 / kCloudDetailScale));
        const double detail_fbm = (fine[0] * 0.625) + (fine[1] * 0.25) + (fine[2] * 0.125);
        const double erode = lerp(detail_fbm, 1.0 - detail_fbm, saturate(height * 5.0));
        density = saturate(remap(density, erode * 0.3, 1.0, 0.0, 1.0));
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
    const Vec3d& d = ray.direction;
    if (meets_ground(origin, d)) {
        return result;
    }
    const double start = leave_sphere(origin, d, kGroundRadius + kCloudBase);
    double end = leave_sphere(origin, d, kGroundRadius + kCloudTop);
    if (start < 0.0 || end <= start) {
        return result;
    }
    result.marched = true;
    end = std::min(end, start + kCloudFar);
    const uint32_t steps = std::max(1U, ray.steps);
    const double span = (end - start) / steps;
    const double cosine = (d[0] * ray.to_sun[0]) + (d[1] * ray.to_sun[1]) + (d[2] * ray.to_sun[2]);
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
        const double t = start + ((i + ray.jitter) * span);
        const Vec3d at{origin[0] + (d[0] * t), origin[1] + (d[1] * t), origin[2] + (d[2] * t)};
        const double altitude = std::sqrt((at[0] * at[0]) + (at[1] * at[1]) + (at[2] * at[2])) - kGroundRadius;
        const Vec3d point{at[0] + ray.offset[0], altitude, at[2] + ray.offset[1]};
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
            const double s = along + (kHalf * length);
            const Vec3d light_at{point[0] + (ray.to_sun[0] * s), point[1] + (ray.to_sun[1] * s),
                                 point[2] + (ray.to_sun[2] * s)};
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
        for (std::size_t c = 0; c < 3; ++c) {
            const double ambient = lerp(core::at(ray.ambient_bottom, c), core::at(ray.ambient_top, c), height);
            const double source = (core::at(ray.sun_light, c) * lit * powder) + ambient;
            core::at(result.light, c) += transmittance * source * integral;
        }
        weighted += transmittance * (1.0 - kept) * t;
        weight += transmittance * (1.0 - kept);
        transmittance *= kept;
    }
    result.transmittance = transmittance;
    result.distance = weight > 1e-6 ? weighted / weight : -1.0;
    return result;
}

} // namespace render::rt
