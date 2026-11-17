// NOT ORIGINAL - round W: the level's air transport made from the loaded world (air_world.h).
#include "render_pc/air_world.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "engine/gfx/air_volume.h"
#include "engine/gfx/rt_material_table.h"
#include "engine/gfx/snow.h"
#include "engine/sound/surface_kind.h"
#include "platform/run_log.h"
#include "render_pc/snow_world.h"

namespace render::air_world {

namespace {

constexpr int kTerrainCount = 256;
// The terrains whose faces let air through or are glass, and their index in the shipped terrain table (when the game's
// table isn't loaded - render_pc/scatter_world.cpp's kShippedTerrains order).
struct Known {
    std::string_view name;
    int shipped;
};
constexpr std::array<Known, 3> kKnown{
    {{"TERRAIN_METALGRATING", 6}, {"TERRAIN_PLEXIGLASS", 28}, {"TERRAIN_CHAINLINK", 32}}};

struct Build {
    uint64_t sectors = 0;
    std::future<std::shared_ptr<const air::Volume>> volume;
    std::chrono::steady_clock::time_point started;
    std::size_t faces = 0;
    air::Report report;
};

struct State {
    uint64_t sectors = 0;
    bool keyed = false;
    std::unique_ptr<Build> building;
    std::vector<std::unique_ptr<Build>> outdated; // (builds overtaken by another level: kept until they finish)
};

State& state()
{
    static State instance;
    return instance;
}

// Each terrain index's share open to air.
std::array<float, kTerrainCount> terrain_air()
{
    std::array<float, kTerrainCount> air{};
    bool from_table = false;
    for (int index = 0; index < kTerrainCount; ++index) {
        const uint32_t key = surface_kind::fetch_surface_key(static_cast<uint8_t>(index));
        if (key == 0) {
            continue;
        }
        from_table = true;
        for (const Known& known : kKnown) {
            if (key == checksums::hash_from_text(std::string(known.name).c_str())) {
                core::at(air, index) = rt::material_values(rt::kind_of_terrain(known.name)).air_transmission;
            }
        }
    }
    if (!from_table) {
        for (const Known& known : kKnown) {
            core::at(air, known.shipped) = rt::material_values(rt::kind_of_terrain(known.name)).air_transmission;
        }
    }
    return air;
}

void start(State& own)
{
    bool has_sky = false;
    const std::vector<snow::Face> collision = snow_world::collision_faces(&has_sky);
    const std::array<float, kTerrainCount> shares = terrain_air();
    std::vector<air::Face> faces;
    faces.reserve(collision.size());
    for (const snow::Face& face : collision) {
        faces.push_back({.corners = face.corners, .air = core::at(shares, face.terrain)});
    }
    if (own.building != nullptr) {
        own.outdated.push_back(std::move(own.building));
    }
    air::set_volume(nullptr);
    auto build = std::make_unique<Build>();
    build->sectors = own.sectors;
    build->faces = faces.size();
    build->started = std::chrono::steady_clock::now();
    Build* raw = build.get();
    build->volume = std::async(std::launch::async, [raw, faces = std::move(faces)]() {
        return std::shared_ptr<const air::Volume>(
            std::make_shared<air::Volume>(air::Volume::build(faces, &raw->report)));
    });
    own.building = std::move(build);
}

bool ready(const Build& build)
{
    return build.volume.valid() && build.volume.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

} // namespace

void step(uint64_t sectors)
{
    State& own = state();
    if (!own.keyed || sectors != own.sectors) {
        own.keyed = true;
        own.sectors = sectors;
        start(own);
    }
    std::erase_if(own.outdated, [](const std::unique_ptr<Build>& build) { return ready(*build); });
    if (own.building == nullptr || !ready(*own.building)) {
        return;
    }
    std::shared_ptr<const air::Volume> volume = own.building->volume.get();
    const air::Report& r = own.building->report;
    const double waited =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - own.building->started).count();
    get_log("air: the level's air transport from %zu faces (%zu screens - fences, gratings): %dx%dx%d bricks of %.0f "
            "units, %zu "
            "kept by cell; their cells %zu in the open air, %zu reached from it, %zu still (closed off), %zu walls; "
            "built in "
            "%.0f ms on a worker (ready after %.0f ms), %.1f MB",
            own.building->faces, r.screens, r.bricks[0], r.bricks[1], r.bricks[2],
            static_cast<double>(air::kCell * static_cast<float>(air::kBrickCells)), r.mixed, r.free_cells, r.sheltered,
            r.stilled, r.walls, r.milliseconds, waited, static_cast<double>(r.bytes) / 1048576.0);
    air::set_volume(volume->empty() ? nullptr : std::move(volume));
    own.building.reset();
}

} // namespace render::air_world
