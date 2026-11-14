#include "app/renderer_choice.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/render_device.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
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
#include "gpu_vulkan/vulkan_cel.h"
#include "gpu_vulkan/style_film_settings.h"
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
// (added by the split comparison) The [Graphics] values side B of a split comparison puts over the ini's while its
// settings are made (null: the ini's alone) - compare_prepare below.
const render::graphics_options::CompareSettings*& graphics_overrides()
{
    static const render::graphics_options::CompareSettings* overrides = nullptr;
    return overrides;
}

const std::string* override_of(const char* key)
{
    const render::graphics_options::CompareSettings* overrides = graphics_overrides();
    if (overrides == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(*overrides, [key](const auto& each) { return each.first == key; });
    return found == overrides->end() ? nullptr : &found->second;
}

// [Graphics] `key` as GetPrivateProfileInt reads it (a leading number; `fallback` when there's none), an override first.
int graphics_int(const char* key, int fallback)
{
    if (const std::string* value = override_of(key)) {
        char* end = nullptr;
        const long parsed = std::strtol(value->c_str(), &end, 10);
        return end == value->c_str() ? fallback : static_cast<int>(parsed);
    }
    return build_info::get_int(build_info::kGraphics, key, fallback);
}
bool graphics_flag(const char* key, bool fallback) { return graphics_int(key, fallback ? 1 : 0) != 0; }
void graphics_text(const char* key, const char* fallback, char* out, std::size_t size)
{
    if (size == 0) {
        return;
    }
    if (const std::string* value = override_of(key)) {
        const std::span<char> buffer(out, size);
        const std::size_t length = std::min(value->size(), size - 1);
        std::copy_n(value->c_str(), length, buffer.begin());
        core::at(buffer, length) = '\0';
        return;
    }
    build_info::fetch_text(build_info::kGraphics, key, fallback, out, size);
}

// A [Graphics] number with a fraction (`fallback` when it's missing or not a number).
float ini_float(const char* key, float fallback)
{
    constexpr std::size_t kLongest = 32; // (characters of a number)
    std::array<char, kLongest> text{};
    graphics_text(key, "", text.data(), text.size());
    char* end = nullptr;
    const double value = std::strtod(text.data(), &end);
    return end == text.data() || !std::isfinite(value) ? fallback : static_cast<float>(value);
}

// (added by the scene's sun) [Graphics] rt_sun_source: the one sun every effect lights by (engine/gfx/rt_scene.h
// scene_sun) - level (the default: the sun the level's lighting was baked with; the game's own is the same default
// SetSun, 20 degrees up toward +z, in 12 of the 15 levels, and relit by it they lose their baked sunlight - docs/design/
// vulkan.md "One sun for every effect"), game (the sun the game shows, the level relit by it; a level whose game sun is
// too low, or whose entry in rt_relit_levels.json says "sun": "level", keeps its baked sun) or x,y,z /
// heading,elevation (a direction, for experiments); anything else is the default, logged. The per-effect keys it
// replaces (rt_shadow_sun, rt_relit_sun, rt_sun_place) are read no more - a line in the log says so. One sun for the
// whole frame: the split comparison's sides share it.
constexpr const char* kDefaultSunSource = "level";
void read_sun_source()
{
    constexpr std::size_t kLongest = 64;
    std::array<char, kLongest> text{};
    build_info::fetch_text(build_info::kGraphics, "rt_sun_source", kDefaultSunSource, text.data(), text.size());
    render::rt::SunChoice choice;
    if (!render::rt::parse_sun_source(text.data(), &choice)) {
        get_log("[Graphics] rt_sun_source=%s isn't game, level, x,y,z or heading,elevation - %s used", text.data(),
                kDefaultSunSource);
        render::rt::parse_sun_source(kDefaultSunSource, &choice);
    }
    render::rt::set_sun_choice(choice);
    static bool retired_logged = false;
    if (!retired_logged) {
        retired_logged = true;
        for (const char* retired : {"rt_shadow_sun", "rt_relit_sun", "rt_sun_place"}) {
            std::array<char, kLongest> old{};
            build_info::fetch_text(build_info::kGraphics, retired, "", old.data(), old.size());
            if (old.front() != '\0') {
                get_log("[Graphics] %s=%s is retired: rt_sun_source=%s sets the sun of every effect", retired,
                        old.data(), text.data());
            }
        }
    }
}

// An r,g,b colour (0 - 1 each); false when the text isn't one.
bool parse_rgb(const std::string& value, std::array<float, 3>* colour)
{
    std::array<float, 3> read_colour{};
    std::size_t read = 0;
    std::istringstream numbers(value);
    std::string number;
    while (read < read_colour.size() && std::getline(numbers, number, ',')) {
        char* end = nullptr;
        const double parsed = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || !std::isfinite(parsed)) {
            return false;
        }
        read_colour.at(read++) = static_cast<float>(parsed);
    }
    if (read != read_colour.size()) {
        return false;
    }
    *colour = read_colour;
    return true;
}

// [Graphics] rt_sun_colour: auto (the default: the sky model's colour for the sun's elevation) or r,g,b (0 - 1 each);
// anything else is the default, logged.
void read_rt_sun_colour(render::vulkan::RtSunSettings* settings)
{
    constexpr std::size_t kLongest = 64;
    std::array<char, kLongest> text{};
    graphics_text("rt_sun_colour", "auto", text.data(), text.size());
    const std::string value(text.data());
    std::array<float, 3> colour{};
    if (parse_rgb(value, &colour)) {
        settings->colour_set = true;
        settings->colour = colour;
    } else if (!value.empty() && value != "auto") {
        get_log("[Graphics] rt_sun_colour=%s isn't auto or r,g,b - the sky model's colour used", value.c_str());
    }
}
#endif

#ifdef THUG2_VULKAN
// What the device was made for: rt_hdr as the game started (its colour targets' format). A setting read again while
// the game runs keeps it - a changed rt_hdr waits for the restart.
struct LiveState {
    bool hdr_at_start = false;
};

