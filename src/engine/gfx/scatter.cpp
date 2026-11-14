// NOT ORIGINAL - decals and clutter: placed over the collision's surfaces, clear of the skate lines (scatter.h).
#include "engine/gfx/scatter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"

namespace render::scatter {

namespace {

constexpr float kCell = 64.0F;              // the faces' and rails' grid (world units, seen from above)
constexpr float kLeastArea = 1.0e-3F;       // (square units: smaller faces are skipped)
constexpr float kInsideSlack = 1.0e-4F;     // (barycentric: a point on an edge is inside)
constexpr float kRampUp = 0.94F;            // a ramp: its normal's y below this (and above kWallUp), or flagged vert
constexpr float kRiseBase = 2.0F;           // a rise starts at most this far above the point ...
constexpr float kRiseFoot = 3.0F;           // ... and comes this near the point's height
constexpr float kDropTop = 1.5F;            // an edge that drops: its top within this of the point's height
constexpr float kCoplanarDot = 0.995F;      // a decal's faces: normals this close ...
constexpr float kCoplanarDistance = 0.5F;   // ... and planes this close
constexpr float kDipLow = 0.05F;            // a dip: the ground round it no lower than this below it ...
constexpr float kDipHigh = 0.25F;           // ... and somewhere at least this above it
constexpr float kDipSearch = 12.0F;         // (the ground looked for this far above and below)
constexpr float kGutterRise = 10.0F;        // a gutter: a rise of at most this ...
constexpr float kGutterReach = 14.0F;       // ... at most this far away
constexpr float kPlatformReach = 160.0F;    // a platform: the ground ends in a drop this near ...
constexpr float kPlatformStep = 8.0F;       // (looked along in these steps: a slope over 20 degrees drops)
constexpr int kPlatformDrops = 6;           // ... in this many of eight ways (a ledge's, a box's, a table's top)
constexpr float kSameLevel = 1.0F;          // (the ground goes on: within this of the point's height)
constexpr float kGrimeRate = 1.0F / 150.0F; // grime decals a unit of a wall's foot (density 1)
constexpr float kLeastFoot = 24.0F;         // a wall's foot at least this long gets grime
constexpr float kGroundUnder = 2.0F;        // (a wall stands on ground: flat ground within this of its foot ...)
constexpr float kGroundOut = 6.0F;          // (... this far out from it)
constexpr float kFootSlack = 0.5F;          // (a foot edge: both its corners within this of the lowest)
constexpr float kGrimeSink = 1.0F;          // grime starts this far below the wall's foot (no gap at the ground)
constexpr float kUnit = 1.0F / 4294967296.0F;
constexpr float kHalf = 0.5F;
constexpr float kTau = 6.28318530718F;
constexpr uint32_t kByte = 0xFFU;

// Ground decals a square unit at density 1, by surface (about one in 5 - 10 square metres on asphalt and concrete).
constexpr std::array<float, kSurfaceCount> kDecalRate{
    0.0F,    // other
    1.2e-4F, // asphalt
    0.8e-4F, // concrete
    1.0e-4F, // paving
    0.5e-4F, // brick
    0.3e-4F, // dirt (wet patches only)
    0.0F,    // gravel
    0.0F,    // grass
};
// Each surface's decal kinds' weights (crack, stain, oil, tyre, gum, wet, grime).
using KindWeights = std::array<float, kDecalKinds>;
constexpr std::array<KindWeights, kSurfaceCount> kDecalWeights{{
    {0, 0, 0, 0, 0, 0, 0},
    {3.0F, 2.0F, 2.0F, 1.5F, 0.0F, 1.5F, 0.0F}, // asphalt
    {2.0F, 3.0F, 0.0F, 0.0F, 1.5F, 1.0F, 0.0F}, // concrete
    {1.0F, 2.0F, 0.0F, 0.0F, 3.0F, 0.0F, 0.0F}, // paving
    {0.5F, 2.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}, // brick
    {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F}, // dirt
    {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0},
}};
// Each kind's size (across, along: the least and the most, world units) and how far it shows for its size.
struct KindSize {
    std::array<float, 2> across;
    std::array<float, 2> along_share; // along = across x this (the art's shape)
};
constexpr std::array<KindSize, kDecalKinds> kDecalSizes{{
    {.across = {28.0F, 64.0F}, .along_share = {1.6F, 2.2F}},  // crack (long)
    {.across = {20.0F, 52.0F}, .along_share = {0.8F, 1.25F}}, // stain
    {.across = {16.0F, 40.0F}, .along_share = {0.8F, 1.25F}}, // oil
    {.across = {14.0F, 18.0F}, .along_share = {6.0F, 10.0F}}, // tyre (a pair of tracks)
    {.across = {14.0F, 26.0F}, .along_share = {0.9F, 1.1F}},  // gum (a cluster of spots)
    {.across = {30.0F, 84.0F}, .along_share = {0.7F, 1.3F}},  // wet
    {.across = {40.0F, 96.0F}, .along_share = {0.25F, 0.5F}}, // grime (along: its height, up the wall)
}};
constexpr float kWeedShare = 0.35F;   // hard ground's items at a rise's very foot: this share weeds
constexpr float kLeafShare = 0.6F;    // the rest: leaves, ...
constexpr float kPaperShare = 0.3F;   // ... paper, and cans
constexpr float kDirtPebbles = 0.7F;  // dirt's: pebbles, ...
constexpr float kDirtWeeds = 0.15F;   // ... weeds, and leaves
constexpr float kFadePerSize = 48.0F; // a decal fades out at this x its larger side (at most the decal distance)
constexpr float kLeastOpacity = 0.55F;

// Clutter candidates a square unit at density 1, by surface (hard ground: only near a rise is kept).
constexpr std::array<float, kSurfaceCount> kItemRate{
    0.0F,    // other
    4.0e-3F, // asphalt
    4.0e-3F, // concrete
    4.0e-3F, // paving
    3.0e-3F, // brick
    4.0e-3F, // dirt
    5.0e-3F, // gravel
    4.0e-4F, // grass (leaves)
};
constexpr std::array<std::array<float, 2>, kItemKinds> kItemSizes{{
    {2.5F, 4.0F},  // leaf (its length)
    {3.0F, 6.0F},  // paper scrap
    {4.6F, 5.0F},  // can (its length)
    {4.0F, 10.0F}, // weed (its height)
    {0.6F, 2.0F},  // pebble
}};

using Vec2 = std::array<float, 2>;

// A 32-bit integer hash (every input bit reaches every output bit).
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

// A stream of hashes from a seed (each next() a new one).
class Hashes {
public:
    explicit Hashes(uint32_t seed) : state_(seed) {}
    uint32_t next()
    {
        state_ = combine(state_, ++count_);
        return state_;
    }
    float next_unit() { return unit(next()); }
    float between(float low, float high) { return low + ((high - low) * next_unit()); }

private:
    uint32_t state_;
    uint32_t count_ = 0;
};

float x_of(const Vec3& v) { return core::at(v, 0); }
Vec2 flat_of(const Vec3& v) { return {core::at(v, 0), core::at(v, 2)}; }
float y_of(const Vec3& v) { return core::at(v, 1); }
float z_of(const Vec3& v) { return core::at(v, 2); }
Vec3 plus(const Vec3& a, const Vec3& b) { return {x_of(a) + x_of(b), y_of(a) + y_of(b), z_of(a) + z_of(b)}; }
Vec3 minus(const Vec3& a, const Vec3& b) { return {x_of(a) - x_of(b), y_of(a) - y_of(b), z_of(a) - z_of(b)}; }
Vec3 times(const Vec3& a, float scale) { return {x_of(a) * scale, y_of(a) * scale, z_of(a) * scale}; }
float dot(const Vec3& a, const Vec3& b) { return (x_of(a) * x_of(b)) + (y_of(a) * y_of(b)) + (z_of(a) * z_of(b)); }
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(y_of(a) * z_of(b)) - (z_of(a) * y_of(b)), (z_of(a) * x_of(b)) - (x_of(a) * z_of(b)),
            (x_of(a) * y_of(b)) - (y_of(a) * x_of(b))};
}
float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
Vec3 normalised(const Vec3& a)
{
    const float size = length(a);
    return size > 0.0F ? times(a, 1.0F / size) : Vec3{0.0F, 1.0F, 0.0F};
}
bool finite3(const Vec3& v) { return std::isfinite(x_of(v)) && std::isfinite(y_of(v)) && std::isfinite(z_of(v)); }

