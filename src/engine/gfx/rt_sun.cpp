// NOT ORIGINAL - the sun a level's lighting was baked with: see rt_sun.h.
#include "engine/gfx/rt_sun.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"

namespace render::rt {

namespace {

constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
constexpr double kGoldenAngle = 2.39996322972865332; // radians
constexpr double kHalf = 0.5;
constexpr double kMinGroup = 0.03;   // either group under this share of the samples: no score
constexpr float kOffset = 2.0F;      // the rays start this far off the surface (world units)
constexpr float kRayLength = 1.0e6F; // (further than any level)
constexpr uint32_t kMaskWorld = 0x01; // the opaque level geometry (the TLAS's instance masks)
constexpr std::size_t kCoarse = 240;
constexpr std::size_t kFine = 60;
constexpr float kFineDegrees = 10.0F;
constexpr std::size_t kFinest = 40;
constexpr float kFinestDegrees = 3.0F;
constexpr float kNearlyVertical = 0.99F; // an axis this close to straight up takes x for its side
constexpr double kMinCells = 4.0;        // the agreement grid's cells along its longer side, at least
constexpr float kCellSlack = 1.0001F;    // (the last sample inside the last cell)
constexpr float kCentreWeight = 2.0F;    // a cell's own share counts this many times its neighbours'
constexpr float kNotScored = -1.0F;

Vec3 normalised(const Vec3& v)
{
    const double length = std::hypot(static_cast<double>(x_of(v)), static_cast<double>(y_of(v)),
                                      static_cast<double>(z_of(v)));
    if (!(length > 0.0)) {
        return {0.0F, 1.0F, 0.0F};
    }
    return scaled(v, static_cast<float>(1.0 / length));
}

// Each direction's score, its rays traced from every sample.
std::vector<float> scores_of(std::span<const BakeSample> samples, std::span<const Vec3> directions, RayTracing& tracer,
                             std::size_t* rays)
{
    std::vector<ProbeRequest> requests;
    requests.reserve(samples.size() * directions.size());
    for (const Vec3& toward : directions) {
        for (const BakeSample& sample : samples) {
            requests.push_back({.origin = plus(sample.position, scaled(sample.normal, kOffset)),
                                .direction = toward,
                                .max_distance = kRayLength,
                                .cull_mask = kMaskWorld});
        }
    }
    std::vector<ProbeResult> results;
    tracer.probe_many(requests, &results);
    *rays += requests.size();
    std::vector<float> scores(directions.size(), kNotScored);
    if (results.size() != requests.size()) {
        return scores;
    }
    std::vector<uint8_t> lit(samples.size());
    for (std::size_t direction = 0; direction < directions.size(); ++direction) {
        bool traced = true;
        const Vec3& toward = core::at(directions, direction);
        for (std::size_t sample = 0; sample < samples.size(); ++sample) {
            const ProbeResult& result = core::at(results, (direction * samples.size()) + sample);
            traced = traced && result.traced;
            const bool facing = dot(core::at(samples, sample).normal, toward) > 0.0F;
            core::at(lit, sample) = facing && !result.hit ? 1 : 0;
        }
        core::at(scores, direction) = traced ? sun_score(samples, lit) : kNotScored;
    }
    return scores;
}

// The best of a set of directions: its index (the first of equals).
std::size_t best_of(const std::vector<float>& scores)
{
    return static_cast<std::size_t>(std::distance(scores.begin(), std::ranges::max_element(scores)));
}

// The low and high values of the samples along an axis, but their outermost kAgreementOutliers on each side.
std::pair<float, float> bounds_of(std::span<const BakeSample> samples, std::size_t axis)
{
    std::vector<float> values(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        core::at(values, i) = core::at(core::at(samples, i).position, axis);
    }
    const auto edge = static_cast<std::size_t>(static_cast<double>(values.size() - 1) * kAgreementOutliers);
    const std::size_t last = values.size() - 1 - edge;
    std::ranges::nth_element(values, values.begin() + static_cast<std::ptrdiff_t>(edge));
    const float low = core::at(values, edge);
    std::ranges::nth_element(values, values.begin() + static_cast<std::ptrdiff_t>(last));
    return {low, core::at(values, last)};
}

} // namespace

float sun_score(std::span<const BakeSample> samples, std::span<const uint8_t> lit)
{
    const std::size_t count = std::min(samples.size(), lit.size());
    if (count == 0) {
        return 0.0F;
    }
    double sum = 0.0;
    double squares = 0.0;
    double lit_sum = 0.0;
    std::size_t lit_count = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double value = core::at(samples, i).brightness;
        sum += value;
        squares += value * value;
        if (core::at(lit, i) != 0) {
            lit_sum += value;
            ++lit_count;
        }
    }
    const auto total = static_cast<double>(count);
    const double share = static_cast<double>(lit_count) / total;
    if (share < kMinGroup || share > 1.0 - kMinGroup) {
        return 0.0F;
    }
    const double mean = sum / total;
    const double deviation = std::sqrt(std::max(0.0, (squares / total) - (mean * mean)));
    if (!(deviation > 0.0)) {
        return 0.0F;
    }
    const double lit_mean = lit_sum / static_cast<double>(lit_count);
    const double shaded_mean = (sum - lit_sum) / (total - static_cast<double>(lit_count));
    return static_cast<float>(((lit_mean - shaded_mean) / deviation) * std::sqrt(share * (1.0 - share)));
}

