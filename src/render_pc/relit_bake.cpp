// NOT ORIGINAL - the relit bake: see relit_bake.h.
#include "render_pc/relit_bake.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/config/settings_model.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/game_hooks.h"
#include "engine/gfx/lamp_fixtures.h"
#include "engine/gfx/relit_data.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "engine/gfx/rt_sun.h"
#include "engine/gfx/tod_manager.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/platform_config.h"
#include "platform/process_flags.h"
#include "platform/run_log.h"
#include "platform/test_workspace.h"

namespace render::relit_bake {

namespace {

constexpr float kLumaRed = 0.299F;
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;
constexpr std::size_t kKilobyte = 1024;
constexpr float kLeastColour = 1e-4F;
// (bake) The sun's candidates are told apart on a sample of about this many vertices (a level of up to this size is its
// own sample): enough edges that the fits' objectives are the whole level's to well within the candidates' differences.
constexpr std::size_t kCandidateVertices = 500000;

float luma(const rt::Rgbf& rgb)
{
    return (kLumaRed * std::get<0>(rgb)) + (kLumaGreen * std::get<1>(rgb)) + (kLumaBlue * std::get<2>(rgb));
}

struct State {
    uint64_t key = 0;     // the scenes' key the world has been ready for ...
    uint32_t steady = 0;  // ... this many frames in a row
    bool done = false;
    bool waiting = false;                        // the wait for the level begun ...
    std::chrono::steady_clock::time_point since; // ... then
    bool level_seen = false;                     // the level asked for running (logged once) ...
    uint32_t named_scene = 0;                    // ... and the scene its level structure names (0: none)
};

State& state()
{
    static core::NoDestroy<State> made;
    return made.get();
}

// The load script of the level asked for: Load_<name> (the name may have its Load_ already - as -level takes it,
// game/front_end/start_level.cpp read_in).
std::string requested_load_name()
{
    std::string name = level_name();
    constexpr std::size_t kPrefix = 5; // "load_"
    if (name.size() < kPrefix || _strnicmp(name.c_str(), "load_", kPrefix) != 0) {
        name = "Load_" + name;
    }
    return name;
}
uint32_t requested_load() { return checksums::hash_from_text(requested_load_name().c_str()); }

// The load script running now (the skate module's level: load_mainmenu in the front end), 0 without one.
uint32_t running_load()
{
    const auto level_script = render::game_hooks().level_script;
    return level_script != nullptr ? level_script() : 0;
}

std::string load_name(uint32_t load)
{
    if (load == requested_load()) {
        return requested_load_name();
    }
    const char* name = script::look_up_hash_name(load);
    return name != nullptr ? std::string(name) : text::format("%08x", load);
}

// The scene the level whose load script is `load` names: its level structure's level= (levels.qb's Level_<x>, and the
// ported levels' that make_menu_level adds the same way) - the scene its world is (rt_relit_world::level_scene). 0 when
// no level structure names one.
uint32_t named_scene(uint32_t load)
{
    static const uint32_t kLoadScript = checksums::hash_from_text("load_script");
    static const uint32_t kLevel = checksums::hash_from_text("level");
    for (script::SymbolTableEntry* entry = script::get_next_symbol(nullptr); entry != nullptr;
         entry = script::get_next_symbol(entry)) {
        script::Struct* level = entry->type() == script::QbValueKind::kStruct ? entry->value().structure() : nullptr;
        uint32_t its_load = 0;
        if (level == nullptr || !level->read_hash_value(kLoadScript, &its_load, 0) || its_load != load) {
            continue;
        }
        const char* scene = nullptr;
        if (level->fetch_text(kLevel, &scene, 0) && scene != nullptr && *scene != '\0') {
            return checksums::hash_from_text(scene);
        }
    }
    return 0;
}

// The bake given up: nothing written, the game closed with `code` (platform::kBatch...).
void give_up(State& st, int code)
{
    st.done = true;
    platform::end_batch_run(code);
}

// The level's lamps as the lamps pass shines them: the emitters found in the level (glows; self-lit and textured
// candidates - lamps only where the bake shows their light) and the point lights on now.
std::vector<rt::RelitLamp> level_lamps()
{
    std::vector<rt::RelitLamp> lamps;
    if (const std::shared_ptr<const rt::LevelEmitters> found = rt::level_emitters(); found != nullptr) {
        for (const rt::Emitter& emitter : found->emitters) {
            lamps.push_back({.position = emitter.position,
                             .size = emitter.size,
                             .light = rt::scaled(emitter.colour, emitter.intensity),
                             .core = emitter.core,
                             .reach = emitter.reach,
                             .shape = emitter.shape,
                             .axis = emitter.axis,
                             .half_length = emitter.half_length,
                             .kind = emitter.kind});
        }
    }
    for (const rt::Light& light : rt::frame_record().lights) {
        if (light.kind != rt::Light::Kind::kPoint || !(light.radius > 0.0F) || !(luma(light.colour) > kLeastColour)) {
            continue;
        }
        lamps.push_back({.position = light.position,
                         .size = 0.0F,
                         .light = light.colour,
                         .core = rt::point_core(light.radius),
                         .reach = light.radius,
                         .kind = rt::EmitterKind::kPoint});
    }
    return lamps;
}

// (lighting round A2) The level's own time's light (its own day from its baked sun and its time-of-day state at load -
// the same day the clock makes: rt_daylight.h fit_native_day), in the global unit.
// The level's own time-of-day state at load (the original's).
rt::TodState own_state()
{
    rt::TodState state = rt::TodState::kAfternoon;
    if (const gfx::CtodManager* manager = gfx::tod_manager(); manager != nullptr) {
        const uint32_t own = manager->current_state();
        state = own == gfx::kMorning ? rt::TodState::kMorning
                : own == gfx::kEvening ? rt::TodState::kEvening
                : own == gfx::kNight   ? rt::TodState::kNight
                                       : rt::TodState::kAfternoon;
    }
    return state;
}

rt::DayLight own_day_light(const rt::BakedSun& sun)
{
    const rt::NativeDay native = rt::fit_native_day(
        {.sun = sun.toward, .have_sun = sun.found, .from_bake = sun.found, .state = own_state()});
    return rt::day_light_at(native, native.hours);
}

// (lighting round A3) How much each lamp is on at the level's own time: by its own switch against the level's own open
// sky (rt_lights.h lamp_on - the photocell's rule, no "dark level").
float light_luma(const rt::Rgbf& rgb)
{
    return (0.2126F * std::get<0>(rgb)) + (0.7152F * std::get<1>(rgb)) + (0.0722F * std::get<2>(rgb));
}
std::vector<float> own_lamps_on(std::span<const rt::RelitLamp> lamps, const rt::DayLight& own_day)
{
    std::vector<float> on;
    on.reserve(lamps.size());
    for (const rt::RelitLamp& lamp : lamps) {
        on.push_back(rt::lamp_on(lamp.switch_sky, light_luma(own_day.sky_light), own_day.lamps));
    }
    return on;
}

// (lighting round A3) The vertices of the lamps' own surfaces: the corners of a confirmed candidate's triangles (a
// self-lit or textured emitter the bake told lit, or one its fixture's model lights). (round relit-misses) Each
// triangle's own vertices in its own mesh (rt_lights.h EmitterTriangle::mesh, vertices - the bake's mesh of that
// content, `mesh_of`): before, every vertex of the level at a corner's position was taken - a neighbouring face's, a
// tree's cards laid over a candidate's -, and those surfaces glowed with the lamp's radiance at night (College's stand
// and the tree cards round it, at full brightness on a night level).
std::vector<std::vector<rt::RelitEmitting>> emitting_vertices(std::span<const rt::RelitMesh> meshes,
                                                              std::span<const rt::RelitLamp> lamps,
                                                              const std::unordered_map<rt::MeshId, std::size_t>& mesh_of)
{
    std::vector<std::vector<rt::RelitEmitting>> out(meshes.size());
    const std::shared_ptr<const rt::LevelEmitters> found = rt::level_emitters();
    if (found == nullptr) {
        return out;
    }
    std::vector<std::unordered_map<uint32_t, uint32_t>> marked(meshes.size()); // a mesh's vertex -> its lamp
    for (std::size_t ti = 0; ti < found->triangles.size() && ti < found->triangle_emitter.size(); ++ti) {
        const uint32_t lamp = core::at(found->triangle_emitter, ti);
        const rt::EmitterTriangle& triangle = core::at(found->triangles, ti);
        // (round L: a candidate with light - solved or its fixture's model)
        if (lamp >= lamps.size() || !rt::candidate_kind(triangle.kind) || !(luma(core::at(lamps, lamp).light) > 0.0F)) {
            continue;
        }
        const auto owner = mesh_of.find(triangle.mesh);
        if (owner == mesh_of.end() || owner->second >= meshes.size()) {
            continue;
        }
        const std::size_t vertex_count = core::at(meshes, owner->second).positions.size();
        for (const uint32_t vertex : triangle.vertices) {
            if (vertex < vertex_count) {
                core::at(marked, owner->second).emplace(vertex, lamp);
            }
        }
    }
    // (round indoor lamps) a lamp whose own surface isn't drawn - a light card's (hidden in the relit level), the level's
    // point light, a lamp found from its pool - takes its head on the fixture geometry at it: the world's triangles
    // (not a light card's) wholly within its luminous radius of it - the disc of its luminous area A = flux / (pi L) at
    // its luminance -, past its size (a fitting's depth under its mount, a card's own extent), each no larger than one
    // fixture's luminous area (a lantern's, a fitting's faces - not the wall or ceiling it hangs on). None there: no head
    // (a glowing card is never one).
    std::vector<uint8_t> has_head(lamps.size(), 0);
    for (const auto& mesh_marks : marked) {
        for (const auto& [vertex, lamp] : mesh_marks) {
            core::at(has_head, lamp) = 1;
        }
    }
    std::unordered_set<rt::MeshId> cards;
    for (const rt::EmitterTriangle& triangle : found->triangles) {
        if (triangle.kind == rt::EmitterKind::kGlow) {
            cards.insert(triangle.mesh);
        }
    }
    constexpr double kUnitsPerMetre = 39.37;
    constexpr double kPiD = 3.14159265358979323846;
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        const rt::RelitLamp& lamp = core::at(lamps, li);
        // (a glow's card is drawn round its fixture, a pool's lamp hangs from its mount: the geometry at them is theirs;
        // a level's point light is placed by the artists in the air, no fixture's)
        if (core::at(has_head, li) != 0 || (lamp.kind != rt::EmitterKind::kGlow && lamp.kind != rt::EmitterKind::kPool) ||
            !(luma(lamp.light) > 0.0F) || !(lamp.lumens > 0.0F) || !(lamp.luminance > 0.0F)) {
            continue;
        }
        const rt::FixtureValues& values = rt::fixture_values(static_cast<rt::LampFixture>(lamp.fixture));
        const double area = static_cast<double>(lamp.lumens) / (kPiD * static_cast<double>(lamp.luminance)); // (m^2)
        const float radius = static_cast<float>(std::sqrt(area / kPiD) * kUnitsPerMetre) + lamp.size;
        const double most_area = static_cast<double>(std::max(values.most_area, static_cast<float>(area))) *
                                 kUnitsPerMetre * kUnitsPerMetre;
        for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
            const rt::RelitMesh& mesh = core::at(meshes, mi);
            if (cards.contains(mesh.id)) {
                continue;
            }
            // (an emitting vertex shows its lamp on every triangle it is a corner of: only vertices whose every triangle
            // is one of the head's - its light never spreads onto the floor or wall a small face touches)
            std::vector<std::array<uint32_t, 3>> head;
            for (const auto& [one, two, three] : mesh.triangles) {
                if (one >= mesh.positions.size() || two >= mesh.positions.size() || three >= mesh.positions.size()) {
                    continue;
                }
                const rt::Vec3& a = core::at(mesh.positions, one);
                const rt::Vec3& b = core::at(mesh.positions, two);
                const rt::Vec3& c = core::at(mesh.positions, three);
                const auto within = [&](const rt::Vec3& at) {
                    const rt::Vec3 apart = rt::minus(at, lamp.position);
                    return rt::dot(apart, apart) <= radius * radius;
                };
                if (!within(a) || !within(b) || !within(c)) {
                    continue;
                }
                const rt::Vec3 n = rt::cross(rt::minus(b, a), rt::minus(c, a));
                const float twice = std::sqrt(rt::dot(n, n));
                if (0.5 * static_cast<double>(twice) > most_area || !(twice > 0.0F)) {
                    continue;
                }
                // (a panel's luminous face lies in its plane - a fitting's diffuser, not its housing's sides)
                if (lamp.shape == rt::EmitterShape::kPanel && std::abs(rt::dot(n, lamp.axis)) < rt::kPanelFacing * twice) {
                    continue;
                }
                head.push_back({one, two, three});
            }
            if (head.empty()) {
                continue;
            }
            std::unordered_set<uint32_t> in_head;
            for (const auto& corner : head) {
                in_head.insert(corner.begin(), corner.end());
            }
            std::unordered_set<uint32_t> spreads; // (a head vertex that is a corner of a triangle outside the head)
            std::unordered_set<uint64_t> head_keys;
            const auto key_of = [](uint32_t x, uint32_t y, uint32_t z) {
                std::array<uint32_t, 3> sorted{x, y, z};
                std::ranges::sort(sorted);
                return (static_cast<uint64_t>(sorted[0]) * 0x9E3779B97F4A7C15ULL) ^
                       (static_cast<uint64_t>(sorted[1]) << 21U) ^ static_cast<uint64_t>(sorted[2]);
            };
            for (const auto& [x, y, z] : head) {
                head_keys.insert(key_of(x, y, z));
            }
            for (const auto& [x, y, z] : mesh.triangles) {
                if ((in_head.contains(x) || in_head.contains(y) || in_head.contains(z)) &&
                    !head_keys.contains(key_of(x, y, z))) {
                    spreads.insert(x);
                    spreads.insert(y);
                    spreads.insert(z);
                }
            }
            for (const uint32_t vertex : in_head) {
                if (!spreads.contains(vertex)) {
                    core::at(marked, mi).emplace(vertex, static_cast<uint32_t>(li));
                }
            }
        }
    }
    {
        // (reported) the lamps whose head is drawn, by where it is: their own surface, or fixture geometry at them
        std::vector<uint8_t> drawn(lamps.size(), 0);
        for (const auto& mesh_marks : marked) {
            for (const auto& [vertex, lamp] : mesh_marks) {
                core::at(drawn, lamp) = 1;
            }
        }
        std::size_t own = 0;
        std::size_t attached = 0;
        std::size_t none = 0;
        for (std::size_t li = 0; li < lamps.size(); ++li) {
            if (!(luma(core::at(lamps, li).light) > 0.0F)) {
                continue;
            }
            own += core::at(has_head, li);
            attached += core::at(drawn, li) != 0 && core::at(has_head, li) == 0 ? 1 : 0;
            none += core::at(drawn, li) == 0 ? 1 : 0;
        }
        get_log("relit bake: lamp heads - %zu on their own surface, %zu on the fixture geometry at them, %zu shining lamps "
                "with no surface to show (no fixture geometry within their luminous radius)",
                own, attached, none);
    }
    for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
        std::vector<rt::RelitEmitting>& emitting = core::at(out, mi);
        for (const auto& [vertex, lamp] : core::at(marked, mi)) {
            emitting.push_back({.vertex = vertex, .lamp = lamp});
        }
        std::ranges::sort(emitting,
                          [](const rt::RelitEmitting& a, const rt::RelitEmitting& b) { return a.vertex < b.vertex; });
    }
    return out;
}

