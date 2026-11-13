#include "app/renderer_choice.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <sstream>
#include <string>

#include "engine/gfx/render_device.h"
#include "engine/gfx/ui_backend.h"
#include "engine/sys/editor_mode.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "platform/sdl_window.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "gpu_d3d9/d3d9_context.h"
#include "render_pc/d3d9_device.h"
#include "gpu_d3d9/d3d9_ui_backend.h"
#include "gpu_d3d9/gpu_d3d9.h"
#ifdef THUG2_VULKAN
#include "gpu_vulkan/gpu_vulkan.h"
#endif

namespace app {

namespace {

#ifdef THUG2_VULKAN
constexpr bool kVulkanBuilt = true;
#else
constexpr bool kVulkanBuilt = false;
#endif
// The Vulkan Gpu draws (src/gpu_vulkan, docs/design/vulkan.md V7): the default renderer when this machine can.
constexpr bool kVulkanDeviceExists = kVulkanBuilt;

void install_direct3d9()
{
    render::install_device(render::d3d9::device());
    render::install_gpu(render::d3d9::gpu());
    render::install_gpu_context(render::d3d9::gpu_context());
    render::install_ui_backend(render::d3d9::ui_backend());
}

#ifdef THUG2_VULKAN
// A [Graphics] number with a fraction (`fallback` when it's missing or not a number).
float ini_float(const char* key, float fallback)
{
    constexpr std::size_t kLongest = 32; // (characters of a number)
    std::array<char, kLongest> text{};
    build_info::fetch_text(build_info::kGraphics, key, "", text.data(), text.size());
    char* end = nullptr;
    const double value = std::strtod(text.data(), &end);
    return end == text.data() || !std::isfinite(value) ? fallback : static_cast<float>(value);
}

// [Graphics] rt_shadow_sun: bake (the default: the level's baked sun), light (the frame's first directional light) or
// x,y,z (a direction toward the sun); anything else is the default, logged.
void read_rt_shadow_sun(render::vulkan::RtShadowSettings* settings)
{
    constexpr std::size_t kLongest = 64;
    std::array<char, kLongest> text{};
    build_info::fetch_text(build_info::kGraphics, "rt_shadow_sun", "bake", text.data(), text.size());
    const std::string value(text.data());
    if (value == "light") {
        settings->sun = render::vulkan::RtShadowSettings::SunSource::kLight;
        return;
    }
    std::array<float, 3> toward{};
    std::size_t read = 0;
    std::istringstream numbers(value);
    std::string number;
    while (read < toward.size() && std::getline(numbers, number, ',')) {
        char* end = nullptr;
        const double parsed = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || !std::isfinite(parsed)) {
            break;
        }
        toward.at(read++) = static_cast<float>(parsed);
    }
    if (read == toward.size()) {
        settings->sun = render::vulkan::RtShadowSettings::SunSource::kFixed;
        settings->fixed_sun = toward;
    } else if (!value.empty() && value != "bake") {
        get_log("[Graphics] rt_shadow_sun=%s isn't bake, light or x,y,z - the baked sun used", value.c_str());
    }
}

// [Graphics] rt_sun_colour: auto (the default: the sky model's colour for the sun's elevation) or r,g,b (0 - 1 each);
// anything else is the default, logged.
void read_rt_sun_colour(render::vulkan::RtSunSettings* settings)
{
    constexpr std::size_t kLongest = 64;
    std::array<char, kLongest> text{};
    build_info::fetch_text(build_info::kGraphics, "rt_sun_colour", "auto", text.data(), text.size());
    const std::string value(text.data());
    std::array<float, 3> colour{};
    std::size_t read = 0;
    std::istringstream numbers(value);
    std::string number;
    while (read < colour.size() && std::getline(numbers, number, ',')) {
        char* end = nullptr;
        const double parsed = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || !std::isfinite(parsed)) {
            break;
        }
        colour.at(read++) = static_cast<float>(parsed);
    }
    if (read == colour.size()) {
        settings->colour_set = true;
        settings->colour = colour;
    } else if (!value.empty() && value != "auto") {
        get_log("[Graphics] rt_sun_colour=%s isn't auto or r,g,b - the sky model's colour used", value.c_str());
    }
}
#endif

