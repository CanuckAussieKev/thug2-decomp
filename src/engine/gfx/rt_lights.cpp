// NOT ORIGINAL - the night lighting's light sources: see rt_lights.h.
#include "engine/gfx/rt_lights.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numeric>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"

namespace render::rt {

namespace {

constexpr float kHalf = 0.5F;
constexpr float kPi = 3.14159265F;
constexpr float kMostBrightness = 2.0F; // a cluster's colour's brightest channel at most (the game's 2x)
constexpr float kSmallest = 1e-6F;
constexpr float kLeastColour = 1e-3F; // a cluster's colour darker than this (its brightest channel) gives no light
constexpr uint64_t kCellBits = 21U;   // a cell key's bits per axis
constexpr int64_t kCellBias = int64_t{1} << (kCellBits - 1U);
constexpr uint64_t kCellMask = (uint64_t{1} << kCellBits) - 1U;

// A cluster being gathered.
struct Cluster {
    Vec3 seed{};                // its first triangle's centre
    EmitterKind kind = EmitterKind::kGlow;
    double area = 0.0;
    std::array<double, 3> centre{}; // x area
    std::array<double, 3> colour{}; // x area
    double up = 0.0;                // |n.y| x area
    Vec3 low{};
    Vec3 high{};
    MaterialId material = 0; // its largest triangle's
    double largest = 0.0;
    // (lighting round A2) its shape: the area-weighted second moments of its triangles' points (x area) and its normals'
    // sum (x area, turned to agree with the first)
    std::array<double, 6> moments{}; // xx, yy, zz, xy, xz, yz
    std::array<double, 3> normals{};
    std::vector<std::size_t> members; // its triangles (their indices)
};

// The symmetric 3x3 (xx, yy, zz, xy, xz, yz) taken apart (Jacobi): its eigenvalues, largest first, and their unit
// vectors.
void eigen_of(const std::array<double, 6>& moments, std::array<double, 3>* values, std::array<std::array<double, 3>, 3>* vectors)
{
    std::array<std::array<double, 3>, 3> rows{{{core::at(moments, 0), core::at(moments, 3), core::at(moments, 4)}, {core::at(moments, 3), core::at(moments, 1), core::at(moments, 5)}, {core::at(moments, 4), core::at(moments, 5), core::at(moments, 2)}}};
    std::array<std::array<double, 3>, 3> axes{{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    constexpr int kSweeps = 32; // (Jacobi converges in a handful for a 3x3)
    constexpr double kTiny = 1e-12;
    for (int sweep = 0; sweep < kSweeps; ++sweep) {
        const double off = std::abs(core::at(core::at(rows, 0), 1)) + std::abs(core::at(core::at(rows, 0), 2)) + std::abs(core::at(core::at(rows, 1), 2));
        if (off <= kTiny * (std::abs(core::at(core::at(rows, 0), 0)) + std::abs(core::at(core::at(rows, 1), 1)) + std::abs(core::at(core::at(rows, 2), 2)) + kTiny)) {
            break;
        }
        for (std::size_t p = 0; p < 2; ++p) {
            for (std::size_t q = p + 1; q < 3; ++q) {
                const double apq = core::at(core::at(rows, p), q);
                if (!(std::abs(apq) > 0.0)) {
                    continue;
                }
                const double theta = (core::at(core::at(rows, q), q) - core::at(core::at(rows, p), p)) / (2.0 * apq);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt((theta * theta) + 1.0));
                const double c = 1.0 / std::sqrt((t * t) + 1.0);
                const double sn = t * c;
                for (std::size_t k = 0; k < 3; ++k) {
                    const double akp = core::at(core::at(rows, k), p);
                    const double akq = core::at(core::at(rows, k), q);
                    core::at(core::at(rows, k), p) = (c * akp) - (sn * akq);
                    core::at(core::at(rows, k), q) = (sn * akp) + (c * akq);
                }
                for (std::size_t k = 0; k < 3; ++k) {
                    const double apk = core::at(core::at(rows, p), k);
                    const double aqk = core::at(core::at(rows, q), k);
                    core::at(core::at(rows, p), k) = (c * apk) - (sn * aqk);
                    core::at(core::at(rows, q), k) = (sn * apk) + (c * aqk);
                }
                for (std::size_t k = 0; k < 3; ++k) {
                    const double vkp = core::at(core::at(axes, k), p);
                    const double vkq = core::at(core::at(axes, k), q);
                    core::at(core::at(axes, k), p) = (c * vkp) - (sn * vkq);
                    core::at(core::at(axes, k), q) = (sn * vkp) + (c * vkq);
                }
            }
        }
    }
    std::array<std::size_t, 3> order{0, 1, 2};
    std::ranges::sort(order, [&rows](std::size_t one, std::size_t other) {
        return core::at(core::at(rows, one), one) > core::at(core::at(rows, other), other);
    });
    for (std::size_t k = 0; k < 3; ++k) {
        const std::size_t from = core::at(order, k);
        core::at(*values, k) = std::max(0.0, core::at(core::at(rows, from), from));
        core::at(*vectors, k) = {core::at(core::at(axes, 0), from), core::at(core::at(axes, 1), from),
                                 core::at(core::at(axes, 2), from)};
    }
}

int64_t cell_of(float value) { return static_cast<int64_t>(std::floor(value / kEmitterJoin)); }

uint64_t cell_key(int64_t x, int64_t y, int64_t z)
{
    const auto part = [](int64_t v) { return static_cast<uint64_t>(v + kCellBias) & kCellMask; };
    return part(x) | (part(y) << kCellBits) | (part(z) << (2U * kCellBits));
}

Vec3 min_of(const Vec3& a, const Vec3& b)
{
    return {std::min(x_of(a), x_of(b)), std::min(y_of(a), y_of(b)), std::min(z_of(a), z_of(b))};
}
Vec3 max_of(const Vec3& a, const Vec3& b)
{
    return {std::max(x_of(a), x_of(b)), std::max(y_of(a), y_of(b)), std::max(z_of(a), z_of(b))};
}
float length_of(const Vec3& v) { return std::sqrt(dot(v, v)); }

// The cluster as a light; false: it gives none (dark, or a self-lit patch of floor). `facing` the world's winding (0:
// unknown).
bool emitter_of(const Cluster& cluster, float facing, Emitter* out)
{
    if (!(cluster.area > 0.0)) {
        return false;
    }
    const double inverse = 1.0 / cluster.area;
    Vec3 centre{static_cast<float>(core::at(cluster.centre, 0) * inverse), static_cast<float>(core::at(cluster.centre, 1) * inverse),
                static_cast<float>(core::at(cluster.centre, 2) * inverse)};
    Vec3 colour{static_cast<float>(cluster.colour[0] * inverse), static_cast<float>(cluster.colour[1] * inverse),
                static_cast<float>(cluster.colour[2] * inverse)};
    const float brightest = std::max({x_of(colour), y_of(colour), z_of(colour)});
    if (!(brightest > kLeastColour) || !std::isfinite(brightest)) {
        return false;
    }
    const Vec3 extent = minus(cluster.high, cluster.low);
    const float size = std::max(kEmitterMinSize, length_of(extent) * kHalf);
    const float width = std::max(x_of(extent), z_of(extent));
    const bool flat_up = static_cast<float>(cluster.up * inverse) >= kFlatUpShare &&
                         y_of(extent) <= kFlatHeightShare * std::max(width, kSmallest);
    // (changed for the washed-out streets) the core: the radius of a disc of the cluster's own area - not its box's
    // half diagonal, nor a share of its reach (a quarter of up to 1500 units lit whole streets as evenly as the sun)
    const float core = emitter_core(static_cast<float>(cluster.area));
    // (lighting round A2) a flat self-lit cluster facing down - its triangles' fronts, by the world's winding - is a
    // ceiling's lit panel, a light; facing up (or the winding unknown) a pool of light baked on the floor
    const bool facing_down = facing != 0.0F && core::at(cluster.normals, 1) * static_cast<double>(facing) < 0.0;
    if (flat_up && !(cluster.kind == EmitterKind::kSelfLit && facing_down)) {
        if (cluster.kind == EmitterKind::kSelfLit) {
            return false; // (a baked pool of light on the floor, not a lamp)
        }
        std::get<1>(centre) += core; // (a glow's pool: the light falls from above it - half its brightness at its middle)
    }
    // (lighting round A2) its shape: the spread of its points about the centre along its principal axes - long one way
    // a line, thin across and facing one way a panel, else a point
    EmitterShape shape = EmitterShape::kPoint;
    Vec3 axis{0.0F, 1.0F, 0.0F};
    float half_length = 0.0F;
    float tube_core = core;
    float reach = std::clamp(core * kEmitterReachPerCore, kEmitterMinReach, kEmitterMaxReach);
    {
        const std::array<double, 3> mean{core::at(cluster.centre, 0) * inverse, core::at(cluster.centre, 1) * inverse,
                                         core::at(cluster.centre, 2) * inverse};
        const std::array<double, 6> covariance{
            (core::at(cluster.moments, 0) * inverse) - (core::at(mean, 0) * core::at(mean, 0)), (core::at(cluster.moments, 1) * inverse) - (core::at(mean, 1) * core::at(mean, 1)),
            (core::at(cluster.moments, 2) * inverse) - (core::at(mean, 2) * core::at(mean, 2)), (core::at(cluster.moments, 3) * inverse) - (core::at(mean, 0) * core::at(mean, 1)),
            (core::at(cluster.moments, 4) * inverse) - (core::at(mean, 0) * core::at(mean, 2)), (core::at(cluster.moments, 5) * inverse) - (core::at(mean, 1) * core::at(mean, 2))};
        std::array<double, 3> values{};
        std::array<std::array<double, 3>, 3> vectors{};
        eigen_of(covariance, &values, &vectors);
        const double longest = std::sqrt(core::at(values, 0));
        const double middle = std::sqrt(core::at(values, 1));
        const double thinnest = std::sqrt(core::at(values, 2));
        const double coherence = std::sqrt((core::at(cluster.normals, 0) * core::at(cluster.normals, 0)) +
                                        (core::at(cluster.normals, 1) * core::at(cluster.normals, 1)) +
                                        (core::at(cluster.normals, 2) * core::at(cluster.normals, 2))) *
                              inverse;
        const auto unit = [](const std::array<double, 3>& v) {
            const double length = std::sqrt((core::at(v, 0) * core::at(v, 0)) + (core::at(v, 1) * core::at(v, 1)) + (core::at(v, 2) * core::at(v, 2)));
            return length > 0.0 ? Vec3{static_cast<float>(core::at(v, 0) / length), static_cast<float>(core::at(v, 1) / length),
                                       static_cast<float>(core::at(v, 2) / length)}
                                : Vec3{0.0F, 1.0F, 0.0F};
        };
        constexpr double kLeastSpread = 1e-6;
        if (longest > static_cast<double>(kLineRatio) * std::max(middle, kLeastSpread)) {
            shape = EmitterShape::kLine;
            axis = unit(core::at(vectors, 0));
            half_length = static_cast<float>(longest) * kSpreadToHalfLength;
            // (a tube: its core its half thickness - the area over its length -, its light 1/d near it: reach by its
            // length)
            tube_core = std::max(kEmitterMinCore,
                                 static_cast<float>(cluster.area) / std::max(2.0F * half_length, kSmallest) * kHalf);
            reach = std::clamp(std::max(half_length, tube_core) * kEmitterReachPerCore, kEmitterMinReach,
                               kEmitterMaxReach);
        } else if (thinnest < static_cast<double>(kPanelThin) * std::max(middle, kLeastSpread) &&
                   coherence >= static_cast<double>(kPanelFacing)) {
            shape = EmitterShape::kPanel;
            axis = unit(cluster.normals);
        }
    }
    *out = {.position = centre,
            .size = size,
            .colour = scaled(colour, 1.0F / brightest),
            .intensity = std::min(brightest, kMostBrightness),
            .reach = reach,
            .material = cluster.material,
            .kind = cluster.kind,
            .core = tube_core,
            .shape = shape,
            .axis = axis,
            .half_length = half_length};
    return true;
}

// The six planes (a, b, c, d: a x + b y + c z + d >= 0 inside, normalised) of a row-vector view x projection.
std::array<std::array<float, 4>, 6> frustum_planes(const std::array<std::array<float, 4>, 4>& m)
{
    const auto column = [&m](std::size_t c) {
        return std::array<float, 4>{core::at(core::at(m, 0), c), core::at(core::at(m, 1), c), core::at(core::at(m, 2), c),
                                    core::at(core::at(m, 3), c)};
    };
    const std::array<float, 4> x = column(0);
    const std::array<float, 4> y = column(1);
    const std::array<float, 4> z = column(2);
    const std::array<float, 4> w = column(3);
    const auto add = [](const std::array<float, 4>& a, const std::array<float, 4>& b, float sign) {
        return std::array<float, 4>{a[0] + (sign * b[0]), a[1] + (sign * b[1]), a[2] + (sign * b[2]),
                                    a[3] + (sign * b[3])};
    };
    std::array<std::array<float, 4>, 6> planes{add(w, x, 1.0F), add(w, x, -1.0F), add(w, y, 1.0F),
                                               add(w, y, -1.0F), z,                add(w, z, -1.0F)};
    for (std::array<float, 4>& plane : planes) {
        const float length = std::sqrt((plane[0] * plane[0]) + (plane[1] * plane[1]) + (plane[2] * plane[2]));
        if (length > kSmallest) {
            for (float& value : plane) {
                value /= length;
            }
        }
    }
    return planes;
}

struct EmitterSlot {
    std::mutex mutex;
    std::shared_ptr<const LevelEmitters> emitters = std::make_shared<const LevelEmitters>();
    bool wanted = false;
};
EmitterSlot& emitter_slot()
{
    static EmitterSlot slot;
    return slot;
}

} // namespace

std::vector<Emitter> cluster_emitters(std::span<const EmitterTriangle> triangles, std::vector<uint32_t>* triangle_emitter,
                                      float facing)
{
    std::vector<Cluster> clusters;
    std::unordered_map<uint64_t, std::vector<std::size_t>> cells; // a cell's clusters (by their seed's cell)
    for (std::size_t ti = 0; ti < triangles.size(); ++ti) {
        const EmitterTriangle& triangle = core::at(triangles, ti);
        const auto& [a, b, c] = triangle.corners;
        const Vec3 normal = cross(minus(b, a), minus(c, a));
        const float twice_area = length_of(normal);
        if (!(twice_area > kSmallest) || !std::isfinite(twice_area)) {
            continue;
        }
        const double area = static_cast<double>(twice_area) * 0.5;
        const Vec3 centre = scaled(plus(plus(a, b), c), 1.0F / 3.0F);
        const int64_t cx = cell_of(x_of(centre));
        const int64_t cy = cell_of(y_of(centre));
        const int64_t cz = cell_of(z_of(centre));
        std::size_t joined = clusters.size();
        float nearest = kEmitterJoin;
        for (int64_t dx = -1; dx <= 1; ++dx) {
            for (int64_t dy = -1; dy <= 1; ++dy) {
                for (int64_t dz = -1; dz <= 1; ++dz) {
                    const auto found = cells.find(cell_key(cx + dx, cy + dy, cz + dz));
                    if (found == cells.end()) {
                        continue;
                    }
                    for (const std::size_t index : found->second) {
                        const Cluster& other = core::at(clusters, index);
                        const float apart = length_of(minus(other.seed, centre));
                        if (other.kind == triangle.kind && apart < nearest) {
                            nearest = apart;
                            joined = index;
                        }
                    }
                }
            }
        }
        if (joined == clusters.size()) {
            Cluster made;
            made.seed = centre;
            made.kind = triangle.kind;
            made.low = min_of(min_of(a, b), c);
            made.high = max_of(max_of(a, b), c);
            clusters.push_back(made);
            cells[cell_key(cx, cy, cz)].push_back(joined);
        }
        Cluster& cluster = core::at(clusters, joined);
        cluster.area += area;
        for (std::size_t k = 0; k < 3; ++k) {
            core::at(cluster.centre, k) += static_cast<double>(core::at(centre, k)) * area;
            core::at(cluster.colour, k) += static_cast<double>(core::at(triangle.colour, k)) * area;
        }
        cluster.up += std::abs(static_cast<double>(y_of(normal) / twice_area)) * area;
        // (lighting round A2) the shape's moments: its corners' and centre's (a triangle's points, evenly), its normal
        // turned to agree with the cluster's so far
        constexpr double kQuarter = 0.25;
        for (const Vec3& point : {a, b, c, centre}) {
            const double px = x_of(point);
            const double py = y_of(point);
            const double pz = z_of(point);
            const double each = area * kQuarter;
            core::at(cluster.moments, 0) += px * px * each;
            core::at(cluster.moments, 1) += py * py * each;
            core::at(cluster.moments, 2) += pz * pz * each;
            core::at(cluster.moments, 3) += px * py * each;
            core::at(cluster.moments, 4) += px * pz * each;
            core::at(cluster.moments, 5) += py * pz * each;
        }
        {
            const double unit_scale = area / static_cast<double>(twice_area);
            const std::array<double, 3> n{x_of(normal) * unit_scale, y_of(normal) * unit_scale,
                                          z_of(normal) * unit_scale};
            const double agree = (core::at(n, 0) * core::at(cluster.normals, 0)) + (core::at(n, 1) * core::at(cluster.normals, 1)) + (core::at(n, 2) * core::at(cluster.normals, 2));
            const double sign = agree < 0.0 ? -1.0 : 1.0;
            for (std::size_t k = 0; k < 3; ++k) {
                core::at(cluster.normals, k) += sign * core::at(n, k);
            }
        }
        cluster.members.push_back(ti);
        cluster.low = min_of(cluster.low, min_of(min_of(a, b), c));
        cluster.high = max_of(cluster.high, max_of(max_of(a, b), c));
        if (area > cluster.largest) {
            cluster.largest = area;
            cluster.material = triangle.material;
        }
    }
    std::vector<Emitter> emitters;
    std::vector<std::size_t> from; // each emitter's cluster
    emitters.reserve(clusters.size());
    for (std::size_t ci = 0; ci < clusters.size(); ++ci) {
        Emitter emitter;
        if (emitter_of(core::at(clusters, ci), facing, &emitter)) {
            emitters.push_back(emitter);
            from.push_back(ci);
        }
    }
    std::vector<std::size_t> order(emitters.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&emitters](std::size_t one, std::size_t other) {
        return core::at(emitters, one).intensity * core::at(emitters, one).reach >
               core::at(emitters, other).intensity * core::at(emitters, other).reach;
    });
    if (order.size() > kMaxEmitters) {
        order.resize(kMaxEmitters);
    }
    std::vector<Emitter> sorted;
    sorted.reserve(order.size());
    if (triangle_emitter != nullptr) {
        triangle_emitter->assign(triangles.size(), kNoEmitter);
    }
    for (const std::size_t ei : order) {
        if (triangle_emitter != nullptr) {
            for (const std::size_t ti : core::at(clusters, core::at(from, ei)).members) {
                core::at(*triangle_emitter, ti) = static_cast<uint32_t>(sorted.size());
            }
        }
        sorted.push_back(core::at(emitters, ei));
    }
    return sorted;
}

void set_level_emitters(LevelEmitters emitters)
{
    EmitterSlot& slot = emitter_slot();
    auto made = std::make_shared<const LevelEmitters>(std::move(emitters));
    const std::scoped_lock lock(slot.mutex);
    slot.emitters = std::move(made);
}

std::shared_ptr<const LevelEmitters> level_emitters()
{
    EmitterSlot& slot = emitter_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.emitters;
}


namespace {
struct LampsOwner {
    std::atomic<bool> owner{false};
    std::atomic<bool> automatic{true};
};
LampsOwner& lamps_owner()
{
    static LampsOwner state;
    return state;
}
} // namespace

void set_lamps_owner(bool owner, bool automatic)
{
    lamps_owner().owner.store(owner, std::memory_order_relaxed);
    lamps_owner().automatic.store(automatic, std::memory_order_relaxed);
}

float lamps_drawn()
{
    const LampsOwner& own = lamps_owner();
    if (!own.owner.load(std::memory_order_relaxed)) {
        return 0.0F;
    }
    const DayLight day = day_light();
    if (!own.automatic.load(std::memory_order_relaxed)) {
        return day.on ? std::clamp(day.lamps, 0.0F, 1.0F) : 1.0F;
    }
    const std::shared_ptr<const LevelEmitters> level = level_emitters();
    return lamps_on(day, level != nullptr && level->dark);
}

float lamps_on(const DayLight& day, bool dark_level)
{
    if (day.on) {
        return std::isfinite(day.lamps) ? std::clamp(day.lamps, 0.0F, 1.0F) : 0.0F;
    }
    return dark_level ? 1.0F : 0.0F;
}



float emitter_core(float area)
{
    return std::max(kEmitterMinCore, std::sqrt(std::max(area, 0.0F) / kPi));
}

float point_core(float reach) { return kPointCoreShare * reach; }

float light_falloff(float distance, float core, float reach)
{
    if (!(reach > 0.0F) || !(distance < reach) || !(core > 0.0F)) {
        return 0.0F;
    }
    const float x = distance / reach;
    const float x2 = x * x;
    const float window = 1.0F - (x2 * x2);
    return (window * window) * (core * core) / ((core * core) + (distance * distance));
}

float lamp_geometry(const LampShape& lamp, const std::array<float, 3>& at, const std::array<float, 3>& normal,
                    std::array<float, 3>* toward)
{
    if (!(lamp.reach > 0.0F) || !(lamp.core > 0.0F)) {
        return 0.0F;
    }
    const float core2 = lamp.core * lamp.core;
    const bool line = lamp.shape == EmitterShape::kLine && lamp.half_length > 0.0F;
    Vec3 point = lamp.position;
    float line_factor = 1.0F;
    if (line) {
        // the segment's nearest point; the angles of its ends seen from the line's foot
        const Vec3 rel = minus(at, lamp.position);
        const float along = dot(rel, lamp.axis);
        const Vec3 foot = plus(lamp.position, scaled(lamp.axis, along));
        const Vec3 across = minus(at, foot);
        const float d2 = dot(across, across) + core2;
        const float s_low = -lamp.half_length - along;
        const float s_high = lamp.half_length - along;
        const float spread = (s_high / std::sqrt(d2 + (s_high * s_high))) - (s_low / std::sqrt(d2 + (s_low * s_low)));
        point = plus(lamp.position, scaled(lamp.axis, std::clamp(along, -lamp.half_length, lamp.half_length)));
        line_factor = lamp.core * spread / (2.0F * std::sqrt(d2));
    }
    const Vec3 to = minus(point, at);
    const float distance = std::sqrt(dot(to, to));
    if (!(distance < lamp.reach)) {
        return 0.0F;
    }
    const Vec3 dir = distance > 0.0F ? scaled(to, 1.0F / distance) : Vec3{0.0F, 1.0F, 0.0F};
    const float facing = dot(normal, dir);
    if (!(facing > 0.0F)) {
        return 0.0F;
    }
    const float x = distance / lamp.reach;
    const float window = 1.0F - (x * x * x * x);
    float geometry = facing * window * window;
    if (line) {
        geometry *= line_factor;
    } else {
        geometry *= core2 / (core2 + (distance * distance));
        if (lamp.shape == EmitterShape::kPanel) {
            geometry *= std::abs(dot(lamp.axis, dir));
        }
    }
    if (toward != nullptr) {
        *toward = point;
    }
    return geometry;
}

float light_importance(const Emitter& light, const std::array<float, 3>& camera)
{
    const float distance = length_of(minus(light.position, camera));
    const float outside = std::max(0.0F, distance - light.reach);
    const float reach2 = light.reach * light.reach;
    return light.intensity * reach2 / (reach2 + (outside * outside));
}

bool sphere_in_frustum(const std::array<std::array<float, 4>, 4>& view_projection, const std::array<float, 3>& centre,
                       float radius)
{
    for (const std::array<float, 4>& plane : frustum_planes(view_projection)) {
        const float side = (plane[0] * x_of(centre)) + (plane[1] * y_of(centre)) + (plane[2] * z_of(centre)) + plane[3];
        if (side < -radius) {
            return false;
        }
    }
    return true;
}

std::vector<Emitter> select_view_lights(const FrameRecord& record, const LevelEmitters* level,
                                        const ViewLightRequest& request)
{
    std::vector<std::pair<float, Emitter>> candidates;
    const auto consider = [&](const Emitter& light) {
        if (!(light.intensity > 0.0F) || !(light.reach > 0.0F) ||
            !sphere_in_frustum(request.view_projection, light.position, light.reach)) {
            return;
        }
        candidates.emplace_back(light_importance(light, request.camera), light);
    };
    if (request.points) {
        for (const Light& light : record.lights) {
            if (light.kind != Light::Kind::kPoint || !(light.radius > 0.0F)) {
                continue;
            }
            const float brightest = std::max({std::get<0>(light.colour), std::get<1>(light.colour),
                                              std::get<2>(light.colour)});
            if (!(brightest > kLeastColour) || !std::isfinite(brightest)) {
                continue;
            }
            consider({.position = light.position,
                      .size = 0.0F,
                      .colour = scaled(light.colour, 1.0F / brightest),
                      .intensity = brightest * request.point_scale,
                      .reach = light.radius,
                      .material = 0,
                      .kind = EmitterKind::kPoint,
                      .core = point_core(light.radius)});
        }
    }
    if (request.emitters && level != nullptr) {
        for (Emitter light : level->emitters) {
            light.intensity *= request.emitter_scale;
            consider(light);
        }
    }
    const std::size_t keep = std::min({request.max, kMaxViewLights, candidates.size()});
    std::ranges::stable_sort(candidates, [](const auto& one, const auto& other) { return one.first > other.first; });
    std::vector<Emitter> lights;
    lights.reserve(keep);
    for (std::size_t i = 0; i < keep; ++i) {
        lights.push_back(core::at(candidates, i).second);
    }
    return lights;
}

} // namespace render::rt