LiveState& live_state()
{
    static LiveState state;
    return state;
}

// NOT ORIGINAL - ray-traced sun shadows (gpu_vulkan/vulkan_rt_shadows.h): off unless asked for, and only with
// ray_tracing=1 on a device that has it
render::vulkan::RtShadowSettings make_rt_shadows()
{
    render::vulkan::RtShadowSettings shadows{
         .on = graphics_flag("rt_shadows", false),
         .strength = ini_float("rt_shadow_strength", render::vulkan::RtShadowSettings{}.strength),
         .softness = ini_float("rt_shadow_softness", render::vulkan::RtShadowSettings{}.softness),
         .rays = static_cast<unsigned>(std::max(1, graphics_int("rt_shadow_rays", 4))),
         // (1 by default since the water and glass work: the skater's own projected shadow - a blocky 256 x 256
         // texture - is hidden while the ray-traced one is drawn; rt::object_shadows_replaced)
         .objects = graphics_flag("rt_shadow_objects", true),
         .covered = graphics_flag("rt_shadow_covered", false),
         .sky_rays = static_cast<unsigned>(
             std::max(1, graphics_int("rt_shadow_sky_rays",
                                             static_cast<int>(render::vulkan::kRtShadowSkyRays)))),
         .match_bake = graphics_flag("rt_shadow_match_bake", true)};
    return shadows;
}
void read_rt_shadows() { render::vulkan::set_rt_shadow_settings(make_rt_shadows()); }

// NOT ORIGINAL - ray-traced ambient occlusion (gpu_vulkan/vulkan_rt_effects.h): likewise
render::vulkan::RtAoSettings make_rt_ao()
{
    return render::vulkan::RtAoSettings{
        .on = graphics_flag("rt_ao", false),
         .strength = ini_float("rt_ao_strength", render::vulkan::RtAoSettings{}.strength),
         .radius = ini_float("rt_ao_radius", render::vulkan::RtAoSettings{}.radius),
         .rays = static_cast<unsigned>(
             std::max(1, graphics_int("rt_ao_rays",
                                             static_cast<int>(render::vulkan::RtAoSettings{}.rays)))),
         .denoise = graphics_flag("rt_ao_denoise", true)};
}
void read_rt_ao() { render::vulkan::set_rt_ao_settings(make_rt_ao()); }

// NOT ORIGINAL - one-bounce ray-traced indirect light (gpu_vulkan/vulkan_rt_effects.h): likewise. (After the shadows':
// their `off` clears the baked sun the bounce wants.)
render::vulkan::RtGiSettings make_rt_gi()
{
    return render::vulkan::RtGiSettings{
        .on = graphics_flag("rt_gi", false),
         .strength = ini_float("rt_gi_strength", render::vulkan::RtGiSettings{}.strength),
         .distance = ini_float("rt_gi_distance", render::vulkan::RtGiSettings{}.distance),
         .rays = static_cast<unsigned>(
             std::max(1, graphics_int("rt_gi_rays",
                                             static_cast<int>(render::vulkan::RtGiSettings{}.rays)))),
         .denoise = graphics_flag("rt_gi_denoise", true)};
}
void read_rt_gi() { render::vulkan::set_rt_gi_settings(make_rt_gi()); }

// NOT ORIGINAL - the night lighting (gpu_vulkan/vulkan_rt_lights.h): likewise
render::vulkan::RtLightSettings make_rt_lights()
{
    const render::vulkan::RtLightSettings defaults;
    return render::vulkan::RtLightSettings{
        .on = graphics_flag("rt_lights", false),
        .strength = ini_float("rt_lights_strength", defaults.strength),
        .shadowed = static_cast<unsigned>(
            std::max(1, graphics_int("rt_lights_shadowed", static_cast<int>(defaults.shadowed)))),
        .max_lights = static_cast<unsigned>(
            std::max(1, graphics_int("rt_lights_max", static_cast<int>(defaults.max_lights)))),
        .emissive = ini_float("rt_lights_emissive", defaults.emissive),
        .level = ini_float("rt_lights_level", defaults.level)};
}
void read_rt_lights() { render::vulkan::set_rt_light_settings(make_rt_lights()); }

// The shadows read again, and the bounce after them (see read_rt_gi).
void read_rt_shadows_and_gi()
{
    read_rt_shadows();
    read_rt_gi();
}

// NOT ORIGINAL - ray-traced reflections (gpu_vulkan/vulkan_rt_reflections.h): likewise
render::vulkan::RtReflectionSettings make_rt_reflections()
{
    return render::vulkan::RtReflectionSettings{
        .on = graphics_flag("rt_reflections", false),
         .strength = ini_float("rt_reflection_strength", render::vulkan::RtReflectionSettings{}.strength),
         .fresnel = graphics_flag("rt_reflection_fresnel", true),
         .glass_roughness = ini_float("rt_glass_roughness", render::vulkan::kRtGlassRoughness),
         .glass_reflectance = ini_float("rt_glass_reflectance", render::vulkan::kRtGlassReflectance)};
}
void read_rt_reflections() { render::vulkan::set_rt_reflection_settings(make_rt_reflections()); }

// NOT ORIGINAL - HDR rendering and tone mapping (gpu_vulkan/vulkan_hdr.h): off unless asked for; no ray tracing
// needed. The grading table's path is the game folder's unless it is a full one.
render::vulkan::HdrSettings make_hdr()
{
    constexpr std::size_t kLongestPath = 260;
    std::array<char, kLongestPath> lut{};
    graphics_text("rt_grade_lut", "", lut.data(), lut.size());
    std::string lut_path(lut.data());
    const bool full = lut_path.find(':') == 1 || lut_path.starts_with('\\') || lut_path.starts_with('/');
    if (!lut_path.empty() && !full) {
        lut_path = build_info::executable_directory() + lut_path;
    }
    const render::vulkan::HdrSettings defaults;
    return render::vulkan::HdrSettings{
        .on = live_state().hdr_at_start,
         .exposure = ini_float("rt_exposure", defaults.exposure),
         .tonemap = static_cast<unsigned>(std::max(0, graphics_int("rt_tonemap", 0))),
         .saturation = ini_float("rt_saturation", defaults.saturation),
         .contrast = ini_float("rt_contrast", defaults.contrast),
         .white_point = ini_float("rt_white_point", defaults.white_point),
         .grade_lut = lut_path};
}
void read_hdr() { render::vulkan::set_hdr_settings(make_hdr()); }