// (round L) Each lamp's luminous area (m^2): its emissive triangles' areas x their lit share (rt_lights.h
// EmitterTriangle::lit_share - a light card's saturated core, a texture's lamp texels, a self-lit triangle whole).
std::vector<float> lamp_areas(std::size_t count)
{
    std::vector<float> out(count, 0.0F);
    const std::shared_ptr<const rt::LevelEmitters> found = rt::level_emitters();
    if (found == nullptr) {
        return out;
    }
    constexpr double kSquareMetresPerUnit = (1.0 / 39.37) * (1.0 / 39.37); // (the world's units are inches)
    for (std::size_t ti = 0; ti < found->triangles.size() && ti < found->triangle_emitter.size(); ++ti) {
        const uint32_t lamp = core::at(found->triangle_emitter, ti);
        if (lamp >= count) {
            continue;
        }
        const rt::EmitterTriangle& triangle = core::at(found->triangles, ti);
        const rt::Vec3 a = std::get<0>(triangle.corners);
        const rt::Vec3 n = rt::cross(rt::minus(std::get<1>(triangle.corners), a), rt::minus(std::get<2>(triangle.corners), a));
        const double area = 0.5 * std::sqrt(static_cast<double>(rt::dot(n, n))) * kSquareMetresPerUnit;
        core::at(out, lamp) += static_cast<float>(area * static_cast<double>(std::clamp(triangle.lit_share, 0.0F, 1.0F)));
    }
    return out;
}

// (round L, the bake report) The lamps by their light's source and fixture, and the lumens per lamp - median, 90th
// percentile, most - with the brightest few (tools/game/relit_bake.ts reads the "lamps by physics" line).
void log_lamp_models(std::span<const rt::RelitLamp> lamps, std::span<const rt::RelitLampTold> told,
                     const rt::LampModelCounts& counts)
{
    std::vector<float> lumens;
    double total = 0.0;
    for (const rt::RelitLamp& lamp : lamps) {
        if (luma(lamp.light) > 0.0F) {
            lumens.push_back(lamp.lumens);
            total += static_cast<double>(lamp.lumens);
        }
    }
    std::ranges::sort(lumens);
    const auto at = [&lumens](double share) {
        return lumens.empty() ? 0.0
                              : static_cast<double>(core::at(lumens, static_cast<std::size_t>(share * static_cast<double>(lumens.size() - 1))));
    };
    std::array<std::size_t, 5> verdicts{};
    for (const rt::RelitLampTold verdict : told) {
        core::at(verdicts, static_cast<std::size_t>(verdict)) += 1;
    }
    get_log("relit bake: lamps by physics - %zu lamps: %zu solved, %zu by their fixture's model (the bake couldn't tell), "
            "%zu implausible solves replaced by the model, %zu by their own surface, %zu not a fixture's size, %zu no "
            "light; the bake knew: %zu told, "
            "%zu no edges, %zu off in the bake, %zu dark, %zu run off; fixtures shining: %zu sodium, %zu white street, "
            "%zu tubes, %zu panels, %zu bulbs, %zu neon, %zu signs / windows, %zu high-bays, %zu unknown; lumens per shining lamp: median "
            "%.0f, 90th percentile %.0f, most %.0f; %.0f lm in all; the art's lamp exposure %.3g (from %zu solved lamps)",
            lamps.size(), core::at(counts.by_source, 1), core::at(counts.by_source, 2), core::at(counts.by_source, 3),
            core::at(counts.by_source, 4), core::at(counts.by_source, 5), core::at(counts.by_source, 0),
            core::at(verdicts, 0), core::at(verdicts, 1),
            core::at(verdicts, 2), core::at(verdicts, 3), core::at(verdicts, 4), core::at(counts.by_fixture, 1),
            core::at(counts.by_fixture, 2), core::at(counts.by_fixture, 3), core::at(counts.by_fixture, 4),
            core::at(counts.by_fixture, 5), core::at(counts.by_fixture, 6), core::at(counts.by_fixture, 7),
            core::at(counts.by_fixture, 8), core::at(counts.by_fixture, 0), at(0.5), at(0.9), lumens.empty() ? 0.0 : static_cast<double>(lumens.back()),
            total, counts.exposure, counts.exposure_lamps);
    // the implausible solves, each (a failed measurement replaced by the model)
    std::size_t shown = 0;
    for (std::size_t li = 0; li < lamps.size() && shown < 24; ++li) {
        const rt::RelitLamp& lamp = core::at(lamps, li);
        if (lamp.source != static_cast<uint8_t>(rt::RelitLampSource::kImplausible)) {
            continue;
        }
        ++shown;
        get_log("relit bake: lamp %zu (%s) at %.0f %.0f %.0f: an implausible solve - now %s, %.4f m^2 at %.0f cd/m^2, "
                "%.0f lm, %.0f K",
                li, rt::fixture_name(static_cast<rt::LampFixture>(lamp.fixture)).data(),
                static_cast<double>(rt::x_of(lamp.position)), static_cast<double>(rt::y_of(lamp.position)),
                static_cast<double>(rt::z_of(lamp.position)),
                rt::relit_lamp_source_name(static_cast<rt::RelitLampSource>(lamp.source)),
                static_cast<double>(lamp.area), static_cast<double>(lamp.luminance), static_cast<double>(lamp.lumens),
                static_cast<double>(lamp.kelvin));
    }
}