// The point of triangle (a, b, third) closest to `point` (the Voronoi regions' walk).
Vec3 closest_on_triangle(const Vec3& point, const Vec3& a, const Vec3& b, const Vec3& third)
{
    const Vec3 ab = minus(b, a);
    const Vec3 ac = minus(third, a);
    const Vec3 ap = minus(point, a);
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        return a;
    }
    const Vec3 bp = minus(point, b);
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        return b;
    }
    const float vc = (d1 * d4) - (d3 * d2);
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        return plus(a, times(ab, d1 / (d1 - d3)));
    }
    const Vec3 cp = minus(point, third);
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        return third;
    }
    const float vb = (d5 * d2) - (d1 * d6);
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        return plus(a, times(ac, d2 / (d2 - d6)));
    }
    const float va = (d3 * d6) - (d5 * d4);
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
        return plus(b, times(minus(third, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    }
    const float denominator = 1.0F / (va + vb + vc);
    return plus(a, plus(times(ab, vb * denominator), times(ac, vc * denominator)));
}

float flat_distance(const Vec3& a, const Vec3& b)
{
    const float dx = x_of(a) - x_of(b);
    const float dz = z_of(a) - z_of(b);
    return std::sqrt((dx * dx) + (dz * dz));
}

// What a face is, for the rules.
enum class Shape : uint8_t {
    kGround, // facing up, flat enough to put things on
    kWall,   // standing
    kRamp,   // a ramp, a quarter pipe, a bank, a vert face
    kOther,  // between ground and ramp, or facing down
};

struct FaceInfo {
    Vec3 normal{};
    float area = 0.0F;
    float bottom = 0.0F;
    float top = 0.0F;
    Shape shape = Shape::kOther;
    bool valid = false;
};

int64_t cell_of(float value) { return static_cast<int64_t>(std::floor(value / kCell)); }
uint64_t cell_key(int64_t x, int64_t z)
{
    constexpr uint64_t kShift = 32;
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << kShift) | static_cast<uint32_t>(z);
}

// A box seen from above.
struct Footprint {
    float min_x = 0.0F;
    float max_x = 0.0F;
    float min_z = 0.0F;
    float max_z = 0.0F;
};

// Indices by the grid's cells (seen from above).
class Grid {
public:
    void add(uint32_t index, const Footprint& box)
    {
        const int64_t x0 = cell_of(box.min_x);
        const int64_t x1 = cell_of(box.max_x);
        const int64_t z0 = cell_of(box.min_z);
        const int64_t z1 = cell_of(box.max_z);
        for (int64_t cx = x0; cx <= x1; ++cx) {
            for (int64_t cz = z0; cz <= z1; ++cz) {
                cells_[cell_key(cx, cz)].push_back(index);
            }
        }
    }
    // Each index whose cells meet the square of half-side `reach` round `at` (x, z), once.
    template <typename Visit>
    void near(const Vec2& at, float reach, std::vector<uint32_t>* stamps, uint32_t* stamp, Visit visit) const
    {
        ++*stamp;
        const int64_t x0 = cell_of(core::at(at, 0) - reach);
        const int64_t x1 = cell_of(core::at(at, 0) + reach);
        const int64_t z0 = cell_of(core::at(at, 1) - reach);
        const int64_t z1 = cell_of(core::at(at, 1) + reach);
        for (int64_t cx = x0; cx <= x1; ++cx) {
            for (int64_t cz = z0; cz <= z1; ++cz) {
                const auto found = cells_.find(cell_key(cx, cz));
                if (found == cells_.end()) {
                    continue;
                }
                for (const uint32_t index : found->second) {
                    uint32_t& seen = core::at(*stamps, index);
                    if (seen != *stamp) {
                        seen = *stamp;
                        visit(index);
                    }
                }
            }
        }
    }

private:
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
};

// The face's height over the ground point (x, z) when it covers it (seen from above).
bool height_over(const Face& face, const Vec2& ground, float* height)
{
    const float x = core::at(ground, 0);
    const float z = core::at(ground, 1);
    const Vec3& first = core::at(face.corners, 0);
    const Vec3& second = core::at(face.corners, 1);
    const Vec3& third = core::at(face.corners, 2);
    const float abx = x_of(second) - x_of(first);
    const float abz = z_of(second) - z_of(first);
    const float acx = x_of(third) - x_of(first);
    const float acz = z_of(third) - z_of(first);
    const float area = (abx * acz) - (acx * abz);
    if (std::abs(area) < kLeastArea) {
        return false;
    }
    const float px = x - x_of(first);
    const float pz = z - z_of(first);
    const float u = ((px * acz) - (acx * pz)) / area;
    const float v = ((abx * pz) - (px * abz)) / area;
    if (u < -kInsideSlack || v < -kInsideSlack || u + v > 1.0F + kInsideSlack) {
        return false;
    }
    *height = y_of(first) + (u * (y_of(second) - y_of(first))) + (v * (y_of(third) - y_of(first)));
    return true;
}