// NOT ORIGINAL - volumetric fog and light shafts (gpu_vulkan/vulkan_rt_fog.h): likewise
render::vulkan::RtFogSettings make_rt_fog()
{
    return render::vulkan::RtFogSettings{
        .on = graphics_flag("rt_fog", false),
         .density = ini_float("rt_fog_density", render::vulkan::RtFogSettings{}.density),
         .height = ini_float("rt_fog_height", render::vulkan::RtFogSettings{}.height),
         .anisotropy = ini_float("rt_fog_anisotropy", render::vulkan::RtFogSettings{}.anisotropy),
         .steps = static_cast<unsigned>(std::max(
             1, graphics_int("rt_fog_steps", static_cast<int>(render::vulkan::kRtFogSteps)))),
         .mode = graphics_int("rt_fog_mode", 0) == 1 ? render::vulkan::RtFogMode::kShafts
                                                                                   : render::vulkan::RtFogMode::kReplace,
         .hdr = live_state().hdr_at_start,
         .haze = graphics_flag("rt_fog_haze", true),
         .haze_density = ini_float("rt_fog_haze_density", render::vulkan::RtFogSettings{}.haze_density),
         .haze_height = ini_float("rt_fog_haze_height", render::vulkan::RtFogSettings{}.haze_height)};
}
void read_rt_fog() { render::vulkan::set_rt_fog_settings(make_rt_fog()); }

// NOT ORIGINAL - the sun's disc and glare, screen-space rays, the bloom and the sky model
// (gpu_vulkan/vulkan_rt_sun.h, vulkan_rt_sky.h): off unless asked for; the bloom needs no ray tracing
render::vulkan::RtSunSettings make_rt_sun()
{
    render::vulkan::RtSunSettings sun{
        .sun = graphics_flag("rt_sun", false),
        .sun_size = ini_float("rt_sun_size", render::vulkan::RtSunSettings{}.sun_size),
        .bloom = graphics_flag("rt_bloom", false),
        .bloom_strength = ini_float("rt_bloom_strength", render::vulkan::kRtBloomStrength),
        .shafts = graphics_flag("rt_shafts", false),
        .shaft_strength = ini_float("rt_shaft_strength", render::vulkan::kRtShaftStrength),
        .sky_model = graphics_flag("rt_sky_model", false),
        .lens_dirt = graphics_flag("rt_lens_dirt", false),
        .hdr = live_state().hdr_at_start};
    read_rt_sun_colour(&sun);
    return sun;
}
void read_rt_sun() { render::vulkan::set_rt_sun_settings(make_rt_sun()); }

// NOT ORIGINAL - a modern sky: the atmosphere and volumetric clouds (gpu_vulkan/vulkan_rt_sky_pass.h): off unless
// asked for; no ray tracing needed. rt_cloud_coverage: auto (the level's own sky) or 0 - 1.
render::vulkan::RtSkySettings make_rt_sky()
{
    constexpr std::size_t kLongest = 16;
    std::array<char, kLongest> coverage{};
    graphics_text("rt_cloud_coverage", "auto", coverage.data(), coverage.size());
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
    return render::vulkan::RtSkySettings{
        .on = graphics_flag("rt_sky", false),
         .match = ini_float("rt_sky_match", render::vulkan::kRtSkyMatch),
         .clouds = graphics_flag("rt_clouds", false),
         .coverage = cover,
         .speed = ini_float("rt_cloud_speed", 1.0F),
         .quality = static_cast<unsigned>(std::max(0, graphics_int("rt_cloud_quality", 1))),
         .cloud_shadows = graphics_flag("rt_cloud_shadows", false),
         .hdr = live_state().hdr_at_start};
}
void read_rt_sky() { render::vulkan::set_rt_sky_settings(make_rt_sky()); }

// NOT ORIGINAL - the relit mode (engine/gfx/rt_relit.h, render_pc/rt_relit_world.h): off unless asked for; it needs
// ray tracing (the rays that take the bake apart)
void read_rt_relit()
{
    render::rt::RelitSettings relit{
        .on = graphics_flag("rt_relit", false),
        .strength = ini_float("rt_relit_strength", render::rt::RelitSettings{}.strength),
        .residual = graphics_flag("rt_relit_residual", true),
        .detail = ini_float("rt_relit_detail", render::rt::RelitSettings{}.detail),
        .sky_rays = static_cast<uint32_t>(std::max(
            1, graphics_int("rt_relit_sky_rays", static_cast<int>(render::rt::kRelitSkyRays)))),
        .sun_rays = static_cast<uint32_t>(std::max(
            1, graphics_int("rt_relit_sun_rays", static_cast<int>(render::rt::kRelitSunRays)))),
        .cache = graphics_flag("rt_relit_cache", true)};
    // (added by the scene's sun) a sun other than the baked one needs the level relit by it - its baked shadows and
    // light turned to that sun -, so rt_sun_source=game (or a direction) turns the mode on
    if (!relit.on && render::rt::sun_choice().source != render::rt::SunSource::kLevel) {
        relit.on = true;
        get_log("[Graphics] rt_relit=0, but rt_sun_source isn't level: the relit mode on (the levels relit by that sun)");
    }
    render::rt::set_relit_settings(relit);
}