// The Vulkan backend: render_pc's device over Vulkan's Gpu and GpuContext, and its UI backend (src/gpu_vulkan; the
// ImGui parts have none on Vulkan yet). The shader and pipeline caches are kept beside the exe.
void install_vulkan()
{
#ifdef THUG2_VULKAN
    render::vulkan::configure({.device_index = build_info::get_int(build_info::kGraphics, "vulkan_device", -1),
                               .cache_folder = build_info::executable_directory(),
                               .validation = build_info::fetch_flag(build_info::kGraphics, "vulkan_validation", false),
                               .ray_tracing = build_info::fetch_flag(build_info::kGraphics, "ray_tracing", false)});
    // NOT ORIGINAL - ray-traced sun shadows (gpu_vulkan/vulkan_rt_shadows.h): off unless asked for, and only with
    // ray_tracing=1 on a device that has it
    render::vulkan::RtShadowSettings shadows{
         .on = build_info::fetch_flag(build_info::kGraphics, "rt_shadows", false),
         .strength = ini_float("rt_shadow_strength", render::vulkan::RtShadowSettings{}.strength),
         .softness = ini_float("rt_shadow_softness", render::vulkan::RtShadowSettings{}.softness),
         .rays = static_cast<unsigned>(std::max(1, build_info::get_int(build_info::kGraphics, "rt_shadow_rays", 4))),
         .objects = build_info::fetch_flag(build_info::kGraphics, "rt_shadow_objects", false),
         .covered = build_info::fetch_flag(build_info::kGraphics, "rt_shadow_covered", false),
         .sky_rays = static_cast<unsigned>(
             std::max(1, build_info::get_int(build_info::kGraphics, "rt_shadow_sky_rays",
                                             static_cast<int>(render::vulkan::kRtShadowSkyRays)))),
         .match_bake = build_info::fetch_flag(build_info::kGraphics, "rt_shadow_match_bake", true)};
    read_rt_shadow_sun(&shadows);
    render::vulkan::set_rt_shadow_settings(shadows);
    // NOT ORIGINAL - ray-traced ambient occlusion (gpu_vulkan/vulkan_rt_effects.h): likewise
    render::vulkan::set_rt_ao_settings(
        {.on = build_info::fetch_flag(build_info::kGraphics, "rt_ao", false),
         .strength = ini_float("rt_ao_strength", render::vulkan::RtAoSettings{}.strength),
         .radius = ini_float("rt_ao_radius", render::vulkan::RtAoSettings{}.radius),
         .rays = static_cast<unsigned>(
             std::max(1, build_info::get_int(build_info::kGraphics, "rt_ao_rays",
                                             static_cast<int>(render::vulkan::RtAoSettings{}.rays)))),
         .denoise = build_info::fetch_flag(build_info::kGraphics, "rt_ao_denoise", true)});
    // NOT ORIGINAL - one-bounce ray-traced indirect light (gpu_vulkan/vulkan_rt_effects.h): likewise
    render::vulkan::set_rt_gi_settings(
        {.on = build_info::fetch_flag(build_info::kGraphics, "rt_gi", false),
         .strength = ini_float("rt_gi_strength", render::vulkan::RtGiSettings{}.strength),
         .distance = ini_float("rt_gi_distance", render::vulkan::RtGiSettings{}.distance),
         .rays = static_cast<unsigned>(
             std::max(1, build_info::get_int(build_info::kGraphics, "rt_gi_rays",
                                             static_cast<int>(render::vulkan::RtGiSettings{}.rays)))),
         .denoise = build_info::fetch_flag(build_info::kGraphics, "rt_gi_denoise", true)});
    // NOT ORIGINAL - ray-traced reflections (gpu_vulkan/vulkan_rt_reflections.h): likewise
    render::vulkan::set_rt_reflection_settings(
        {.on = build_info::fetch_flag(build_info::kGraphics, "rt_reflections", false),
         .strength = ini_float("rt_reflection_strength", render::vulkan::RtReflectionSettings{}.strength)});
    // NOT ORIGINAL - HDR rendering and tone mapping (gpu_vulkan/vulkan_hdr.h): off unless asked for; no ray tracing
    // needed. The grading table's path is the game folder's unless it is a full one.
    {
        constexpr std::size_t kLongestPath = 260;
        std::array<char, kLongestPath> lut{};
        build_info::fetch_text(build_info::kGraphics, "rt_grade_lut", "", lut.data(), lut.size());
        std::string lut_path(lut.data());
        const bool full = lut_path.find(':') == 1 || lut_path.starts_with('\\') || lut_path.starts_with('/');
        if (!lut_path.empty() && !full) {
            lut_path = build_info::executable_directory() + lut_path;
        }
        const render::vulkan::HdrSettings defaults;
        render::vulkan::set_hdr_settings(
            {.on = build_info::fetch_flag(build_info::kGraphics, "rt_hdr", false),
             .exposure = ini_float("rt_exposure", defaults.exposure),
             .tonemap = static_cast<unsigned>(std::max(0, build_info::get_int(build_info::kGraphics, "rt_tonemap", 0))),
             .saturation = ini_float("rt_saturation", defaults.saturation),
             .contrast = ini_float("rt_contrast", defaults.contrast),
             .white_point = ini_float("rt_white_point", defaults.white_point),
             .grade_lut = lut_path});
    }
    // NOT ORIGINAL - volumetric fog and light shafts (gpu_vulkan/vulkan_rt_fog.h): likewise
    render::vulkan::set_rt_fog_settings(
        {.on = build_info::fetch_flag(build_info::kGraphics, "rt_fog", false),
         .density = ini_float("rt_fog_density", render::vulkan::RtFogSettings{}.density),
         .height = ini_float("rt_fog_height", render::vulkan::RtFogSettings{}.height),
         .anisotropy = ini_float("rt_fog_anisotropy", render::vulkan::RtFogSettings{}.anisotropy),
         .steps = static_cast<unsigned>(std::max(
             1, build_info::get_int(build_info::kGraphics, "rt_fog_steps", static_cast<int>(render::vulkan::kRtFogSteps)))),
         .mode = build_info::get_int(build_info::kGraphics, "rt_fog_mode", 0) == 1 ? render::vulkan::RtFogMode::kShafts
                                                                                   : render::vulkan::RtFogMode::kReplace,
         .hdr = build_info::fetch_flag(build_info::kGraphics, "rt_hdr", false),
         .haze = build_info::fetch_flag(build_info::kGraphics, "rt_fog_haze", true),
         .haze_density = ini_float("rt_fog_haze_density", render::vulkan::RtFogSettings{}.haze_density),
         .haze_height = ini_float("rt_fog_haze_height", render::vulkan::RtFogSettings{}.haze_height)});
    // NOT ORIGINAL - the sun's disc and glare, screen-space rays, the bloom and the sky model
    // (gpu_vulkan/vulkan_rt_sun.h, vulkan_rt_sky.h): off unless asked for; the bloom needs no ray tracing
    {
        render::vulkan::RtSunSettings sun{
            .sun = build_info::fetch_flag(build_info::kGraphics, "rt_sun", false),
            .sun_size = ini_float("rt_sun_size", render::vulkan::RtSunSettings{}.sun_size),
            .bloom = build_info::fetch_flag(build_info::kGraphics, "rt_bloom", false),
            .bloom_strength = ini_float("rt_bloom_strength", render::vulkan::kRtBloomStrength),
            .shafts = build_info::fetch_flag(build_info::kGraphics, "rt_shafts", false),
            .shaft_strength = ini_float("rt_shaft_strength", render::vulkan::kRtShaftStrength),
            .sky_model = build_info::fetch_flag(build_info::kGraphics, "rt_sky_model", false),
            .lens_dirt = build_info::fetch_flag(build_info::kGraphics, "rt_lens_dirt", false),
            .hdr = build_info::fetch_flag(build_info::kGraphics, "rt_hdr", false)};
        read_rt_sun_colour(&sun);
        // [Graphics] rt_sun_place: game (the default: where the game shows its sun) or bake (the lighting's sun)
        constexpr std::size_t kLongest = 16;
        std::array<char, kLongest> place{};
        build_info::fetch_text(build_info::kGraphics, "rt_sun_place", "game", place.data(), place.size());
        sun.place_game = std::string(place.data()) != "bake";
        render::vulkan::set_rt_sun_settings(sun);
    }
    // NOT ORIGINAL - a modern sky: the atmosphere and volumetric clouds (gpu_vulkan/vulkan_rt_sky_pass.h): off unless
    // asked for; no ray tracing needed. rt_cloud_coverage: auto (the level's own sky) or 0 - 1.
    {
        constexpr std::size_t kLongest = 16;
        std::array<char, kLongest> coverage{};
        build_info::fetch_text(build_info::kGraphics, "rt_cloud_coverage", "auto", coverage.data(), coverage.size());
        float cover = render::vulkan::kRtCloudCoverageAuto;
        const std::string cover_text(coverage.data());
        if (!cover_text.empty() && cover_text != "auto") {
            char* end = nullptr;
            const double parsed = std::strtod(cover_text.c_str(), &end);
            if (end != cover_text.c_str() && std::isfinite(parsed)) {
                cover = static_cast<float>(parsed);
            } else {
                get_log("[Graphics] rt_cloud_coverage=%s isn't auto or 0 - 1 - auto used", cover_text.c_str());
            }
        }
        render::vulkan::set_rt_sky_settings(
            {.on = build_info::fetch_flag(build_info::kGraphics, "rt_sky", false),
             .match = ini_float("rt_sky_match", render::vulkan::kRtSkyMatch),
             .clouds = build_info::fetch_flag(build_info::kGraphics, "rt_clouds", false),
             .coverage = cover,
             .speed = ini_float("rt_cloud_speed", 1.0F),
             .quality = static_cast<unsigned>(std::max(0, build_info::get_int(build_info::kGraphics, "rt_cloud_quality", 1))),
             .cloud_shadows = build_info::fetch_flag(build_info::kGraphics, "rt_cloud_shadows", false),
             .hdr = build_info::fetch_flag(build_info::kGraphics, "rt_hdr", false)});
    }
    render::install_device(render::d3d9::device());
    render::install_gpu(render::vulkan::gpu());
    render::install_gpu_context(render::vulkan::gpu_context());
    render::install_ui_backend(render::vulkan::ui_backend());
#else
    install_direct3d9();
#endif
}

