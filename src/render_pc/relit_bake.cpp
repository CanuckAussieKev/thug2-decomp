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
#include "engine/gfx/relit_data.h"
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
// self-lit or textured emitter the bake told lit) - found by position among the meshes' vertices (the same bytes).
std::vector<std::vector<rt::RelitEmitting>> emitting_vertices(std::span<const rt::RelitMesh> meshes,
                                                              std::span<const rt::RelitLamp> lamps)
{
    std::vector<std::vector<rt::RelitEmitting>> out(meshes.size());
    const std::shared_ptr<const rt::LevelEmitters> found = rt::level_emitters();
    if (found == nullptr) {
        return out;
    }
    const auto key_of = [](const rt::Vec3& at) {
        uint64_t hash = 0xCBF29CE484222325ULL;
        for (const float value : at) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            hash = (hash ^ bits) * 0x100000001B3ULL;
        }
        return hash;
    };
    std::unordered_map<uint64_t, uint32_t> corners; // a corner's position -> its lamp
    for (std::size_t ti = 0; ti < found->triangles.size() && ti < found->triangle_emitter.size(); ++ti) {
        const uint32_t lamp = core::at(found->triangle_emitter, ti);
        const rt::EmitterTriangle& triangle = core::at(found->triangles, ti);
        if (lamp >= lamps.size() || !rt::candidate_kind(triangle.kind) || !core::at(lamps, lamp).solved) {
            continue;
        }
        for (const rt::Vec3& corner : triangle.corners) {
            corners.emplace(key_of(corner), lamp);
        }
    }
    if (corners.empty()) {
        return out;
    }
    for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
        const rt::RelitMesh& mesh = core::at(meshes, mi);
        for (std::size_t vi = 0; vi < mesh.positions.size(); ++vi) {
            const auto hit = corners.find(key_of(core::at(mesh.positions, vi)));
            if (hit != corners.end()) {
                core::at(out, mi).push_back({.vertex = static_cast<uint32_t>(vi), .lamp = hit->second});
            }
        }
    }
    return out;
}

// (round A2) The median share of the sky the level's upward faces see (their traced K): under kRelitIndoorSky the
// level is roofed - indoors.
float open_sky(std::span<const rt::RelitVertices> light)
{
    constexpr float kUpward = 0.7F;
    std::vector<float> open;
    for (const rt::RelitVertices& vertices : light) {
        for (const rt::RelitVertex& vertex : vertices) {
            if (vertex.used && rt::y_of(vertex.normal) >= kUpward) {
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
    for (const rt_relit_world::WorldMesh& mesh : world) {
        rt_relit_world::BakeMesh one;
        if (!rt_relit_world::read_world_mesh(mesh, &one)) {
            ++refused;
            continue;
        }
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
        rt::RelitModel model = rt::fit_relit_model(meshes, light, up, true);
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
        const std::vector<std::array<float, 4>> candidates = data.sun.candidates;
        for (const std::array<float, 4>& candidate : candidates) {
            const rt::Vec3 toward{std::get<0>(candidate), std::get<1>(candidate), std::get<2>(candidate)};
            rt::trace_relit_sun(meshes, {}, toward, params, tracer, &light, &stats);
            for (rt::RelitVertices& traced_vertices : light) {
                for (rt::RelitVertex& vertex : traced_vertices) {
                    vertex.sun_baked = vertex.sun;
                }
            }
            const rt::RelitModel fitted = rt::fit_relit_model(meshes, light, toward, false);
            const float error = fitted.valid ? rt::relit_edge_error(meshes, light, fitted) : 1.0F;
            const float share = rt::relit_sun_share(fitted);
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
                pick.suns.assign(light.size(), {});
                for (std::size_t mi = 0; mi < light.size(); ++mi) {
                    for (const rt::RelitVertex& vertex : core::at(light, mi)) {
                        core::at(pick.suns, mi).push_back(vertex.sun);
                    }
                }
            }
        }
        // (the level's own data: a night state's bake shows the night - its directional light the moon's, not a sun;
        // a roofed level's floors see too little of the sky for its bake's sun to be told - daylight reaches it through
        // its openings, traced, whatever the bake)
        const float floors_sky = open_sky(light);
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
        if (!sunless) {
            set_suns(&pick.suns);
            model = pick.model;
        } else {
            set_suns(nullptr);
        }
        trace_ms = ms_since(traced);
        const auto fitted = std::chrono::steady_clock::now();
        // (round A2) each lamp's intensity solved from the bake (no level-wide c); (round A3) in every bake - the bake's
        // light tells which were on (told above the fit's noise), no "dark level" or lamps-on guess
        {
            const rt::DayLight bake_day = own_day_light(data.sun);
            rt::solve_relit_lamps(meshes, light, links, lamps, &model,
                                  {.unit_lux = rt::unit_illuminance(),
                                   .sky = light_luma(bake_day.sky_light),
                                   .lamps = bake_day.lamps});
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
                        li, lamp.kind == rt::EmitterKind::kTextured ? "textured" : "self-lit",
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
        // the lamps in physical units (a path tracer's: candela, lumens, colour temperature) and the emissive triangles
        // with their lamps' radiance
        for (rt::RelitLamp& lamp : lamps) {
            rt::relit_lamp_photometry(&lamp, rt::unit_illuminance());
        }
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
                    emissive.radiance = rt::scaled(core::at(lamps, lamp).light, kLambertian);
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
                for (std::size_t vi = 0; vi < mesh.colours.size() && vi < lit.size(); ++vi) {
                    core::at(mesh.colours, vi) = rt::relit_colour(model, core::at(lit, vi), own);
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
        const std::vector<std::vector<rt::RelitEmitting>> emitting = emitting_vertices(meshes, lamps);
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
        // under a roof sees none of it)
        std::vector<float> open;
        for (const rt::RelitDataMesh& mesh : data.meshes) {
            for (const rt::RelitVertex& vertex : mesh.vertices) {
                constexpr float kUpward = 0.7F;
                if (vertex.used && rt::y_of(vertex.normal) >= kUpward) {
                    open.push_back(vertex.sky);
                }
            }
        }
        float open_sky = 0.0F;
        if (!open.empty()) {
            const auto middle = open.begin() + static_cast<std::ptrdiff_t>(open.size() / 2);
            std::ranges::nth_element(open, middle);
            open_sky = *middle;
        }
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
                static_cast<double>(light_luma(own_day.sky_light)), static_cast<double>(open_sky),
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