std::vector<Vec3> sky_directions(std::size_t count)
{
    std::vector<Vec3> out;
    const double low = std::sin(static_cast<double>(kMinElevationDegrees) * kRadiansPerDegree);
    for (std::size_t i = 0; i < count; ++i) {
        // (equal areas: the height even between the lowest elevation and straight up; the azimuth by the golden angle)
        const double height = low + ((1.0 - low) * (static_cast<double>(i) + kHalf) / static_cast<double>(count));
        const double across = std::sqrt(std::max(0.0, 1.0 - (height * height)));
        const double angle = kGoldenAngle * static_cast<double>(i);
        out.push_back({static_cast<float>(across * std::sin(angle)), static_cast<float>(height),
                       static_cast<float>(across * std::cos(angle))});
    }
    return out;
}

std::vector<Vec3> directions_round(const Cone& cone)
{
    const Vec3 axis = normalised(cone.centre);
    const float degrees = cone.degrees;
    const std::size_t count = cone.count;
    const Vec3 side = normalised(std::abs(y_of(axis)) < kNearlyVertical ? cross(axis, {0.0F, 1.0F, 0.0F})
                                                                         : cross(axis, {1.0F, 0.0F, 0.0F}));
    const Vec3 up = cross(axis, side);
    const double low = std::sin(static_cast<double>(kMinElevationDegrees) * kRadiansPerDegree);
    const double cos_max = std::cos(static_cast<double>(degrees) * kRadiansPerDegree);
    std::vector<Vec3> out{axis};
    for (std::size_t i = 1; i < count; ++i) {
        const double along = 1.0 - ((1.0 - cos_max) * static_cast<double>(i) / static_cast<double>(count - 1));
        const double out_from = std::sqrt(std::max(0.0, 1.0 - (along * along)));
        const double angle = kGoldenAngle * static_cast<double>(i);
        const Vec3 direction = plus(plus(scaled(axis, static_cast<float>(along)),
                                         scaled(side, static_cast<float>(out_from * std::cos(angle)))),
                                    scaled(up, static_cast<float>(out_from * std::sin(angle))));
        if (y_of(direction) >= low) {
            out.push_back(normalised(direction));
        }
    }
    return out;
}

