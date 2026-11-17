// NOT ORIGINAL - the level's water bodies for the physical waves (water_bodies.h).
#include "render_pc/water_bodies.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <ratio>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/air_volume.h"
#include "engine/gfx/snow.h"
#include "engine/gfx/water_surfaces.h"
#include "engine/gfx/water_waves.h"
#include "engine/gfx/wind.h"
#include "formats/scene_format.h"
#include "platform/run_log.h"
#include "render_pc/snow_world.h"
#include "render_pc/water_materials.h"

namespace render::water_bodies {

namespace {

constexpr uint32_t kPositionMask = 0x400E; // (Direct3D's position bits)
constexpr uint32_t kPositionXyz = 0x002;   // an untransformed, unskinned position: three floats first
constexpr std::size_t kLoggedBodies = 48;

struct Entry {
    std::vector<water::Triangle> triangles;
    water::WaterBody body;
    bool instanced = false; // drawn as an instance's: not measured, still
};

struct State {
    std::mutex mutex;
    std::unordered_map<const IndexBufferData*, Entry> meshes;
    bool dirty = false;  // a mesh noted or forgotten since the last measure
    double wind = -1.0;  // the weather's wind the waves were worked out for
    uint64_t air = 0;    // (round W) the air volume's serial the bodies' shelter was taken from (0: none)
    uint32_t version = 0;
};

State& state()
{
    static State instance;
    return instance;
}

// Each body's waves for the weather's wind now.
void work_out_waves(State& own, double wind)
{
    own.wind = wind;
    ++own.version;
    for (auto& [drawn, entry] : own.meshes) {
        water::WaterBody& body = entry.body;
        if (!body.known) {
            continue;
        }
        body.version = own.version;
        body.wind = static_cast<float>(wind * static_cast<double>(body.shelter));
        const water::WindSea sea = water::wind_sea(body.wind, body.fetch, body.depth);
        body.waves = water::sea_waves(sea, body.fetch, body.depth);
    }
}

// (round W) Each measured mesh's shelter and sky from the level's air transport (air_volume.h) over its own
// triangles; none yet (the volume being built, or none for the level): still and dry - nothing is moved by an air the
// level hasn't been seen to let in.
void take_air(State& own)
{
    const std::shared_ptr<const air::Volume> volume = air::volume();
    own.air = volume != nullptr ? volume->serial() : 0;
    for (auto& [drawn, entry] : own.meshes) {
        water::WaterBody& body = entry.body;
        if (!body.known) {
            continue;
        }
        water::AirOver over{.shelter = 0.0, .sky = 0.0};
        if (volume != nullptr) {
            over = water::air_over({.triangles = entry.triangles}, *volume);
        }
        body.shelter = static_cast<float>(over.shelter);
        body.sky = static_cast<float>(over.sky);
    }
}

// Every water mesh measured together against the level's collision.
void measure(State& own)
{
    own.dirty = false;
    const auto started = std::chrono::steady_clock::now();
    std::vector<std::pair<const IndexBufferData*, Entry*>> order;
    order.reserve(own.meshes.size());
    for (auto& [drawn, entry] : own.meshes) {
        if (entry.instanced) {
            entry.body = {};
            continue;
        }
        order.emplace_back(drawn, &entry);
    }
    // (the log's order: the largest first - the area its triangles cover on the ground)
    const auto area = [](const Entry& entry) {
        double sum = 0.0;
        for (const water::Triangle& t : entry.triangles) {
            const water::Corner& a = core::at(t, 0);
            const water::Corner& b = core::at(t, 1);
            const water::Corner& c = core::at(t, 2);
            sum += std::abs(((core::at(b, 0) - core::at(a, 0)) * (core::at(c, 2) - core::at(a, 2))) -
                            ((core::at(c, 0) - core::at(a, 0)) * (core::at(b, 2) - core::at(a, 2))));
        }
        return sum;
    };
    std::vector<double> areas;
    for (const auto& [drawn, entry] : order) {
        areas.push_back(area(*entry));
    }
    std::vector<std::size_t> ranked(order.size());
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        core::at(ranked, i) = i;
    }
    std::ranges::sort(ranked, [&](std::size_t one, std::size_t other) {
        const double a = core::at(areas, one);
        const double b = core::at(areas, other);
        return a != b ? a > b : std::less<const IndexBufferData*>()(core::at(order, one).first, core::at(order, other).first);
    });
    std::vector<std::pair<const IndexBufferData*, Entry*>> sorted;
    sorted.reserve(order.size());
    for (const std::size_t i : ranked) {
        sorted.push_back(core::at(order, i));
    }
    order = std::move(sorted);
    std::vector<water::BodyMesh> meshes;
    meshes.reserve(order.size());
    for (const auto& [drawn, entry] : order) {
        meshes.push_back({.triangles = entry->triangles});
    }
    bool has_sky = false;
    const std::vector<snow::Face> faces = snow_world::collision_faces(&has_sky);
    std::vector<water::Triangle> collision;
    collision.reserve(faces.size());
    for (const snow::Face& face : faces) {
        collision.push_back(face.corners);
    }
    water::BodiesReport report;
    const std::vector<water::BodyMeasure> measures = water::measure_bodies(meshes, collision, wind::kDirection, &report);
    uint32_t measured = 0;
    std::vector<std::pair<uint32_t, uint32_t>> cells(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        const water::BodyMeasure& got = core::at(measures, i);
        core::at(cells, i) = {got.cells, got.surface_cells};
        water::WaterBody& body = core::at(order, i).second->body;
        body = {};
        if (!got.measured) {
            continue;
        }
        ++measured;
        body.known = true;
        body.id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(core::at(order, i).first));
        body.fetch = static_cast<float>(got.fetch);
        body.depth = static_cast<float>(got.depth);
    }
    take_air(own);
    work_out_waves(own, wind::speed());
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    get_log("rt_water: %u of %zu water meshes measured against %u collision faces (%s; %ux%u cells of %.1f units, %u of water) "
            "in %.0f ms - the wind %.1f m/s",
            measured, order.size(), report.collision_faces, has_sky ? "a sky" : "no sky", report.columns, report.rows,
            static_cast<double>(report.cell), report.water_cells, ms, own.wind);
    for (std::size_t i = 0; i < order.size() && i < kLoggedBodies; ++i) {
        const water::WaterBody& body = core::at(order, i).second->body;
        const water::Triangle& first = core::at(core::at(order, i).second->triangles, 0);
        const water::Corner& at = core::at(first, 0);
        if (!body.known) {
            get_log("rt_water: mesh %zu near (%.0f, %.0f, %.0f), %zu triangles: not measured (%u cells under it facing up) - still", i,
                    static_cast<double>(core::at(at, 0)), static_cast<double>(core::at(at, 1)), static_cast<double>(core::at(at, 2)),
                    core::at(order, i).second->triangles.size(), core::at(cells, i).first);
            continue;
        }
        get_log("rt_water: mesh %zu near (%.0f, %.0f, %.0f), %zu triangles, body %u (%u cells): fetch %s%.1f m, depth %.2f m, the "
                "air's share %.2f - wind over it %.1f m/s: waves %.3f m high, %.2f m long (ripples x %.2f, whitecaps %.2f)",
                i, static_cast<double>(core::at(at, 0)), static_cast<double>(core::at(at, 1)), static_cast<double>(core::at(at, 2)),
                core::at(order, i).second->triangles.size(), core::at(measures, i).body, core::at(cells, i).second,
                body.fetch >= static_cast<float>(water::kOpenFetch) ? "open, " : "",
                body.fetch >= static_cast<float>(water::kOpenFetch) ? 0.0 : static_cast<double>(body.fetch),
                static_cast<double>(body.depth), static_cast<double>(body.shelter), static_cast<double>(body.wind),
                static_cast<double>(body.waves.height) / water::kUnitsPerMetre,
                static_cast<double>(body.waves.wavelength) / water::kUnitsPerMetre, static_cast<double>(body.waves.ripples),
                static_cast<double>(body.waves.whitecaps));
    }
}

} // namespace

