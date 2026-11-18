// NOT ORIGINAL - one light list with physical power (rt_light_list.h).
#include "engine/gfx/rt_light_list.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "base/core/checked_index.h"

namespace render::rt::lights {

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr float kLeastColour = 1.0e-3F; // (a point light whose brightest channel is under this: none)

double clean(double weight) { return std::isfinite(weight) && weight > 0.0 ? weight : 0.0; }

} // namespace

AliasTable build_alias_table(std::span<const double> weights)
{
    AliasTable out;
    const std::size_t n = weights.size();
    out.entries.resize(n);
    for (const double weight : weights) {
        out.total += clean(weight);
    }
    if (n == 0 || !(out.total > 0.0)) {
        out.total = 0.0;
        return out;
    }
    // Vose's construction: scaled probabilities n w / total; the small ones filled up from the large ones
    std::vector<double> scaled(n);
    std::vector<uint32_t> small;
    std::vector<uint32_t> large;
    uint32_t some_large = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double p = clean(core::at(weights, i)) / out.total;
        core::at(out.entries, i).pdf = static_cast<float>(p);
        core::at(out.entries, i).alias = static_cast<uint32_t>(i);
        core::at(scaled, i) = p * static_cast<double>(n);
        if (core::at(scaled, i) < 1.0) {
            small.push_back(static_cast<uint32_t>(i));
        } else {
            large.push_back(static_cast<uint32_t>(i));
            some_large = static_cast<uint32_t>(i);
        }
    }
    while (!small.empty() && !large.empty()) {
        const uint32_t s = small.back();
        small.pop_back();
        const uint32_t l = large.back();
        core::at(out.entries, s).threshold = static_cast<float>(core::at(scaled, s));
        core::at(out.entries, s).alias = l;
        core::at(scaled, l) = (core::at(scaled, l) + core::at(scaled, s)) - 1.0;
        if (core::at(scaled, l) < 1.0) {
            large.pop_back();
            small.push_back(l);
        }
    }
    // what is left is 1 up to rounding: kept whole - except an entry of weight 0, which a rounding left here (it must
    // never be picked: aliased to a light that has weight)
    for (const uint32_t i : large) {
        core::at(out.entries, i).threshold = 1.0F;
    }
    for (const uint32_t i : small) {
        const bool weightless = !(core::at(out.entries, i).pdf > 0.0F);
        core::at(out.entries, i).threshold = weightless ? 0.0F : 1.0F;
        core::at(out.entries, i).alias = weightless ? some_large : i;
    }
    return out;
}

uint32_t alias_pick(std::span<const AliasEntry> entries, float u)
{
    const auto n = static_cast<uint32_t>(entries.size());
    if (n == 0) {
        return 0;
    }
    const float x = u * static_cast<float>(n);
    const uint32_t i = std::min(static_cast<uint32_t>(x), n - 1);
    const AliasEntry& entry = core::at(entries, i);
    return x - static_cast<float>(i) < entry.threshold ? i : entry.alias;
}

float luma(const Rgb& c)
{
    return (0.2126F * std::get<0>(c)) + (0.7152F * std::get<1>(c)) + (0.0722F * std::get<2>(c));
}

double disc_power(const Rgb& light) { return kPiD * static_cast<double>(luma(light)); }

double sky_power(double integral) { return integral; }

double triangle_power(double radiance, double area) { return 2.0 * kPiD * area * radiance; }

double lamp_power(const Emitter& lamp)
{
    const double intensity =
        static_cast<double>(lamp.intensity) *
        static_cast<double>(luma({std::get<0>(lamp.colour), std::get<1>(lamp.colour), std::get<2>(lamp.colour)}));
    const auto core = static_cast<double>(lamp.core);
    if (!(intensity > 0.0) || !(core > 0.0) || !(lamp.reach > 0.0F)) {
        return 0.0;
    }
    if (lamp.shape == EmitterShape::kLine && lamp.half_length > 0.0F) {
        return kPiD * kPiD * kPiD * intensity * core * static_cast<double>(lamp.half_length);
    }
    if (lamp.shape == EmitterShape::kPanel) {
        return 2.0 * kPiD * kPiD * intensity * core * core;
    }
    return 4.0 * kPiD * kPiD * intensity * core * core;
}