// The world as the rules read it: the faces' shapes and grids, the rails' grid.
class World {
public:
    World(std::span<const Face> faces, std::span<const Rail> rails) : faces_(faces), rails_(rails)
    {
        infos_.resize(faces.size());
        face_stamps_.assign(faces.size(), 0);
        rail_stamps_.assign(rails.size(), 0);
        for (std::size_t index = 0; index < faces.size(); ++index) {
            const Face& face = core::at(faces, index);
            FaceInfo& info = core::at(infos_, index);
            const Vec3& first = core::at(face.corners, 0);
            const Vec3& second = core::at(face.corners, 1);
            const Vec3& third = core::at(face.corners, 2);
            if (!finite3(first) || !finite3(second) || !finite3(third)) {
                continue;
            }
            // (the game's winding: (v2 - v0) x (v1 - v0) - TriMeshSource::get_triangle_normal - faces up on the ground)
            const Vec3 normal = cross(minus(third, first), minus(second, first));
            const float twice = length(normal);
            if (!(twice * kHalf > kLeastArea)) {
                continue;
            }
            info.valid = true;
            info.normal = times(normal, 1.0F / twice);
            info.area = twice * kHalf;
            info.bottom = std::min({y_of(first), y_of(second), y_of(third)});
            info.top = std::max({y_of(first), y_of(second), y_of(third)});
            const float up = y_of(info.normal);
            if (face.vert) {
                info.shape = Shape::kRamp;
            } else if (up >= kFlatUp) {
                info.shape = Shape::kGround;
            } else if (std::abs(up) < kWallUp) {
                info.shape = Shape::kWall;
            } else if (up >= kWallUp && up < kRampUp) {
                // (first slope of dirt, gravel or grass is first hillside, not first ramp built to skate)
                const bool natural = face.surface == Surface::kDirt || face.surface == Surface::kGravel ||
                                     face.surface == Surface::kGrass;
                info.shape = natural ? Shape::kOther : Shape::kRamp;
            } else {
                info.shape = Shape::kOther;
            }
            const Footprint box{.min_x = std::min({x_of(first), x_of(second), x_of(third)}),
                                .max_x = std::max({x_of(first), x_of(second), x_of(third)}),
                                .min_z = std::min({z_of(first), z_of(second), z_of(third)}),
                                .max_z = std::max({z_of(first), z_of(second), z_of(third)})};
            const auto at = static_cast<uint32_t>(index);
            all_.add(at, box);
            if (up >= kWallUp) {
                floors_.add(at, box); // (anything facing up: the platform rule's ground)
            }
            switch (info.shape) {
            case Shape::kWall:
                walls_.add(at, box);
                break;
            case Shape::kRamp:
                ramps_.add(at, box);
                break;
            case Shape::kGround:
                grounds_.add(at, box);
                break;
            case Shape::kOther:
                break;
            }
        }
        for (std::size_t index = 0; index < rails.size(); ++index) {
            const Rail& rail = core::at(rails, index);
            if (!finite3(rail.a) || !finite3(rail.b)) {
                continue;
            }
            rails_grid_.add(static_cast<uint32_t>(index), {.min_x = std::min(x_of(rail.a), x_of(rail.b)),
                                                           .max_x = std::max(x_of(rail.a), x_of(rail.b)),
                                                           .min_z = std::min(z_of(rail.a), z_of(rail.b)),
                                                           .max_z = std::max(z_of(rail.a), z_of(rail.b))});
        }
    }

    [[nodiscard]] const FaceInfo& info(std::size_t index) const { return core::at(infos_, index); }
    [[nodiscard]] const Face& face(std::size_t index) const { return core::at(faces_, index); }
    [[nodiscard]] std::size_t size() const { return faces_.size(); }

    // Whether a face (not coplanar ground) covers the point: kCoverFloor - kCoverReach above it.
    bool covered(const Vec3& point)
    {
        bool found = false;
        all_.near(flat_of(point), 0.0F, &face_stamps_, &face_stamp_, [&](uint32_t index) {
            if (found || !info(index).valid || info(index).top < y_of(point) + kCoverFloor) {
                return;
            }
            float height = 0.0F;
            if (!height_over(face(index), flat_of(point), &height)) {
                return;
            }
            const float above = height - y_of(point);
            found = above >= kCoverFloor && above <= kCoverReach;
        });
        return found;
    }

    // A ramp's (quarter pipe's, bank's, vert face's) part near the point: within kRampReach, from just below it to
    // kRampHeight above it.
    bool near_ramp(const Vec3& point)
    {
        bool found = false;
        ramps_.near(flat_of(point), kRampReach, &face_stamps_, &face_stamp_, [&](uint32_t index) {
            const FaceInfo& ramp = info(index);
            if (found || ramp.top < y_of(point) - kRiseFoot || ramp.bottom > y_of(point) + kRampHeight) {
                return;
            }
            const Face& face = this->face(index);
            const Vec3 near = closest_on_triangle(point, core::at(face.corners, 0), core::at(face.corners, 1),
                                                  core::at(face.corners, 2));
            found = length(minus(near, point)) <= kRampReach;
        });
        return found;
    }

    struct Walls {
        bool drop = false;
        float rise = -1.0F; // the nearest rise's distance (seen from above; -1: none within `reach`)
        float rise_height = 0.0F;
        Vec3 rise_point{}; // its nearest point
        uint32_t rise_face = 0;
    };
    // The walls near the point: an edge dropping away within kDropReach, the nearest rise within `reach`.
    Walls walls_near(const Vec3& point, float reach)
    {
        Walls out;
        const float look = std::max(reach, kDropReach);
        walls_.near(flat_of(point), look, &face_stamps_, &face_stamp_, [&](uint32_t index) {
            const FaceInfo& wall = info(index);
            const float py = y_of(point);
            const Face& face = this->face(index);
            const Vec3 probe{x_of(point), std::clamp(py + 1.0F, wall.bottom, wall.top), z_of(point)};
            const Vec3 near = closest_on_triangle(probe, core::at(face.corners, 0), core::at(face.corners, 1),
                                                  core::at(face.corners, 2));
            const float across = flat_distance(near, point);
            if (std::abs(wall.top - py) <= kDropTop && wall.bottom <= py - kDropDepth && across <= kDropReach) {
                out.drop = true;
            }
            if (wall.bottom <= py + kRiseBase && wall.top >= py + kLeastRise && y_of(near) <= py + kRiseFoot &&
                across <= reach && (out.rise < 0.0F || across < out.rise)) {
                out.rise = across;
                out.rise_height = wall.top - py;
                out.rise_point = near;
                out.rise_face = index;
            }
        });
        return out;
    }

    // A low rise at `rise_point` (`height` above the point) with another rise beyond its top: a stair's first step.
    bool stairs_beyond(const Vec3& point, const Vec3& rise_point, float height)
    {
        const float dx = x_of(rise_point) - x_of(point);
        const float dz = z_of(rise_point) - z_of(point);
        const float apart = std::sqrt((dx * dx) + (dz * dz));
        constexpr float kLeastApart = 1.0e-3F;
        if (!(apart > kLeastApart)) {
            return false;
        }
        constexpr float kStep = 6.0F; // (onto the step: this far past its riser)
        const Vec3 top{x_of(rise_point) + (dx / apart * kStep), y_of(point) + height,
                       z_of(rise_point) + (dz / apart * kStep)};
        const Walls beyond = walls_near(top, kStairLook);
        return beyond.rise >= 0.0F;
    }

