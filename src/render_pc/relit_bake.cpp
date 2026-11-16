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
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
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

// The level's lamps as the lamps pass shines them: the emitters found in the level (glows; self-lit surfaces in a dark
// level) and the point lights on now.
std::vector<rt::RelitLamp> level_lamps()
{
    std::vector<rt::RelitLamp> lamps;
    if (const std::shared_ptr<const rt::LevelEmitters> found = rt::level_emitters(); found != nullptr) {
        for (const rt::Emitter& emitter : found->emitters) {
            lamps.push_back({.position = emitter.position,
                             .size = emitter.size,
                             .light = rt::scaled(emitter.colour, emitter.intensity),
                             .core = emitter.core,
                             .reach = emitter.reach});
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
                         .reach = light.radius});
    }
    return lamps;
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
    data.sun_margin = data.sun.confidence / rt::kMinConfidence;
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
    const std::vector<rt::RelitLamp> lamps = level_lamps();
    data.lamps = static_cast<uint32_t>(lamps.size());
    data.lamp_list = lamps;
    double trace_ms = 0.0;
    double fit_ms = 0.0;
    double bounce_ms = 0.0;
    {
        // (lighting round A) every bake is taken apart: with the sun its fit found, or - a night or indoor bake, no sun
        // in it - by its sky, bounce and lamps alone (the vertices' sun share 0; their rays traced straight up only for
        // the shape of the trace)
        const bool sunless = !data.sun.found;
        const rt::Vec3 toward = sunless ? rt::Vec3{0.0F, 1.0F, 0.0F} : data.sun.toward;
        const rt::RelitParams params{.sky_rays = rt::kRelitBakeSkyRays,
                                     .sun_rays = rt::kRelitBakeSunRays,
                                     .sun_radius_degrees = rt::kRelitSunRadiusDegrees,
                                     .detail = 1.0F,
                                     .keep_sky = false};
        const auto traced = std::chrono::steady_clock::now();
        rt::trace_relit_light(meshes, {}, toward, params, tracer, &light, &stats);
        if (sunless) {
            for (rt::RelitVertices& traced_vertices : light) {
                for (rt::RelitVertex& vertex : traced_vertices) {
                    vertex.sun = 0.0F;
                    vertex.sun_baked = 0.0F;
                }
            }
        }
        rt::trace_relit_lamps(meshes, {}, lamps, tracer, &light, &lamp_stats);
        trace_ms = ms_since(traced);
        const auto fitted = std::chrono::steady_clock::now();
        rt::RelitModel model = rt::fit_relit_model(meshes, light, toward, sunless);
        if (model.valid) {
            rt::fit_relit_lamps(meshes, light, &model);
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
        if (model.valid) {
            // the bounce of the relit light at the level's own time (the hits' colours the relit ones: tint x light),
            // not the bake's colours - nothing of the bake's light is kept
            const auto bounced = std::chrono::steady_clock::now();
            const std::shared_ptr<const rt::LevelEmitters> found = rt::level_emitters();
            const rt::RelitLighting own{.toward = model.toward,
                                        .direct_scale = sunless ? rt::Rgbf{} : rt::Rgbf{1.0F, 1.0F, 1.0F},
                                        .lamps = found != nullptr && found->dark ? 1.0F : 0.0F};
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
            stats.rays += bounce_stats.rays;
            bounce_ms = ms_since(bounced);
        }
        data.model = model;
        for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
            data.meshes.push_back({.content = core::at(contents, mi), .vertices = std::move(core::at(light, mi))});
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
    get_log("relit bake: the lamps' fit - %zu %zu %zu edge votes, their medians %.4f %.4f %.4f; the bake's light the model "
            "doesn't explain dropped - brighter at %zu vertices, darker (its shadows the rays don't see) at %zu, told "
            "across meshes at %zu",
            core::at(model.lamp_votes, 0), core::at(model.lamp_votes, 1), core::at(model.lamp_votes, 2),
            static_cast<double>(std::get<0>(model.lamp_median)), static_cast<double>(std::get<1>(model.lamp_median)),
            static_cast<double>(std::get<2>(model.lamp_median)), model.brighter, model.darker, model.across);
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
        const std::shared_ptr<const rt::LevelEmitters> found = rt::level_emitters();
        get_log("relit bake: evidence - the level's own time of day %s; the baked sun's confidence %.2f (%s); vertex light "
                "median %.3f, brightest 5%% from %.3f (x%.2f); %zu glow and %zu self-lit triangles, %zu lamps (%s level)",
                state_name, static_cast<double>(data.sun.confidence), data.sun.found ? "a sun" : "no sun",
                static_cast<double>(median), static_cast<double>(brightest),
                median > 0.0F ? static_cast<double>(brightest / median) : 0.0,
                found != nullptr ? found->glow_triangles : 0U, found != nullptr ? found->self_lit_triangles : 0U,
                lamps.size(), found != nullptr && found->dark ? "a dark" : "a lit");
    }
    get_log("relit bake: %s %s (%zu KB, content %016llx) - %.0f ms in all", written.written && written.closed ? "wrote" : "couldn't write",
            path.c_str(), bytes.size() / kKilobyte, static_cast<unsigned long long>(data.content), ms_since(start));
}

} // namespace

bool requested() { return platform::batch_run(); }

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