// (round indoor lamps) More lamps' links put after a mesh's own (their lamps numbered from `first`).
void append_links(rt::RelitLampLinks* links, const rt::RelitLampLinks& extra, uint32_t first)
{
    if (links->size() < extra.size()) {
        links->resize(extra.size());
    }
    for (std::size_t mi = 0; mi < extra.size(); ++mi) {
        const rt::RelitMeshLinks& more = core::at(extra, mi);
        if (more.first.empty()) {
            continue;
        }
        rt::RelitMeshLinks& mesh = core::at(*links, mi);
        rt::RelitMeshLinks merged;
        const std::size_t vertices = more.first.size() - 1;
        merged.first.reserve(more.first.size());
        for (std::size_t vi = 0; vi < vertices; ++vi) {
            merged.first.push_back(static_cast<uint32_t>(merged.links.size()));
            if (vi + 1 < mesh.first.size()) {
                for (uint32_t k = core::at(mesh.first, vi); k < core::at(mesh.first, vi + 1); ++k) {
                    merged.links.push_back(core::at(mesh.links, k));
                }
            }
            for (uint32_t k = core::at(more.first, vi); k < core::at(more.first, vi + 1); ++k) {
                rt::RelitLampLink link = core::at(more.links, k);
                link.lamp += first;
                merged.links.push_back(link);
            }
        }
        merged.first.push_back(static_cast<uint32_t>(merged.links.size()));
        mesh = std::move(merged);
    }
}

// (lamp solve check) Each lamp the bake told: its solved light against its fixture - the solved flux, its surface's
// luminance over its luminous area, its kind's ranges, and its pool: the lamp's own light at the vertex it lights most
// (unit and lux) and the light there in all.
void log_told_lamps(std::span<const rt::RelitLamp> solved, std::span<const rt::RelitLamp> lamps,
                    std::span<const rt::RelitLampTold> told, std::span<const float> areas,
                    const rt::RelitLampLinks& links, std::span<const rt::RelitVertices> light, double unit_lux)
{
    std::vector<float> most(lamps.size(), 0.0F);
    std::vector<float> there(lamps.size(), 0.0F);
    for (std::size_t mi = 0; mi < links.size() && mi < light.size(); ++mi) {
        const rt::RelitMeshLinks& mesh_links = core::at(links, mi);
        const rt::RelitVertices& vertices = core::at(light, mi);
        for (std::size_t vi = 0; vi < vertices.size() && vi + 1 < mesh_links.first.size(); ++vi) {
            for (uint32_t k = core::at(mesh_links.first, vi); k < core::at(mesh_links.first, vi + 1); ++k) {
                const rt::RelitLampLink& link = core::at(mesh_links.links, k);
                if (link.lamp < solved.size()) {
                    const float mine = luma(core::at(solved, link.lamp).light) * link.weight;
                    if (mine > core::at(most, link.lamp)) {
                        core::at(most, link.lamp) = mine;
                        core::at(there, link.lamp) = luma(core::at(vertices, vi).lamp);
                    }
                }
            }
        }
    }
    std::size_t shown = 0;
    for (std::size_t li = 0; li < lamps.size() && li < solved.size() && li < told.size() && shown < 40; ++li) {
        if (core::at(told, li) != rt::RelitLampTold::kSolved) {
            continue;
        }
        ++shown;
        rt::RelitLamp measured = core::at(solved, li);
        rt::relit_lamp_photometry(&measured, unit_lux);
        const rt::RelitLamp& lamp = core::at(lamps, li);
        const rt::FixtureValues& values = rt::fixture_values(static_cast<rt::LampFixture>(lamp.fixture));
        const double area = li < areas.size() ? static_cast<double>(core::at(areas, li)) : 0.0;
        const double luminance = area > 0.0 ? static_cast<double>(measured.lumens) / (3.14159265358979 * area) : 0.0;
        get_log("relit bake: told lamp %zu (%s, kind %u shape %u, core %.0f half %.0f) - solved %.4f in the unit: %.0f cd, "
                "%.0f lm; luminous area %.4f m^2 -> %.0f cd/m^2 (its kind's %.0f - %.0f cd/m^2, %.0f - %.0f lm, %.4f - "
                "%.2f m^2); its pool at most %.4f of the unit (%.0f lux), the lamps' light there %.4f; now %s, %.0f lm",
                li, rt::fixture_name(static_cast<rt::LampFixture>(lamp.fixture)).data(), static_cast<unsigned>(lamp.kind),
                static_cast<unsigned>(lamp.shape), static_cast<double>(lamp.core), static_cast<double>(lamp.half_length),
                static_cast<double>(luma(measured.light)), static_cast<double>(measured.candela),
                static_cast<double>(measured.lumens), area, luminance, static_cast<double>(values.least),
                static_cast<double>(values.most), static_cast<double>(values.least_lumens),
                static_cast<double>(values.most_lumens), static_cast<double>(values.least_area),
                static_cast<double>(values.most_area), static_cast<double>(core::at(most, li)),
                static_cast<double>(core::at(most, li)) * unit_lux, static_cast<double>(core::at(there, li)),
                rt::relit_lamp_source_name(static_cast<rt::RelitLampSource>(lamp.source)),
                static_cast<double>(lamp.lumens));
    }
    // the shining tubes: where they are (roofed or not), what they are, their source
    std::size_t tubes = 0;
    for (std::size_t li = 0; li < lamps.size() && li < told.size() && tubes < 30; ++li) {
        const rt::RelitLamp& lamp = core::at(lamps, li);
        if (lamp.fixture != static_cast<uint8_t>(rt::LampFixture::kTube) || !(luma(lamp.light) > 0.0F)) {
            continue;
        }
        ++tubes;
        get_log("relit bake: tube %zu at %.0f %.0f %.0f (kind %u, half %.0f, core %.0f, sky switch %.4f%s) - %s; told %u; "
                "luminous area %.4f m^2, %.0f lm, %.0f K",
                li, static_cast<double>(rt::x_of(lamp.position)), static_cast<double>(rt::y_of(lamp.position)),
                static_cast<double>(rt::z_of(lamp.position)), static_cast<unsigned>(lamp.kind),
                static_cast<double>(lamp.half_length), static_cast<double>(lamp.core), static_cast<double>(lamp.switch_sky),
                lamp.switch_sky >= 0.5F * rt::kLampAlwaysOn ? " - roofed" : "",
                rt::relit_lamp_source_name(static_cast<rt::RelitLampSource>(lamp.source)),
                static_cast<unsigned>(core::at(told, li)), li < areas.size() ? static_cast<double>(core::at(areas, li)) : 0.0,
                static_cast<double>(lamp.lumens), static_cast<double>(lamp.kelvin));
    }
    // and the lamps given no light: their kind, verdict, fixture and luminous area
    std::array<std::array<std::size_t, 5>, 8> none{}; // [kind][verdict]
    std::size_t listed = 0;
    for (std::size_t li = 0; li < lamps.size() && li < told.size(); ++li) {
        const rt::RelitLamp& lamp = core::at(lamps, li);
        const auto source = static_cast<rt::RelitLampSource>(lamp.source);
        if (source != rt::RelitLampSource::kNone && source != rt::RelitLampSource::kNotAFixture) {
            continue;
        }
        const auto kind = std::min<std::size_t>(static_cast<std::size_t>(lamp.kind), 7);
        core::at(core::at(none, kind), static_cast<std::size_t>(core::at(told, li))) += 1;
        if (listed < 40) {
            ++listed;
            get_log("relit bake: dark lamp %zu (%s, kind %u shape %u, core %.0f half %.0f, sky switch %.4f) - %s; told %u; "
                    "luminous area %.4f m^2 (its kind's %.4f - %.2f)",
                    li, rt::fixture_name(static_cast<rt::LampFixture>(lamp.fixture)).data(),
                    static_cast<unsigned>(lamp.kind), static_cast<unsigned>(lamp.shape), static_cast<double>(lamp.core),
                    static_cast<double>(lamp.half_length), static_cast<double>(lamp.switch_sky),
                    rt::relit_lamp_source_name(source), static_cast<unsigned>(core::at(told, li)),
                    li < areas.size() ? static_cast<double>(core::at(areas, li)) : 0.0,
                    static_cast<double>(rt::fixture_values(static_cast<rt::LampFixture>(lamp.fixture)).least_area),
                    static_cast<double>(rt::fixture_values(static_cast<rt::LampFixture>(lamp.fixture)).most_area));
        }
    }
    // (round indoor lamps) every lamp, one line each: where it is, what it is and what became of it
    std::vector<std::size_t> linked(lamps.size(), 0);
    for (const rt::RelitMeshLinks& mesh_links : links) {
        for (const rt::RelitLampLink& link : mesh_links.links) {
            if (link.lamp < linked.size()) {
                core::at(linked, link.lamp) += 1;
            }
        }
    }
    for (std::size_t li = 0; li < lamps.size() && li < told.size(); ++li) {
        const rt::RelitLamp& lamp = core::at(lamps, li);
        get_log("relit bake: list %zu kind %u shape %u axis %.2f %.2f %.2f links %zu at %.0f %.0f %.0f core %.0f half %.0f - "
                "told %u, switch %s, %s, %s, area %.4f m^2, %.0f lm",
                li, static_cast<unsigned>(lamp.kind), static_cast<unsigned>(lamp.shape),
                static_cast<double>(rt::x_of(lamp.axis)), static_cast<double>(rt::y_of(lamp.axis)),
                static_cast<double>(rt::z_of(lamp.axis)), core::at(linked, li),
                static_cast<double>(rt::x_of(lamp.position)), static_cast<double>(rt::y_of(lamp.position)),
                static_cast<double>(rt::z_of(lamp.position)), static_cast<double>(lamp.core),
                static_cast<double>(lamp.half_length), static_cast<unsigned>(core::at(told, li)),
                lamp.switch_sky >= 0.5F * rt::kLampAlwaysOn ? "roofed" : text::format("%.4f", static_cast<double>(lamp.switch_sky)).c_str(),
                rt::fixture_name(static_cast<rt::LampFixture>(lamp.fixture)).data(),
                rt::relit_lamp_source_name(static_cast<rt::RelitLampSource>(lamp.source)),
                li < areas.size() ? static_cast<double>(core::at(areas, li)) : 0.0, static_cast<double>(lamp.lumens));
    }
    for (std::size_t kind = 0; kind < none.size(); ++kind) {
        const std::array<std::size_t, 5>& row = core::at(none, kind);
        if (row[0] + row[1] + row[2] + row[3] + row[4] > 0) {
            get_log("relit bake: dark lamps of kind %zu by verdict - solved %zu, no edges %zu, off in the bake %zu, dark "
                    "%zu, run off %zu",
                    kind, row[0], row[1], row[2], row[3], row[4]);
        }
    }
}