// NOT ORIGINAL - upscaling (gpu_vulkan/vulkan_upscale.h): off unless asked for; DLSS / FSR from beside the exe. Read
// at start-up only (the device's targets and the provider are made for it).
void read_upscale()
{
    constexpr std::size_t kLongest = 32;
    std::array<char, kLongest> text{};
    render::vulkan::UpscaleSettings upscale;
    graphics_text("upscaler", "off", text.data(), text.size());
    if (!render::vulkan::parse_upscaler(text.data(), &upscale.upscaler)) {
        get_log("[Graphics] upscaler=%s isn't off, dlss, fsr or auto - off", text.data());
    }
    text = {};
    graphics_text("upscale_quality", "quality", text.data(), text.size());
    if (!render::vulkan::parse_upscale_quality(text.data(), &upscale.quality)) {
        get_log("[Graphics] upscale_quality=%s isn't dlaa, quality, balanced, performance or ultra - quality",
                text.data());
    }
    upscale.ray_reconstruction = graphics_flag("ray_reconstruction", false);
    upscale.sharpness = ini_float("upscale_sharpness", 0.0F);
    upscale.fsr_major = static_cast<unsigned>(std::max(0, graphics_int("fsr_version", 0)));
    render::vulkan::set_upscale_settings(upscale);
}

// NOT ORIGINAL - per-pixel lighting and the surface maps beside the textures (gpu_vulkan/vulkan_surface.h): off unless
// asked for; no ray tracing needed (with it, the bake's sun found by ray tracing lights the maps)
void read_rt_pbr()
{
    const render::vulkan::RtPbrSettings defaults;
    render::vulkan::set_rt_pbr_settings(
        {.on = build_info::fetch_flag(build_info::kGraphics, "rt_pbr", false),
         .specular = std::max(0.0F, ini_float("rt_pbr_specular", defaults.specular)),
         .normal_strength = std::max(0.0F, ini_float("rt_pbr_normal_strength", defaults.normal_strength)),
         .objects = build_info::fetch_flag(build_info::kGraphics, "rt_pbr_objects", true)});
}

// NOT ORIGINAL - detail materials and de-lighting (gpu_vulkan/vulkan_detail.h): off unless asked for; they act on the
// draws rt_pbr lights with a surface map
void read_rt_detail()
{
    const render::vulkan::RtDetailSettings defaults;
    render::vulkan::set_rt_detail_settings(
        {.on = build_info::fetch_flag(build_info::kGraphics, "rt_detail", false),
         .strength = std::clamp(ini_float("rt_detail_strength", defaults.strength), 0.0F, 2.0F),
         .delight = build_info::fetch_flag(build_info::kGraphics, "rt_delight", false),
         .delight_strength = std::clamp(ini_float("rt_delight_strength", defaults.delight_strength), 0.0F, 1.0F)});
}

// NOT ORIGINAL - the vhs, retro and watercolour art styles' knobs (gpu_vulkan/vulkan_style_film.h), with rt_style as the
// toon's reading set it (the retro geometry is on with it)
void read_style_film()
{
    const render::vulkan::VhsSettings vhs;
    const render::vulkan::RetroSettings retro;
    const render::vulkan::WatercolourSettings watercolour;
    render::vulkan::set_film_settings(
        {.style = render::vulkan::style_settings().style,
         .vhs = {.fisheye = ini_float("rt_style_vhs_fisheye", vhs.fisheye),
                 .bleed = ini_float("rt_style_vhs_bleed", vhs.bleed),
                 .wobble = ini_float("rt_style_vhs_wobble", vhs.wobble),
                 .noise = ini_float("rt_style_vhs_noise", vhs.noise),
                 .scanlines = ini_float("rt_style_vhs_scanlines", vhs.scanlines),
                 .saturation = ini_float("rt_style_vhs_saturation", vhs.saturation),
                 .overlay = build_info::fetch_flag(build_info::kGraphics, "rt_style_vhs_overlay", vhs.overlay)},
         .retro = {.lines = ini_float("rt_style_retro_lines", retro.lines),
                   .bits = ini_float("rt_style_retro_bits", retro.bits),
                   .dither = ini_float("rt_style_retro_dither", retro.dither),
                   .snap = ini_float("rt_style_retro_snap", retro.snap),
                   .affine = ini_float("rt_style_retro_affine", retro.affine)},
         .watercolour = {.bleed = ini_float("rt_style_watercolour_bleed", watercolour.bleed),
                         .flow = ini_float("rt_style_watercolour_flow", watercolour.flow),
                         .granulation = ini_float("rt_style_watercolour_granulation", watercolour.granulation),
                         .edges = ini_float("rt_style_watercolour_edges", watercolour.edges),
                         .paper = ini_float("rt_style_watercolour_paper", watercolour.paper),
                         .outlines = ini_float("rt_style_watercolour_outlines", watercolour.outlines)}});
}

// NOT ORIGINAL - an art style, the toon filter (gpu_vulkan/vulkan_style.h): off unless asked for; no ray tracing or HDR
// needed
// [Graphics] rt_style_noir_dark / _light: r,g,b; anything else is the default (logged when it isn't empty).
std::array<float, 3> style_colour(const char* key, const std::array<float, 3>& fallback)
{
    constexpr std::size_t kLongest = 64;
    std::array<char, kLongest> text{};
    graphics_text(key, "", text.data(), text.size());
    std::array<float, 3> colour = fallback;
    if (text.front() != '\0' && !parse_rgb(text.data(), &colour)) {
        get_log("[Graphics] %s=%s isn't r,g,b - the default used", key, text.data());
    }
    return colour;
}

// [Graphics] rt_style as a name (off when it is none - logged when `log`).
render::vulkan::ArtStyle art_style_of_ini(bool log)
{
    constexpr std::size_t kLongest = 32;
    std::array<char, kLongest> name{};
    graphics_text("rt_style", "off", name.data(), name.size());
    render::vulkan::ArtStyle style = render::vulkan::ArtStyle::kOff;
    if (!render::vulkan::parse_art_style(name.data(), &style) && log) {
        get_log("[Graphics] rt_style=%s isn't off, toon, cel, comic, sketch, vhs, retro, watercolour, noir or thermal - "
                "off",
                name.data());
    }
    return style;
}