bool point_emitter(const Light& light, float scale, Emitter* out)
{
    if (light.kind != Light::Kind::kPoint || !(light.radius > 0.0F)) {
        return false;
    }
    const float brightest = std::max({std::get<0>(light.colour), std::get<1>(light.colour), std::get<2>(light.colour)});
    if (!(brightest > kLeastColour) || !std::isfinite(brightest)) {
        return false;
    }
    const float inverse = 1.0F / brightest;
    *out = Emitter{.position = light.position,
                   .size = 0.0F,
                   .colour = {std::get<0>(light.colour) * inverse, std::get<1>(light.colour) * inverse,
                              std::get<2>(light.colour) * inverse},
                   .intensity = brightest * scale,
                   .reach = light.radius,
                   .material = 0,
                   .kind = EmitterKind::kPoint,
                   .core = point_core(light.radius)};
    return true;
}

LightList build_light_list(const LightSet& set, LightWeights weights)
{
    LightList out;
    const std::size_t triangles = set.triangle_powers.size();
    const std::size_t count = kFirstTriangleEntry + triangles + set.lamps.size();
    out.first_lamp = static_cast<uint32_t>(kFirstTriangleEntry + triangles);
    std::vector<double> power(count, 0.0);
    std::vector<LightKind> kinds(count, LightKind::kLamp);
    std::vector<uint32_t> indices(count, 0);
    if (set.disc_cos < 1.0F) {
        core::at(power, kDiscEntry) = clean(disc_power(set.disc_light));
    }
    core::at(kinds, kDiscEntry) = LightKind::kDisc;
    core::at(power, kSkyEntry) = clean(sky_power(set.sky_integral));
    core::at(kinds, kSkyEntry) = LightKind::kSky;
    for (std::size_t i = 0; i < triangles; ++i) {
        core::at(power, kFirstTriangleEntry + i) = clean(static_cast<double>(core::at(set.triangle_powers, i)));
        core::at(kinds, kFirstTriangleEntry + i) = LightKind::kTriangle;
        core::at(indices, kFirstTriangleEntry + i) = static_cast<uint32_t>(i);
    }
    for (std::size_t i = 0; i < set.lamps.size(); ++i) {
        core::at(power, out.first_lamp + i) = clean(lamp_power(core::at(set.lamps, i)));
        core::at(indices, out.first_lamp + i) = static_cast<uint32_t>(i);
    }
    std::array<std::size_t, 4> lit{};
    for (std::size_t i = 0; i < count; ++i) {
        const auto kind = static_cast<std::size_t>(core::at(kinds, i));
        core::at(out.kind_power, kind) += core::at(power, i);
        core::at(lit, kind) += core::at(power, i) > 0.0 ? 1U : 0U;
    }
    std::vector<double> pick(count, 0.0);
    if (weights == LightWeights::kKindsEvenly) {
        // stage 1's: the kinds there are evenly, each kind's lights evenly
        const auto kinds_there = static_cast<double>(std::ranges::count_if(lit, [](std::size_t n) { return n > 0; }));
        for (std::size_t i = 0; i < count; ++i) {
            const auto kind = static_cast<std::size_t>(core::at(kinds, i));
            core::at(pick, i) =
                core::at(power, i) > 0.0 ? 1.0 / (kinds_there * static_cast<double>(core::at(lit, kind))) : 0.0;
        }
    } else {
        // the distant group and the local group evenly (those there are), each light by its power within its group
        const double distant = core::at(out.kind_power, 0) + core::at(out.kind_power, 1);
        const double local = core::at(out.kind_power, 2) + core::at(out.kind_power, 3);
        const double groups = (distant > 0.0 ? 1.0 : 0.0) + (local > 0.0 ? 1.0 : 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            const bool far = core::at(kinds, i) == LightKind::kDisc || core::at(kinds, i) == LightKind::kSky;
            const double group = far ? distant : local;
            core::at(pick, i) = group > 0.0 ? core::at(power, i) / (group * groups) : 0.0;
        }
    }
    out.table = build_alias_table(pick);
    for (std::size_t i = 0; i < count; ++i) {
        core::at(out.table.entries, i).payload = payload_of(core::at(kinds, i), core::at(indices, i));
        core::at(out.kind_pick, static_cast<std::size_t>(core::at(kinds, i))) +=
            static_cast<double>(core::at(out.table.entries, i).pdf);
    }
    return out;
}