    // A grind rail near the point (seen from above, kRailBelow under to kRailAbove over it).
    bool near_rail(const Vec3& point)
    {
        bool found = false;
        rails_grid_.near(flat_of(point), kRailReach, &rail_stamps_, &rail_stamp_, [&](uint32_t index) {
            if (found) {
                return;
            }
            const Rail& rail = core::at(rails_, index);
            const float rx = x_of(rail.b) - x_of(rail.a);
            const float rz = z_of(rail.b) - z_of(rail.a);
            const float span = (rx * rx) + (rz * rz);
            constexpr float kLeastSpan = 1.0e-6F;
            float along = 0.0F;
            if (span > kLeastSpan) {
                along = std::clamp((((x_of(point) - x_of(rail.a)) * rx) + ((z_of(point) - z_of(rail.a)) * rz)) / span,
                                   0.0F, 1.0F);
            }
            const Vec3 on = plus(rail.a, times(minus(rail.b, rail.a), along));
            const float above = y_of(on) - y_of(point);
            found = flat_distance(on, point) <= kRailReach && above >= -kRailBelow && above <= kRailAbove;
        });
        return found;
    }

    // A raised platform: the ground the point stands on ends in a drop (lower ground, or none) within kPlatformReach in
    // at least kPlatformDrops of eight ways - a ledge's, a box's, a planter's or a table's top wider than kDropReach.
    bool on_platform(const Vec3& point)
    {
        constexpr int kWays = 8;
        int drops = 0;
        for (int way = 0; way < kWays && drops + (kWays - way) >= kPlatformDrops; ++way) {
            const float angle = kTau * static_cast<float>(way) / static_cast<float>(kWays);
            const float dx = std::cos(angle);
            const float dz = std::sin(angle);
            float running = y_of(point); // (the ground followed out: a hillside's slope isn't a drop)
            constexpr int kSteps = static_cast<int>(kPlatformReach / kPlatformStep);
            for (int step = 1; step <= kSteps; ++step) {
                const float out = kPlatformStep * static_cast<float>(step);
                float height = 0.0F;
                if (!floor_at(x_of(point) + (dx * out), z_of(point) + (dz * out), running, &height) ||
                    height < running - kDropDepth) {
                    ++drops; // (it ends in a drop, or over nothing)
                    break;
                }
                if (height > running + kDropDepth) {
                    break; // (it steps up - a wall, a kerb: not a drop)
                }
                running = height;
            }
        }
        return drops >= kPlatformDrops;
    }

    // The height at (x, z) of anything facing up within kDipSearch of `near_height` (the highest; false: none).
    bool floor_at(float x, float z, float near_height, float* height)
    {
        bool found = false;
        floors_.near({x, z}, 0.0F, &face_stamps_, &face_stamp_, [&](uint32_t index) {
            float here = 0.0F;
            if (!height_over(face(index), {x, z}, &here) || std::abs(here - near_height) > kDipSearch) {
                return;
            }
            if (!found || here > *height) {
                *height = here;
                found = true;
            }
        });
        return found;
    }

    // The ground's height at (x, z) within kDipSearch of `near_height` (the highest such ground face; false: none).
    bool ground_at(float x, float z, float near_height, float* height)
    {
        bool found = false;
        grounds_.near({x, z}, 0.0F, &face_stamps_, &face_stamp_, [&](uint32_t index) {
            float here = 0.0F;
            if (!height_over(face(index), {x, z}, &here) || std::abs(here - near_height) > kDipSearch) {
                return;
            }
            if (!found || here > *height) {
                *height = here;
                found = true;
            }
        });
        return found;
    }

    // A dip: the ground round the point (a ring of eight at `radius`) all there, none lower, some higher.
    bool in_dip(const Vec3& point, float radius)
    {
        bool higher = false;
        constexpr int kRing = 8;
        for (int k = 0; k < kRing; ++k) {
            const float angle = kTau * static_cast<float>(k) / static_cast<float>(kRing);
            float height = 0.0F;
            if (!ground_at(x_of(point) + (radius * std::cos(angle)), z_of(point) + (radius * std::sin(angle)),
                           y_of(point), &height)) {
                return false;
            }
            if (height < y_of(point) - kDipLow) {
                return false;
            }
            higher = higher || height >= y_of(point) + kDipHigh;
        }
        return higher;
    }

    // The faces coplanar with face `host` (the same surface) that a disc of `radius` round `centre` may meet.
    std::vector<uint32_t> coplanar(uint32_t host, const Vec3& centre, float radius)
    {
        std::vector<uint32_t> found;
        const FaceInfo& host_info = info(host);
        const Surface surface = face(host).surface;
        const float plane = dot(host_info.normal, core::at(face(host).corners, 0));
        all_.near(flat_of(centre), radius, &face_stamps_, &face_stamp_, [&](uint32_t index) {
            const FaceInfo& other = info(index);
            if (!other.valid || face(index).surface != surface || dot(other.normal, host_info.normal) < kCoplanarDot) {
                return;
            }
            for (const Vec3& corner : face(index).corners) {
                if (std::abs(dot(host_info.normal, corner) - plane) > kCoplanarDistance) {
                    return;
                }
            }
            found.push_back(index);
        });
        std::ranges::sort(found);
        return found;
    }

private:
    std::span<const Face> faces_;
    std::span<const Rail> rails_;
    std::vector<FaceInfo> infos_;
    Grid all_;
    Grid walls_;
    Grid ramps_;
    Grid grounds_;
    Grid floors_;
    Grid rails_grid_;
    std::vector<uint32_t> face_stamps_;
    uint32_t face_stamp_ = 0;
    std::vector<uint32_t> rail_stamps_;
    uint32_t rail_stamp_ = 0;
};

// A face's seed: its corners' bits and the level's seed (the same face: the same things, whatever order it loaded in).
uint32_t face_seed(const Face& face, uint32_t seed, uint32_t salt)
{
    uint32_t value = combine(seed, salt);
    for (const Vec3& corner : face.corners) {
        for (const float coordinate : corner) {
            value = combine(value, std::bit_cast<uint32_t>(coordinate == 0.0F ? 0.0F : coordinate)); // (-0 as 0)
        }
    }
    return value;
}

// A uniform point on the face from two hashes.
Vec3 point_on(const Face& face, uint32_t first, uint32_t second)
{
    const float r1 = std::sqrt(unit(first));
    const float r2 = unit(second);
    const float wa = 1.0F - r1;
    const float wb = r1 * (1.0F - r2);
    const float wc = r1 * r2;
    const Vec3& corner_a = core::at(face.corners, 0);
    const Vec3& corner_b = core::at(face.corners, 1);
    const Vec3& third = core::at(face.corners, 2);
    return {(wa * x_of(corner_a)) + (wb * x_of(corner_b)) + (wc * x_of(third)),
            (wa * y_of(corner_a)) + (wb * y_of(corner_b)) + (wc * y_of(third)),
            (wa * z_of(corner_a)) + (wb * z_of(corner_b)) + (wc * z_of(third))};
}