void read_rt_cel();

void read_style()
{
    const render::vulkan::StyleSettings defaults;
    const render::vulkan::ArtStyle style = art_style_of_ini(true);
    render::vulkan::set_style_settings({.style = style,
                                        .strength = ini_float("rt_style_strength", defaults.strength),
                                        .smooth = ini_float("rt_style_smooth", defaults.smooth),
                                        .bands = ini_float("rt_style_bands", defaults.bands),
                                        .outlines = ini_float("rt_style_outlines", defaults.outlines),
                                        .outline_width = ini_float("rt_style_outline_width", defaults.outline_width),
                                        .saturation = ini_float("rt_style_saturation", defaults.saturation),
                                        .halftone = ini_float("rt_style_comic_halftone", defaults.halftone),
                                        .hatch = ini_float("rt_style_sketch_hatch", defaults.hatch),
                                        .wash = ini_float("rt_style_sketch_wash", defaults.wash),
                                        .paper = ini_float("rt_style_paper", defaults.paper),
                                        .grain = ini_float("rt_style_grain", defaults.grain),
                                        .vignette = ini_float("rt_style_vignette", defaults.vignette),
                                        .contrast = ini_float("rt_style_noir_contrast", defaults.contrast),
                                        .focus = ini_float("rt_style_thermal_focus", defaults.focus),
                                        .noir_dark = style_colour("rt_style_noir_dark", defaults.noir_dark),
                                        .noir_light = style_colour("rt_style_noir_light", defaults.noir_light)});
    read_style_film();
    read_rt_cel(); // (rt_style=cel is cel shading)
}

// NOT ORIGINAL - grass (gpu_vulkan/vulkan_grass.h): blades where the level's collision says grass; off unless asked for,
// no ray tracing needed
void read_grass()
{
    const render::vulkan::GrassSettings defaults;
    render::vulkan::set_grass_settings({.on = build_info::fetch_flag(build_info::kGraphics, "grass", false),
                                        .density = ini_float("grass_density", defaults.density),
                                        .height = ini_float("grass_height", defaults.height),
                                        .distance = ini_float("grass_distance", defaults.distance)});
}

// NOT ORIGINAL - cel shading (engine/gfx/rt_cel.h, gpu_vulkan/vulkan_cel.h): off unless asked for; the relit mode's
// decomposition and ray tracing make it fuller (the light in bands from the relit world's parts, hard traced shadows).
// On with rt_style=cel (an art style in the Options screen) and only then: an older ini's rt_cel line does nothing (it
// turned cel on under Art style Off, which nothing in the Options screen could undo).
void read_rt_cel()
{
    const render::rt::CelSettings defaults;
    const bool on = art_style_of_ini(false) == render::vulkan::ArtStyle::kCel;
    render::rt::CelSettings cel{.on = on,
                                .bands = static_cast<uint32_t>(std::max(0, graphics_int("rt_cel_bands", static_cast<int>(defaults.bands)))),
                                .shadow_colour = defaults.shadow_colour,
                                .rim = ini_float("rt_cel_rim", defaults.rim),
                                .outline_width = ini_float("rt_cel_outline_width", defaults.outline_width),
                                .flatten = ini_float("rt_cel_flatten", defaults.flatten)};
    constexpr std::size_t kLongest = 32;
    std::array<char, kLongest> text{};
    graphics_text("rt_cel_shadow_colour", "", text.data(), text.size());
    if (text.front() != '\0' && !render::rt::parse_cel_colour(text.data(), &cel.shadow_colour)) {
        get_log("[Graphics] rt_cel_shadow_colour=%s isn't r,g,b or #rrggbb - the default kept", text.data());
    }
    render::vulkan::set_cel_settings(cel);
}

// NOT ORIGINAL - water shading (gpu_vulkan/vulkan_water.h): off unless asked for; no ray tracing needed.
// rt_water_wind_heading: auto (the level's) or degrees; rt_water_tint: auto (the water's own colour as the game draws it) or r,g,b.
void read_water()
{
    const render::vulkan::WaterSettings defaults;
    render::vulkan::WaterSettings water{
        .on = build_info::fetch_flag(build_info::kGraphics, "rt_water", false),
        .waves = ini_float("rt_water_waves", defaults.waves),
        .wind = ini_float("rt_water_wind", defaults.wind),
        .heading = render::vulkan::kWaterHeadingAuto,
        .clarity = ini_float("rt_water_clarity", defaults.clarity),
        .foam = ini_float("rt_water_foam", defaults.foam),
        .hdr = live_state().hdr_at_start,
        .screen_reflections = build_info::fetch_flag(build_info::kGraphics, "rt_water_screen_reflections", true),
        .debug_view = build_info::fetch_flag("debug", "rt_water_view", false),
        .geometry = build_info::fetch_flag(build_info::kGraphics, "rt_water_geometry", defaults.geometry)};
    constexpr std::size_t kLongest = 64;
    std::array<char, kLongest> text{};
    graphics_text("rt_water_wind_heading", "auto", text.data(), text.size());
    const std::string heading(text.data());
    if (!heading.empty() && heading != "auto") {
        char* end = nullptr;
        const double parsed = std::strtod(heading.c_str(), &end);
        if (end != heading.c_str() && std::isfinite(parsed)) {
            constexpr double kTurn = 360.0;
            water.heading = static_cast<float>(std::fmod(std::fmod(parsed, kTurn) + kTurn, kTurn));
        } else {
            get_log("[Graphics] rt_water_wind_heading=%s isn't auto or degrees - the level's used", heading.c_str());
        }
    }
    text = {};
    graphics_text("rt_water_tint", "auto", text.data(), text.size());
    const std::string tint(text.data());
    std::array<float, 3> colour{};
    std::size_t read = 0;
    std::istringstream numbers(tint);
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
        water.tint_auto = false;
        water.tint = colour;
    } else if (!tint.empty() && tint != "auto") {
        get_log("[Graphics] rt_water_tint=%s isn't auto or r,g,b - the water's own colour used", tint.c_str());
    }
    render::vulkan::set_water_settings(water);
}