// (round A2) The median share of the sky the level's upward faces see (their traced K): under kRelitIndoorSky the
// level is roofed - indoors. The level's own surfaces' vertices (rt_relit.h relit_baked_vertex: not the water's grid or
// the smoothing's made ones - a grid's density would outvote the rest).
float open_sky(std::span<const rt::RelitMesh> meshes, std::span<const rt::RelitVertices> light)
{
    constexpr float kUpward = 0.7F;
    std::vector<float> open;
    for (std::size_t mi = 0; mi < meshes.size() && mi < light.size(); ++mi) {
        const rt::RelitVertices& vertices = core::at(light, mi);
        for (std::size_t vi = 0; vi < vertices.size(); ++vi) {
            const rt::RelitVertex& vertex = core::at(vertices, vi);
            if (vertex.used && rt::relit_baked_vertex(core::at(meshes, mi), vi) && rt::y_of(vertex.normal) >= kUpward) {
                open.push_back(vertex.sky);
            }
        }
    }
    if (open.empty()) {
        return 0.0F;
    }
    const auto middle = open.begin() + static_cast<std::ptrdiff_t>(open.size() / 2);
    std::ranges::nth_element(open, middle);
    return *middle;
}

// (reported - the evidence the fit's light colours come from) The bake's colours on the open upward faces (K at least a
// half) in the rays' sun and in their shade: each group's median per channel (light: rt_relit.h colour_light), the
// traced bounce's there, and their count.
void log_open_colours(std::span<const rt::RelitMesh> meshes, std::span<const rt::RelitVertices> light)
{
    constexpr float kUpward = 0.7F;
    constexpr float kOpen = 0.5F;
    constexpr float kSunlit = 0.9F;
    constexpr float kShaded = 0.1F;
    std::array<std::array<std::vector<float>, 3>, 2> colours;
    std::array<std::array<std::vector<float>, 3>, 2> bounces;
    for (std::size_t mi = 0; mi < meshes.size() && mi < light.size(); ++mi) {
        const rt::RelitMesh& mesh = core::at(meshes, mi);
        const rt::RelitVertices& vertices = core::at(light, mi);
        for (std::size_t vi = 0; vi < vertices.size() && vi < mesh.colours.size(); ++vi) {
            const rt::RelitVertex& vertex = core::at(vertices, vi);
            if (!vertex.used || !rt::relit_baked_vertex(mesh, vi) || rt::y_of(vertex.normal) < kUpward ||
                vertex.sky < kOpen || (vertex.sun > kShaded && vertex.sun < kSunlit)) {
                continue;
            }
            const std::size_t group = vertex.sun >= kSunlit ? 0 : 1;
            const rt::Rgbf colour = rt::colour_light(core::at(mesh.colours, vi));
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(core::at(colours, group), ch).push_back(core::at(colour, ch));
                core::at(core::at(bounces, group), ch).push_back(core::at(vertex.bounce, ch));
            }
        }
    }
    const auto median = [](std::vector<float>* values) {
        if (values->empty()) {
            return 0.0;
        }
        const auto middle = values->begin() + static_cast<std::ptrdiff_t>(values->size() / 2);
        std::ranges::nth_element(*values, middle);
        return static_cast<double>(*middle);
    };
    std::array<std::array<double, 3>, 2> y{};
    std::array<std::array<double, 3>, 2> b{};
    for (std::size_t group = 0; group < 2; ++group) {
        for (std::size_t ch = 0; ch < 3; ++ch) {
            core::at(core::at(y, group), ch) = median(&core::at(core::at(colours, group), ch));
            core::at(core::at(b, group), ch) = median(&core::at(core::at(bounces, group), ch));
        }
    }
    get_log("relit bake: the open upward faces - in the sun (%zu) colours %.3f %.3f %.3f, bounce %.3f %.3f %.3f; in the "
            "shade (%zu) colours %.3f %.3f %.3f, bounce %.3f %.3f %.3f",
            core::at(colours, 0).front().size(), std::get<0>(std::get<0>(y)), std::get<1>(std::get<0>(y)),
            std::get<2>(std::get<0>(y)), std::get<0>(std::get<0>(b)), std::get<1>(std::get<0>(b)),
            std::get<2>(std::get<0>(b)), core::at(colours, 1).front().size(), std::get<0>(std::get<1>(y)),
            std::get<1>(std::get<1>(y)), std::get<2>(std::get<1>(y)), std::get<0>(std::get<1>(b)),
            std::get<1>(std::get<1>(b)), std::get<2>(std::get<1>(b)));
}