// How many of something a face gets: its area x the rate, the fraction rounded by a hash.
uint32_t count_for(float amount, uint32_t hash)
{
    return amount > 0.0F ? static_cast<uint32_t>(std::floor(amount + unit(hash))) : 0U;
}

// --- the decals' geometry --------------------------------------------------------------------------------------

// A polygon in a plane's coordinates (s along `across`, t along `along`).
using Polygon = std::vector<Vec2>;

// The polygon clipped to the half-plane left of the edge `from` - `to` (Sutherland - Hodgman, one edge).
Polygon clip_edge(const Polygon& polygon, const Vec2& from, const Vec2& to, float winding)
{
    Polygon out;
    const auto side = [&](const Vec2& v) {
        return winding * (((core::at(to, 0) - core::at(from, 0)) * (core::at(v, 1) - core::at(from, 1))) -
                          ((core::at(to, 1) - core::at(from, 1)) * (core::at(v, 0) - core::at(from, 0))));
    };
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Vec2& current = core::at(polygon, index);
        const Vec2& next = core::at(polygon, (index + 1) % polygon.size());
        const float sc = side(current);
        const float sn = side(next);
        if (sc >= 0.0F) {
            out.push_back(current);
        }
        if ((sc >= 0.0F) != (sn >= 0.0F)) {
            const float along = sc / (sc - sn);
            out.push_back({core::at(current, 0) + (along * (core::at(next, 0) - core::at(current, 0))),
                           core::at(current, 1) + (along * (core::at(next, 1) - core::at(current, 1)))});
        }
    }
    return out;
}

// A decal's frame: its centre, its two axes in the plane (unit), its normal, its half sizes.
struct Frame {
    Vec3 centre{};
    Vec3 across{};
    Vec3 along{};
    Vec3 normal{};
    float half_across = 0.0F;
    float half_along = 0.0F;
    float along_start = -1.0F; // (the quad's t from along_start to 1 x half_along: grime starts at its foot)
};

struct DecalArt {
    DecalKind kind = DecalKind::kStain;
    uint8_t variant = 0;
    float opacity = 1.0F;
    float fade = 0.0F; // world units
};

uint32_t pack_decal(const DecalArt& art)
{
    constexpr uint32_t kVariantShift = 4;
    constexpr uint32_t kOpacityShift = 8;
    constexpr uint32_t kFadeShift = 16;
    const auto opacity = static_cast<uint32_t>(std::lround(std::clamp(art.opacity, 0.0F, 1.0F) * 255.0F));
    const auto fade = static_cast<uint32_t>(
        std::clamp(std::lround(art.fade / static_cast<float>(kDecalFadeUnits)), 1L, static_cast<long>(kByte)));
    return static_cast<uint32_t>(art.kind) | (static_cast<uint32_t>(art.variant & 3U) << kVariantShift) |
           (opacity << kOpacityShift) | (fade << kFadeShift);
}

// The decal's triangles: its quad cut to each of `faces` (coplanar with its host), in the decal's coordinates.
void cut_decal(const World& world, std::span<const uint32_t> faces, const Frame& frame, const DecalArt& art,
               std::vector<DecalVertex>* out)
{
    const uint32_t packed = pack_decal(art);
    const float start = frame.along_start * frame.half_along;
    const Polygon quad{{-frame.half_across, start},
                       {frame.half_across, start},
                       {frame.half_across, frame.half_along},
                       {-frame.half_across, frame.half_along}};
    const float span_along = frame.half_along - start;
    for (const uint32_t index : faces) {
        const Face& face = world.face(index);
        std::array<Vec2, 3> corners{};
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec3 offset = minus(core::at(face.corners, k), frame.centre);
            core::at(corners, k) = {dot(offset, frame.across), dot(offset, frame.along)};
        }
        const float signed_area = ((core::at(core::at(corners, 1), 0) - core::at(core::at(corners, 0), 0)) *
                                   (core::at(core::at(corners, 2), 1) - core::at(core::at(corners, 0), 1))) -
                                  ((core::at(core::at(corners, 2), 0) - core::at(core::at(corners, 0), 0)) *
                                   (core::at(core::at(corners, 1), 1) - core::at(core::at(corners, 0), 1)));
        if (std::abs(signed_area) < kLeastArea) {
            continue;
        }
        const float winding = signed_area > 0.0F ? 1.0F : -1.0F;
        Polygon piece = quad;
        for (std::size_t k = 0; k < 3 && piece.size() >= 3; ++k) {
            piece = clip_edge(piece, core::at(corners, k), core::at(corners, (k + 1) % 3), winding);
        }
        if (piece.size() < 3) {
            continue;
        }
        // (the plane's point under each of the piece's corners: on the host's plane through the centre)
        const auto vertex = [&](const Vec2& at) {
            const Vec3 position =
                plus(frame.centre, plus(times(frame.across, core::at(at, 0)), times(frame.along, core::at(at, 1))));
            return DecalVertex{.position = position,
                               .uv = {(core::at(at, 0) / (frame.half_across + frame.half_across)) + kHalf,
                                      (core::at(at, 1) - start) / span_along},
                               .packed = packed};
        };
        for (std::size_t k = 1; k + 1 < piece.size(); ++k) {
            out->push_back(vertex(core::at(piece, 0)));
            out->push_back(vertex(core::at(piece, k)));
            out->push_back(vertex(core::at(piece, k + 1)));
        }
    }
}

DecalKind pick_kind(const KindWeights& weights, float choice)
{
    float total = 0.0F;
    for (const float weight : weights) {
        total += weight;
    }
    float left = choice * total;
    for (std::size_t kind = 0; kind < kDecalKinds; ++kind) {
        left -= core::at(weights, kind);
        if (left < 0.0F) {
            return static_cast<DecalKind>(kind);
        }
    }
    for (std::size_t kind = kDecalKinds; kind > 0; --kind) {
        if (core::at(weights, kind - 1) > 0.0F) {
            return static_cast<DecalKind>(kind - 1);
        }
    }
    return DecalKind::kStain;
}

struct Placed {
    std::vector<DecalVertex> vertices;
    Decal decal;
};

struct PlacedItem {
    Item item;
    int64_t tile_x = 0;
    int64_t tile_z = 0;
    uint32_t order = 0;
};

int64_t tile_of(float value) { return static_cast<int64_t>(std::floor(value / kTileSize)); }

bool is_hard(Surface surface)
{
    return surface == Surface::kAsphalt || surface == Surface::kConcrete || surface == Surface::kPaving ||
           surface == Surface::kBrick;
}

// --- the passes over the faces -------------------------------------------------------------------------------------

class Maker {
public:
    Maker(const Input& input, Scatter* out) : seed_(input.seed), out_(out), world_(input.faces, input.rails) {}