// NOT ORIGINAL - the camera effects (gpu_vulkan/vulkan_camera_fx.h): motion blur, depth of field, the colour grade, the
// fisheye lens (vulkan_fisheye.h); each
// off unless asked for, no ray tracing or HDR needed. rt_grade: a look's name (off, neutral, warm, cool, filmic,
// bleach, skate_video, level); anything else is off, logged.
void read_camera_fx()
{
    const render::vulkan::CameraFxSettings defaults;
    render::vulkan::CameraFxSettings settings{
        .motion_blur = build_info::fetch_flag(build_info::kGraphics, "rt_motion_blur", false),
        .motion_blur_strength = ini_float("rt_motion_blur_strength", defaults.motion_blur_strength),
        .dof = build_info::fetch_flag(build_info::kGraphics, "rt_dof", false),
        .dof_strength = ini_float("rt_dof_strength", defaults.dof_strength),
        .grade = render::vulkan::GradeLook::kOff,
        .grade_strength = ini_float("rt_grade_strength", defaults.grade_strength),
        .grade_vignette = ini_float("rt_grade_vignette", defaults.grade_vignette),
        .grade_grain = ini_float("rt_grade_grain", defaults.grade_grain),
        .grade_levels = {},
        // (the fisheye lens - gpu_vulkan/vulkan_fisheye.h)
        .fisheye = build_info::fetch_flag(build_info::kGraphics, "rt_fisheye", false),
        .fisheye_strength = ini_float("rt_fisheye_strength", defaults.fisheye_strength),
        .fisheye_vignette = ini_float("rt_fisheye_vignette", defaults.fisheye_vignette),
        .fisheye_circle = ini_float("rt_fisheye_circle", defaults.fisheye_circle)};
    constexpr std::size_t kLongest = 512;
    std::array<char, kLongest> text{};
    graphics_text("rt_grade", "off", text.data(), text.size());
    if (!render::vulkan::parse_grade_look(text.data(), &settings.grade)) {
        get_log("[Graphics] rt_grade=%s isn't off, neutral, warm, cool, filmic, bleach, skate_video or level - off",
                text.data());
    }
    text = {};
    graphics_text("rt_grade_levels", "", text.data(), text.size());
    settings.grade_levels = text.data();
    render::vulkan::set_camera_fx_settings(settings);
}

// The [Graphics] keys a change of which is put to use at once (the Options screen): each key (or every key starting
// with it, `prefix`) and what reads its effect's settings again. Everything else - renderer, ray_tracing, rt_hdr,
// upscaling, rt_tessellation - waits for the restart.
struct LiveKey {
    const char* key;
    bool prefix;
    void (*read)();
};
// (added by the scene's sun) rt_sun_source: the choice set, then every effect that lights by the sun read again (their
// histories start again; the relit mode turned on or off with it - it relights by the new sun)
void read_sun_source_and_effects()
{
    read_sun_source();
    read_rt_shadows_and_gi();
    read_rt_fog();
    read_rt_sun();
    read_rt_sky();
    read_rt_relit();
}

constexpr std::array kLiveKeys = {
    LiveKey{.key = "rt_sun_source", .prefix = false, .read = &read_sun_source_and_effects},
    LiveKey{.key = "rt_shadows", .prefix = false, .read = &read_rt_shadows_and_gi},
    LiveKey{.key = "rt_shadow_", .prefix = true, .read = &read_rt_shadows_and_gi},
    LiveKey{.key = "rt_ao", .prefix = false, .read = &read_rt_ao},
    LiveKey{.key = "rt_ao_", .prefix = true, .read = &read_rt_ao},
    LiveKey{.key = "rt_gi", .prefix = false, .read = &read_rt_gi},
    LiveKey{.key = "rt_gi_", .prefix = true, .read = &read_rt_gi},
    LiveKey{.key = "rt_lights", .prefix = true, .read = &read_rt_lights}, // (rt_lights and every rt_lights_)
    LiveKey{.key = "rt_reflections", .prefix = false, .read = &read_rt_reflections},
    LiveKey{.key = "rt_reflection_", .prefix = true, .read = &read_rt_reflections},
    LiveKey{.key = "rt_glass_", .prefix = true, .read = &read_rt_reflections},
    LiveKey{.key = "rt_exposure", .prefix = false, .read = &read_hdr},
    LiveKey{.key = "rt_tonemap", .prefix = false, .read = &read_hdr},
    LiveKey{.key = "rt_white_point", .prefix = false, .read = &read_hdr},
    LiveKey{.key = "rt_saturation", .prefix = false, .read = &read_hdr},
    LiveKey{.key = "rt_contrast", .prefix = false, .read = &read_hdr},
    LiveKey{.key = "rt_grade_lut", .prefix = false, .read = &read_hdr},
    LiveKey{.key = "rt_fog", .prefix = false, .read = &read_rt_fog},
    LiveKey{.key = "rt_fog_", .prefix = true, .read = &read_rt_fog},
    LiveKey{.key = "rt_sun", .prefix = false, .read = &read_rt_sun},
    LiveKey{.key = "rt_sun_", .prefix = true, .read = &read_rt_sun},
    LiveKey{.key = "rt_shafts", .prefix = false, .read = &read_rt_sun},
    LiveKey{.key = "rt_shaft_", .prefix = true, .read = &read_rt_sun},
    LiveKey{.key = "rt_bloom", .prefix = false, .read = &read_rt_sun},
    LiveKey{.key = "rt_bloom_", .prefix = true, .read = &read_rt_sun},
    LiveKey{.key = "rt_sky_model", .prefix = false, .read = &read_rt_sun},
    LiveKey{.key = "rt_lens_dirt", .prefix = false, .read = &read_rt_sun},
    LiveKey{.key = "rt_sky", .prefix = false, .read = &read_rt_sky},
    LiveKey{.key = "rt_sky_match", .prefix = false, .read = &read_rt_sky},
    LiveKey{.key = "rt_clouds", .prefix = false, .read = &read_rt_sky},
    LiveKey{.key = "rt_cloud_", .prefix = true, .read = &read_rt_sky},
    LiveKey{.key = "rt_relit", .prefix = false, .read = &read_rt_relit},
    LiveKey{.key = "rt_relit_", .prefix = true, .read = &read_rt_relit},
    LiveKey{.key = "rt_pbr", .prefix = false, .read = &read_rt_pbr},
    LiveKey{.key = "rt_pbr_", .prefix = true, .read = &read_rt_pbr},
    LiveKey{.key = "rt_detail", .prefix = true, .read = &read_rt_detail},  // (NOT ORIGINAL - rt_detail and rt_detail_*)
    LiveKey{.key = "rt_delight", .prefix = true, .read = &read_rt_detail}, // (NOT ORIGINAL - rt_delight and rt_delight_*)
    LiveKey{.key = "rt_style", .prefix = true, .read = &read_style}, // (rt_style and every rt_style_)
    LiveKey{.key = "grass", .prefix = true, .read = &read_grass},    // (grass and every grass_)
    LiveKey{.key = "rt_cel", .prefix = true, .read = &read_rt_cel},   // (added by cel shading: rt_cel and every rt_cel_)
    LiveKey{.key = "rt_water", .prefix = true, .read = &read_water}, // (NOT ORIGINAL - rt_water and every rt_water_)
    // (NOT ORIGINAL - the camera effects; rt_grade_lut, HDR's .cube table, is read_hdr's above)
    LiveKey{.key = "rt_motion_blur", .prefix = true, .read = &read_camera_fx},
    LiveKey{.key = "rt_dof", .prefix = true, .read = &read_camera_fx},
    LiveKey{.key = "rt_grade", .prefix = false, .read = &read_camera_fx},
    LiveKey{.key = "rt_grade_strength", .prefix = false, .read = &read_camera_fx},
    LiveKey{.key = "rt_grade_vignette", .prefix = false, .read = &read_camera_fx},
    LiveKey{.key = "rt_grade_grain", .prefix = false, .read = &read_camera_fx},
    LiveKey{.key = "rt_grade_levels", .prefix = false, .read = &read_camera_fx},
    LiveKey{.key = "rt_fisheye", .prefix = true, .read = &read_camera_fx}, // (the fisheye lens and its knobs)
};