std::vector<water::Triangle> triangles_of(const scene_format::MeshBody& body, uint32_t fvf, const MaterialRef* material, bool dynamic)
{
    std::vector<water::Triangle> out;
    if (!water::wanted() || material == nullptr || dynamic || !body.scaled.empty() || (fvf & kPositionMask) != kPositionXyz ||
        body.stride < sizeof(water::Corner) || core::at(body.index_counts, 0) < 3 || !water_materials::is_water(material)) {
        return out;
    }
    const std::size_t stride = body.stride;
    const std::size_t count = std::min<std::size_t>(body.vertex_count, body.vertices.size() / stride);
    const std::vector<uint16_t>& strip = core::at(body.indices, 0);
    const std::size_t used = std::min<std::size_t>(core::at(body.index_counts, 0), strip.size());
    for (std::size_t i = 2; i < used; ++i) {
        const std::array<uint16_t, 3> corners{core::at(strip, i - 2), core::at(strip, i - 1), core::at(strip, i)};
        if (corners[0] == corners[1] || corners[1] == corners[2] || corners[0] == corners[2]) {
            continue; // (the strip's degenerate joins)
        }
        water::Triangle triangle{};
        bool inside = true;
        for (std::size_t c = 0; c < corners.size() && inside; ++c) {
            const std::size_t index = core::at(corners, c);
            inside = index < count;
            if (inside) {
                std::memcpy(core::at(triangle, c).data(), body.vertices.data() + (index * stride), sizeof(water::Corner));
            }
        }
        if (inside) {
            out.push_back(triangle);
        }
    }
    return out;
}