void bake(std::span<const rt_relit_world::WorldMesh> world, rt::RayTracing& tracer)
{
    const auto start = std::chrono::steady_clock::now();
    const auto ms_since = [](std::chrono::steady_clock::time_point from) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
    };
    const uint32_t scene = rt_relit_world::level_scene();
    rt::RelitLevelData data;
    data.scene = scene;
    data.sun = rt::baked_sun();
    data.sun.scene = 0;
    data.sky_rays = rt::kRelitBakeSkyRays;
    data.sun_rays = rt::kRelitBakeSunRays;
    // the world's meshes, one each by content, in the order of their content's hash
    std::vector<rt_relit_world::BakeMesh> read;
    std::unordered_map<uint64_t, std::size_t> seen;
    std::size_t refused = 0;
    std::unordered_map<rt::MeshId, uint64_t> content_of; // (every world mesh's content - its copies' too)
    for (const rt_relit_world::WorldMesh& mesh : world) {
        rt_relit_world::BakeMesh one;
        if (!rt_relit_world::read_world_mesh(mesh, &one)) {
            ++refused;
            continue;
        }
        content_of.emplace(mesh.id, one.content);
        if (seen.emplace(one.content, read.size()).second) {
            read.push_back(std::move(one));
        }
    }
    std::ranges::sort(read, [](const rt_relit_world::BakeMesh& a, const rt_relit_world::BakeMesh& b) {
        return a.content < b.content;
    });
    std::vector<uint64_t> contents;
    std::vector<rt::RelitMesh> meshes;
    std::size_t vertices = 0;
    for (rt_relit_world::BakeMesh& mesh : read) {
        contents.push_back(mesh.content);
        vertices += mesh.mesh.positions.size();
        meshes.push_back(std::move(mesh.mesh));
    }
    data.content = rt::relit_level_content(contents);
    data.facing = rt::relit_facing(meshes);
    std::vector<rt::RelitVertices> light(meshes.size());
    rt::RelitStats stats;
    rt::RelitStats lamp_stats;
    std::vector<rt::RelitLamp> lamps = level_lamps();
    data.lamps = static_cast<uint32_t>(lamps.size());
    rt::RelitLampLinks links;
    double trace_ms = 0.0;
    rt::Rgbf bake_sky{}; // (round A2, the evidence) the bake's own sky light in the global unit, and its sun's elevation
    float sun_elevation = 0.0F;
    float floors_open = 0.0F; // (the floors' sky share - open_sky -, reported)
    double fit_ms = 0.0;
    double bounce_ms = 0.0;
    {
        // (lighting round A) every bake is taken apart: with a sun, or - a night or indoor bake, no sun in it - by its sky,
        // bounce and lamps alone. (round A2) Which: the relit fit itself picks the baked sun - each of the baked sun's
        // search's candidates (rt_scene.h BakedSun::candidates: a correlation of "lit" with the brightness alone misses
        // a sun whose shadows fall on many-coloured ground; and a low sun's ring) traced and fitted, the one whose sun
        // improves the fit most (RelitModel::sun_needed); a sun when it does so by rt_relit.h kRelitMinSunGain - its
        // confidence the share of an open surface's light its sun carries (relit_sun_share, reported).
        const rt::RelitParams params{.sky_rays = rt::kRelitBakeSkyRays,
                                     .sun_rays = rt::kRelitBakeSunRays,
                                     .sun_radius_degrees = rt::kRelitSunRadiusDegrees,
                                     .detail = 1.0F,
                                     .keep_sky = false};
        const auto traced = std::chrono::steady_clock::now();
        const rt::Vec3 up{0.0F, 1.0F, 0.0F};
        rt::trace_relit_light(meshes, {}, up, params, tracer, &light, &stats);
        rt::trace_relit_lamps(meshes, {}, lamps, tracer, &light, &lamp_stats, &links);
        const auto set_suns = [&light](const std::vector<std::vector<float>>* suns) {
            for (std::size_t mi = 0; mi < light.size(); ++mi) {
                rt::RelitVertices& traced_vertices = core::at(light, mi);
                for (std::size_t vi = 0; vi < traced_vertices.size(); ++vi) {
                    const float sun = suns != nullptr ? core::at(core::at(*suns, mi), vi) : 0.0F;
                    core::at(traced_vertices, vi).sun = sun;
                    core::at(traced_vertices, vi).sun_baked = sun;
                }
            }
        };
        set_suns(nullptr);
        // (round relit-misses) the light's colours its sources' at the bake's own time: the physical sky's (and, for each
        // sun candidate, the physical sun's toward it)
        const auto colours_toward = [&data](const rt::Vec3* toward) {
            rt::BakedSun sun = data.sun;
            if (toward != nullptr) {
                sun.toward = *toward;
                sun.found = true;
            }
            const rt::DayLight day = own_day_light(sun);
            return rt::RelitLightColours{.sun = day.sun_light, .sky = day.sky_light};
        };
        rt::RelitModel model = rt::fit_relit_model(meshes, light, up, true); // (the evidence: the bake's own colours)
        const float sunless_error = rt::relit_edge_error(meshes, light, model);
        get_log("relit bake: the sunless model - error %.4f (%s)", static_cast<double>(sunless_error),
                model.valid ? "valid" : "not valid");
        struct Pick {
            bool found = false;
            rt::Vec3 toward{};
            float error = 0.0F;
            float gain = 0.0F; // the fit's objective without the sun over with it (RelitModel::sun_needed)
            float share = 0.0F;
            rt::RelitModel model;
            std::vector<std::vector<float>> suns;
        } pick;
        // (round relit-misses) The candidates are told apart on a sample of the level: the meshes are in the order of their
        // content's hash - an order unrelated to where they are -, so the first of them up to kCandidateVertices
        // vertices are a random sample of the level, and each candidate's fit over it estimates the fit over the
        // whole. A level that size or smaller is its own sample (as before); a bigger one traces and fits each
        // candidate over the sample alone and the one it picks over the whole level. Before, every candidate was
        // traced and fitted over every vertex: Proving Ground's whole world (16000 meshes, 2.5 million vertices,
        // 32 candidates) spent 15 of its bake's 20 minutes there and was given up.
        std::size_t sample_count = 0;
        for (std::size_t sampled_vertices = 0;
             sample_count < meshes.size() && sampled_vertices < kCandidateVertices; ++sample_count) {
            sampled_vertices += core::at(meshes, sample_count).positions.size();
        }
        const bool sampled = sample_count < meshes.size();
        const std::span<const rt::RelitMesh> sample(meshes.data(), sample_count);
        std::vector<rt::RelitVertices> sample_light(light.begin(),
                                                    light.begin() + static_cast<std::ptrdiff_t>(sample_count));
        if (sampled) {
            std::size_t sample_vertices = 0;
            for (const rt::RelitMesh& mesh : sample) {
                sample_vertices += mesh.positions.size();
            }
            get_log("relit bake: the sun's candidates told apart on a sample - %zu of %zu meshes, %zu of %zu vertices",
                    sample_count, meshes.size(), sample_vertices, vertices);
        }
        const auto candidate_fit = [&](const rt::Vec3& toward, std::span<const rt::RelitMesh> over,
                                       std::vector<rt::RelitVertices>* over_light, float* error, float* share) {
            rt::trace_relit_sun(over, {}, toward, params, tracer, over_light, &stats);
            for (rt::RelitVertices& traced_vertices : *over_light) {
                for (rt::RelitVertex& vertex : traced_vertices) {
                    vertex.sun_baked = vertex.sun;
                }
            }
            rt::RelitModel fitted = rt::fit_relit_model(over, *over_light, toward, false);
            *error = fitted.valid ? rt::relit_edge_error(over, *over_light, fitted) : 1.0F;
            *share = rt::relit_sun_share(fitted);
            return fitted;
        };
        const std::vector<std::array<float, 4>> candidates = data.sun.candidates;
        for (const std::array<float, 4>& candidate : candidates) {
            const rt::Vec3 toward{std::get<0>(candidate), std::get<1>(candidate), std::get<2>(candidate)};
            float error = 1.0F;
            float share = 0.0F;
            const rt::RelitModel fitted = candidate_fit(toward, sample, &sample_light, &error, &share);
            constexpr double kDegrees = 180.0 / 3.14159265358979323846;
            get_log("relit bake: a sun candidate heading %.0f elevation %.0f (search score %.2f) - error %.4f, the sun's "
                    "share %.2f, the fit without the sun %.4f x worse (%s)",
                    std::atan2(static_cast<double>(rt::x_of(toward)), static_cast<double>(rt::z_of(toward))) * kDegrees,
                    std::asin(std::clamp(static_cast<double>(rt::y_of(toward)), -1.0, 1.0)) * kDegrees,
                    static_cast<double>(std::get<3>(candidate)), static_cast<double>(error), static_cast<double>(share),
                    static_cast<double>(fitted.sun_needed), fitted.valid ? "valid" : "no sunlight");
            if (fitted.valid && (!pick.found || fitted.sun_needed > pick.gain)) {
                pick.found = true;
                pick.toward = toward;
                pick.error = error;
                pick.gain = fitted.sun_needed;
                pick.share = share;
                pick.model = fitted;
                pick.suns.assign(sample_light.size(), {});
                for (std::size_t mi = 0; mi < sample_light.size(); ++mi) {
                    for (const rt::RelitVertex& vertex : core::at(sample_light, mi)) {
                        core::at(pick.suns, mi).push_back(vertex.sun);
                    }
                }
            }
        }
        if (pick.found && sampled) {
            // the pick over the whole level: its sun traced for every vertex, its fit, error and share the whole's
            pick.model = candidate_fit(pick.toward, meshes, &light, &pick.error, &pick.share);
            pick.gain = pick.model.sun_needed;
            pick.found = pick.model.valid;
            pick.suns.assign(light.size(), {});
            for (std::size_t mi = 0; mi < light.size(); ++mi) {
                for (const rt::RelitVertex& vertex : core::at(light, mi)) {
                    core::at(pick.suns, mi).push_back(vertex.sun);
                }
            }
            get_log("relit bake: the picked sun over the whole level - error %.4f, the sun's share %.2f, the fit without "
                    "the sun %.4f x worse (%s)",
                    static_cast<double>(pick.error), static_cast<double>(pick.share),
                    static_cast<double>(pick.gain), pick.found ? "valid" : "no sunlight");
        } else if (!sampled) {
            light = std::move(sample_light); // (the sample was the whole level: its traces are the level's)
        }
        // (the level's own data: a night state's bake shows the night - its directional light the moon's, not a sun;
        // a roofed level's floors see too little of the sky for its bake's sun to be told - daylight reaches it through
        // its openings, traced, whatever the bake)
        const float floors_sky = open_sky(meshes, light);
        floors_open = floors_sky;
        const bool night = own_state() == rt::TodState::kNight;
        const bool roofed = floors_sky < rt::kRelitIndoorSky;
        const bool sunless = night || roofed || !(pick.found && pick.gain >= rt::kRelitMinSunGain);
        get_log("relit bake: the relit fit's sun - %s (the fit %.4f x worse without it, its share %.2f of an open "
                "surface's light; the floors see %.2f of the sky%s%s; the sunless model's error %.4f, the sun's %.4f)",
                sunless ? "none" : "found", static_cast<double>(pick.gain), static_cast<double>(pick.share),
                static_cast<double>(floors_sky), roofed ? " - roofed" : "", night ? "; a night state" : "",
                static_cast<double>(sunless_error), static_cast<double>(pick.error));
        data.sun.found = !sunless;
        data.sun.confidence = pick.found ? pick.share : 0.0F;
        if (pick.found) {
            data.sun.toward = pick.toward;
        }
        data.sun_margin = pick.found ? pick.gain / rt::kRelitMinSunGain : 0.0F; // (the verdict's: the fit's gain)
        // (round relit-misses) the model the level is relit by: the light's strengths over every channel, its colours the
        // physical sun's and sky's at the bake's own time (the candidates and the verdict read each channel's own fit)
        if (!sunless) {
            set_suns(&pick.suns);
            const rt::RelitLightColours colours = colours_toward(&pick.toward);
            model = rt::fit_relit_model(meshes, light, pick.toward, false, &colours);
        } else {
            set_suns(nullptr);
            const rt::RelitLightColours colours = colours_toward(nullptr);
            model = rt::fit_relit_model(meshes, light, up, true, &colours);
        }
        log_open_colours(meshes, light);
        trace_ms = ms_since(traced);
        // (reported) the fit as it came out, in the bake's own colours - before the unit: each channel's sun, sky and
        // bounce coefficients
        get_log("relit bake: the fit in the bake's colours - sun %.3f %.3f %.3f, sky %.3f %.3f %.3f, bounce %.3f %.3f %.3f "
                "(%zu dead sun channels)",
                static_cast<double>(std::get<0>(model.sun)), static_cast<double>(std::get<1>(model.sun)),
                static_cast<double>(std::get<2>(model.sun)), static_cast<double>(std::get<0>(model.sky)),
                static_cast<double>(std::get<1>(model.sky)), static_cast<double>(std::get<2>(model.sky)),
                static_cast<double>(std::get<0>(model.bounce)), static_cast<double>(std::get<1>(model.bounce)),
                static_cast<double>(std::get<2>(model.bounce)), model.dead_channels);
        const auto fitted = std::chrono::steady_clock::now();
        // (round A2) each lamp's intensity solved from the bake (no level-wide c); (round A3) in every bake - the bake's
        // light tells which were on (told above the fit's noise), no "dark level" or lamps-on guess
        // (round L) each lamp's own colour as found (linear: its glow's or surface's) and what the bake knew of it
        std::vector<rt::Rgbf> own_colours;
        own_colours.reserve(lamps.size());
        for (const rt::RelitLamp& lamp : lamps) {
            own_colours.push_back({rt::linear_from_display(std::get<0>(lamp.light)),
                                   rt::linear_from_display(std::get<1>(lamp.light)),
                                   rt::linear_from_display(std::get<2>(lamp.light))});
        }
        std::vector<rt::RelitLampTold> told;
        {
            const rt::DayLight bake_day = own_day_light(data.sun);
            const rt::BakeTime bake_time{.unit_lux = rt::unit_illuminance(),
                                         .sky = light_luma(bake_day.sky_light),
                                         .lamps = bake_day.lamps};
            const std::vector<rt::RelitLamp> listed = lamps; // (the solve changes them: a second solve starts again)
            rt::solve_relit_lamps(meshes, light, links, lamps, &model, bake_time, &told);
            get_log("relit bake: lamps told by the bake's light level (not its edges) - %zu; the level's tint where no lamp "
                    "reaches %.3f, its spread %.3f; %zu told in all",
                    model.lamps_by_level, static_cast<double>(model.level_tint), static_cast<double>(model.level_spread),
                    model.lamps_solved);
            // (round indoor lamps) the bake's light no listed lamp explains: a lamp looked for above each pool of it
            // (rt_relit.h find_pool_lamps), traced, and every lamp solved again with them
            rt::PoolSearch search;
            const std::vector<rt::RelitLamp> pools =
                model.valid ? rt::find_pool_lamps(meshes, light, links, lamps, model, tracer, &search)
                            : std::vector<rt::RelitLamp>{};
            if (!pools.empty()) {
                rt::RelitLampLinks extra;
                rt::trace_relit_lamps(meshes, {}, pools, tracer, &light, &lamp_stats, &extra);
                append_links(&links, extra, static_cast<uint32_t>(listed.size()));
                lamps = listed;
                lamps.insert(lamps.end(), pools.begin(), pools.end());
                for (const rt::RelitLamp& pool : pools) {
                    own_colours.push_back(pool.light);
                }
                data.lamps = static_cast<uint32_t>(lamps.size());
                rt::solve_relit_lamps(meshes, light, links, lamps, &model, bake_time, &told);
            }
            std::size_t told_pools = 0;
            for (std::size_t li = listed.size(); li < lamps.size() && li < told.size(); ++li) {
                told_pools += core::at(told, li) == rt::RelitLampTold::kSolved ? 1 : 0;
            }
            get_log("relit bake: lamps from their pools - %zu vertices brighter than the model and the told lamps by %.1f x, "
                    "%zu pools, %zu with a mount: a candidate each, %zu told by the bake; %zu lamps told in all (%zu by the "
                    "light's level)",
                    search.seeds, static_cast<double>(rt::kRelitLampHigh), search.pools, search.mounted, told_pools,
                    model.lamps_solved, model.lamps_by_level);
        }
        {
            // (reported, the first candidates: what each lights and what the bake told of it)
            std::vector<std::size_t> reach(lamps.size(), 0);
            for (const rt::RelitMeshLinks& mesh_links : links) {
                for (const rt::RelitLampLink& link : mesh_links.links) {
                    if (link.lamp < reach.size()) {
                        core::at(reach, link.lamp) += 1;
                    }
                }
            }
            std::size_t shown = 0;
            for (std::size_t li = 0; li < lamps.size() && shown < 12; ++li) {
                const rt::RelitLamp& lamp = core::at(lamps, li);
                if (!rt::candidate_kind(lamp.kind)) {
                    continue;
                }
                ++shown;
                get_log("relit bake: candidate %zu (%s, shape %u) at %.0f %.0f %.0f core %.0f reach %.0f - lights %zu "
                        "vertices; %s, light %.3f %.3f %.3f",
                        li, lamp.kind == rt::EmitterKind::kTextured ? "textured" : lamp.kind == rt::EmitterKind::kPool ? "pool" : "self-lit",
                        static_cast<unsigned>(lamp.shape), static_cast<double>(rt::x_of(lamp.position)),
                        static_cast<double>(rt::y_of(lamp.position)), static_cast<double>(rt::z_of(lamp.position)),
                        static_cast<double>(lamp.core), static_cast<double>(lamp.reach), core::at(reach, li),
                        lamp.solved ? "told lit" : "not told", static_cast<double>(std::get<0>(lamp.light)),
                        static_cast<double>(std::get<1>(lamp.light)), static_cast<double>(std::get<2>(lamp.light)));
            }
        }
        data.model_margin = sunless ? (luma(model.sky) + luma(model.bounce)) / rt::kRelitMinSunLight
                                    : luma(model.sun) / rt::kRelitMinSunLight;
        if (model.valid) {
            rt::decompose_relit(meshes, {}, params, &model, &light);
            data.verdict = sunless ? rt::RelitVerdict::kRelitSunless : rt::RelitVerdict::kRelit;
        } else {
            data.verdict = rt::RelitVerdict::kNoModel;
        }
        fit_ms = ms_since(fitted);
        // (lighting round A2 - one light unit for every level) the tints as albedos and the light in the global unit by
        // the bounce's energy balance (rt_relit.h relit_to_unit: the same rule for every bake), then the level's own
        // light the physical sun's and sky's at its own time of day (rt_daylight.h - none from the sun in a sunless
        // bake); the bake's own daylight over the physical one reported
        const rt::DayLight own_day = own_day_light(data.sun);
        if (model.valid) {
            rt::relit_to_unit(&model, light, lamps);
            rt::relit_lamp_light(meshes, light, links, lamps); // (L in the unit: the lamps' own and the solved)
            rt::relit_lamp_switches(light, links, lamps, rt::unit_illuminance()); // (round A3: a photocell's switch)
            rt::relit_lamp_mounts(meshes, light, links, lamps); // (round indoor lamps: how high each hangs)
            const float elevation_sine = std::max(0.0F, rt::y_of(model.toward));
            const float baked = luma(rt::plus(rt::scaled(model.sun, elevation_sine), model.sky));
            const float physical = luma(rt::plus(rt::scaled(own_day.sun_light, elevation_sine), own_day.sky_light));
            model.daylight_ratio = !sunless && physical > 0.0F ? baked / physical : 0.0F;
            get_log("relit bake: in the global unit - albedo scale (the fit's bounce) %.3f; the bake's own sun %.3f %.3f "
                    "%.3f, sky %.3f %.3f %.3f against the physical sun %.3f %.3f %.3f per N.L%s, sky %.3f %.3f %.3f at its "
                    "own time - its daylight %.2f of the physical",
                    static_cast<double>(model.albedo_scale), static_cast<double>(std::get<0>(model.sun)),
                    static_cast<double>(std::get<1>(model.sun)), static_cast<double>(std::get<2>(model.sun)),
                    static_cast<double>(std::get<0>(model.sky)), static_cast<double>(std::get<1>(model.sky)),
                    static_cast<double>(std::get<2>(model.sky)), static_cast<double>(std::get<0>(own_day.sun_light)),
                    static_cast<double>(std::get<1>(own_day.sun_light)), static_cast<double>(std::get<2>(own_day.sun_light)),
                    sunless ? " (none: no sun in the bake)" : "", static_cast<double>(std::get<0>(own_day.sky_light)),
                    static_cast<double>(std::get<1>(own_day.sky_light)), static_cast<double>(std::get<2>(own_day.sky_light)),
                    static_cast<double>(model.daylight_ratio));
            model.sun = sunless ? rt::Rgbf{} : own_day.sun_light;
            bake_sky = model.sky;
            sun_elevation = sunless ? 0.0F : static_cast<float>(std::asin(std::clamp(static_cast<double>(rt::y_of(model.toward)), -1.0, 1.0)) *
                                               180.0 / 3.14159265358979323846);
            model.sky = own_day.sky_light;
        }
        // (round L - lamps by physics) each lamp's light where the bake can't tell it, or told it implausibly: its
        // fixture's model (rt_relit.h relit_lamp_models: its kind from its own data x its luminous area); then L again.
        // The lamps in physical units (a path tracer's: candela, lumens, colour temperature) and the emissive triangles
        // with their lamps' radiance
        const std::vector<float> areas = lamp_areas(lamps.size());
        const std::vector<rt::RelitLamp> as_solved(lamps.begin(), lamps.end());
        const rt::LampModelCounts modelled =
            rt::relit_lamp_models(lamps, told, areas, own_colours, rt::unit_illuminance());
        model.lamp_exposure = static_cast<float>(modelled.exposure);
        log_told_lamps(as_solved, lamps, told, areas, links, light, rt::unit_illuminance());
        if (model.valid) {
            rt::relit_lamp_light(meshes, light, links, lamps);
        }
        log_lamp_models(lamps, told, modelled);
        data.lamp_list = lamps;
        if (const std::shared_ptr<const rt::LevelEmitters> found = rt::level_emitters(); found != nullptr) {
            constexpr float kLambertian = 1.0F / 3.14159265F; // (a Lambertian emitter's radiance: its intensity over pi)
            for (std::size_t ti = 0; ti < found->triangles.size(); ++ti) {
                const rt::EmitterTriangle& triangle = core::at(found->triangles, ti);
                const uint32_t lamp =
                    ti < found->triangle_emitter.size() ? core::at(found->triangle_emitter, ti) : rt::kNoEmitter;
                rt::RelitEmissive emissive{.lamp = lamp};
                for (std::size_t k = 0; k < 3; ++k) {
                    core::at(emissive.corners, k) = core::at(triangle.corners, k);
                }
                if (lamp < lamps.size()) {
                    // (round L: its surface's luminance in the unit where the model or the solve gives one)
                    const rt::RelitLamp& owner = core::at(lamps, lamp);
                    const float y = luma(owner.light);
                    emissive.radiance =
                        owner.luminance > 0.0F && y > 0.0F
                            ? rt::scaled(owner.light, owner.luminance * 3.14159265F /
                                                          static_cast<float>(rt::unit_illuminance()) / y)
                            : rt::scaled(owner.light, kLambertian);
                }
                data.emissive.push_back(emissive);
            }
        }
        if (model.valid) {
            // the bounce of the relit light at the level's own time (the hits' colours the relit ones: tint x light),
            // not the bake's colours - nothing of the bake's light is kept
            const auto bounced = std::chrono::steady_clock::now();
            // (round A3) the lamps on at the level's own time, each by its own switch - L with them, for the bounce
            const std::vector<float> on = own_lamps_on(lamps, own_day);
            rt::relit_lamp_light(meshes, light, links, lamps, on);
            const rt::RelitLighting own = rt::relit_own_lighting(model, 1.0F);
            std::vector<rt::RelitMesh> relit = meshes;
            for (std::size_t mi = 0; mi < relit.size(); ++mi) {
                rt::RelitMesh& mesh = core::at(relit, mi);
                const rt::RelitVertices& lit = core::at(light, mi);
                // (round L: the light each vertex reflects, physical - no 8-bit colour between)
                mesh.light.assign(mesh.positions.size(), rt::Rgbf{});
                for (std::size_t vi = 0; vi < mesh.light.size() && vi < lit.size(); ++vi) {
                    core::at(mesh.light, vi) = rt::relit_reflected(model, core::at(lit, vi), own);
                }
            }
            rt::RelitParams again = params;
            again.keep_sky = true;
            rt::RelitStats bounce_stats;
            rt::trace_relit_sky(relit, {}, again, tracer, &light, &bounce_stats);
            rt::relit_lamp_light(meshes, light, links, lamps); // (kept: L with every lamp on - the game switches them)
            stats.rays += bounce_stats.rays;
            bounce_ms = ms_since(bounced);
        }
        data.model = model;
        std::unordered_map<rt::MeshId, std::size_t> mesh_of; // (a world mesh's id -> the bake's mesh of its content)
        {
            std::unordered_map<uint64_t, std::size_t> by_content;
            for (std::size_t mi = 0; mi < contents.size(); ++mi) {
                by_content.emplace(core::at(contents, mi), mi);
            }
            for (const auto& [id, content] : content_of) {
                if (const auto at = by_content.find(content); at != by_content.end()) {
                    mesh_of.emplace(id, at->second);
                }
            }
        }
        const std::vector<std::vector<rt::RelitEmitting>> emitting = emitting_vertices(meshes, lamps, mesh_of);
        for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
            rt::RelitDataMesh mesh{.content = core::at(contents, mi), .vertices = std::move(core::at(light, mi))};
            if (mi < links.size()) {
                mesh.links = std::move(core::at(links, mi));
            }
            mesh.emitting = core::at(emitting, mi);
            data.meshes.push_back(std::move(mesh));
        }
    }
    // the file
    const std::string folder = std::string(build_info::executable_directory()) + rt::kRelitDataFolder;
    fs::disk().make_folders(folder.c_str());
    const std::string path = folder + "\\" + rt::relit_data_name(scene);
    const std::string bytes = rt::write_relit_data(data);
    const fs::Written written = fs::write_text(path, bytes);
    // the summary (tools/game/relit_bake.ts reads these lines)
    std::size_t used = 0;
    std::size_t lamp_lit = 0;
    double lamp_light = 0.0;
    std::vector<float> tints;
    for (const rt::RelitDataMesh& mesh : data.meshes) {
        for (const rt::RelitVertex& vertex : mesh.vertices) {
            used += vertex.tint != rt::Rgbf{} ? 1 : 0;
            if (vertex.tint != rt::Rgbf{}) {
                tints.push_back(luma(vertex.tint));
            }
            lamp_lit += luma(vertex.lamp) > kLeastColour ? 1 : 0;
            lamp_light += luma(vertex.lamp);
        }
    }
    float median_tint = 0.0F;
    if (!tints.empty()) {
        const auto middle = tints.begin() + static_cast<std::ptrdiff_t>(tints.size() / 2);
        std::ranges::nth_element(tints, middle);
        median_tint = *middle;
    }
    const rt::RelitModel& model = data.model;
    get_log("relit bake: level %08x (%s) - %s: %zu meshes (%zu not relightable), %zu vertices; the baked sun %s "
            "(confidence %.2f, margin %.2f); %zu lamps; %zu rays traced (%zu for the lamps) in %.0f ms, the fit and the "
            "tints %.0f ms",
            scene, level_name().c_str(), rt::relit_verdict_name(data.verdict), meshes.size(), refused, vertices,
            data.sun.found ? "found" : "none", static_cast<double>(data.sun.confidence),
            static_cast<double>(data.sun_margin), lamps.size(), stats.rays + lamp_stats.rays, lamp_stats.rays, trace_ms,
            fit_ms);
    get_log("relit bake: the model sun %.3f %.3f %.3f sky %.3f %.3f %.3f bounce %.3f %.3f %.3f lamps %.3f %.3f %.3f "
            "(%s; margin %.2f, median error %.4f, %zu vertices fitted, %zu dead sun channels); the median tint %.3f; the "
            "lamps light %zu vertices (mean L %.3f); the bounce of the relit light %.0f ms",
            static_cast<double>(std::get<0>(model.sun)), static_cast<double>(std::get<1>(model.sun)),
            static_cast<double>(std::get<2>(model.sun)), static_cast<double>(std::get<0>(model.sky)),
            static_cast<double>(std::get<1>(model.sky)), static_cast<double>(std::get<2>(model.sky)),
            static_cast<double>(std::get<0>(model.bounce)), static_cast<double>(std::get<1>(model.bounce)),
            static_cast<double>(std::get<2>(model.bounce)), static_cast<double>(std::get<0>(model.lamps)),
            static_cast<double>(std::get<1>(model.lamps)), static_cast<double>(std::get<2>(model.lamps)),
            model.sunless ? "no sun in the bake" : "the bake's sun",
            static_cast<double>(data.model_margin), static_cast<double>(model.error), model.vertices,
            model.dead_channels, static_cast<double>(median_tint), lamp_lit,
            lamp_lit > 0 ? lamp_light / static_cast<double>(lamp_lit) : 0.0, bounce_ms);
    std::array<std::size_t, 3> shapes{};
    double lumens = 0.0;
    std::vector<float> kelvins;
    for (const rt::RelitLamp& lamp : data.lamp_list) {
        core::at(shapes, static_cast<std::size_t>(lamp.shape)) += 1;
        lumens += static_cast<double>(lamp.lumens);
        if (lamp.kelvin > 0.0F) {
            kelvins.push_back(lamp.kelvin);
        }
    }
    {
        // (reported) the brightest lamp: an outlier the solve made shows here
        std::size_t brightest = 0;
        for (std::size_t li = 1; li < data.lamp_list.size(); ++li) {
            if (luma(core::at(data.lamp_list, li).light) > luma(core::at(data.lamp_list, brightest).light)) {
                brightest = li;
            }
        }
        if (!data.lamp_list.empty()) {
            const rt::RelitLamp& lamp = core::at(data.lamp_list, brightest);
            get_log("relit bake: the brightest lamp %zu (kind %u, shape %u, %s) at %.0f %.0f %.0f, core %.0f: %.3f %.3f %.3f "
                    "in the unit, %.0f lm",
                    brightest, static_cast<unsigned>(lamp.kind), static_cast<unsigned>(lamp.shape),
                    lamp.solved ? "told" : "its own surface's", static_cast<double>(rt::x_of(lamp.position)),
                    static_cast<double>(rt::y_of(lamp.position)), static_cast<double>(rt::z_of(lamp.position)),
                    static_cast<double>(lamp.core), static_cast<double>(std::get<0>(lamp.light)),
                    static_cast<double>(std::get<1>(lamp.light)), static_cast<double>(std::get<2>(lamp.light)),
                    static_cast<double>(lamp.lumens));
        }
    }
    float median_kelvin = 0.0F;
    if (!kelvins.empty()) {
        const auto middle = kelvins.begin() + static_cast<std::ptrdiff_t>(kelvins.size() / 2);
        std::ranges::nth_element(kelvins, middle);
        median_kelvin = *middle;
    }
    get_log("relit bake: the lamps' solve - %zu %zu %zu edges, %zu of %zu lamps told by the bake (the rest by their own "
            "surfaces' radiance; %zu told dark - %zu of them none at all, %zu under the daylight where they light (off by their own switch), %zu leaving their pools darker than any unlit surface; the fit's noise %.3f; the lit ones %.3f %.3f %.3f x their surfaces' light, median, %.2f at the "
            "95th percentile); shapes: %zu points, %zu panels, %zu lines; %.0f lumens in all, "
            "median %.0f K; %zu emissive triangles; the bake's light the model doesn't explain dropped - brighter at %zu "
            "vertices, darker (its shadows the rays don't see) at %zu, told across meshes at %zu",
            core::at(model.lamp_votes, 0), core::at(model.lamp_votes, 1), core::at(model.lamp_votes, 2),
            model.lamps_solved, data.lamp_list.size(), model.lamps_dark, model.lamps_zero, model.lamps_off_in_bake, model.lamps_implausible, static_cast<double>(model.lamp_noise), static_cast<double>(std::get<0>(model.lamp_median)),
            static_cast<double>(std::get<1>(model.lamp_median)), static_cast<double>(std::get<2>(model.lamp_median)),
            static_cast<double>(std::get<1>(model.lamp_high)),
            core::at(shapes, 0), core::at(shapes, 1), core::at(shapes, 2), lumens, static_cast<double>(median_kelvin),
            data.emissive.size(), model.brighter, model.darker, model.across);
    // (lighting round A) the level's lighting state as its own data shows it - never assumed: its time-of-day state at
    // load, its lamps, how bright its brightest surfaces are against its median, the baked sun's confidence - the
    // evidence the bake report's table gives for each level (tools/game/relit_bake.ts)
    {
        const char* state_name = "unknown";
        if (const gfx::CtodManager* manager = gfx::tod_manager(); manager != nullptr) {
            const uint32_t own = manager->current_state();
            state_name = own == gfx::kMorning     ? "morning"
                         : own == gfx::kAfternoon ? "afternoon"
                         : own == gfx::kEvening   ? "evening"
                         : own == gfx::kNight     ? "night"
                                                  : "other";
        }
        std::vector<float> lumas;
        for (const rt::RelitMesh& mesh : meshes) {
            for (const uint32_t colour : mesh.colours) {
                lumas.push_back(luma(rt::colour_light(colour)));
            }
        }
        float median = 0.0F;
        float brightest = 0.0F;
        if (!lumas.empty()) {
            constexpr double kBrightShare = 0.95;
            const auto middle = lumas.begin() + static_cast<std::ptrdiff_t>(lumas.size() / 2);
            std::ranges::nth_element(lumas, middle);
            median = *middle;
            const auto top = lumas.begin() + static_cast<std::ptrdiff_t>(static_cast<double>(lumas.size() - 1) * kBrightShare);
            std::ranges::nth_element(lumas, top);
            brightest = *top;
        }
        // (round A2) indoors or out, from the rays: the median share of the sky the level's upward faces see (a floor
        // under a roof sees none of it) - the fit's (open_sky)
        const std::shared_ptr<const rt::LevelEmitters> found = rt::level_emitters();
        // (round A3 - no "dark level": the lamps by the bake's light and their own daylight) the lamps the bake shows
        // lit, those whose surfaces see no sky (on whenever), those on at the level's own time; the candidates found
        const rt::DayLight own_day = own_day_light(data.sun);
        const std::vector<float> on = own_lamps_on(data.lamp_list, own_day);
        std::size_t shining = 0;
        std::size_t always = 0;
        std::size_t on_now = 0;
        for (std::size_t li = 0; li < data.lamp_list.size(); ++li) {
            const rt::RelitLamp& lamp = core::at(data.lamp_list, li);
            if (!(light_luma(lamp.light) > 0.0F)) {
                continue;
            }
            ++shining;
            always += lamp.switch_sky >= rt::kLampAlwaysOn ? 1 : 0;
            on_now += core::at(on, li) >= 0.5F ? 1 : 0;
        }
        get_log("relit bake: evidence - the level's own time of day %s; the baked sun's confidence %.2f (%s, %.0f degrees "
                "up); vertex light median %.3f, brightest 5%% from %.3f (x%.2f); %zu glow, %zu self-lit and %zu textured "
                "triangles (%zu glow meshes), %zu lamps - %zu shining, %zu lit in the bake, %zu always on (no sky where "
                "they light), %zu on at its own time (its open sky %.4f); upward faces see %.2f of the sky (median); the "
                "bake's sky %.3f %.3f %.3f in the global unit, its daylight %.2f of the physical",
                state_name, static_cast<double>(data.sun.confidence), data.sun.found ? "a sun" : "no sun",
                static_cast<double>(sun_elevation), static_cast<double>(median), static_cast<double>(brightest),
                median > 0.0F ? static_cast<double>(brightest / median) : 0.0,
                found != nullptr ? found->glow_triangles : 0U, found != nullptr ? found->self_lit_triangles : 0U,
                found != nullptr ? found->textured_triangles : 0U, found != nullptr ? found->glow_meshes : 0U,
                lamps.size(), shining, data.model.lamps_solved, always, on_now,
                static_cast<double>(light_luma(own_day.sky_light)), static_cast<double>(floors_open),
                static_cast<double>(std::get<0>(bake_sky)), static_cast<double>(std::get<1>(bake_sky)),
                static_cast<double>(std::get<2>(bake_sky)), static_cast<double>(data.model.daylight_ratio));
    }
    get_log("relit bake: %s %s (%zu KB, content %016llx) - %.0f ms in all", written.written && written.closed ? "wrote" : "couldn't write",
            path.c_str(), bytes.size() / kKilobyte, static_cast<unsigned long long>(data.content), ms_since(start));
}

} // namespace

