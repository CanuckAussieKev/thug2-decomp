// NOT ORIGINAL - the level's light where a thing is (rt_light_field.h).
#include "engine/gfx/rt_light_field.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_display.h"

namespace render::rt {

namespace {

constexpr int64_t kKeyBits = 21;
constexpr int64_t kKeyMask = (int64_t{1} << kKeyBits) - 1;
constexpr int64_t kKeyBias = int64_t{1} << (kKeyBits - 1);
constexpr uint32_t kByteMask = 0xFFU;
constexpr float kMostByte = 255.0F;
constexpr float kLumaRed = 0.299F;
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;
constexpr float kLeastLength = 1e-6F;
constexpr double kSingular = 1e-12;
constexpr std::size_t kBasis = 4;  // [1, nx, ny, nz]
constexpr std::size_t kSystem = 5; // a row of the system: kBasis coefficients and the right side

// (the upper triangle's place of row r, column c of the 4 x 4 moments)
constexpr std::array<std::array<std::size_t, kBasis>, kBasis> kMomentAt{
    {{0, 1, 2, 3}, {1, 4, 5, 6}, {2, 5, 7, 8}, {3, 6, 8, 9}}};

void add_to(FieldRgb* into, const FieldRgb& value, float weight)
{
    for (std::size_t ch = 0; ch < into->size(); ++ch) {
        core::at(*into, ch) += core::at(value, ch) * weight;
    }
}

void add_parts(LightParts* into, const LightParts& parts, float weight)
{
    add_to(&into->direct_now, parts.direct_now, weight);
    add_to(&into->indirect_now, parts.indirect_now, weight);
    add_to(&into->direct_own, parts.direct_own, weight);
    add_to(&into->indirect_own, parts.indirect_own, weight);
    add_to(&into->local, parts.local, weight);
    add_to(&into->sky_now, parts.sky_now, weight);
}

int cell_of(float value) { return static_cast<int>(std::floor(value / kLightFieldCell)); }

using System = std::array<std::array<double, kSystem>, kBasis>;

// Solves the 4 x 4 system (Gaussian elimination, partial pivoting); false when singular.
bool solve4(System m, std::array<double, kBasis>* out)
{
    for (std::size_t col = 0; col < kBasis; ++col) {
        std::size_t pivot = col;
        for (std::size_t row = col + 1; row < kBasis; ++row) {
            if (std::abs(core::at(core::at(m, row), col)) > std::abs(core::at(core::at(m, pivot), col))) {
                pivot = row;
            }
        }
        if (std::abs(core::at(core::at(m, pivot), col)) < kSingular) {
            return false;
        }
        std::swap(core::at(m, col), core::at(m, pivot));
        const double inverse = 1.0 / core::at(core::at(m, col), col);
        for (double& value : core::at(m, col)) {
            value *= inverse;
        }
        for (std::size_t row = 0; row < kBasis; ++row) {
            if (row == col) {
                continue;
            }
            const double factor = core::at(core::at(m, row), col);
            for (std::size_t k = 0; k < kSystem; ++k) {
                core::at(core::at(m, row), k) -= factor * core::at(core::at(m, col), k);
            }
        }
    }
    for (std::size_t row = 0; row < kBasis; ++row) {
        core::at(*out, row) = core::at(core::at(m, row), kBasis);
    }
    return true;
}

struct Published {
    std::mutex mutex;
    std::shared_ptr<const LightField> field;
};

Published& published()
{
    static Published slot;
    return slot;
}

} // namespace

int64_t LightField::key_of(int x, int y, int z)
{
    return ((static_cast<int64_t>(x) + kKeyBias) & kKeyMask) |
           (((static_cast<int64_t>(y) + kKeyBias) & kKeyMask) << kKeyBits) |
           (((static_cast<int64_t>(z) + kKeyBias) & kKeyMask) << (2 * kKeyBits));
}

const LightField::Cell* LightField::cell(int x, int y, int z) const
{
    const auto found = cells_.find(key_of(x, y, z));
    return found == cells_.end() ? nullptr : &found->second;
}

void LightField::add(const FieldPoint& at, const LightParts& parts)
{
    Cell& into = cells_[key_of(cell_of(std::get<0>(at)), cell_of(std::get<1>(at)), cell_of(std::get<2>(at)))];
    const std::array<float, kBasis> basis{1.0F, std::get<0>(parts.normal), std::get<1>(parts.normal),
                                          std::get<2>(parts.normal)};
    for (Cell* each : {&into, &all_}) {
        add_parts(&each->sum, parts, 1.0F);
        each->count += 1.0F;
        // the probes' fits' sums: [1, n] [1, n]^T and [1, n] x each part's light
        for (std::size_t row = 0; row < kBasis; ++row) {
            for (std::size_t col = row; col < kBasis; ++col) {
                core::at(each->moments.normals, core::at(core::at(kMomentAt, row), col)) +=
                    core::at(basis, row) * core::at(basis, col);
            }
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const float sky = core::at(parts.sky_now, ch);
                const float bounce = std::max(0.0F, core::at(parts.indirect_now, ch) - sky);
                core::at(core::at(each->moments.sky, ch), row) += core::at(basis, row) * sky;
                core::at(core::at(each->moments.bounce, ch), row) += core::at(basis, row) * bounce;
                core::at(core::at(each->moments.lamps, ch), row) += core::at(basis, row) * core::at(parts.local, ch);
            }
        }
    }
    ++vertices_;
}

