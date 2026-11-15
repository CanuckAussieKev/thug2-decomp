#pragma once

// NOT ORIGINAL - the surface trails (docs/design/vulkan.md "Surface trails"): what the skaters leave on the level as they
// ride - the snow's trails (engine/gfx/snow.h: the press and berm in the snow) and the paint's (engine/gfx/paint_trails.h:
// colour and coverage) are both kept here, in one kind of field, and drawn by one kind of window on the device
// (gpu_vulkan/vulkan_surface_trail.h).
//
// A field is a surface seen along one axis (the snow's and the floors' paint from above - x, z; the walls' paint from
// the side - z, y or x, y) cut into tiles of kTileTexels x kTileTexels texels, each texel one or more 32-bit words (its
// planes: the snow's one - press, berm, activity; the paint's two - its colour and coverage, the painted surface's place
// along the axis). A tile is made where something first stamped and kept for the whole level session: a field holds
// what was left anywhere, the device's window shows the part round the camera. Each tile's version goes up each time a
// texel of it changes, so the window copies a tile again only when it changed.
//
// What a stamp does to a texel is its owner's (the snow presses and banks, the paint coats): the field hands each texel
// within a capsule's reach - a stroke from one place to the next - to the owner's rule with its distance from the stroke.
// Purely visual and the same every run: nothing here is read by the game; given the same strokes, the same texels.
//
// The drawn world marks (set by render_pc while it draws): whether the draw is the world's ground or a scene object (a
// mesh that isn't skinned) - only those take the snow's cover and the paint - and whether a character is being drawn
// (its draws never do).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"

