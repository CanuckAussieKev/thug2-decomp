// NOT ORIGINAL - grass: the field of blades made from the collision's grass faces (grass.h).
#include "engine/gfx/grass.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "base/core/checked_index.h"

namespace render::grass {

namespace {

using Vec3 = std::array<float, 3>;

constexpr float kCell = 64.0F;          // the covering faces' grid (world units)
constexpr float kGrassStack = 8.0F;     // a grass face covers a root only this far above it (another lawn over it)
constexpr float kInsideSlack = 1.0e-4F; // (barycentric: a root on an edge is inside)
constexpr float kLeastArea = 1.0e-3F;   // (square units: smaller faces are skipped)
constexpr uint32_t kByte = 0xFFU;
constexpr uint32_t kRankShift = 0;
constexpr uint32_t kHeightShift = 8;
constexpr uint32_t kFacingShift = 16;
constexpr uint32_t kShadeShift = 25;
constexpr uint32_t kShadeBits = 0x7FU;
constexpr float kUnit = 1.0F / 4294967296.0F; // (a hash as 0 - 1)
constexpr float kHalf = 0.5F;                 // (a triangle's area: half its cross product's length)

// A 32-bit integer hash (a finaliser of the xorshift-multiply kind: every input bit reaches every output bit).
constexpr uint32_t mix(uint32_t value)
{
    constexpr uint32_t kFirst = 0x7FEB352DU;
    constexpr uint32_t kSecond = 0x846CA68BU;
    constexpr uint32_t kShiftA = 16;
    constexpr uint32_t kShiftB = 15;
    value ^= value >> kShiftA;
    value *= kFirst;
    value ^= value >> kShiftB;
    value *= kSecond;
    value ^= value >> kShiftA;
    return value;
}

constexpr uint32_t combine(uint32_t seed, uint32_t value)
{
    constexpr uint32_t kGolden = 0x9E3779B9U;
    constexpr uint32_t kUp = 6;
    constexpr uint32_t kDown = 2;
    return mix(seed ^ (value + kGolden + (seed << kUp) + (seed >> kDown)));
}

float unit(uint32_t hash) { return static_cast<float>(hash) * kUnit; }

Vec3 minus(const Vec3& corner_a, const Vec3& corner_b)
{
    return {core::at(corner_a, 0) - core::at(corner_b, 0), core::at(corner_a, 1) - core::at(corner_b, 1),
            core::at(corner_a, 2) - core::at(corner_b, 2)};
}
Vec3 cross(const Vec3& corner_a, const Vec3& corner_b)
{
    return {(core::at(corner_a, 1) * core::at(corner_b, 2)) - (core::at(corner_a, 2) * core::at(corner_b, 1)),
            (core::at(corner_a, 2) * core::at(corner_b, 0)) - (core::at(corner_a, 0) * core::at(corner_b, 2)),
            (core::at(corner_a, 0) * core::at(corner_b, 1)) - (core::at(corner_a, 1) * core::at(corner_b, 0))};
}
float length(const Vec3& corner_a)
{
    return std::sqrt((core::at(corner_a, 0) * core::at(corner_a, 0)) + (core::at(corner_a, 1) * core::at(corner_a, 1)) +
                     (core::at(corner_a, 2) * core::at(corner_a, 2)));
}

// A face's seed: its corners' bits (the same face: the same blades, whatever order the sectors load in).
uint32_t face_seed(const Face& face)
{
    uint32_t seed = static_cast<uint32_t>(face.kind) + 1U;
    for (const Vec3& corner : face.corners) {
        for (const float value : corner) {
            seed = combine(seed, std::bit_cast<uint32_t>(value == 0.0F ? 0.0F : value)); // (-0 as 0)
        }
    }
    return seed;
}

int64_t cell_of(float value) { return static_cast<int64_t>(std::floor(value / kCell)); }
uint64_t cell_key(int64_t x, int64_t z)
{
    constexpr uint64_t kHalf = 32;
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << kHalf) | static_cast<uint32_t>(z);
}

bool finite3(const Vec3& value)
{
    return std::isfinite(core::at(value, 0)) && std::isfinite(core::at(value, 1)) && std::isfinite(core::at(value, 2));
}

// The face's height over the ground point (x, z) when it covers it (seen from above).
bool height_over(const Face& face, const std::array<float, 2>& ground, float* height)
{
    const Vec3& corner_a = core::at(face.corners, 0);
    const Vec3& corner_b = core::at(face.corners, 1);
    const Vec3& corner_c = core::at(face.corners, 2);
    const float abx = core::at(corner_b, 0) - core::at(corner_a, 0);
    const float abz = core::at(corner_b, 2) - core::at(corner_a, 2);
    const float acx = core::at(corner_c, 0) - core::at(corner_a, 0);
    const float acz = core::at(corner_c, 2) - core::at(corner_a, 2);
    const float area = (abx * acz) - (acx * abz);
    if (std::abs(area) < kLeastArea) {
        return false; // (a wall seen from above covers nothing)
    }
    const float px = core::at(ground, 0) - core::at(corner_a, 0);
    const float pz = core::at(ground, 1) - core::at(corner_a, 2);
    const float u = ((px * acz) - (acx * pz)) / area;
    const float v = ((abx * pz) - (px * abz)) / area;
    if (u < -kInsideSlack || v < -kInsideSlack || u + v > 1.0F + kInsideSlack) {
        return false;
    }
    *height = core::at(corner_a, 1) + (u * (core::at(corner_b, 1) - core::at(corner_a, 1))) +
              (v * (core::at(corner_c, 1) - core::at(corner_a, 1)));
    return true;
}

// The faces that may cover roots, by the cells of the ground they lie over (only those over the grass's box).
class CoverGrid {
public:
    CoverGrid(std::span<const Face> faces, const Vec3& low, const Vec3& high) : faces_(faces)
    {
        for (std::size_t index = 0; index < faces.size(); ++index) {
            const Face& face = core::at(faces, index);
            float min_x = core::at(core::at(face.corners, 0), 0);
            float max_x = min_x;
            float min_z = core::at(core::at(face.corners, 0), 2);
            float max_z = min_z;
            float max_y = core::at(core::at(face.corners, 0), 1);
            for (const Vec3& corner : face.corners) {
                min_x = std::min(min_x, core::at(corner, 0));
                max_x = std::max(max_x, core::at(corner, 0));
                min_z = std::min(min_z, core::at(corner, 2));
                max_z = std::max(max_z, core::at(corner, 2));
                max_y = std::max(max_y, core::at(corner, 1));
            }
            // (not over the grass, or below all of it: it covers none)
            if (max_x < core::at(low, 0) || min_x > core::at(high, 0) || max_z < core::at(low, 2) ||
                min_z > core::at(high, 2) || max_y < core::at(low, 1) + kBlockFloor) {
                continue;
            }
            const int64_t x0 = cell_of(std::max(min_x, core::at(low, 0)));
            const int64_t x1 = cell_of(std::min(max_x, core::at(high, 0)));
            const int64_t z0 = cell_of(std::max(min_z, core::at(low, 2)));
            const int64_t z1 = cell_of(std::min(max_z, core::at(high, 2)));
            for (int64_t cell_x = x0; cell_x <= x1; ++cell_x) {
                for (int64_t cell_z = z0; cell_z <= z1; ++cell_z) {
                    cells_[cell_key(cell_x, cell_z)].push_back(static_cast<uint32_t>(index));
                }
            }
        }
    }