    void ground_decals()
    {
        const float density = out_->decal_density;
        for (std::size_t index = 0; index < world_.size(); ++index) {
            const FaceInfo& info = world_.info(index);
            const Face& face = world_.face(index);
            if (!info.valid || info.shape != Shape::kGround) {
                continue;
            }
            const auto surface = static_cast<std::size_t>(face.surface);
            const float rate = core::at(kDecalRate, surface);
            if (!(rate > 0.0F)) {
                continue;
            }
            const uint32_t seed = face_seed(face, seed_, 1U);
            const uint32_t count = count_for(info.area * rate * density, mix(seed));
            for (uint32_t k = 0; k < count && !out_->decals_capped; ++k) {
                Hashes hashes(combine(seed, k));
                const uint32_t first = hashes.next();
                const Vec3 centre = point_on(face, first, hashes.next());
                const DecalKind kind = pick_kind(core::at(kDecalWeights, surface), hashes.next_unit());
                ground_decal(static_cast<uint32_t>(index), centre, kind, &hashes);
            }
        }
    }

    void wall_grime()
    {
        const float density = out_->decal_density;
        for (std::size_t index = 0; index < world_.size(); ++index) {
            const FaceInfo& info = world_.info(index);
            const Face& face = world_.face(index);
            if (!info.valid || info.shape != Shape::kWall ||
                (face.surface != Surface::kBrick && face.surface != Surface::kConcrete)) {
                continue;
            }
            // the foot: the edge whose two corners are lowest
            std::size_t foot = 3;
            for (std::size_t k = 0; k < 3; ++k) {
                const Vec3& one = core::at(face.corners, k);
                const Vec3& two = core::at(face.corners, (k + 1) % 3);
                if (y_of(one) <= info.bottom + kFootSlack && y_of(two) <= info.bottom + kFootSlack) {
                    foot = k;
                }
            }
            if (foot == 3) {
                continue;
            }
            const Vec3& foot_from = core::at(face.corners, foot);
            const Vec3& foot_to = core::at(face.corners, (foot + 1) % 3);
            const float foot_length = length(minus(foot_to, foot_from));
            if (foot_length < kLeastFoot) {
                continue;
            }
            const uint32_t seed = face_seed(face, seed_, 2U);
            const uint32_t count = count_for(foot_length * kGrimeRate * density, mix(seed));
            for (uint32_t k = 0; k < count && !out_->decals_capped; ++k) {
                Hashes hashes(combine(seed, k));
                const float along = hashes.next_unit();
                wall_decal(static_cast<uint32_t>(index),
                           {.at = plus(foot_from, times(minus(foot_to, foot_from), along)),
                            .direction = minus(foot_to, foot_from)},
                           &hashes);
            }
        }
    }

    void clutter()
    {
        const float density = out_->clutter_density;
        for (std::size_t index = 0; index < world_.size(); ++index) {
            const FaceInfo& info = world_.info(index);
            const Face& face = world_.face(index);
            if (!info.valid || info.shape != Shape::kGround) {
                continue;
            }
            const float rate = core::at(kItemRate, static_cast<std::size_t>(face.surface));
            if (!(rate > 0.0F)) {
                continue;
            }
            const uint32_t seed = face_seed(face, seed_, 3U);
            const uint32_t count = count_for(info.area * rate * density, mix(seed));
            for (uint32_t k = 0; k < count; ++k) {
                Hashes hashes(combine(seed, k));
                const uint32_t first = hashes.next();
                const Vec3 root = point_on(face, first, hashes.next());
                item(face.surface, root, &hashes);
            }
        }
    }

    void finish();

private:
    // The rules every decal and item meets; true: turned away (counted in `rejected`).
    bool skate_line(const Vec3& point, Rejected* rejected)
    {
        if (world_.near_ramp(point)) {
            ++rejected->ramp;
            return true;
        }
        if (world_.near_rail(point)) {
            ++rejected->rail;
            return true;
        }
        if (world_.covered(point)) {
            ++rejected->covered;
            return true;
        }
        if (world_.on_platform(point)) {
            ++rejected->drop;
            return true;
        }
        return false;
    }

    void ground_decal(uint32_t host, const Vec3& centre, DecalKind kind, Hashes* hashes)
    {
        Rejected& rejected = out_->decal_rejected;
        const KindSize& size = core::at(kDecalSizes, static_cast<std::size_t>(kind));
        const float across = hashes->between(core::at(size.across, 0), core::at(size.across, 1));
        const float along = across * hashes->between(core::at(size.along_share, 0), core::at(size.along_share, 1));
        const float turn = hashes->next_unit() * kTau;
        const auto variant = static_cast<uint8_t>(hashes->next() & 3U);
        const float opacity = hashes->between(kLeastOpacity, 1.0F);
        const float radius = kHalf * std::sqrt((across * across) + (along * along));
        if (skate_line(centre, &rejected)) {
            return;
        }
        const Walls walls = world_.walls_near(centre, kGutterReach);
        if (walls.drop) {
            ++rejected.drop;
            return;
        }
        if (kind == DecalKind::kWet) {
            const bool gutter =
                walls.rise >= 0.0F && walls.rise_height <= kGutterRise &&
                (world_.face(host).surface == Surface::kAsphalt || world_.face(host).surface == Surface::kConcrete);
            constexpr float kDipShare = 0.8F; // (of the decal's radius: the ring looked at)
            constexpr float kLeastDip = 24.0F;
            if (!gutter && !world_.in_dip(centre, std::max(radius * kDipShare, kLeastDip))) {
                ++rejected.no_dip;
                return;
            }
        }
        const FaceInfo& info = world_.info(host);
        const Vec3 normal = info.normal;
        // the axes: `across` turned in the plane
        const Vec3 reference = std::abs(x_of(normal)) < 0.9F ? Vec3{1.0F, 0.0F, 0.0F} : Vec3{0.0F, 0.0F, 1.0F};
        const Vec3 first = normalised(cross(normal, reference));
        const Vec3 second = cross(normal, first);
        const Vec3 axis_across = plus(times(first, std::cos(turn)), times(second, std::sin(turn)));
        const Vec3 axis_along = cross(normal, axis_across);
        const Frame frame{.centre = centre,
                          .across = axis_across,
                          .along = axis_along,
                          .normal = normal,
                          .half_across = across * kHalf,
                          .half_along = along * kHalf,
                          .along_start = -1.0F};
        add_decal(
            host, frame,
            {.kind = kind, .variant = variant, .opacity = opacity, .fade = kFadePerSize * std::max(across, along)},
            {across, along});
    }

    // A wall's foot: a point on it and its direction.
    struct Foot {
        Vec3 at;
        Vec3 direction;
    };