SunFit fit_baked_sun(std::span<const BakeSample> samples, RayTracing& tracer)
{
    SunFit fit;
    fit.samples = samples.size();
    if (samples.empty()) {
        return fit;
    }
    std::vector<Vec3> directions = sky_directions(kCoarse);
    std::vector<float> scores = scores_of(samples, directions, tracer, &fit.rays);
    Vec3 best = core::at(directions, best_of(scores));
    float best_score = std::ranges::max(scores);
    // (the coarse set's next best, for the log)
    std::vector<std::size_t> order(scores.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order, [&scores](std::size_t one, std::size_t other) {
        return core::at(scores, one) > core::at(scores, other);
    });
    for (std::size_t k = 0; k < fit.runners_up.size() && k + 1 < order.size(); ++k) {
        const std::size_t index = core::at(order, k + 1);
        const Vec3& toward = core::at(directions, index);
        core::at(fit.runners_up, k) = {x_of(toward), y_of(toward), z_of(toward), core::at(scores, index)};
    }
    for (const auto& [count, degrees] : {std::pair{kFine, kFineDegrees}, std::pair{kFinest, kFinestDegrees}}) {
        directions = directions_round({.centre = best, .degrees = degrees, .count = count});
        scores = scores_of(samples, directions, tracer, &fit.rays);
        const std::size_t at = best_of(scores);
        if (core::at(scores, at) > best_score) {
            best_score = core::at(scores, at);
            best = core::at(directions, at);
        }
    }
    fit.sun.toward = best;
    fit.sun.confidence = std::max(0.0F, best_score);
    fit.sun.found = best_score >= kMinConfidence;
    // (added by the sun's glare) the level's ground: the samples' heights' kGroundShare quantile
    std::vector<float> heights;
    heights.reserve(samples.size());
    for (const BakeSample& sample : samples) {
        heights.push_back(core::at(sample.position, 1));
    }
    const auto at = static_cast<std::ptrdiff_t>(static_cast<double>(heights.size() - 1) * kGroundShare);
    std::ranges::nth_element(heights, heights.begin() + at);
    fit.sun.ground = *(heights.begin() + at);
    fit.sun.ground_known = true;
    return fit;
}

void make_agreement(std::span<const BakeSample> samples, std::span<const uint8_t> lit, BakedSun* sun)
{
    sun->agreement.clear();
    sun->columns = 0;
    sun->rows = 0;
    const std::span<const BakeSample> used = samples.first(std::min(samples.size(), lit.size()));
    double lit_sum = 0.0;
    double blocked_sum = 0.0;
    std::size_t lit_count = 0;
    for (std::size_t i = 0; i < used.size(); ++i) {
        const float brightness = core::at(used, i).brightness;
        if (core::at(lit, i) != 0) {
            lit_sum += brightness;
            ++lit_count;
        } else {
            blocked_sum += brightness;
        }
    }
    const std::size_t blocked_count = used.size() - lit_count;
    const double least = kMinGroup * static_cast<double>(used.size());
    if (used.empty() || static_cast<double>(lit_count) < least || static_cast<double>(blocked_count) < least) {
        return;
    }
    const double split =
        ((lit_sum / static_cast<double>(lit_count)) + (blocked_sum / static_cast<double>(blocked_count))) * kHalf;
    // the grid: over the samples but the outermost; along the longer side as many cells as give each about
    // kAgreementPerCell blocked samples
    const auto [low_x, high_x] = bounds_of(used, 0);
    const auto [low_z, high_z] = bounds_of(used, 2);
    const float side = std::max({high_x - low_x, high_z - low_z, 1.0F});
    const double across = std::sqrt(static_cast<double>(blocked_count) / kAgreementPerCell);
    const auto cells = static_cast<float>(std::clamp(std::floor(across), kMinCells, double{kAgreementSide}));
    const float cell = (side / cells) * kCellSlack;
    const auto columns =
        static_cast<uint32_t>(std::clamp(std::ceil((high_x - low_x) / cell), 1.0F, float{kAgreementSide}));
    const auto rows = static_cast<uint32_t>(std::clamp(std::ceil((high_z - low_z) / cell), 1.0F, float{kAgreementSide}));
    const auto cell_of = [&](const Vec3& at) {
        const auto column =
            static_cast<std::size_t>(std::clamp((x_of(at) - low_x) / cell, 0.0F, static_cast<float>(columns - 1)));
        const auto row =
            static_cast<std::size_t>(std::clamp((z_of(at) - low_z) / cell, 0.0F, static_cast<float>(rows - 1)));
        return (row * columns) + column;
    };
    std::vector<float> blocked(static_cast<std::size_t>(columns) * rows, 0.0F);
    std::vector<float> dark(blocked.size(), 0.0F);
    std::size_t dark_count = 0;
    for (std::size_t i = 0; i < used.size(); ++i) {
        const BakeSample& sample = core::at(used, i);
        if (core::at(lit, i) != 0) {
            continue;
        }
        const std::size_t at = cell_of(sample.position);
        core::at(blocked, at) += 1.0F;
        if (sample.brightness < split) {
            core::at(dark, at) += 1.0F;
            ++dark_count;
        }
    }
    // each cell's share of dark among its blocked samples (pulled toward the level's), smoothed over its neighbours
    const float level_share = static_cast<float>(dark_count) / static_cast<float>(blocked_count);
    sun->agreement.assign(blocked.size(), 1.0F);
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t column = 0; column < columns; ++column) {
            float weights = 0.0F;
            float sum = 0.0F;
            for (uint32_t near_row = row > 0 ? row - 1 : 0; near_row <= std::min(rows - 1, row + 1); ++near_row) {
                for (uint32_t near_column = column > 0 ? column - 1 : 0; near_column <= std::min(columns - 1, column + 1);
                     ++near_column) {
                    const std::size_t at = (static_cast<std::size_t>(near_row) * columns) + near_column;
                    const float count = core::at(blocked, at);
                    const float share = (core::at(dark, at) + (kAgreementPrior * level_share)) / (count + kAgreementPrior);
                    const float weight = (near_row == row && near_column == column ? kCentreWeight : 1.0F) * (count + 1.0F);
                    weights += weight;
                    sum += weight * share;
                }
            }
            const float value =
                std::clamp(((sum / weights) - kAgreementLow) / (kAgreementHigh - kAgreementLow), 0.0F, 1.0F);
            constexpr float kSmoothA = 3.0F; // (smoothstep: 3v^2 - 2v^3)
            constexpr float kSmoothB = 2.0F;
            core::at(sun->agreement, (static_cast<std::size_t>(row) * columns) + column) =
                value * value * (kSmoothA - (kSmoothB * value));
        }
    }
    sun->columns = columns;
    sun->rows = rows;
    sun->origin = {low_x, low_z};
    sun->cell = cell;
}