bool requested() { return platform::batch_run(); }

rt::DayLight level_own_day(const rt::BakedSun& sun) { return own_day_light(sun); }

std::string level_name()
{
    // (-relit-bake <level>: the word after it)
    const std::string& line = sys::startup_command_line();
    const std::string flag = "-relit-bake ";
    const std::size_t at = line.find(flag);
    if (at == std::string::npos) {
        return {};
    }
    const std::size_t from = line.find_first_not_of(' ', at + flag.size());
    if (from == std::string::npos) {
        return {};
    }
    const std::size_t to = line.find_first_of(" \"", from);
    return line.substr(from, to == std::string::npos ? std::string::npos : to - from);
}

config::IniValues overrides()
{
    // (the level's bake in the buffers: no relit mode, no lighting pass to turn it on, the level's own time; ray tracing
    // for the rays; nothing the bake doesn't need - the geometry settings left as the user has them)
    const std::array<std::pair<const char*, const char*>, 17> kGraphics{{
        {"renderer", "vulkan"},
        {"ray_tracing", "1"},
        {"rt_relit", "0"},
        {"rt_shadows", "0"},
        {"rt_ao", "0"},
        {"rt_lights", "0"},
        {"tod", "level"},
        {"tod_cycle", "off"},
        {"tod_levels", ""},
        {"rt_sun_source", "level"},
        {"frame_generation", "off"},
        {"upscaler", "off"},
        {"compare_split", "0"},
        {"vulkan_validation", "0"},
        {"rt_style", "off"},
        {"Fullscreen", "0"},
        {"Windowed", "1"},
    }};
    config::IniValues values;
    for (const auto& [key, value] : kGraphics) {
        values[config::ini_slot("Graphics", key)] = value;
    }
    values[config::ini_slot("features", "mute_audio")] = "1";
    values[config::ini_slot("features", "skip_intro_movies")] = "1";
    return values;
}