    void wall_decal(uint32_t host, const Foot& foot_line, Hashes* hashes)
    {
        const Vec3& foot = foot_line.at;
        const Vec3& foot_direction = foot_line.direction;
        Rejected& rejected = out_->decal_rejected;
        const KindSize& size = core::at(kDecalSizes, static_cast<std::size_t>(DecalKind::kGrime));
        const float across = hashes->between(core::at(size.across, 0), core::at(size.across, 1));
        const float height = across * hashes->between(core::at(size.along_share, 0), core::at(size.along_share, 1));
        const auto variant = static_cast<uint8_t>(hashes->next() & 3U);
        const float opacity = hashes->between(kLeastOpacity, 1.0F);
        Vec3 normal = world_.info(host).normal;
        // the ground the wall stands on: on its front (else its back: the wall faced the other way)
        Vec3 out_dir = normalised(Vec3{x_of(normal), 0.0F, z_of(normal)});
        float ground = 0.0F;
        Vec3 ground_point = plus(foot, times(out_dir, kGroundOut));
        bool stands = world_.ground_at(x_of(ground_point), z_of(ground_point), y_of(foot), &ground) &&
                      std::abs(ground - y_of(foot)) <= kGroundUnder;
        if (!stands) {
            out_dir = times(out_dir, -1.0F);
            normal = times(normal, -1.0F);
            ground_point = plus(foot, times(out_dir, kGroundOut));
            stands = world_.ground_at(x_of(ground_point), z_of(ground_point), y_of(foot), &ground) &&
                     std::abs(ground - y_of(foot)) <= kGroundUnder;
        }
        if (!stands) {
            return; // (a wall standing on nothing flat: a ledge's side over a drop, a pillar in the air)
        }
        ground_point = {x_of(ground_point), ground, z_of(ground_point)};
        if (skate_line(ground_point, &rejected)) {
            return;
        }
        const Vec3 axis_across = normalised(foot_direction);
        Vec3 axis_up = cross(normal, axis_across);
        if (y_of(axis_up) < 0.0F) {
            axis_up = times(axis_up, -1.0F);
        }
        const Vec3 base{x_of(foot), ground - kGrimeSink, z_of(foot)};
        // (the frame's centre at the foot: the quad runs from the foot (t 0) up to its height)
        const Frame frame{.centre = base,
                          .across = axis_across,
                          .along = axis_up,
                          .normal = normal,
                          .half_across = across * kHalf,
                          .half_along = height + kGrimeSink,
                          .along_start = 0.0F};
        add_decal(host, frame,
                  {.kind = DecalKind::kGrime, .variant = variant, .opacity = opacity, .fade = kFadePerSize * across},
                  {across, height});
    }

    void add_decal(uint32_t host, const Frame& frame, const DecalArt& art, const Vec2& size)
    {
        const float radius = std::sqrt((frame.half_across * frame.half_across) + (frame.half_along * frame.half_along));
        const std::vector<uint32_t> faces = world_.coplanar(host, frame.centre, radius);
        Placed placed;
        cut_decal(world_, faces, frame, art, &placed.vertices);
        if (placed.vertices.empty()) {
            return;
        }
        if (decal_vertices_ + placed.vertices.size() > kMostDecalVertices) {
            out_->decals_capped = true;
            return;
        }
        decal_vertices_ += placed.vertices.size();
        placed.decal = {.centre = frame.centre,
                        .normal = frame.normal,
                        .kind = art.kind,
                        .variant = art.variant,
                        .size = size,
                        .face = host,
                        .first_vertex = 0,
                        .vertex_count = static_cast<uint32_t>(placed.vertices.size())};
        ++core::at(out_->decals_of, static_cast<std::size_t>(art.kind));
        placed_.push_back(std::move(placed));
    }

    void item(Surface surface, const Vec3& root, Hashes* hashes)
    {
        Rejected& rejected = out_->item_rejected;
        ItemKind kind = ItemKind::kLeaf;
        const float choice = hashes->next_unit();
        const float keep = hashes->next_unit();
        const bool hard = is_hard(surface);
        Walls walls;
        if (hard) {
            // hard ground: only near a rise - a wall or a kerb (the cheap test first)
            walls = world_.walls_near(root, kNearWall);
            if (walls.rise < 0.0F) {
                ++rejected.far_from_wall;
                return;
            }
            const float near = 1.0F - (walls.rise / kNearWall);
            if (keep > near * near) {
                ++rejected.far_from_wall;
                return;
            }
            if (walls.rise <= kWeedReach && choice < kWeedShare) {
                kind = ItemKind::kWeed;
            } else {
                const float rest = hashes->next_unit();
                kind = ItemKind::kCan;
                if (rest < kLeafShare) {
                    kind = ItemKind::kLeaf;
                } else if (rest < kLeafShare + kPaperShare) {
                    kind = ItemKind::kPaper;
                }
            }
        } else {
            walls = world_.walls_near(root, kDropReach);
            switch (surface) {
            case Surface::kDirt:
                kind = ItemKind::kLeaf;
                if (choice < kDirtPebbles) {
                    kind = ItemKind::kPebble;
                } else if (choice < kDirtPebbles + kDirtWeeds) {
                    kind = ItemKind::kWeed;
                }
                break;
            case Surface::kGravel:
                kind = ItemKind::kPebble;
                break;
            default:
                kind = ItemKind::kLeaf;
                break;
            }
        }
        if (walls.drop) {
            ++rejected.drop;
            return;
        }
        if (skate_line(root, &rejected)) {
            return;
        }
        if (walls.rise >= 0.0F && walls.rise_height < kLowRise &&
            world_.stairs_beyond(root, walls.rise_point, walls.rise_height)) {
            ++rejected.stairs;
            return;
        }
        const auto which = static_cast<std::size_t>(kind);
        const float size =
            hashes->between(core::at(core::at(kItemSizes, which), 0), core::at(core::at(kItemSizes, which), 1));
        const uint32_t look = hashes->next();
        constexpr float kMostTilt = 0.35F;
        const float tilt = (kind == ItemKind::kLeaf || kind == ItemKind::kPaper)
                               ? ((hashes->next_unit() * 2.0F) - 1.0F) * kMostTilt
                               : 0.0F;
        const uint32_t packed = (look & kByte) | (static_cast<uint32_t>(kind) << kItemKindShift) |
                                (((look >> kItemKindShift) & 0xFU) << kItemVariantShift) |
                                (((look >> kItemFacingShift) & kByte) << kItemFacingShift) |
                                (mix(look) & (kByte << kItemHashShift));
        if (items_.size() >= item_cap_) {
            out_->items_capped = true;
            return;
        }
        items_.push_back({.item = {.root = root, .packed = packed, .size = size, .tilt = tilt, .spare = {}},
                          .tile_x = tile_of(x_of(root)),
                          .tile_z = tile_of(z_of(root)),
                          .order = static_cast<uint32_t>(items_.size())});
        ++core::at(out_->items_of, which);
    }

    using Walls = World::Walls;

    uint32_t seed_ = 0;
    Scatter* out_;
    World world_;
    std::vector<Placed> placed_;
    std::size_t decal_vertices_ = 0;
    std::vector<PlacedItem> items_;
    std::size_t item_cap_ = kMostItems;
};