void LightField::set_sun(const FieldPoint& toward, const FieldRgb& light)
{
    sun_toward_ = toward;
    sun_ = light;
}

LightRatio LightField::ratio_of(const LightParts& sum)
{
    LightRatio out;
    for (std::size_t ch = 0; ch < out.whole.size(); ++ch) {
        const float local = core::at(sum.local, ch);
        const float indirect_now = core::at(sum.indirect_now, ch);
        const float indirect_own = core::at(sum.indirect_own, ch);
        // (the level's own lights in the whole - a baked colour holds them; not in the indirect: the game's lights for
        // objects take them as a light of their own, local_at)
        const float now = core::at(sum.direct_now, ch) + indirect_now + local;
        const float own = core::at(sum.direct_own, ch) + indirect_own + local;
        core::at(out.whole, ch) = own > kLightFieldLeast ? std::max(now / own, 0.0F) : 1.0F;
        core::at(out.indirect, ch) = indirect_own > kLightFieldLeast ? std::max(indirect_now / indirect_own, 0.0F) : 1.0F;
    }
    return out;
}

LightRatio LightField::whole() const { return all_.count > 0.0F ? ratio_of(all_.sum) : LightRatio{}; }

std::vector<LightField::Weighted> LightField::around(const FieldPoint& point) const
{
    std::vector<Weighted> out;
    if (cells_.empty()) {
        return out;
    }
    // the eight cells round the point (their centres), trilinearly; the empty ones left out
    const float fx = (std::get<0>(point) / kLightFieldCell) - 0.5F;
    const float fy = (std::get<1>(point) / kLightFieldCell) - 0.5F;
    const float fz = (std::get<2>(point) / kLightFieldCell) - 0.5F;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int z0 = static_cast<int>(std::floor(fz));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float tz = fz - static_cast<float>(z0);
    float weights = 0.0F;
    for (int corner = 0; corner < 8; ++corner) {
        const int dx = corner & 1;
        const int dy = (corner >> 1) & 1;
        const int dz = (corner >> 2) & 1;
        const Cell* found = cell(x0 + dx, y0 + dy, z0 + dz);
        if (found == nullptr || found->count <= 0.0F) {
            continue;
        }
        const float weight = (dx != 0 ? tx : 1.0F - tx) * (dy != 0 ? ty : 1.0F - ty) * (dz != 0 ? tz : 1.0F - tz);
        out.push_back({.cell = found, .weight = weight / found->count});
        weights += weight;
    }
    if (weights > kLightFieldLeast) {
        return out;
    }
    out.clear();
    // none round it: the nearest cells within kLightFieldReach (the shell at the least distance that has any)
    const int cx = cell_of(std::get<0>(point));
    const int cy = cell_of(std::get<1>(point));
    const int cz = cell_of(std::get<2>(point));
    for (int ring = 1; ring <= kLightFieldReach; ++ring) {
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dy = -ring; dy <= ring; ++dy) {
                for (int dx = -ring; dx <= ring; ++dx) {
                    if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != ring) {
                        continue;
                    }
                    if (const Cell* found = cell(cx + dx, cy + dy, cz + dz); found != nullptr) {
                        out.push_back({.cell = found, .weight = 1.0F});
                    }
                }
            }
        }
        if (!out.empty()) {
            return out;
        }
    }
    return out;
}

LightRatio LightField::at(const FieldPoint& point) const
{
    if (cells_.empty()) {
        return {};
    }
    const std::vector<Weighted> cells = around(point);
    if (cells.empty()) {
        return whole();
    }
    LightParts sum;
    for (const Weighted& each : cells) {
        add_parts(&sum, each.cell->sum, each.weight);
    }
    return ratio_of(sum);
}