void step(std::span<const rt_relit_world::WorldMesh> world, bool ready, uint64_t scene_key, rt::RayTracing& tracer)
{
    State& st = state();
    if (st.done) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!st.waiting) {
        st.waiting = true;
        st.since = now;
    }
    const double waited = std::chrono::duration<double>(now - st.since).count();
    // Only the level asked for is baked: the front end's world (the main menu is a level of its own, its scene up from
    // the first frames) or any other is waited past - game/front_end/start_level.h starts the level from the menu.
    const uint32_t load = requested_load();
    const uint32_t running = running_load();
    if (running != load) {
        st.steady = 0;
        if (waited > kLevelWaitSeconds) {
            get_log("relit bake: FAILED - %s never came up: %s still running after %.0f s (the log's \"start level:\" "
                    "line says why) - nothing written",
                    load_name(load).c_str(), running != 0 ? load_name(running).c_str() : "no level", waited);
            give_up(st, platform::kBatchNoLevel);
        }
        return;
    }
    if (!st.level_seen) {
        st.level_seen = true;
        st.named_scene = named_scene(load);
        get_log("relit bake: %s is up (%.0f s) - its world (scene %08x) baked once it has held for %u frames",
                load_name(load).c_str(), waited, st.named_scene, kSteadyFrames);
    }
    const bool emitters = rt::level_emitters() != nullptr && rt::level_emitters()->serial != 0;
    if (!ready || world.empty() || !emitters || rt_relit_world::level_scene() == 0) {
        st.steady = 0;
        if (waited > kLevelWaitSeconds) {
            get_log("relit bake: FAILED - %s came up but its world wasn't ready after %.0f s - nothing written",
                    load_name(load).c_str(), waited);
            give_up(st, platform::kBatchNoLevel);
        }
        return;
    }
    // (the world is the level's own - the scene its level structure names -, never another scene's data under its name:
    // a world of another scene, the menu's still up as the level begins to load, is waited past)
    const uint32_t scene = rt_relit_world::level_scene();
    if (const uint32_t named = st.named_scene; named != 0 && scene != named) {
        st.steady = 0;
        if (waited > kLevelWaitSeconds) {
            get_log("relit bake: FAILED - %s is up but its world is scene %08x, not %08x (its level structure's) after "
                    "%.0f s - nothing written",
                    load_name(load).c_str(), scene, named, waited);
            give_up(st, platform::kBatchOtherScene);
        }
        return;
    }
    if (scene_key != st.key) {
        st.key = scene_key;
        st.steady = 0;
    }
    if (++st.steady < kSteadyFrames) {
        return;
    }
    st.done = true;
    bake(world, tracer);
    platform::end_batch_run(platform::kBatchMade);
}

} // namespace render::relit_bake