// The decals and items put in tiles: the decals' vertices by tile (as placed within it), the items by tile then rank.
void Maker::finish()
{
    struct TileKey {
        int64_t x = 0;
        int64_t z = 0;
        bool operator<(const TileKey& other) const { return x != other.x ? x < other.x : z < other.z; }
        bool operator==(const TileKey& other) const = default;
    };
    // the decals: by the tile of their centre, then as placed
    std::vector<std::size_t> order(placed_.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        core::at(order, index) = index;
    }
    const auto key_of_decal = [&](std::size_t index) {
        const Vec3& centre = core::at(placed_, index).decal.centre;
        return TileKey{.x = tile_of(x_of(centre)), .z = tile_of(z_of(centre))};
    };
    std::ranges::stable_sort(order,
                             [&](std::size_t one, std::size_t two) { return key_of_decal(one) < key_of_decal(two); });
    std::ranges::sort(items_, [](const PlacedItem& one, const PlacedItem& two) {
        if (one.tile_x != two.tile_x) {
            return one.tile_x < two.tile_x;
        }
        if (one.tile_z != two.tile_z) {
            return one.tile_z < two.tile_z;
        }
        const uint32_t rank_one = one.item.packed & kByte;
        const uint32_t rank_two = two.item.packed & kByte;
        return rank_one != rank_two ? rank_one < rank_two : one.order < two.order;
    });
    // walk both in tile order, making the tiles
    std::size_t next_decal = 0;
    std::size_t next_item = 0;
    const auto grow = [](Tile* tile, const Vec3& point, float reach) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(tile->low, axis) = std::min(core::at(tile->low, axis), core::at(point, axis) - reach);
            core::at(tile->high, axis) = std::max(core::at(tile->high, axis), core::at(point, axis) + reach);
        }
    };
    out_->decal_vertices.reserve(decal_vertices_);
    out_->items.reserve(items_.size());
    while (next_decal < order.size() || next_item < items_.size()) {
        TileKey key{};
        const bool decal_first = next_item >= items_.size() ||
                                 (next_decal < order.size() && !(TileKey{.x = core::at(items_, next_item).tile_x,
                                                                         .z = core::at(items_, next_item).tile_z} <
                                                                 key_of_decal(core::at(order, next_decal))));
        key = decal_first ? key_of_decal(core::at(order, next_decal))
                          : TileKey{.x = core::at(items_, next_item).tile_x, .z = core::at(items_, next_item).tile_z};
        Tile tile{.low = {INFINITY, INFINITY, INFINITY},
                  .high = {-INFINITY, -INFINITY, -INFINITY},
                  .first_vertex = static_cast<uint32_t>(out_->decal_vertices.size()),
                  .vertex_count = 0,
                  .first_item = static_cast<uint32_t>(out_->items.size()),
                  .item_count = 0};
        while (next_decal < order.size() && key_of_decal(core::at(order, next_decal)) == key) {
            Placed& placed = core::at(placed_, core::at(order, next_decal));
            placed.decal.first_vertex = static_cast<uint32_t>(out_->decal_vertices.size());
            for (const DecalVertex& vertex : placed.vertices) {
                grow(&tile, vertex.position, 0.0F);
                out_->decal_vertices.push_back(vertex);
            }
            tile.vertex_count += static_cast<uint32_t>(placed.vertices.size());
            out_->decals.push_back(placed.decal);
            ++next_decal;
        }
        while (next_item < items_.size() &&
               TileKey{.x = core::at(items_, next_item).tile_x, .z = core::at(items_, next_item).tile_z} == key) {
            const Item& item = core::at(items_, next_item).item;
            grow(&tile, item.root, item.size);
            out_->items.push_back(item);
            ++tile.item_count;
            ++next_item;
        }
        out_->tiles.push_back(tile);
    }
}

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

float clean_density(float density)
{
    if (!std::isfinite(density) || !(density > 0.0F)) {
        return 0.0F;
    }
    return std::clamp(density, kLeastDensity, kMostDensity);
}

} // namespace

Surface surface_of_terrain(const std::string& name)
{
    struct Named {
        const char* name;
        Surface surface;
    };
    static constexpr std::array kNames{
        Named{.name = "TERRAIN_ASPHALT", .surface = Surface::kAsphalt},
        Named{.name = "TERRAIN_CONCSMOOTH", .surface = Surface::kConcrete},
        Named{.name = "TERRAIN_CONCROUGH", .surface = Surface::kConcrete},
        Named{.name = "TERRAIN_WETCONC", .surface = Surface::kConcrete},
        Named{.name = "TERRAIN_DEFAULT", .surface = Surface::kConcrete},
        Named{.name = "TERRAIN_SIDEWALK", .surface = Surface::kPaving},
        Named{.name = "TERRAIN_TILE", .surface = Surface::kPaving},
        Named{.name = "TERRAIN_BRICK", .surface = Surface::kBrick},
        Named{.name = "TERRAIN_ROCK", .surface = Surface::kBrick},
        Named{.name = "TERRAIN_DIRT", .surface = Surface::kDirt},
        Named{.name = "TERRAIN_DIRTPACKED", .surface = Surface::kDirt},
        Named{.name = "TERRAIN_SAND", .surface = Surface::kDirt},
        Named{.name = "TERRAIN_GRAVEL", .surface = Surface::kGravel},
        Named{.name = "TERRAIN_GRASS", .surface = Surface::kGrass},
        Named{.name = "TERRAIN_GRASSDRIED", .surface = Surface::kGrass},
    };
    for (const Named& named : kNames) {
        if (name == named.name) {
            return named.surface;
        }
    }
    return Surface::kOther;
}

Scatter make_scatter(const Input& input)
{
    Scatter out;
    out.serial = ++serials();
    out.seed = input.seed;
    out.decal_density = clean_density(input.decal_density);
    out.clutter_density = clean_density(input.clutter_density);
    out.rails = static_cast<uint32_t>(input.rails.size());
    for (const Face& face : input.faces) {
        ++core::at(out.faces_of, static_cast<std::size_t>(face.surface));
    }
    Maker maker(input, &out);
    if (out.decal_density > 0.0F) {
        maker.ground_decals();
        maker.wall_grime();
    }
    if (out.clutter_density > 0.0F) {
        maker.clutter();
    }
    maker.finish();
    return out;
}

Placement placement_at(std::span<const Face> faces, std::span<const Rail> rails, const Vec3& point)
{
    World world(faces, rails);
    Placement out;
    float ground = 0.0F;
    out.flat = world.ground_at(x_of(point), z_of(point), y_of(point), &ground) &&
               std::abs(ground - y_of(point)) <= kCoplanarDistance;
    out.covered = world.covered(point);
    out.ramp = world.near_ramp(point);
    out.rail = world.near_rail(point);
    const World::Walls walls = world.walls_near(point, kNearWall);
    out.drop = walls.drop;
    out.rise_distance = walls.rise;
    return out;
}

void set_sink(Sink* sink) { installed() = sink; }

Sink* sink() { return installed(); }

} // namespace render::scatter