namespace {

// A probe fitted to a cell's sums: per channel c0 + c1 . n by least squares over the vertices, c1 held toward
// c0 x `prior` (kProbeRidge x their count) - the penalty |c1 - c0 prior|^2 in the normal equations.
ProbeL1 fit_probe(const std::array<float, 10>& normals, const std::array<std::array<float, 4>, 3>& light,
                  const FieldPoint& prior)
{
    ProbeL1 out;
    const float total = core::at(normals, 0);
    if (!(total > kLightFieldLeast)) {
        return out;
    }
    const double ridge = static_cast<double>(kProbeRidge * total);
    // (-prior, in the basis' places 1 - 3)
    const std::array<double, kBasis> minus_prior{0.0, -static_cast<double>(std::get<0>(prior)),
                                                 -static_cast<double>(std::get<1>(prior)),
                                                 -static_cast<double>(std::get<2>(prior))};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        System system{};
        for (std::size_t row = 0; row < kBasis; ++row) {
            for (std::size_t col = 0; col < kBasis; ++col) {
                // the data's [1, n] [1, n]^T, and the penalty's: |c1 - c0 prior|^2 = x^T Q x with
                // Q = [[|prior|^2, -prior^T], [-prior, I]]
                double penalty = 0.0;
                if (row == 0 && col == 0) {
                    for (const double value : minus_prior) {
                        penalty += value * value;
                    }
                } else if (row == 0 || col == 0) {
                    penalty = core::at(minus_prior, row + col);
                } else if (row == col) {
                    penalty = 1.0;
                }
                core::at(core::at(system, row), col) =
                    static_cast<double>(core::at(normals, core::at(core::at(kMomentAt, row), col))) + (ridge * penalty);
            }
            core::at(core::at(system, row), kBasis) = core::at(core::at(light, ch), row);
        }
        std::array<double, kBasis> solved{};
        if (!solve4(system, &solved)) {
            solved = {core::at(core::at(light, ch), 0) / total, 0.0, 0.0, 0.0};
        }
        core::at(out.c0, ch) = static_cast<float>(core::at(solved, 0));
        core::at(out.c1, ch) = {static_cast<float>(core::at(solved, 1)), static_cast<float>(core::at(solved, 2)),
                                static_cast<float>(core::at(solved, 3))};
    }
    return out;
}

float dot3(const FieldPoint& a, const FieldPoint& b)
{
    return (std::get<0>(a) * std::get<0>(b)) + (std::get<1>(a) * std::get<1>(b)) + (std::get<2>(a) * std::get<2>(b));
}

} // namespace

FieldRgb ProbeL1::at(const FieldPoint& normal) const
{
    FieldRgb out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = std::max(0.0F, core::at(c0, ch) + dot3(core::at(c1, ch), normal));
    }
    return out;
}

ProbeLights probe_lights(const ProbeL1& probe)
{
    constexpr std::array<float, 3> kLuma{kLumaRed, kLumaGreen, kLumaBlue};
    ProbeLights out;
    out.ambient = probe.c0;
    FieldPoint toward{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(toward, axis) += core::at(kLuma, ch) * core::at(core::at(probe.c1, ch), axis);
        }
    }
    const float length = std::sqrt(dot3(toward, toward));
    if (!(length > kLeastLength)) {
        return out;
    }
    out.toward = {std::get<0>(toward) / length, std::get<1>(toward) / length, std::get<2>(toward) / length};
    float lost = 0.0F;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const FieldPoint& c1 = core::at(probe.c1, ch);
        const float along = dot3(c1, out.toward);
        core::at(out.lobe, ch) = along;
        lost += core::at(kLuma, ch) * std::sqrt(std::max(0.0F, dot3(c1, c1) - (along * along)));
    }
    out.lost = lost;
    return out;
}

ProbeL1 PointLight::indirect(float lamps_share) const
{
    ProbeL1 out;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        core::at(out.c0, ch) = core::at(sky.c0, ch) + core::at(bounce.c0, ch) + (lamps_share * core::at(lamps.c0, ch));
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(core::at(out.c1, ch), axis) = core::at(core::at(sky.c1, ch), axis) +
                                                   core::at(core::at(bounce.c1, ch), axis) +
                                                   (lamps_share * core::at(core::at(lamps.c1, ch), axis));
        }
    }
    return out;
}

FieldRgb PointLight::on(const FieldPoint& normal, float seen, float lamps_share) const
{
    FieldRgb out = indirect(lamps_share).at(normal);
    const float facing = std::max(0.0F, dot3(normal, toward)) * seen;
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) += core::at(sun, ch) * facing;
    }
    return out;
}