// (added by the split comparison - engine/gfx/graphics_options.h CompareBackend, devtools/compare_split.h) The effects'
// settings of a side: A, the ini's; B, the ini's with the comparison's overrides over them.
struct EffectSet {
    render::vulkan::RtShadowSettings shadows;
    render::vulkan::RtAoSettings ao;
    render::vulkan::RtGiSettings gi;
    render::vulkan::RtReflectionSettings reflections;
    render::vulkan::HdrSettings hdr;
    render::vulkan::RtFogSettings fog;
    render::vulkan::RtSunSettings sun;
    render::vulkan::RtSkySettings sky;
};

// Which effects a key's reading sets (EffectSet's members, one bit each; 0: none that can differ per side).
constexpr uint32_t kShadowsBit = 1U << 0U;
constexpr uint32_t kAoBit = 1U << 1U;
constexpr uint32_t kGiBit = 1U << 2U;
constexpr uint32_t kReflectionsBit = 1U << 3U;
constexpr uint32_t kHdrBit = 1U << 4U;
constexpr uint32_t kFogBit = 1U << 5U;
constexpr uint32_t kSunBit = 1U << 6U;
constexpr uint32_t kSkyBit = 1U << 7U;

uint32_t effect_bits(void (*read)())
{
    if (read == &read_rt_shadows_and_gi) {
        return kShadowsBit | kGiBit;
    }
    if (read == &read_rt_ao) {
        return kAoBit;
    }
    if (read == &read_rt_gi) {
        return kGiBit;
    }
    if (read == &read_rt_reflections) {
        return kReflectionsBit;
    }
    if (read == &read_hdr) {
        return kHdrBit;
    }
    if (read == &read_rt_fog) {
        return kFogBit;
    }
    if (read == &read_rt_sun) {
        return kSunBit;
    }
    if (read == &read_rt_sky) {
        return kSkyBit;
    }
    return 0; // (the relit mode: it changes how the world itself is drawn and keeps a cache - both sides)
}

const LiveKey* live_key_of(std::string_view name)
{
    const auto live = std::ranges::find_if(
        kLiveKeys, [name](const LiveKey& each) { return each.prefix ? name.starts_with(each.key) : name == each.key; });
    return live == kLiveKeys.end() ? nullptr : &*live;
}

EffectSet make_effects()
{
    return {.shadows = make_rt_shadows(),
            .ao = make_rt_ao(),
            .gi = make_rt_gi(),
            .reflections = make_rt_reflections(),
            .hdr = make_hdr(),
            .fog = make_rt_fog(),
            .sun = make_rt_sun(),
            .sky = make_rt_sky()};
}

struct CompareState {
    bool on = false;
    std::array<EffectSet, 2> sides{};
    render::graphics_options::CompareSettings overrides; // (the ones that can differ)
    uint32_t differ = 0; // (k...Bit: the effects set again at a swap)
    bool wants_baked_sun = false; // (either side's shadows from the bake, or its bounce)
    int applied = 0;
};

CompareState& compare_state()
{
    static CompareState state;
    return state;
}