namespace render::surface_trail {

inline constexpr int kTileTexels = 64;
inline constexpr std::size_t kTileTexelCount = static_cast<std::size_t>(kTileTexels) * kTileTexels;

using Point = std::array<float, 2>; // a place on the field's plane (its two axes, world units)

// A tile: its place (texels / kTileTexels from the plane's origin), its version, its texels - plane after plane, each
// row by row (the second axis), so a plane's words are one tile-sized block the device copies as it is.
struct Tile {
    int x = 0;
    int z = 0;
    uint32_t version = 0;
    std::vector<uint32_t> texels; // planes x kTileTexelCount
    [[nodiscard]] std::span<const uint32_t> plane(std::size_t index) const
    {
        return std::span<const uint32_t>(texels).subspan(index * kTileTexelCount, kTileTexelCount);
    }
};

// A texel being stamped: its words, plane by plane.
class TexelRef {
public:
    TexelRef(Tile* tile, std::size_t index) : tile_(tile), index_(index) {}
    [[nodiscard]] uint32_t& word(std::size_t plane) const { return core::at(tile_->texels, (plane * kTileTexelCount) + index_); }

private:
    Tile* tile_;
    std::size_t index_;
};

// The trails' own random numbers (PCG32 - O'Neill's permuted congruential generator: XSH RR): the snow's spray, the
// paint's splatter - seeded the same each level session, never the game's.
class Random {
public:
    void seed(uint64_t value)
    {
        state_ = 0;
        next();
        state_ += value;
        next();
    }
    uint32_t next()
    {
        constexpr uint64_t kMultiplier = 6364136223846793005ULL;
        constexpr uint64_t kIncrement = 1442695040888963407ULL;
        constexpr uint32_t kXorShift = 18;
        constexpr uint32_t kShiftDown = 27;
        constexpr uint32_t kRotateShift = 59;
        constexpr uint32_t kRotateMask = 31;
        const uint64_t old = state_;
        state_ = (old * kMultiplier) + kIncrement;
        const auto shifted = static_cast<uint32_t>(((old >> kXorShift) ^ old) >> kShiftDown);
        const auto rotate = static_cast<uint32_t>(old >> kRotateShift);
        return (shifted >> rotate) | (shifted << ((0U - rotate) & kRotateMask));
    }
    // 0 - 1
    float unit()
    {
        constexpr float kScale = 1.0F / 16777216.0F;
        constexpr uint32_t kTopBits = 8;
        return static_cast<float>(next() >> kTopBits) * kScale;
    }
    float between(float low, float high) { return low + ((high - low) * unit()); }

private:
    uint64_t state_ = 0;
};

[[nodiscard]] int floor_div(int value, int by);
// The distance from `point` to the segment `from` - `to`.
[[nodiscard]] float distance_to_segment(const Point& point, const Point& from, const Point& to);
[[nodiscard]] float smoothstep(float low, float high, float value);

// A new number for each field made (a session's: a window that showed another field's tiles starts again).
[[nodiscard]] uint64_t next_field_serial();

class Field {
public:
    explicit Field(float texel = 2.0F, std::size_t planes = 1)
        : texel_(texel), planes_(std::max<std::size_t>(planes, 1)), serial_(next_field_serial())
    {
    }
    [[nodiscard]] float texel() const { return texel_; }
    [[nodiscard]] uint64_t serial() const { return serial_; }
    [[nodiscard]] std::size_t planes() const { return planes_; }
    // The tile at (x, z), or null when nothing was stamped there.
    [[nodiscard]] const Tile* tile(int x, int z) const;
    [[nodiscard]] std::size_t tile_count() const { return tiles_.size(); }
    // A texel's word of `plane` (0 where no tile is).
    [[nodiscard]] uint32_t texel_at(int x, int z, std::size_t plane = 0) const;
    // The texel holding the plane's point (u, v).
    [[nodiscard]] std::array<int, 2> texel_of(float u, float v) const;
    // Every tile, read.
    template <typename Read>
    void read_tiles(Read read) const
    {
        for (const std::unique_ptr<Tile>& tile : tiles_) {
            read(*tile);
        }
    }
    // Every tile, changed by `change` (true: it changed - its version goes up).
    template <typename Change>
    void change_tiles(Change change)
    {
        for (const std::unique_ptr<Tile>& tile : tiles_) {
            if (change(*tile)) {
                ++tile->version;
            }
        }
    }
    // A stroke: every texel whose centre is nearer than `reach` to the segment `from` - `to` handed to `rule(distance,
    // centre, texel)` (true: the texel changed) - its tile made when the rule is first called there.
    template <typename Rule>
    void stroke(const Point& from, const Point& to, float reach, Rule rule)
    {
        if (!(reach > 0.0F) || !std::isfinite(core::at(from, 0)) || !std::isfinite(core::at(from, 1)) ||
            !std::isfinite(core::at(to, 0)) || !std::isfinite(core::at(to, 1))) {
            return;
        }
        const std::array<int, 2> first = texel_of(std::min(core::at(from, 0), core::at(to, 0)) - reach,
                                                  std::min(core::at(from, 1), core::at(to, 1)) - reach);
        const std::array<int, 2> last = texel_of(std::max(core::at(from, 0), core::at(to, 0)) + reach,
                                                 std::max(core::at(from, 1), core::at(to, 1)) + reach);
        constexpr float kHalf = 0.5F;
        for (int tz = floor_div(core::at(first, 1), kTileTexels); tz <= floor_div(core::at(last, 1), kTileTexels); ++tz) {
            for (int tx = floor_div(core::at(first, 0), kTileTexels); tx <= floor_div(core::at(last, 0), kTileTexels); ++tx) {
                Tile* tile = nullptr;
                const int z0 = std::max(core::at(first, 1), tz * kTileTexels);
                const int z1 = std::min(core::at(last, 1), (tz * kTileTexels) + kTileTexels - 1);
                const int x0 = std::max(core::at(first, 0), tx * kTileTexels);
                const int x1 = std::min(core::at(last, 0), (tx * kTileTexels) + kTileTexels - 1);
                bool changed = false;
                for (int z = z0; z <= z1; ++z) {
                    for (int x = x0; x <= x1; ++x) {
                        const Point centre{(static_cast<float>(x) + kHalf) * texel_, (static_cast<float>(z) + kHalf) * texel_};
                        const float distance = distance_to_segment(centre, from, to);
                        if (distance >= reach) {
                            continue;
                        }
                        if (tile == nullptr) {
                            tile = &tile_for(tx, tz);
                        }
                        const std::size_t index = (static_cast<std::size_t>(z - (tz * kTileTexels)) * kTileTexels) +
                                                  static_cast<std::size_t>(x - (tx * kTileTexels));
                        changed = rule(distance, centre, TexelRef(tile, index)) || changed;
                    }
                }
                if (changed) {
                    ++tile->version;
                }
            }
        }
    }

private:
    Tile& tile_for(int x, int z);
    float texel_;
    std::size_t planes_;
    uint64_t serial_;
    std::vector<std::unique_ptr<Tile>> tiles_;           // (in the order made)
    std::vector<std::pair<int64_t, std::size_t>> index_; // sorted by key: the tile's place
};

// --- the drawn world's marks ------------------------------------------------------------------------------------------
// render_pc says, for each draw, whether it is the world's ground or a scene object's - a mesh that isn't skinned (the
// characters keep their own colours): only those take the snow's cover and the paint. Cleared after the draw.
void set_current_ground(bool ground);
[[nodiscard]] bool current_ground();
// An instance with this many bones or more is a character (a skater, a ped - their skeleton): render_pc says while one
// is drawn (Singleton::render), and none of its draws is the ground.
inline constexpr int kSkeletonBones = 24;
void set_current_character(bool character);
[[nodiscard]] bool current_character();

} // namespace render::surface_trail