// --- the sky -------------------------------------------------------------------------------------------------------------

uint32_t sky_cell_of(const Vec3& dir, uint32_t width, uint32_t height)
{
    const double length = std::sqrt((static_cast<double>(dir.x) * dir.x) + (static_cast<double>(dir.y) * dir.y) +
                                    (static_cast<double>(dir.z) * dir.z));
    const double y = length > 0.0 ? std::clamp(static_cast<double>(dir.y) / length, -1.0, 1.0) : 1.0;
    const double theta = std::acos(y);
    const double phi = std::atan2(static_cast<double>(dir.z), static_cast<double>(dir.x)) + kPiD;
    const auto row = std::min(static_cast<uint32_t>(theta / kPiD * height), height - 1);
    const auto column = std::min(static_cast<uint32_t>(phi / (2.0 * kPiD) * width), width - 1);
    return (row * width) + column;
}

Vec3 sky_cell_point(uint32_t column, uint32_t row, uint32_t width, uint32_t height, float u1, float u2)
{
    const double phi = (((static_cast<double>(column) + u1) / width) * 2.0 * kPiD) - kPiD;
    const double cos_top = std::cos(static_cast<double>(row) * kPiD / height);
    const double cos_bottom = std::cos(static_cast<double>(row + 1) * kPiD / height);
    const double cos_theta = cos_top + ((cos_bottom - cos_top) * u2);
    const double sin_theta = std::sqrt(std::max(0.0, 1.0 - (cos_theta * cos_theta)));
    return {static_cast<float>(sin_theta * std::cos(phi)), static_cast<float>(cos_theta),
            static_cast<float>(sin_theta * std::sin(phi))};
}

Vec3 sky_cell_centre(uint32_t column, uint32_t row, uint32_t width, uint32_t height)
{
    const double phi = (((static_cast<double>(column) + 0.5) / width) * 2.0 * kPiD) - kPiD;
    const double theta = (static_cast<double>(row) + 0.5) * kPiD / height;
    return {static_cast<float>(std::sin(theta) * std::cos(phi)), static_cast<float>(std::cos(theta)),
            static_cast<float>(std::sin(theta) * std::sin(phi))};
}

double sky_cell_solid_angle(uint32_t row, uint32_t width, uint32_t height)
{
    const double cos_top = std::cos(static_cast<double>(row) * kPiD / height);
    const double cos_bottom = std::cos(static_cast<double>(row + 1) * kPiD / height);
    return (2.0 * kPiD / width) * (cos_top - cos_bottom);
}

SkyDistribution build_sky_distribution(std::span<const std::array<float, 4>> radiance, uint32_t width, uint32_t height)
{
    SkyDistribution out;
    out.width = width;
    out.height = height;
    const std::size_t cells = static_cast<std::size_t>(width) * height;
    if (width == 0 || height == 0 || radiance.size() < cells) {
        out.width = 0;
        out.height = 0;
        return out;
    }
    out.luminance.resize(cells);
    out.cells.resize(cells);
    std::vector<double> row_weights(height, 0.0);
    std::vector<double> cell_weights(width, 0.0);
    for (uint32_t row = 0; row < height; ++row) {
        const double omega = sky_cell_solid_angle(row, width, height);
        double sum = 0.0;
        for (uint32_t column = 0; column < width; ++column) {
            const std::size_t at = (static_cast<std::size_t>(row) * width) + column;
            const std::array<float, 4>& c = core::at(radiance, at);
            const float l = std::max(0.0F, luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)}));
            core::at(out.luminance, at) = std::isfinite(l) ? l : 0.0F;
            core::at(cell_weights, column) = core::at(out.luminance, at);
            sum += core::at(out.luminance, at);
        }
        core::at(row_weights, row) = sum * omega;
        out.integral += sum * omega;
        const AliasTable row_table = build_alias_table(cell_weights);
        std::ranges::copy(row_table.entries,
                          out.cells.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(row) * width));
    }
    out.rows = build_alias_table(row_weights).entries;
    return out;
}

