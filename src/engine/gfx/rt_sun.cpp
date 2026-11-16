// NOT ORIGINAL - the sun a level's lighting was baked with: see rt_sun.h.
#include "engine/gfx/rt_sun.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <unordered_map>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
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

} // namespace

namespace {

// The point-biserial correlation of `values` with `lit` (0 when either side is under kMinGroup of them, or no spread).
float biserial(std::span<const double> values, std::span<const uint8_t> lit)
{
    const std::size_t count = std::min(values.size(), lit.size());
    if (count == 0) {
        return 0.0F;
    }
    double sum = 0.0;
    double squares = 0.0;
    double lit_sum = 0.0;
    std::size_t lit_count = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double value = core::at(values, i);
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

} // namespace

float sun_score(std::span<const BakeSample> samples, std::span<const uint8_t> lit)
{
    const std::size_t count = std::min(samples.size(), lit.size());
    if (count == 0) {
        return 0.0F;
    }
    std::vector<double> raw(count);
    std::vector<double> logs(count);
    double level_sum = 0.0;
    std::unordered_map<uint32_t, std::pair<double, std::size_t>> groups;
    constexpr double kLeastBrightness = 1.0; // (a byte: the logarithm of black held)
    for (std::size_t i = 0; i < count; ++i) {
        const BakeSample& sample = core::at(samples, i);
        core::at(raw, i) = sample.brightness;
        core::at(logs, i) = std::log(std::max(static_cast<double>(sample.brightness), kLeastBrightness));
        level_sum += core::at(logs, i);
        auto& [group_sum, group_count] = groups[sample.group];
        group_sum += core::at(logs, i);
        ++group_count;
    }
    const double level_mean = level_sum / static_cast<double>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& [group_sum, group_count] = groups.at(core::at(samples, i).group);
        core::at(logs, i) -= group_count >= kGroupLeast ? group_sum / static_cast<double>(group_count) : level_mean;
    }
    return std::max(biserial(raw, lit.first(count)), biserial(logs, lit.first(count)));
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
    // (added by the street air) the street level: the middle of the upward samples' heights
    std::vector<float> up;
    up.reserve(samples.size());
    for (const BakeSample& sample : samples) {
        if (core::at(sample.normal, 1) >= kStreetUp) {
            up.push_back(core::at(sample.position, 1));
        }
    }
    if (!up.empty()) {
        const auto middle = static_cast<std::ptrdiff_t>(static_cast<double>(up.size() - 1) * kStreetShare);
        std::ranges::nth_element(up, up.begin() + middle);
        fit.sun.street = std::max(*(up.begin() + middle), fit.sun.ground);
        fit.sun.street_known = true;
    }
    return fit;
}

// --- (added by the scene's sun) -------------------------------------------------------------------------------------

namespace {

constexpr double kShortestSun = 1.0e-6; // a direction shorter than this can't be normalised: none

// `v` normalised, or false.
bool unit_of(const Vec3& v, Vec3* out)
{
    const double length =
        std::hypot(static_cast<double>(x_of(v)), static_cast<double>(y_of(v)), static_cast<double>(z_of(v)));
    if (!(length > kShortestSun) || !std::isfinite(length)) {
        return false;
    }
    *out = scaled(v, static_cast<float>(1.0 / length));
    return true;
}

SceneSun sun_of(SunOrigin origin, const Vec3& toward)
{
    SceneSun out;
    if (unit_of(toward, &out.toward)) {
        out.origin = origin;
    }
    return out;
}

bool blank(char ch) { return ch == ' ' || ch == '\t'; }

// The numbers of a comma-separated list (at most `most`): false when any isn't one.
bool numbers_of(std::string_view text, std::size_t most, std::vector<float>* out)
{
    out->clear();
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string part(text.substr(0, comma));
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        char* end = nullptr;
        const float value = std::strtof(part.c_str(), &end);
        if (end == part.c_str() || !std::isfinite(value) || out->size() >= most) {
            return false;
        }
        const std::string_view rest(end);
        if (!std::ranges::all_of(rest, blank)) {
            return false;
        }
        out->push_back(value);
    }
    return true;
}

} // namespace

SceneSun resolve_scene_sun(const SunInputs& in)
{
    if (in.override_set) {
        const SceneSun set = sun_of(SunOrigin::kOverride, in.override_toward);
        if (set.found()) {
            return set;
        }
    }
    if (in.choice.source == SunSource::kFixed) {
        return sun_of(SunOrigin::kFixed, in.choice.fixed);
    }
    if (in.baked_scene == 0) {
        return {.origin = SunOrigin::kWaiting};
    }
    // (added by the time of day; lighting round A: before the baked sun - a level whose bake shows no sun, night or
    // indoors, is relit by its sky, bounce and lamps, and the clock's sun lights it too)
    if (in.clock_set && in.relight.relights) {
        const SceneSun clock = sun_of(SunOrigin::kClock, in.clock_toward);
        if (clock.found()) {
            return clock;
        }
    }
    if (!in.baked_found) {
        return {}; // (no sun in the bake, and no clock: the level at its own time has none)
    }
    if (in.choice.source == SunSource::kGame && in.game_set && in.relight.relights && !in.relight.level_keeps_bake) {
        const SceneSun game = sun_of(SunOrigin::kGame, in.game_toward);
        const auto lowest = static_cast<float>(std::sin(static_cast<double>(kMinGameSunDegrees) * kRadiansPerDegree));
        if (game.found() && y_of(game.toward) >= lowest) {
            return game;
        }
    }
    return sun_of(SunOrigin::kLevel, in.baked_toward);
}

Vec3 toward_heading(const SunAngles& angles)
{
    const double heading = static_cast<double>(angles.heading_degrees) * kRadiansPerDegree;
    const double elevation = static_cast<double>(angles.elevation_degrees) * kRadiansPerDegree;
    return {static_cast<float>(std::sin(heading) * std::cos(elevation)), static_cast<float>(std::sin(elevation)),
            static_cast<float>(std::cos(heading) * std::cos(elevation))};
}

bool parse_sun_source(std::string_view text, SunChoice* out)
{
    while (!text.empty() && blank(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && blank(text.back())) {
        text.remove_suffix(1);
    }
    if (text == "game") {
        *out = {.source = SunSource::kGame};
        return true;
    }
    if (text == "level" || text == "bake") {
        *out = {.source = SunSource::kLevel};
        return true;
    }
    constexpr std::size_t kMostNumbers = 3;
    std::vector<float> numbers;
    if (!numbers_of(text, kMostNumbers, &numbers) || numbers.size() < 2) {
        return false;
    }
    Vec3 toward{};
    if (numbers.size() == 2) {
        toward = toward_heading({.heading_degrees = numbers.front(), .elevation_degrees = numbers.back()});
    } else if (!unit_of({core::at(numbers, 0), core::at(numbers, 1), core::at(numbers, 2)}, &toward)) {
        return false;
    }
    *out = {.source = SunSource::kFixed, .fixed = toward};
    return true;
}

} // namespace render::rt
