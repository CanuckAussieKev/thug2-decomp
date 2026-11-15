// NOT ORIGINAL - the level's light where a thing is (rt_light_field.h).
#include "engine/gfx/rt_light_field.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include "base/core/checked_index.h"

namespace render::rt {

namespace {

constexpr int64_t kKeyBits = 21;
constexpr int64_t kKeyMask = (int64_t{1} << kKeyBits) - 1;
constexpr int64_t kKeyBias = int64_t{1} << (kKeyBits - 1);
constexpr uint32_t kByteMask = 0xFFU;
constexpr float kMostByte = 255.0F;

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
    add_parts(&into.sum, parts, 1.0F);
    into.count += 1.0F;
    add_parts(&all_.sum, parts, 1.0F);
    all_.count += 1.0F;
    ++vertices_;
}

LightRatio LightField::ratio_of(const LightParts& sum)
{
    LightRatio out;
    for (std::size_t ch = 0; ch < out.whole.size(); ++ch) {
        const float local = core::at(sum.local, ch);
        const float indirect_now = core::at(sum.indirect_now, ch) + local;
        const float indirect_own = core::at(sum.indirect_own, ch) + local;
        const float now = core::at(sum.direct_now, ch) + indirect_now;
        const float own = core::at(sum.direct_own, ch) + indirect_own;
        core::at(out.whole, ch) = own > kLightFieldLeast ? std::clamp(now / own, 0.0F, kLightFieldMostRatio) : 1.0F;
        core::at(out.indirect, ch) =
            indirect_own > kLightFieldLeast ? std::clamp(indirect_now / indirect_own, 0.0F, kLightFieldMostRatio) : 1.0F;
    }
    return out;
}

LightRatio LightField::whole() const { return all_.count > 0.0F ? ratio_of(all_.sum) : LightRatio{}; }

LightRatio LightField::at(const FieldPoint& point) const
{
    if (cells_.empty()) {
        return {};
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
    LightParts sum;
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
        add_parts(&sum, found->sum, weight / found->count);
        weights += weight;
    }
    if (weights > kLightFieldLeast) {
        return ratio_of(sum);
    }
    // none round it: the nearest cells within kLightFieldReach (the shell at the least distance that has any)
    const int cx = cell_of(std::get<0>(point));
    const int cy = cell_of(std::get<1>(point));
    const int cz = cell_of(std::get<2>(point));
    for (int ring = 1; ring <= kLightFieldReach; ++ring) {
        LightParts near;
        float count = 0.0F;
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dy = -ring; dy <= ring; ++dy) {
                for (int dx = -ring; dx <= ring; ++dx) {
                    if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != ring) {
                        continue;
                    }
                    if (const Cell* found = cell(cx + dx, cy + dy, cz + dz); found != nullptr) {
                        add_parts(&near, found->sum, 1.0F);
                        count += found->count;
                    }
                }
            }
        }
        if (count > 0.0F) {
            return ratio_of(near);
        }
    }
    return whole();
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
