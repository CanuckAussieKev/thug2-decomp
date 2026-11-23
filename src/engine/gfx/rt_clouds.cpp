// NOT ORIGINAL - volumetric clouds: the model and its processor reference (rt_clouds.h). Every function here has a GLSL
// twin in gpu_vulkan/vulkan_rt_clouds.cpp - change both.
#include "engine/gfx/rt_clouds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <numbers>
#include <thread>
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
constexpr uint32_t kClumpSeed = 95;
constexpr double kPerlinStretch = 0.9; // Perlin's fractal widened toward 0 - 1
constexpr double kWeatherStretch = 1.1;
constexpr double kPhaseFloor = 1e-4;
constexpr double kWeightFloor = 1e-6;
// the height profile: the kind's top (kTopLowest - 1 of the layer), the base's soft edge, the top's
constexpr double kTopLowest = 0.45;
constexpr double kTopRange = 0.55;
constexpr double kBaseEdge = 0.12;
constexpr double kTopEdge = 0.4;
constexpr double kTopAtEdge = 0.25; // a cloud's top at the edge of its cover, x its kind's
constexpr double kWispyFrom = 5.0;  // (the detail inverts from billowy to wispy over the layer's lowest fifth)

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
            saturate((cloud_perlin(at(kCells2), {.period = kCells2, .seed = kKindSeed}) * kPerlinStretch) + kHalf),
            worley_fbm({u, v, kHalf}, kCells16, kClumpSeed), 1.0};
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

namespace {
// (round S: shared by cloud_density and the layer's light - cloud_layer_light) The density from the weather's texel and
// the shape's at a height in the layer (0 - 1), before the detail.
double density_from(const std::array<double, 4>& weather, const std::array<double, 4>& shape, double height,
                    double coverage)
{
    const double cover = cloud_cover(weather, coverage);
    if (cover <= 0.0) {
        return 0.0;
    }
    // (the top lower where the cover thins - toward a clump's edge: domes, not walls)
    const double top = (kTopLowest + (kTopRange * core::at(weather, 1))) * lerp(kTopAtEdge, 1.0, cover);
    const double profile = saturate(height / kBaseEdge) * saturate((top - height) / (kTopEdge * top));
    if (profile <= 0.0) {
        return 0.0;
    }
    const double fbm = (core::at(shape, 1) * kOctaveFirst) + (core::at(shape, 2) * kOctaveSecond) +
                       (core::at(shape, 3) * kOctaveThird);
    const double base = saturate(remap(core::at(shape, 0), fbm - 1.0, 1.0, 0.0, 1.0)) * profile;
    // (the profile again after the threshold: tops and bases rounded off, not cut)
    return saturate(remap(base, 1.0 - (cloud_threshold_share(coverage) * cover), 1.0, 0.0, 1.0)) * profile;
}
} // namespace

