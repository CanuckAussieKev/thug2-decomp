// NOT ORIGINAL - what a level's own sky looks like (rt_sky_match.h).
#include "engine/gfx/rt_sky_match.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"

namespace render::rt {

namespace {

constexpr double kHalf = 0.5;
constexpr double kDegrees = 180.0 / std::numbers::pi;
constexpr double kPerByte = 1.0 / 255.0;
constexpr std::array<double, 3> kLuma{0.2126, 0.7152, 0.0722};
constexpr uint32_t kColumns = 3;
constexpr uint32_t kRows = 2;
constexpr std::size_t kBytesPerTexel = 4;

using Sum = std::array<double, 3>;

struct BandSums {
    Sum clear{};
    double clear_weight = 0.0;
    Sum all{};
    double all_weight = 0.0;
    double covered = 0.0;
    double total = 0.0;
};

std::array<double, 3> cross3(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    return {(static_cast<double>(a[1]) * b[2]) - (static_cast<double>(a[2]) * b[1]),
            (static_cast<double>(a[2]) * b[0]) - (static_cast<double>(a[0]) * b[2]),
            (static_cast<double>(a[0]) * b[1]) - (static_cast<double>(a[1]) * b[0])};
}

Colour mean_of(const Sum& sum, double weight, const Colour& otherwise)
{
    if (!(weight > 0.0)) {
        return otherwise;
    }
    return {static_cast<float>(sum[0] / weight), static_cast<float>(sum[1] / weight), static_cast<float>(sum[2] / weight)};
}

void add(Sum* sum, const Colour& colour, double weight)
{
    for (std::size_t c = 0; c < 3; ++c) {
        core::at(*sum, c) += static_cast<double>(core::at(colour, c)) * weight;
    }
}

} // namespace

CaptureTexel capture_texel(uint32_t face, uint32_t x, uint32_t y, uint32_t face_size)
{
    const SkyCaptureFace& side = core::at(kSkyCaptureFaces, face % kSkyCaptureFaces.size());
    const double size = std::max(1U, face_size);
    const double across = ((2.0 * (static_cast<double>(x) + kHalf)) / size) - 1.0;
    const double up = 1.0 - ((2.0 * (static_cast<double>(y) + kHalf)) / size);
    const std::array<double, 3> right = cross3(side.forward, side.up); // (render_pc's right-handed look-at)
    std::array<double, 3> direction{};
    double length = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(direction, i) = static_cast<double>(core::at(side.forward, i)) + (across * core::at(right, i)) +
                                 (up * static_cast<double>(core::at(side.up, i)));
        length += core::at(direction, i) * core::at(direction, i);
    }
    length = std::sqrt(length);
    for (double& v : direction) {
        v /= length;
    }
    const double spread = 1.0 + (across * across) + (up * up);
    return {.direction = direction, .weight = 1.0 / (spread * std::sqrt(spread))};
}

double cloudiness(const Colour& colour)
{
    const double most = std::max({colour[0], colour[1], colour[2]});
    const double least = std::min({colour[0], colour[1], colour[2]});
    const double saturation = most > 1e-3 ? (most - least) / most : 0.0;
    return std::clamp(1.0 - ((saturation - kCloudGrey) / (kSkyBlue - kCloudGrey)), 0.0, 1.0);
}

SkyLook analyse_sky(const SkyCaptureTexels& capture)
{
    SkyLook look;
    const uint32_t size = capture.face_size;
    const std::size_t width = static_cast<std::size_t>(size) * kColumns;
    if (size == 0 || capture.bytes.size() < width * size * kRows * kBytesPerTexel) {
        return look;
    }
    std::array<BandSums, kSkyBands> bands{};
    Sum cloud{};
    double cloud_weight = 0.0;
    double coverage_sum = 0.0;
    double coverage_weight = 0.0;
    double luma_sum = 0.0;
    double luma_weight = 0.0;
    for (uint32_t face = 0; face < kSkyCaptureFaces.size(); ++face) {
        const std::size_t left = static_cast<std::size_t>(face % kColumns) * size;
        const std::size_t top = static_cast<std::size_t>(face / kColumns) * size;
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const std::size_t at = (((top + y) * width) + left + x) * kBytesPerTexel;
                const uint8_t first = core::at(capture.bytes, at);
                const uint8_t second = core::at(capture.bytes, at + 1);
                const uint8_t third = core::at(capture.bytes, at + 2);
                const std::array<uint8_t, 3> rgb =
                    capture.bgra ? std::array<uint8_t, 3>{third, second, first} : std::array<uint8_t, 3>{first, second, third};
                const CaptureTexel texel = capture_texel(face, x, y, size);
                const double elevation = std::asin(std::clamp(texel.direction[1], -1.0, 1.0)) * kDegrees;
                const bool drawn = rgb != capture.clear;
                const Colour colour{static_cast<float>(rgb[0] * kPerByte), static_cast<float>(rgb[1] * kPerByte),
                                    static_cast<float>(rgb[2] * kPerByte)};
                const double c = cloudiness(colour);
                for (std::size_t band = 0; band < kSkyBands; ++band) {
                    const BandRange& range = core::at(kSkyBandRanges, band);
                    if (elevation < range.low || elevation > range.high) {
                        continue;
                    }
                    BandSums& sums = core::at(bands, band);
                    sums.total += texel.weight;
                    if (!drawn) {
                        continue;
                    }
                    sums.covered += texel.weight;
                    const double clear_weight = texel.weight * (((1.0 - c) * (1.0 - c)) + kClearFloor);
                    add(&sums.clear, colour, clear_weight);
                    sums.clear_weight += clear_weight;
                    add(&sums.all, colour, texel.weight);
                    sums.all_weight += texel.weight;
                }
                if (!drawn) {
                    continue;
                }
                if (elevation >= kCoverageLowest && elevation <= kCoverageHighest) {
                    coverage_sum += c * texel.weight;
                    coverage_weight += texel.weight;
                    add(&cloud, colour, c * c * texel.weight);
                    cloud_weight += c * c * texel.weight;
                }
                if (elevation > 0.0) {
                    luma_sum += ((kLuma[0] * colour[0]) + (kLuma[1] * colour[1]) + (kLuma[2] * colour[2])) * texel.weight;
                    luma_weight += texel.weight;
                }
            }
        }
    }
    const Colour grey{kHalf, kHalf, kHalf};
    for (std::size_t band = 0; band < kSkyBands; ++band) {
        const BandSums& sums = core::at(bands, band);
        core::at(look.all, band) = mean_of(sums.all, sums.all_weight, grey);
        core::at(look.clear, band) = mean_of(sums.clear, sums.clear_weight, core::at(look.all, band));
        core::at(look.covered, band) = sums.total > 0.0 ? static_cast<float>(sums.covered / sums.total) : 0.0F;
    }
    look.coverage = coverage_weight > 0.0 ? static_cast<float>(coverage_sum / coverage_weight) : 0.0F;
    look.cloud = mean_of(cloud, cloud_weight, Colour{1.0F, 1.0F, 1.0F});
    look.brightness = luma_weight > 0.0 ? static_cast<float>(luma_sum / luma_weight) : 0.0F;
    // a sky worth matching: the backdrop covers some of the upper sky
    look.valid = core::at(look.covered, static_cast<std::size_t>(SkyBand::kHorizon)) > 0.25F ||
                 core::at(look.covered, static_cast<std::size_t>(SkyBand::kMiddle)) > 0.25F;
    return look;
}

} // namespace render::rt
