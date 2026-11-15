// NOT ORIGINAL - the surface trails' fields and the drawn world's marks (surface_trail.h).
#include "engine/gfx/surface_trail.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "base/core/checked_index.h"

namespace render::surface_trail {

namespace {

constexpr int64_t kKeyHalf = int64_t{1} << 31U;
constexpr uint32_t kKeyShift = 32;

int64_t tile_key(int x, int z)
{
    return ((static_cast<int64_t>(x) + kKeyHalf) << kKeyShift) | (static_cast<int64_t>(z) + kKeyHalf);
}

std::atomic<bool>& ground_flag()
{
    static std::atomic<bool> instance{false};
    return instance;
}

std::atomic<bool>& character_flag()
{
    static std::atomic<bool> instance{false};
    return instance;
}

} // namespace

uint64_t next_field_serial()
{
    static std::atomic<uint64_t> serials{0};
    return ++serials;
}

int floor_div(int value, int by) { return value >= 0 ? value / by : -((-value + by - 1) / by); }

float distance_to_segment(const Point& point, const Point& from, const Point& to)
{
    const float dx = core::at(to, 0) - core::at(from, 0);
    const float dz = core::at(to, 1) - core::at(from, 1);
    const float px = core::at(point, 0) - core::at(from, 0);
    const float pz = core::at(point, 1) - core::at(from, 1);
    const float length2 = (dx * dx) + (dz * dz);
    const float t = length2 > 0.0F ? std::clamp(((px * dx) + (pz * dz)) / length2, 0.0F, 1.0F) : 0.0F;
    const float ex = px - (t * dx);
    const float ez = pz - (t * dz);
    return std::sqrt((ex * ex) + (ez * ez));
}

float smoothstep(float low, float high, float value)
{
    const float along = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    constexpr float kThree = 3.0F;
    constexpr float kTwo = 2.0F;
    return along * along * (kThree - (kTwo * along));
}

Tile& Field::tile_for(int x, int z)
{
    const int64_t key = tile_key(x, z);
    const auto found = std::ranges::lower_bound(index_, key, {}, &std::pair<int64_t, std::size_t>::first);
    if (found != index_.end() && found->first == key) {
        return *core::at(tiles_, found->second);
    }
    auto made = std::make_unique<Tile>();
    made->x = x;
    made->z = z;
    made->texels.assign(planes_ * kTileTexelCount, 0U);
    tiles_.push_back(std::move(made));
    index_.insert(found, {key, tiles_.size() - 1});
    return *tiles_.back();
}

const Tile* Field::tile(int x, int z) const
{
    const int64_t key = tile_key(x, z);
    const auto found = std::ranges::lower_bound(index_, key, {}, &std::pair<int64_t, std::size_t>::first);
    if (found != index_.end() && found->first == key) {
        return core::at(tiles_, found->second).get();
    }
    return nullptr;
}

std::array<int, 2> Field::texel_of(float u, float v) const
{
    return {static_cast<int>(std::floor(u / texel_)), static_cast<int>(std::floor(v / texel_))};
}

uint32_t Field::texel_at(int x, int z, std::size_t plane) const
{
    const int tx = floor_div(x, kTileTexels);
    const int tz = floor_div(z, kTileTexels);
    const Tile* found = tile(tx, tz);
    if (found == nullptr || plane >= planes_) {
        return 0;
    }
    const auto column = static_cast<std::size_t>(x - (tx * kTileTexels));
    const auto row = static_cast<std::size_t>(z - (tz * kTileTexels));
    return core::at(found->texels, (plane * kTileTexelCount) + (row * kTileTexels) + column);
}

void set_current_ground(bool ground) { ground_flag().store(ground, std::memory_order_relaxed); }

bool current_ground() { return ground_flag().load(std::memory_order_relaxed); }

void set_current_character(bool character) { character_flag().store(character, std::memory_order_relaxed); }

bool current_character() { return character_flag().load(std::memory_order_relaxed); }

} // namespace render::surface_trail
