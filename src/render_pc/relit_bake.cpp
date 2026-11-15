// NOT ORIGINAL - the relit bake: see relit_bake.h.
#include "render_pc/relit_bake.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/config/settings_model.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/relit_data.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
#include "platform/platform_config.h"
#include "platform/process_flags.h"
#include "platform/run_log.h"
#include "platform/sdl_events.h"
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
};

State& state()
{
    static core::NoDestroy<State> made;
    return made.get();
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
    double trace_ms = 0.0;
    double fit_ms = 0.0;
    if (!data.sun.found) {
        data.verdict = rt::RelitVerdict::kNoSun;
    } else {
        const rt::RelitParams params{.sky_rays = rt::kRelitBakeSkyRays,
                                     .sun_rays = rt::kRelitBakeSunRays,
                                     .sun_radius_degrees = rt::kRelitSunRadiusDegrees,
                                     .detail = 1.0F,
                                     .keep_sky = false};
        const auto traced = std::chrono::steady_clock::now();
        rt::trace_relit_light(meshes, {}, data.sun.toward, params, tracer, &light, &stats);
        rt::trace_relit_lamps(meshes, {}, lamps, tracer, &light, &lamp_stats);
        trace_ms = ms_since(traced);
        const auto fitted = std::chrono::steady_clock::now();
        rt::RelitModel model = rt::fit_relit_model(meshes, light, data.sun.toward);
        if (model.valid) {
            rt::fit_relit_lamps(meshes, light, &model);
        }
        data.model_margin = luma(model.sun) / rt::kRelitMinSunLight;
        if (model.valid) {
            rt::decompose_relit(meshes, {}, params, &model, &light, true);
            data.verdict = rt::RelitVerdict::kRelit;
        } else {
            data.verdict = rt::RelitVerdict::kNoModel;
        }
        fit_ms = ms_since(fitted);
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
    std::size_t missed = 0;
    std::size_t lamp_lit = 0;
    double residual = 0.0;
    double lamp_light = 0.0;
    for (const rt::RelitDataMesh& mesh : data.meshes) {
        for (const rt::RelitVertex& vertex : mesh.vertices) {
            used += vertex.tint != rt::Rgbf{} ? 1 : 0;
            missed += vertex.sun_missed > 0.0F ? 1 : 0;
            const rt::Rgbf lamp = rt::scaled(vertex.lamp, luma(data.model.lamps));
            lamp_lit += luma(lamp) > kLeastColour ? 1 : 0;
            lamp_light += luma(lamp);
            residual += luma(vertex.residual);
        }
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
            "scale %.3f (margin %.2f, median error %.4f, %zu vertices fitted, %zu dead sun channels); the bake's sun "
            "the rays missed at %zu vertices, lamp light at %zu (mean %.3f), residual mean %.4f",
            static_cast<double>(std::get<0>(model.sun)), static_cast<double>(std::get<1>(model.sun)),
            static_cast<double>(std::get<2>(model.sun)), static_cast<double>(std::get<0>(model.sky)),
            static_cast<double>(std::get<1>(model.sky)), static_cast<double>(std::get<2>(model.sky)),
            static_cast<double>(std::get<0>(model.bounce)), static_cast<double>(std::get<1>(model.bounce)),
            static_cast<double>(std::get<2>(model.bounce)), static_cast<double>(std::get<0>(model.lamps)),
            static_cast<double>(std::get<1>(model.lamps)), static_cast<double>(std::get<2>(model.lamps)),
            static_cast<double>(model.scale), static_cast<double>(data.model_margin), static_cast<double>(model.error),
            model.vertices, model.dead_channels, missed, lamp_lit,
            lamp_lit > 0 ? lamp_light / static_cast<double>(lamp_lit) : 0.0,
            used > 0 ? residual / static_cast<double>(used) : 0.0);
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
    // (what the bake needs found for each world: its baked sun and its emitters)
    rt::set_baked_sun_wanted(true);
    rt::set_emitters_wanted(true);
    if (st.done) {
        return;
    }
    const bool emitters = rt::level_emitters() != nullptr && rt::level_emitters()->serial != 0;
    if (!ready || world.empty() || !emitters || rt_relit_world::level_scene() == 0) {
        st.steady = 0;
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
    os_layer::request_quit();
}

} // namespace render::relit_bake
