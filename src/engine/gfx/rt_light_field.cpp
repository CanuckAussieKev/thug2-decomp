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
constexpr float kLambertDirect = 2.0F;  // a Lambert light D seen over the directions ~ D (1/4 + 1/2 n . d) ...
constexpr float kLambertAmbient = 0.25F; // ... so D = 2 |c1|, and D / 4 of c0 is its

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

bool LocalLight::any() const
{
    return std::ranges::any_of(ambient, [](float value) { return value > 0.0F; }) ||
           std::ranges::any_of(directional, [](float value) { return value > 0.0F; });
}

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
        // the local light's fit's sums: [1, n] [1, n]^T and [1, n] x its light
        for (std::size_t row = 0; row < kBasis; ++row) {
            for (std::size_t col = row; col < kBasis; ++col) {
                core::at(each->moments.normals, core::at(core::at(kMomentAt, row), col)) +=
                    core::at(basis, row) * core::at(basis, col);
            }
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(core::at(each->moments.light, ch), row) += core::at(basis, row) * core::at(parts.local, ch);
            }
        }
    }
    ++vertices_;
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
        core::at(out.whole, ch) = own > kLightFieldLeast ? std::clamp(now / own, 0.0F, kLightFieldMostRatio) : 1.0F;
        core::at(out.indirect, ch) =
            indirect_own > kLightFieldLeast ? std::clamp(indirect_now / indirect_own, 0.0F, kLightFieldMostRatio) : 1.0F;
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

IndirectLight LightField::indirect_at(const FieldPoint& point) const
{
    IndirectLight out;
    if (cells_.empty()) {
        return out;
    }
    std::vector<Weighted> cells = around(point);
    if (cells.empty()) {
        cells.push_back({.cell = &all_, .weight = 1.0F});
    }
    // (a vertex's mean: the cells' sums over their counts, each by its weight)
    LightParts sum;
    float count = 0.0F;
    for (const Weighted& each : cells) {
        add_parts(&sum, each.cell->sum, each.weight);
        count += each.cell->count * each.weight;
    }
    if (!(count > kLightFieldLeast)) {
        return out;
    }
    for (std::size_t ch = 0; ch < out.now.size(); ++ch) {
        core::at(out.now, ch) = core::at(sum.indirect_now, ch) * local_scale_ / count;
        core::at(out.own, ch) = core::at(sum.indirect_own, ch) * local_scale_ / count;
        core::at(out.direct_now, ch) = core::at(sum.direct_now, ch) * local_scale_ / count;
        core::at(out.direct_own, ch) = core::at(sum.direct_own, ch) * local_scale_ / count;
    }
    return out;
}

LocalLight LightField::local_at(const FieldPoint& point) const
{
    LocalLight out;
    if (cells_.empty()) {
        return out;
    }
    std::vector<Weighted> cells = around(point);
    if (cells.empty()) {
        cells.push_back({.cell = &all_, .weight = 1.0F});
    }
    Moments moments;
    for (const Weighted& each : cells) {
        for (std::size_t k = 0; k < moments.normals.size(); ++k) {
            core::at(moments.normals, k) += core::at(each.cell->moments.normals, k) * each.weight;
        }
        for (std::size_t ch = 0; ch < 3; ++ch) {
            for (std::size_t k = 0; k < kBasis; ++k) {
                core::at(core::at(moments.light, ch), k) +=
                    core::at(core::at(each.cell->moments.light, ch), k) * each.weight;
            }
        }
    }
    const float total = core::at(moments.normals, 0);
    if (!(total > kLightFieldLeast)) {
        return out;
    }
    // per channel: c0 + c1 . n, least squares over the vertices, c1 held toward 0 (kLocalLightRidge x their count)
    std::array<std::array<float, kBasis>, 3> fitted{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        System system{};
        for (std::size_t row = 0; row < kBasis; ++row) {
            for (std::size_t col = 0; col < kBasis; ++col) {
                core::at(core::at(system, row), col) =
                    core::at(moments.normals, core::at(core::at(kMomentAt, row), col));
            }
            if (row > 0) {
                core::at(core::at(system, row), row) += static_cast<double>(kLocalLightRidge * total);
            }
            core::at(core::at(system, row), kBasis) = core::at(core::at(moments.light, ch), row);
        }
        std::array<double, kBasis> solved{};
        if (!solve4(system, &solved)) {
            solved = {core::at(core::at(moments.light, ch), 0) / total, 0.0, 0.0, 0.0};
        }
        for (std::size_t k = 0; k < kBasis; ++k) {
            core::at(core::at(fitted, ch), k) = static_cast<float>(core::at(solved, k));
        }
    }
    // the direction: the channels' gradients by their brightness; along it a Lambert light, the rest ambient
    constexpr std::array<float, 3> kLuma{kLumaRed, kLumaGreen, kLumaBlue};
    FieldPoint toward{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(toward, axis) += core::at(kLuma, ch) * core::at(core::at(fitted, ch), axis + 1);
        }
    }
    const float length =
        std::sqrt((std::get<0>(toward) * std::get<0>(toward)) + (std::get<1>(toward) * std::get<1>(toward)) +
                  (std::get<2>(toward) * std::get<2>(toward)));
    const bool directed = length > kLeastLength;
    if (directed) {
        out.toward = {std::get<0>(toward) / length, std::get<1>(toward) / length, std::get<2>(toward) / length};
    }
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const std::array<float, kBasis>& fit = core::at(fitted, ch);
        const float along = directed ? (core::at(fit, 1) * std::get<0>(out.toward)) +
                                           (core::at(fit, 2) * std::get<1>(out.toward)) +
                                           (core::at(fit, 3) * std::get<2>(out.toward))
                                     : 0.0F;
        const float directional = kLambertDirect * std::max(0.0F, along);
        core::at(out.directional, ch) = directional * local_scale_;
        core::at(out.ambient, ch) = std::max(0.0F, core::at(fit, 0) - (kLambertAmbient * directional)) * local_scale_;
    }
    return out;
}

namespace {
std::atomic<float>& direct_share_slot()
{
    static std::atomic<float> slot{kDefaultDirectShare};
    return slot;
}
} // namespace

void set_direct_share(float share)
{
    direct_share_slot().store(std::isfinite(share) ? std::clamp(share, 0.0F, 1.0F) : kDefaultDirectShare,
                              std::memory_order_relaxed);
}

float direct_share() { return direct_share_slot().load(std::memory_order_relaxed); }

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

IndirectLight indirect_light_at(const FieldPoint& point)
{
    const std::shared_ptr<const LightField> field = light_field();
    return field != nullptr ? field->indirect_at(point) : IndirectLight{};
}

LocalLight local_light_at(const FieldPoint& point)
{
    const std::shared_ptr<const LightField> field = light_field();
    return field != nullptr ? field->local_at(point) : LocalLight{};
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
        const float value = static_cast<float>((colour >> shift) & kByteMask) * core::at(ratio, ch);
        const auto byte = static_cast<uint32_t>(std::lround(std::clamp(value, 0.0F, kMostByte)));
        out = (out & ~(kByteMask << shift)) | (byte << shift);
    }
    return out;
}

} // namespace render::rt