// Side `side`'s settings for the effects that differ (a swap: rt::settings_swap - the histories kept). The shadows
// before the bounce, as read_rt_shadows_and_gi.
void set_effects(const EffectSet& set, uint32_t which)
{
    render::rt::set_settings_swap(true);
    if ((which & kShadowsBit) != 0U) {
        render::vulkan::set_rt_shadow_settings(set.shadows);
    }
    if ((which & kAoBit) != 0U) {
        render::vulkan::set_rt_ao_settings(set.ao);
    }
    if ((which & kGiBit) != 0U) {
        render::vulkan::set_rt_gi_settings(set.gi);
    }
    if ((which & kReflectionsBit) != 0U) {
        render::vulkan::set_rt_reflection_settings(set.reflections);
    }
    if ((which & kHdrBit) != 0U) {
        render::vulkan::set_hdr_settings(set.hdr);
    }
    if ((which & kFogBit) != 0U) {
        render::vulkan::set_rt_fog_settings(set.fog);
    }
    if ((which & kSunBit) != 0U) {
        render::vulkan::set_rt_sun_settings(set.sun);
    }
    if ((which & kSkyBit) != 0U) {
        render::vulkan::set_rt_sky_settings(set.sky);
    }
    // (one side's shadows off would clear the baked sun the other's wants: render_pc fits it once a world)
    if (compare_state().wants_baked_sun) {
        render::rt::set_baked_sun_wanted(true);
    }
    render::rt::set_settings_swap(false);
}

void compare_apply(int side)
{
    CompareState& state = compare_state();
    const int wanted = side == 1 ? 1 : 0;
    if (!state.on || wanted == state.applied) {
        return;
    }
    set_effects(core::at(state.sides, static_cast<std::size_t>(wanted)), state.differ);
    state.applied = wanted;
}

void compare_end()
{
    compare_apply(0);
    compare_state().on = false;
}

bool compare_prepare(const render::graphics_options::CompareSettings& overrides, std::vector<std::string>* both_sides)
{
    CompareState& state = compare_state();
    compare_apply(0);
    state.overrides.clear();
    state.differ = 0;
    for (const auto& [key, value] : overrides) {
        const LiveKey* live = live_key_of(key);
        const uint32_t bits = live != nullptr ? effect_bits(live->read) : 0U;
        if (bits == 0U) {
            if (both_sides != nullptr) {
                both_sides->push_back(key);
            }
            continue;
        }
        state.differ |= bits;
        state.overrides.emplace_back(key, value);
    }
    graphics_overrides() = nullptr;
    std::get<0>(state.sides) = make_effects();
    graphics_overrides() = &state.overrides;
    std::get<1>(state.sides) = make_effects();
    graphics_overrides() = nullptr;
    const auto wants = [](const EffectSet& set) {
        return set.shadows.on || set.gi.on; // (every scene sun but a fixed one needs the fit)
    };
    state.wants_baked_sun = wants(std::get<0>(state.sides)) || wants(std::get<1>(state.sides));
    // (asked for now: render_pc fits the baked sun in a new world's first frame - with A's shadows off and B's on, A's
    // settings alone wouldn't ask, and B's shadows would have no sun)
    if (state.wants_baked_sun) {
        render::rt::set_baked_sun_wanted(true);
    }
    // (the depth samplable for B's effects although A's have them off: vulkan_gpu's depth targets)
    if (state.differ != 0U) {
        render::rt::set_depth_sampling_wanted(true);
    }
    state.on = true;
    state.applied = 0;
    get_log("renderer: split comparison - %zu setting(s) differ on the right (effects 0x%02X)%s",
            state.overrides.size(), state.differ, both_sides != nullptr && !both_sides->empty() ? "; some can't" : "");
    return true;
}

// A [Graphics] key changed while comparing: side A made again from the ini (B keeps its overrides).
void refresh_compare()
{
    const CompareState& state = compare_state();
    if (!state.on) {
        return;
    }
    const render::graphics_options::CompareSettings overrides = state.overrides;
    compare_prepare(overrides, nullptr);
}

// A [Graphics] key changed while the game runs: its effect's settings read again (true), or false when it waits for
// the restart.
bool reload_live_key(const char* key)
{
    const std::string_view name(key);
    const auto live = std::ranges::find_if(
        kLiveKeys, [name](const LiveKey& each) { return each.prefix ? name.starts_with(each.key) : name == each.key; });
    if (live == kLiveKeys.end()) {
        return false;
    }
    live->read();
    get_log("renderer: [Graphics] %s changed - put to use at once", key);
    refresh_compare();
    return true;
}
#endif

// The Vulkan backend: render_pc's device over Vulkan's Gpu and GpuContext, and its UI backend (src/gpu_vulkan; the
// ImGui parts have none on Vulkan yet). The shader and pipeline caches are kept beside the exe. The effects' settings
// are read here and again, one effect at a time, when the Options screen changes one (reload_live_key).
void install_vulkan()
{
#ifdef THUG2_VULKAN
    render::vulkan::configure({.device_index = build_info::get_int(build_info::kGraphics, "vulkan_device", -1),
                               .cache_folder = build_info::executable_directory(),
                               .validation = build_info::fetch_flag(build_info::kGraphics, "vulkan_validation", false),
                               .ray_tracing = build_info::fetch_flag(build_info::kGraphics, "ray_tracing", false)});
    live_state().hdr_at_start = build_info::fetch_flag(build_info::kGraphics, "rt_hdr", false);
    read_sun_source(); // (added by the scene's sun: before the relit mode, which it may turn on)
    read_rt_shadows();
    read_rt_ao();
    read_rt_gi();
    read_rt_lights(); // (NOT ORIGINAL - the night lighting)
    read_rt_reflections();
    read_hdr();
    read_rt_fog();
    read_rt_sun();
    read_rt_sky();
    read_rt_relit();
    read_upscale();
    read_rt_pbr();
    read_rt_detail(); // (NOT ORIGINAL - detail materials and de-lighting)
    read_style();
    read_grass(); // (NOT ORIGINAL - the grass: gpu_vulkan/vulkan_grass.h)
    read_rt_cel(); // (added by cel shading)
    read_water();
    read_camera_fx(); // (NOT ORIGINAL - the camera effects: gpu_vulkan/vulkan_camera_fx.h)
    render::graphics_options::set_live_reload(&reload_live_key);
    render::graphics_options::set_compare_backend(
        {.prepare = &compare_prepare, .apply = &compare_apply, .end = &compare_end});
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