void remember(const IndexBufferData* drawn, std::vector<water::Triangle> triangles)
{
    if (drawn == nullptr || triangles.empty()) {
        return;
    }
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    own.meshes[drawn] = Entry{.triangles = std::move(triangles), .body = {}};
    own.dirty = true;
}

void copy(const IndexBufferData* from, const IndexBufferData* to)
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    const auto found = own.meshes.find(from);
    if (to != nullptr && found != own.meshes.end()) {
        own.meshes[to] = found->second;
    }
}

void forget(const IndexBufferData* drawn)
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    if (own.meshes.erase(drawn) != 0U) {
        own.dirty = true;
    }
}

void before_draw(const IndexBufferData* first_level, bool instanced)
{
    if (!water::wanted() || water::current_texture() == nullptr) {
        return;
    }
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    const auto found = own.meshes.find(first_level);
    if (found == own.meshes.end()) {
        water::set_current_body({});
        return;
    }
    if (instanced && !found->second.instanced) {
        found->second.instanced = true;
        own.dirty = true;
    }
    if (own.dirty) {
        measure(own);
    }
    const double wind = wind::speed();
    const std::shared_ptr<const air::Volume> volume = air::volume();
    if (const uint64_t air = volume != nullptr ? volume->serial() : 0; air != own.air) {
        take_air(own); // (round W: the level's air came - or went)
        work_out_waves(own, wind);
        get_log("rt_water: the bodies' wind from the level's air transport (%s)", air != 0 ? "made" : "none - still");
        std::size_t logged = 0;
        for (const auto& [drawn, entry] : own.meshes) {
            if (!entry.body.known || entry.triangles.empty() || logged++ >= kLoggedBodies) {
                continue;
            }
            const water::Corner& at = core::at(core::at(entry.triangles, 0), 0);
            get_log("rt_water: the body near (%.0f, %.0f, %.0f): the wind's share %.2f, the rain's %.2f - wind over it %.1f m/s, "
                    "waves %.3f m high",
                    static_cast<double>(core::at(at, 0)), static_cast<double>(core::at(at, 1)), static_cast<double>(core::at(at, 2)),
                    static_cast<double>(entry.body.shelter), static_cast<double>(entry.body.sky), static_cast<double>(entry.body.wind),
                    static_cast<double>(entry.body.waves.height) / water::kUnitsPerMetre);
        }
    } else if (wind != own.wind) {
        work_out_waves(own, wind);
    }
    water::set_current_body(found->second.body);
}

} // namespace render::water_bodies