    // Whether a face covers the root: not grass, from kBlockFloor to kBlockReach above it (grass: from kGrassStack).
    [[nodiscard]] bool covered(const Vec3& root) const
    {
        const auto found = cells_.find(cell_key(cell_of(core::at(root, 0)), cell_of(core::at(root, 2))));
        if (found == cells_.end()) {
            return false;
        }
        for (const uint32_t index : found->second) {
            const Face& face = core::at(faces_, index);
            float height = 0.0F;
            if (!height_over(face, {core::at(root, 0), core::at(root, 2)}, &height)) {
                continue;
            }
            const float above = height - core::at(root, 1);
            const float floor = face.kind == Kind::kBlocker ? kBlockFloor : kGrassStack;
            if (above >= floor && above <= kBlockReach) {
                return true;
            }
        }
        return false;
    }

private:
    std::span<const Face> faces_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
};

struct Source {
    std::size_t face = 0;
    float area = 0.0F;
    uint32_t seed = 0;
};

int64_t tile_of(float value) { return static_cast<int64_t>(std::floor(value / kTileSize)); }

std::atomic<uint64_t>& serials()
{
    static std::atomic<uint64_t> serial{0};
    return serial;
}

Sink*& installed()
{
    static Sink* sink = nullptr;
    return sink;
}

} // namespace

