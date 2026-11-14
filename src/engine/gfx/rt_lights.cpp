// NOT ORIGINAL - the night lighting's light sources: see rt_lights.h.
#include "engine/gfx/rt_lights.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
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
};

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

// The cluster as a light; false: it gives none (dark, or a self-lit patch of floor).
bool emitter_of(const Cluster& cluster, Emitter* out)
{
    if (!(cluster.area > 0.0)) {
        return false;
    }
    const double inverse = 1.0 / cluster.area;
    Vec3 centre{static_cast<float>(cluster.centre[0] * inverse), static_cast<float>(cluster.centre[1] * inverse),
                static_cast<float>(cluster.centre[2] * inverse)};
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
    if (flat_up) {
        if (cluster.kind == EmitterKind::kSelfLit) {
            return false; // (a baked pool of light on the floor, not a lamp)
        }
        std::get<1>(centre) += core; // (a glow's pool: the light falls from above it - half its brightness at its middle)
    }
    *out = {.position = centre,
            .size = size,
            .colour = scaled(colour, 1.0F / brightest),
            .intensity = std::min(brightest, kMostBrightness),
            .reach = std::clamp(core * kEmitterReachPerCore, kEmitterMinReach, kEmitterMaxReach),
            .material = cluster.material,
            .kind = cluster.kind,
            .core = core};
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

std::vector<Emitter> cluster_emitters(std::span<const EmitterTriangle> triangles)
{
    std::vector<Cluster> clusters;
    std::unordered_map<uint64_t, std::vector<std::size_t>> cells; // a cell's clusters (by their seed's cell)
    for (const EmitterTriangle& triangle : triangles) {
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
        cluster.low = min_of(cluster.low, min_of(min_of(a, b), c));
        cluster.high = max_of(cluster.high, max_of(max_of(a, b), c));
        if (area > cluster.largest) {
            cluster.largest = area;
            cluster.material = triangle.material;
        }
    }
    std::vector<Emitter> emitters;
    emitters.reserve(clusters.size());
    for (const Cluster& cluster : clusters) {
        Emitter emitter;
        if (emitter_of(cluster, &emitter)) {
            emitters.push_back(emitter);
        }
    }
    std::ranges::stable_sort(emitters, [](const Emitter& one, const Emitter& other) {
        return one.intensity * one.reach > other.intensity * other.reach;
    });
    if (emitters.size() > kMaxEmitters) {
        emitters.resize(kMaxEmitters);
    }
    return emitters;
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

void set_emitters_wanted(bool wanted)
{
    EmitterSlot& slot = emitter_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.wanted = wanted;
}

bool emitters_wanted()
{
    EmitterSlot& slot = emitter_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.wanted;
}

float emitter_core(float area)
{
    return std::max(kEmitterMinCore, std::sqrt(std::max(area, 0.0F) / kPi));
}

float point_core(float reach) { return kPointCoreShare * reach; }

float limited_light(float luminance)
{
    if (!(luminance > kLightKnee)) {
        return luminance;
    }
    const float over = luminance - kLightKnee;
    const float room = kLightMost - kLightKnee;
    return kLightKnee + (room * over / (room + over));
}

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