void trace_agreement(std::span<const BakeSample> samples, RayTracing& tracer, BakedSun* sun)
{
    std::vector<ProbeRequest> rays;
    rays.reserve(samples.size());
    for (const BakeSample& sample : samples) {
        rays.push_back({.origin = plus(sample.position, scaled(sample.normal, kOffset)),
                        .direction = sun->toward,
                        .max_distance = kRayLength,
                        .cull_mask = kMaskWorld});
    }
    std::vector<ProbeResult> hits;
    tracer.probe_many(rays, &hits);
    if (hits.size() != samples.size()) {
        return;
    }
    std::vector<uint8_t> lit(samples.size());
    std::ranges::transform(hits, lit.begin(), [](const ProbeResult& hit) { return hit.hit ? uint8_t{0} : uint8_t{1}; });
    make_agreement(samples, lit, sun);
}

float agreement_at(const BakedSun& sun, const Vec3& point)
{
    const float x = x_of(point);
    const float z = z_of(point);
    if (sun.agreement.empty() || sun.columns == 0 || sun.rows == 0 || !(sun.cell > 0.0F)) {
        return 1.0F;
    }
    constexpr float kCentre = 0.5F; // (a cell's value is at its centre)
    const float grid_x = ((x - std::get<0>(sun.origin)) / sun.cell) - kCentre;
    const float grid_z = ((z - std::get<1>(sun.origin)) / sun.cell) - kCentre;
    const float first_x = std::floor(grid_x);
    const float first_z = std::floor(grid_z);
    const float across_x = grid_x - first_x;
    const float across_z = grid_z - first_z;
    const auto value = [&sun](float column_at, float row_at) {
        const auto column =
            static_cast<std::size_t>(std::clamp(column_at, 0.0F, static_cast<float>(sun.columns - 1)));
        const auto row = static_cast<std::size_t>(std::clamp(row_at, 0.0F, static_cast<float>(sun.rows - 1)));
        return core::at(sun.agreement, (row * sun.columns) + column);
    };
    const float near = (value(first_x, first_z) * (1.0F - across_x)) + (value(first_x + 1.0F, first_z) * across_x);
    const float far =
        (value(first_x, first_z + 1.0F) * (1.0F - across_x)) + (value(first_x + 1.0F, first_z + 1.0F) * across_x);
    return (near * (1.0F - across_z)) + (far * across_z);
}

} // namespace render::rt