Field make_field(std::span<const Face> faces, float density)
{
    Field field;
    field.serial = ++serials();
    density = std::isfinite(density) ? std::clamp(density, kLeastDensity, kMostDensity) : 1.0F;
    // the grass faces facing up (the sources) and their box
    std::vector<Source> sources;
    Vec3 low{INFINITY, INFINITY, INFINITY};
    Vec3 high{-INFINITY, -INFINITY, -INFINITY};
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const Face& face = core::at(faces, index);
        if (!finite3(core::at(face.corners, 0)) || !finite3(core::at(face.corners, 1)) ||
            !finite3(core::at(face.corners, 2))) {
            continue;
        }
        if (face.kind == Kind::kBlocker) {
            ++field.blocker_faces;
            continue;
        }
        const Vec3 normal = cross(minus(core::at(face.corners, 1), core::at(face.corners, 0)),
                                  minus(core::at(face.corners, 2), core::at(face.corners, 0)));
        const float twice_area = length(normal);
        if (!(twice_area * kHalf > kLeastArea)) {
            continue;
        }
        if (std::abs(core::at(normal, 1)) < kLeastUp * twice_area) {
            ++field.steep_faces;
            continue;
        }
        ++(face.kind == Kind::kDried ? field.dried_faces : field.grass_faces);
        sources.push_back({.face = index, .area = twice_area * kHalf, .seed = face_seed(face)});
        field.area += static_cast<double>(twice_area * kHalf);
        for (const Vec3& corner : face.corners) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                core::at(low, axis) = std::min(core::at(low, axis), core::at(corner, axis));
                core::at(high, axis) = std::max(core::at(high, axis), core::at(corner, axis));
            }
        }
    }
    if (sources.empty()) {
        field.density = density;
        return field;
    }
    // (too many: the density lowered to fit)
    const double wanted = field.area * kBladesPerSquareUnit * density;
    if (wanted > static_cast<double>(kMostBlades)) {
        density = static_cast<float>(density * static_cast<double>(kMostBlades) / wanted);
        field.capped = true;
    }
    field.density = density;
    const CoverGrid grid(faces, low, {core::at(high, 0), core::at(high, 1) + kBlockReach, core::at(high, 2)});
    struct Placed {
        Blade blade;
        int64_t tile_x = 0;
        int64_t tile_z = 0;
        uint32_t order = 0;
    };
    std::vector<Placed> placed;
    placed.reserve(static_cast<std::size_t>(std::min(wanted, static_cast<double>(kMostBlades))) + sources.size());
    for (const Source& source : sources) {
        const Face& face = core::at(faces, source.face);
        const float exact = source.area * kBladesPerSquareUnit * density;
        const auto count = static_cast<uint32_t>(std::floor(exact + unit(mix(source.seed))));
        const Vec3& corner_a = core::at(face.corners, 0);
        const Vec3& corner_b = core::at(face.corners, 1);
        const Vec3& corner_c = core::at(face.corners, 2);
        for (uint32_t k = 0; k < count; ++k) {
            const uint32_t first = combine(source.seed, (k * 2U) + 1U);
            const uint32_t second = combine(first, k);
            const uint32_t look = combine(second, source.seed);
            const float r1 = std::sqrt(unit(first));
            const float r2 = unit(second);
            const float wa = 1.0F - r1;
            const float wb = r1 * (1.0F - r2);
            const float wc = r1 * r2;
            const Vec3 root{(wa * core::at(corner_a, 0)) + (wb * core::at(corner_b, 0)) + (wc * core::at(corner_c, 0)),
                            (wa * core::at(corner_a, 1)) + (wb * core::at(corner_b, 1)) + (wc * core::at(corner_c, 1)),
                            (wa * core::at(corner_a, 2)) + (wb * core::at(corner_b, 2)) + (wc * core::at(corner_c, 2))};
            if (grid.covered(root)) {
                ++field.covered;
                continue;
            }
            uint32_t packed = ((look >> kRankShift) & kByte) | (((look >> kHeightShift) & kByte) << kHeightShift) |
                              (((look >> kFacingShift) & kByte) << kFacingShift) |
                              ((mix(look) & kShadeBits) << kShadeShift);
            if (face.kind == Kind::kDried) {
                packed |= kDriedFlag;
            }
            placed.push_back({.blade = {.root = root, .packed = packed},
                              .tile_x = tile_of(core::at(root, 0)),
                              .tile_z = tile_of(core::at(root, 2)),
                              .order = static_cast<uint32_t>(placed.size())});
        }
    }
    // by tile, then rank (the level-of-detail order), then as made: the same order every run
    std::ranges::sort(placed, [](const Placed& one, const Placed& two) {
        if (one.tile_x != two.tile_x) {
            return one.tile_x < two.tile_x;
        }
        if (one.tile_z != two.tile_z) {
            return one.tile_z < two.tile_z;
        }
        const uint32_t rank_one = one.blade.packed & kByte;
        const uint32_t rank_two = two.blade.packed & kByte;
        return rank_one != rank_two ? rank_one < rank_two : one.order < two.order;
    });
    field.blades.reserve(placed.size());
    for (std::size_t index = 0; index < placed.size(); ++index) {
        const Placed& blade = core::at(placed, index);
        const bool new_tile = index == 0 || core::at(placed, index - 1).tile_x != blade.tile_x ||
                              core::at(placed, index - 1).tile_z != blade.tile_z;
        if (new_tile) {
            field.tiles.push_back({.low = blade.blade.root,
                                   .high = blade.blade.root,
                                   .first = static_cast<uint32_t>(field.blades.size()),
                                   .count = 0});
        }
        Tile& tile = field.tiles.back();
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(tile.low, axis) = std::min(core::at(tile.low, axis), core::at(blade.blade.root, axis));
            core::at(tile.high, axis) = std::max(core::at(tile.high, axis), core::at(blade.blade.root, axis));
        }
        ++tile.count;
        field.blades.push_back(blade.blade);
    }
    return field;
}

void set_sink(Sink* sink) { installed() = sink; }

Sink* sink() { return installed(); }

} // namespace render::grass