double cloud_density(const CloudNoise& noise, const Vec3d& point, double coverage, bool detail)
{
    const double height = (core::at(point, 1) - kCloudBase) / (kCloudTop - kCloudBase);
    if (height <= 0.0 || height >= 1.0) {
        return 0.0;
    }
    const std::array<double, 4> weather =
        sample_plane(noise.weather, kCloudWeatherSize,
                     {core::at(point, 0) / kCloudWeatherScale, core::at(point, 2) / kCloudWeatherScale});
    if (cloud_cover(weather, coverage) <= 0.0) {
        return 0.0;
    }
    const std::array<double, 4> shape =
        sample_volume(noise.shape, kCloudShapeSize, scaled(point, 1.0 / kCloudShapeScale));
    double density = density_from(weather, shape, height, coverage);
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

double cloud_cover(const std::array<double, 4>& weather, double coverage)
{
    const double patch = saturate(((core::at(weather, 0) - (1.0 - coverage)) * kCoverEdge) + (kPatchLift * coverage));
    const double clumps = saturate(remap(core::at(weather, 2), kClumpLow, kClumpHigh, 0.0, 1.0));
    const double overcast = coverage * coverage * coverage * coverage;
    return patch * lerp(clumps, 1.0, overcast);
}

namespace {
std::vector<double> column_depths_at(double amount);
// (round L) each tenth's amount: the bisection on the model's own clouded share (cloud_clouded_share)
const std::array<double, kCloudTenths>& tenth_amounts()
{
    static const std::array<double, kCloudTenths> made = [] {
        std::array<double, kCloudTenths> out{};
        const double last = static_cast<double>(kCloudTenths - 1);
        out.back() = 1.0;
        for (std::size_t t = 1; t + 1 < kCloudTenths; ++t) {
            const double wanted = static_cast<double>(t) / last;
            double low = core::at(out, t - 1);
            double high = 1.0;
            constexpr int kSteps = 40;
            for (int step = 0; step < kSteps; ++step) {
                const double middle = 0.5 * (low + high);
                (cloud_clouded_share(middle) < wanted ? low : high) = middle;
            }
            core::at(out, t) = 0.5 * (low + high);
        }
        return out;
    }();
    return made;
}
} // namespace

double cloud_amount(double coverage)
{
    const std::array<double, kCloudTenths>& amounts = tenth_amounts();
    const double at = saturate(coverage) * static_cast<double>(kCloudTenths - 1);
    const auto low = std::min(static_cast<std::size_t>(at), kCloudTenths - 2);
    return lerp(core::at(amounts, low), core::at(amounts, low + 1), at - static_cast<double>(low));
}

void start_cloud_amounts()
{
    static std::once_flag started;
    std::call_once(started, [] { std::thread([] { (void)tenth_amounts(); }).detach(); });
}

double cloud_clouded_share(double amount)
{
    const std::vector<double> depths = column_depths_at(amount);
    std::size_t clouded = 0;
    for (const double tau : depths) {
        clouded += std::exp(-tau) < kCloudSeenTransmittance ? 1 : 0;
    }
    return depths.empty() ? 0.0 : static_cast<double>(clouded) / static_cast<double>(depths.size());
}

double cloud_threshold_share(double coverage) { return lerp(kCoverThresholdLeast, kCoverThresholdMost, coverage); }

double cloud_phase(double cosine, double asymmetry_scale)
{
    return (kCloudForwardShare * henyey_greenstein(cosine, kCloudForwardG * asymmetry_scale)) +
           ((1.0 - kCloudForwardShare) * henyey_greenstein(cosine, kCloudBackG * asymmetry_scale));
}

CloudSpan cloud_layer_span(double camera_altitude, const Vec3d& direction)
{
    const Vec3d origin{0.0, kGroundRadius + camera_altitude, 0.0};
    if (meets_ground(origin, direction)) {
        return {};
    }
    const double enter = leave_sphere(origin, direction, kGroundRadius + kCloudBase);
    const double leave = leave_sphere(origin, direction, kGroundRadius + kCloudTop);
    if (enter < 0.0 || leave <= enter) {
        return {};
    }
    return {.enter = enter, .leave = leave};
}

CloudResult march_clouds(const CloudNoise& noise, const CloudRay& ray)
{
    CloudResult result;
    const Vec3d origin{0.0, kGroundRadius + ray.camera_altitude, 0.0};
    const Vec3d& way = ray.direction;
    const CloudSpan layer = cloud_layer_span(ray.camera_altitude, way);
    if (layer.enter < 0.0) {
        return result;
    }
    const double start = layer.enter;
    double end = layer.leave;
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

std::vector<CloudSkyTap> cloud_sky_taps()
{
    std::vector<CloudSkyTap> taps;
    constexpr double kDegrees = std::numbers::pi / 180.0;
    const auto texel_of = [](double azimuth, double elevation) {
        const std::array<double, 2> uv = sky_view_uv(
            {std::cos(elevation) * std::cos(azimuth), std::sin(elevation), std::cos(elevation) * std::sin(azimuth)});
        const auto index = [](double unit, uint32_t size) {
            return std::min(size - 1, static_cast<uint32_t>(std::max(0.0, unit * static_cast<double>(size))));
        };
        return std::array<uint32_t, 2>{index(core::at(uv, 0), kSkyViewWidth), index(core::at(uv, 1), kSkyViewHeight)};
    };
    for (const bool top : {false, true}) {
        const std::size_t first = taps.size();
        double sum = 0.0;
        for (uint32_t row = 0; row < kCloudSkyRows; ++row) {
            const double elevation =
                (top ? 1.0 : -1.0) * (static_cast<double>(row) + kHalf) * (90.0 / kCloudSkyRows) * kDegrees;
            for (uint32_t column = 0; column < kCloudSkyAzimuths; ++column) {
                const double azimuth = (static_cast<double>(column) + kHalf) * (180.0 / kCloudSkyAzimuths) * kDegrees;
                const std::array<uint32_t, 2> texel = texel_of(azimuth, elevation);
                // (the texel's own elevation: its centre's)
                const Dir centre = sky_view_direction(
                    {(static_cast<double>(core::at(texel, 0)) + kHalf) / static_cast<double>(kSkyViewWidth),
                     (static_cast<double>(core::at(texel, 1)) + kHalf) / static_cast<double>(kSkyViewHeight)});
                const double rise = core::at(centre, 1);
                const double weight = std::sqrt(std::max(0.0, 1.0 - (rise * rise)));
                taps.push_back({.column = core::at(texel, 0), .row = core::at(texel, 1), .weight = weight, .top = top});
                sum += weight;
            }
        }
        for (std::size_t i = first; i < taps.size(); ++i) {
            core::at(taps, i).weight *= sum > 0.0 ? 1.0 / sum : 0.0;
        }
    }
    return taps;
}

CloudSkyLight cloud_sky_light(const std::vector<std::array<float, 4>>& table)
{
    CloudSkyLight out;
    // (round NIGHTSKY) an image with the moon's table under the sun's (and its marker row) while the marker says drawn
    const std::size_t table_texels = static_cast<std::size_t>(kSkyViewWidth) * kSkyViewHeight;
    const std::size_t marker = 2 * table_texels;
    const bool moon = table.size() > marker && std::get<3>(core::at(table, marker)) > 0.5F;
    for (const std::size_t first : {std::size_t{0}, table_texels}) {
        if (first != 0 && !moon) {
            continue;
        }
        for (const CloudSkyTap& tap : cloud_sky_taps()) {
            const std::size_t index = first + (static_cast<std::size_t>(tap.row) * kSkyViewWidth) + tap.column;
            if (index >= table.size()) {
                continue;
            }
            const std::array<float, 4>& texel = core::at(table, index);
            std::array<double, 3>& sum = tap.top ? out.top : out.bottom;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                core::at(sum, channel) += tap.weight * std::max(0.0, static_cast<double>(core::at(texel, channel)));
            }
        }
    }
    return out;
}

// --- (round S) the weather's cover and the layer's light ------------------------------------------------------------

namespace {
struct CoverSlots {
    std::mutex mutex;
    std::array<float, static_cast<std::size_t>(CloudCoverSource::kCount)> cover{-1.0F, -1.0F, -1.0F};
};
CoverSlots& cover_slots()
{
    static CoverSlots slots;
    return slots;
}
float cover_of(const CoverSlots& slots, CloudCoverSource source)
{
    return core::at(slots.cover, static_cast<std::size_t>(source));
}

// The columns' weather texels and, at each of kLayerHeights heights, the shape texel nearest the column's point there
// (made once: they don't depend on the coverage).
struct Columns {
    std::vector<std::array<double, 4>> weather; // kLayerColumns^2
    std::vector<std::array<double, 4>> shape;   // x kLayerHeights
    std::vector<std::array<double, 4>> detail;  // x kLayerHeights (round L: the march's detail erosion too)
};
// (round L) A noise volume's value at a point as the march samples it (sample_volume: trilinear between texel
// centres, wrapped) - from its texels' own function, so the light's columns see the field the drawn clouds do (its
// nearest texel alone has more variance: more of it over the cover's threshold - more cloud than the sky shows).
template <typename Texel>
std::array<double, 4> trilinear(Texel texel, uint32_t size, double scale, const Vec3d& point)
{
    std::array<double, 4> out{};
    const auto count = static_cast<int>(size);
    std::array<int, 3> low{};
    Vec3d frac{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double at = (core::at(point, axis) / scale * static_cast<double>(size)) - kHalf;
        const double floor = std::floor(at);
        core::at(low, axis) = static_cast<int>(floor);
        core::at(frac, axis) = at - floor;
    }
    for (int i = 0; i < 8; ++i) {
        const int ox = i & 1;
        const int oy = (i >> 1) & 1;
        const int oz = (i >> 2) & 1;
        const double w = (ox != 0 ? core::at(frac, 0) : 1.0 - core::at(frac, 0)) *
                         (oy != 0 ? core::at(frac, 1) : 1.0 - core::at(frac, 1)) *
                         (oz != 0 ? core::at(frac, 2) : 1.0 - core::at(frac, 2));
        const std::array<double, 4> value =
            texel(static_cast<uint32_t>(wrapped(core::at(low, 0) + ox, count)),
                  static_cast<uint32_t>(wrapped(core::at(low, 1) + oy, count)),
                  static_cast<uint32_t>(wrapped(core::at(low, 2) + oz, count)));
        for (std::size_t channel = 0; channel < out.size(); ++channel) {
            core::at(out, channel) += w * core::at(value, channel);
        }
    }
    return out;
}
uint32_t shape_texel_of(double metres)
{
    const double at = std::floor(metres / kCloudShapeScale * static_cast<double>(kCloudShapeSize));
    return wrapped(static_cast<int>(std::fmod(at, static_cast<double>(kCloudShapeSize))),
                   static_cast<int>(kCloudShapeSize));
}
const Columns& columns()
{
    static const Columns made = [] {
        Columns out;
        const uint32_t step = kCloudWeatherSize / kLayerColumns;
        out.weather.reserve(static_cast<std::size_t>(kLayerColumns) * kLayerColumns);
        out.shape.reserve(static_cast<std::size_t>(kLayerColumns) * kLayerColumns * kLayerHeights);
        out.detail.reserve(static_cast<std::size_t>(kLayerColumns) * kLayerColumns * kLayerHeights);
        for (uint32_t row = 0; row < kLayerColumns; ++row) {
            for (uint32_t column = 0; column < kLayerColumns; ++column) {
                const std::array<uint32_t, 2> texel{(column * step) + (step / 2), (row * step) + (step / 2)};
                out.weather.push_back(cloud_weather_texel(texel));
                const double x = (static_cast<double>(core::at(texel, 0)) + kHalf) / kCloudWeatherSize * kCloudWeatherScale;
                const double z = (static_cast<double>(core::at(texel, 1)) + kHalf) / kCloudWeatherSize * kCloudWeatherScale;
                for (uint32_t k = 0; k < kLayerHeights; ++k) {
                    const double y =
                        kCloudBase + ((static_cast<double>(k) + kHalf) / kLayerHeights * (kCloudTop - kCloudBase));
                    const Vec3d point{x, y, z};
                    out.shape.push_back(trilinear(cloud_shape_texel, kCloudShapeSize, kCloudShapeScale, point));
                    out.detail.push_back(trilinear(cloud_detail_texel, kCloudDetailSize, kCloudDetailScale, point));
                }
            }
        }
        return out;
    }();
    return made;
}

// Eddington's two-stream reflectance of a conservatively scattering layer (optical depth tau, asymmetry
// kCloudDropletG) lit from the direction whose zenith cosine is mu.
double layer_reflectance(double tau, double mu)
{
    const double k = 0.75 * (1.0 - kCloudDropletG) * tau;
    const double reflected = (k + ((0.5 - (0.75 * mu)) * (1.0 - std::exp(-tau / mu)))) / (1.0 + k);
    return std::clamp(reflected, 0.0, 1.0);
}

CloudLayerLight layer_light_of(const std::vector<double>& depths, double sine)
{
    CloudLayerLight light{.beam = 0.0, .sun_diffuse = 0.0, .sky_kept = 0.0, .mean_depth = 0.0};
    const double mu = std::max(sine, kLeastLightSine);
    for (const double tau : depths) {
        const double beam = sine > 0.0 ? std::exp(-tau / mu) : 0.0;
        const double through = 1.0 - layer_reflectance(tau, mu);
        const double diffuse_through = 1.0 - layer_reflectance(tau, kDiffuseCosine);
        // (the ground and the deck's base light each other: the ground's albedo x the deck's reflectance from below)
        const double back = 1.0 / (1.0 - (kGroundAlbedo * (1.0 - diffuse_through)));
        light.beam += beam;
        light.sun_diffuse += sine > 0.0 ? sine * ((through * back) - beam) : 0.0;
        light.sky_kept += diffuse_through * back;
        light.mean_depth += tau;
    }
    const double count = std::max(1.0, static_cast<double>(depths.size()));
    light.beam /= count;
    light.sun_diffuse /= count;
    light.sky_kept /= count;
    light.mean_depth /= count;
    return light;
}
} // namespace

void set_cloud_cover(CloudCoverSource source, float coverage)
{
    CoverSlots& slots = cover_slots();
    const std::scoped_lock lock(slots.mutex);
    const float kept = std::isfinite(coverage) && coverage >= 0.0F ? std::min(coverage, 1.0F) : -1.0F;
    core::at(slots.cover, static_cast<std::size_t>(source)) = kept;
}

float cloud_cover_of(CloudCoverSource source)
{
    CoverSlots& slots = cover_slots();
    const std::scoped_lock lock(slots.mutex);
    return cover_of(slots, source);
}

float weather_cloud_cover(Weather weather)
{
    switch (weather) {
    case Weather::kClear:
        return kClearAirCover;
    case Weather::kHazy:
        return kHazyAirCover;
    case Weather::kMisty:
        return kMistyAirCover;
    }
    return kClearAirCover;
}

float cloud_coverage()
{
    const Weather air = weather(); // (rt_sky.h: its own lock - taken before the slots')
    CoverSlots& slots = cover_slots();
    const std::scoped_lock lock(slots.mutex);
    const float settings = cover_of(slots, CloudCoverSource::kSettings);
    const float base = settings >= 0.0F ? settings : weather_cloud_cover(air);
    const float cover =
        std::max({base, cover_of(slots, CloudCoverSource::kSnow), cover_of(slots, CloudCoverSource::kRain)});
    return std::clamp(cover, 0.0F, 1.0F);
}

std::vector<double> cloud_column_depths(double coverage) { return column_depths_at(cloud_amount(coverage)); }

namespace {
std::vector<double> column_depths_at(double amount)
{
    const Columns& made = columns();
    const double dz = (kCloudTop - kCloudBase) / kLayerHeights;
    std::vector<double> depths(made.weather.size(), 0.0);
    for (std::size_t c = 0; c < made.weather.size(); ++c) {
        const std::array<double, 4>& weather = core::at(made.weather, c);
        if (amount <= 0.0 || cloud_cover(weather, amount) <= 0.0) {
            continue;
        }
        double tau = 0.0;
        for (uint32_t k = 0; k < kLayerHeights; ++k) {
            const double height = (static_cast<double>(k) + kHalf) / kLayerHeights;
            // (round L: the density as the drawn clouds' march has it - its detail eating the edges, cloud_density's;
            // without it the columns held cloud where the sky shows none: 54 % of them dimmed the sun at a quarter's
            // cover, which the drawn sky shows 75 % clear)
            double density = density_from(weather, core::at(made.shape, (c * kLayerHeights) + k), height, amount);
            if (density > 0.0) {
                const std::array<double, 4>& fine = core::at(made.detail, (c * kLayerHeights) + k);
                const double detail_fbm = (core::at(fine, 0) * kOctaveFirst) + (core::at(fine, 1) * kOctaveSecond) +
                                          (core::at(fine, 2) * kOctaveThird);
                const double erode = lerp(detail_fbm, 1.0 - detail_fbm, saturate(height * kWispyFrom));
                density = saturate(remap(density, erode * kDetailErosion, 1.0, 0.0, 1.0));
            }
            tau += kCloudDensity * density * dz;
        }
        core::at(depths, c) = tau;
    }
    return depths;
}
} // namespace

CloudLayerLight cloud_layer_light(double coverage, double sine)
{
    if (!(coverage > 0.0)) {
        return {};
    }
    // (the columns' depths at each tenth of the coverage, made once each, the light read between tenths)
    static std::mutex mutex;
    static std::array<std::vector<double>, kCloudTenths> tenths;
    const double last = static_cast<double>(kCloudTenths - 1);
    const double at = std::clamp(coverage, 0.0, 1.0) * last;
    const auto low = std::min(static_cast<std::size_t>(at), kCloudTenths - 2);
    const double share = at - static_cast<double>(low);
    std::array<CloudLayerLight, 2> ends{};
    for (std::size_t end = 0; end < ends.size(); ++end) {
        const std::size_t tenth = low + end;
        std::vector<double> depths;
        {
            const std::scoped_lock lock(mutex);
            std::vector<double>& made = core::at(tenths, tenth);
            if (made.empty()) {
                made = cloud_column_depths(static_cast<double>(tenth) / last);
            }
            depths = made;
        }
        core::at(ends, end) = layer_light_of(depths, sine);
    }
    const CloudLayerLight& a = ends[0];
    const CloudLayerLight& b = ends[1];
    return {.beam = lerp(a.beam, b.beam, share),
            .sun_diffuse = lerp(a.sun_diffuse, b.sun_diffuse, share),
            .sky_kept = lerp(a.sky_kept, b.sky_kept, share),
            .mean_depth = lerp(a.mean_depth, b.mean_depth, share)};
}

} // namespace render::rt