SkySample sample_sky(const SkyDistribution& sky, float u0, float u1, float u2, float u3)
{
    SkySample out;
    if (!(sky.integral > 0.0) || sky.width == 0) {
        return out;
    }
    const uint32_t row = alias_pick(sky.rows, u0);
    const std::span<const AliasEntry> cells(sky.cells);
    const uint32_t column = alias_pick(cells.subspan(static_cast<std::size_t>(row) * sky.width, sky.width), u1);
    out.cell = (row * sky.width) + column;
    out.dir = sky_cell_point(column, row, sky.width, sky.height, u2, u3);
    out.pdf = static_cast<float>(static_cast<double>(core::at(sky.luminance, out.cell)) / sky.integral);
    return out;
}

float sky_pdf(const SkyDistribution& sky, const Vec3& dir)
{
    if (!(sky.integral > 0.0) || sky.width == 0) {
        return 0.0F;
    }
    return static_cast<float>(static_cast<double>(core::at(sky.luminance, sky_cell_of(dir, sky.width, sky.height))) /
                              sky.integral);
}

const char* const kLightListGlsl = R"(
const uint LIGHT_DISC = 0u;
const uint LIGHT_SKY = 1u;
const uint LIGHT_TRIANGLE = 2u;
const uint LIGHT_LAMP = 3u;
uint light_kind(uint payload) { return payload >> 30; }
uint light_index(uint payload) { return payload & 0x3FFFFFFFu; }
// the light list's alias table: an entry, kept by its threshold against the fraction left, else its alias
uint light_pick(float u, uint count)
{
    float x = u * float(count);
    uint i = min(uint(x), count - 1u);
    return (x - float(i)) < light_entries[i].threshold ? i : light_entries[i].alias;
}
uint sky_alias_pick(float u, uint first, uint count)
{
    float x = u * float(count);
    uint i = min(uint(x), count - 1u);
    uvec2 entry = sky_alias[first + i];
    return (x - float(i)) < uintBitsToFloat(entry.x) ? i : entry.y;
}
uint sky_cell_of(vec3 dir)
{
    float theta = acos(clamp(dir.y / max(length(dir), 1e-30), -1.0, 1.0));
    float phi = atan(dir.z, dir.x) + PATH_PI;
    uint row = min(uint(theta / PATH_PI * float(SKY_HEIGHT)), SKY_HEIGHT - 1u);
    uint column = min(uint(phi / PATH_TWO_PI * float(SKY_WIDTH)), SKY_WIDTH - 1u);
    return row * SKY_WIDTH + column;
}
vec3 sky_cell_centre(uvec2 cell)
{
    float phi = (float(cell.x) + 0.5) / float(SKY_WIDTH) * PATH_TWO_PI - PATH_PI;
    float theta = (float(cell.y) + 0.5) * PATH_PI / float(SKY_HEIGHT);
    return vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
}
// a direction of the sky by its luminance (two alias tables, then uniform in solid angle within the cell)
vec3 sky_sample(float u0, float u1, float u2, float u3, out uint cell)
{
    uint row = sky_alias_pick(u0, 0u, SKY_HEIGHT);
    uint column = sky_alias_pick(u1, SKY_HEIGHT + row * SKY_WIDTH, SKY_WIDTH);
    cell = row * SKY_WIDTH + column;
    float phi = (float(column) + u2) / float(SKY_WIDTH) * PATH_TWO_PI - PATH_PI;
    float cos_top = cos(float(row) * PATH_PI / float(SKY_HEIGHT));
    float cos_bottom = cos(float(row + 1u) * PATH_PI / float(SKY_HEIGHT));
    float c = mix(cos_top, cos_bottom, u3);
    float s = sqrt(max(0.0, 1.0 - c * c));
    return vec3(s * cos(phi), c, s * sin(phi));
}
)";

} // namespace render::rt::lights