DeviceLights device_lights(const PointLight& light, float lamps_share)
{
    DeviceLights out;
    const ProbeLights probe = probe_lights(light.indirect(lamps_share));
    for (std::size_t axis = 0; axis < 3; ++axis) {
        core::at(out.key_travel, axis) = -core::at(light.toward, axis);
        core::at(out.fill_travel, axis) = -core::at(probe.toward, axis);
        core::at(out.back_travel, axis) = core::at(probe.toward, axis);
    }
    out.ambient = probe.ambient;
    out.key = light.sun;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        core::at(out.fill, ch) = core::at(probe.lobe, ch);
        core::at(out.back, ch) = -core::at(probe.lobe, ch);
    }
    out.lost = probe.lost;
    return out;
}

FieldRgb DeviceLights::on(const FieldPoint& normal, float seen) const
{
    const auto facing = [&](const FieldPoint& travel) {
        return std::max(0.0F, -dot3(normal, travel));
    };
    FieldRgb out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = std::max(0.0F, core::at(ambient, ch) + (core::at(key, ch) * facing(key_travel) * seen) +
                                               (core::at(fill, ch) * facing(fill_travel)) +
                                               (core::at(back, ch) * facing(back_travel)));
    }
    return out;
}

PointLight LightField::light_at(const FieldPoint& point) const
{
    PointLight out;
    if (cells_.empty()) {
        return out;
    }
    std::vector<Weighted> cells = around(point);
    if (cells.empty()) {
        cells.push_back({.cell = &all_, .weight = 1.0F});
    }
    Moments moments;
    const auto add_sums = [](ProbeSums* into, const ProbeSums& from, float weight) {
        for (std::size_t ch = 0; ch < 3; ++ch) {
            for (std::size_t k = 0; k < kBasis; ++k) {
                core::at(core::at(*into, ch), k) += core::at(core::at(from, ch), k) * weight;
            }
        }
    };
    for (const Weighted& each : cells) {
        for (std::size_t k = 0; k < moments.normals.size(); ++k) {
            core::at(moments.normals, k) += core::at(each.cell->moments.normals, k) * each.weight;
        }
        add_sums(&moments.sky, each.cell->moments.sky, each.weight);
        add_sums(&moments.bounce, each.cell->moments.bounce, each.weight);
        add_sums(&moments.lamps, each.cell->moments.lamps, each.weight);
    }
    // the sky's light comes from above: where the vertices don't tell, a surface takes S (1 + n . up) / 2 (c1 = c0 up);
    // the bounce and the lamps: the same from every side (c1 = 0)
    constexpr FieldPoint kUp{0.0F, 1.0F, 0.0F};
    constexpr FieldPoint kNone{0.0F, 0.0F, 0.0F};
    out.sky = fit_probe(moments.normals, moments.sky, kUp);
    out.bounce = fit_probe(moments.normals, moments.bounce, kNone);
    out.lamps = fit_probe(moments.normals, moments.lamps, kNone);
    out.toward = sun_toward_;
    out.sun = sun_;
    out.valid = true;
    return out;
}
void set_light_field(std::shared_ptr<const LightField> field)
{
    Published& slot = published();
    const std::scoped_lock lock(slot.mutex);
    slot.field = std::move(field);
}

std::shared_ptr<const LightField> light_field()
{
    Published& slot = published();
    const std::scoped_lock lock(slot.mutex);
    return slot.field;
}

LightRatio light_ratio_at(const FieldPoint& point)
{
    const std::shared_ptr<const LightField> field = light_field();
    return field != nullptr ? field->at(point) : LightRatio{};
}

PointLight point_light_at(const FieldPoint& point)
{
    const std::shared_ptr<const LightField> field = light_field();
    return field != nullptr ? field->light_at(point) : PointLight{};
}

LightRatio level_light_ratio()
{
    const std::shared_ptr<const LightField> field = light_field();
    return field != nullptr ? field->whole() : LightRatio{};
}

uint32_t scaled_colour(uint32_t colour, const FieldRgb& ratio, bool red_low)
{
    constexpr std::array<uint32_t, 3> kRedLow{0U, 8U, 16U};
    constexpr std::array<uint32_t, 3> kRedHigh{16U, 8U, 0U};
    const std::array<uint32_t, 3>& shifts = red_low ? kRedLow : kRedHigh;
    uint32_t out = colour;
    for (std::size_t ch = 0; ch < shifts.size(); ++ch) {
        const uint32_t shift = core::at(shifts, ch);
        const float value =
            static_cast<float>((colour >> shift) & kByteMask) * display_from_linear(std::max(core::at(ratio, ch), 0.0F));
        const auto byte = static_cast<uint32_t>(std::lround(std::clamp(value, 0.0F, kMostByte)));
        out = (out & ~(kByteMask << shift)) | (byte << shift);
    }
    return out;
}

} // namespace render::rt