// Whether this machine can draw with Vulkan (asked before the window is made: a window for Vulkan needs its loader).
bool vulkan_usable()
{
#ifdef THUG2_VULKAN
    const int index = build_info::get_int(build_info::kGraphics, "vulkan_device", -1);
    const render::vulkan::Usable found = render::vulkan::usable(index);
    if (found.usable) {
        get_log("renderer: Vulkan on %s%s", found.device.c_str(), index >= 0 ? " (vulkan_device)" : "");
        return true;
    }
    get_log("renderer: Vulkan can't draw here - %s%s%s; using Direct3D 9 (renderer=d3d9 in [Graphics] chooses it)",
            found.device.c_str(), found.device.empty() ? "" : ": ", found.problem.c_str());
    return false;
#else
    return false;
#endif
}

// The key's value, lower-cased.
constexpr size_t kValueSize = 16;

// (constant-initialised data: no initialiser or thread-safe static guard of the C++ runtime's)
struct ChoiceState {
    Renderer choice_ = Renderer::kDirect3D9;
    bool read_ = false;
};

ChoiceState& choice_state()
{
    static ChoiceState state;
    return state;
}

Renderer read_choice()
{
    // (main runs before the PC set-up that finds the settings file; finding it again there changes nothing)
    build_info::prepare_all();
    std::array<char, kValueSize> buffer{};
    // (Vulkan is the default: no key, an empty one or anything but d3d9 - which is logged - asks for it)
    build_info::fetch_text(build_info::kGraphics, "renderer", "vulkan", buffer.data(), buffer.size());
    std::string value(buffer.data());
    std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    // (the editor mode - engine/sys/editor_mode.h - runs on Vulkan whatever the key says: its panels' own windows are
    // Vulkan's; Direct3D 9 only when Vulkan can't draw here)
    const bool editor_mode = sys::editor_mode::on();
    if (value == "d3d9" && !editor_mode) {
        get_log("renderer: Direct3D 9 (renderer=d3d9)");
        return Renderer::kDirect3D9;
    }
    if (value == "d3d9") {
        get_log("renderer: renderer=d3d9 is the game's - the editor mode runs on Vulkan");
    } else if (value != "vulkan" && !value.empty()) {
        get_log("renderer=%s is not a renderer (vulkan or d3d9); Vulkan, the default", value.c_str());
    }
    if (!kVulkanBuilt) {
        get_log("renderer: this build was made without the Vulkan library (THUG2_VULKAN); using Direct3D 9");
    } else if (!kVulkanDeviceExists) {
        get_log("renderer: the Vulkan renderer is not available; using Direct3D 9");
    } else if (vulkan_usable()) {
        return Renderer::kVulkan;
    }
    return Renderer::kDirect3D9;
}

} // namespace

Renderer chosen_renderer()
{
    ChoiceState& state = choice_state();
    if (!state.read_) {
        state.choice_ = read_choice();
        state.read_ = true;
    }
    return state.choice_;
}

sys::WindowApi renderer_window_api()
{
    return chosen_renderer() == Renderer::kVulkan ? sys::WindowApi::kVulkan : sys::WindowApi::kDirect3D9;
}

void install_renderer()
{
    if (chosen_renderer() == Renderer::kVulkan) {
        install_vulkan();
    } else {
        install_direct3d9();
    }
}

} // namespace app
