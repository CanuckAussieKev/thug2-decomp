#include "app/renderer_choice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "app/settings_capabilities.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/config/settings_model.h"
#include "engine/config/settings_state.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/paint_trails.h"
#include "engine/gfx/render_device.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_daylight.h"
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
#include "render_pc/d3d9_device.h" // (the PC layer's own device: API-neutral, over render::Gpu)
#ifndef THUG2_PORTABLE
#include "gpu_d3d9/d3d9_context.h"
#include "gpu_d3d9/d3d9_ui_backend.h"
#include "gpu_d3d9/gpu_d3d9.h"
#endif
#ifdef THUG2_VULKAN
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_cel.h"
#include "gpu_vulkan/style_film_settings.h"
#endif

// NOT ORIGINAL - every setting the renderer reads comes from the settings model (engine/config/settings_model.h): its
// effective values - a setting out of effect is off (Shadows without ray tracing), one another sets has that value
// (the relit mode under a time of day; the upscaler under frame generation), a choice this machine can't run has the
// one that stands in for it. The model says why (the log, the Options screen, the settings report); nothing here
// decides it again.

namespace app {

namespace {

using config::Resolution;

#ifndef THUG2_PORTABLE
void install_direct3d9()
{
    render::install_device(render::d3d9::device());
    render::install_gpu(render::d3d9::gpu());
    render::install_gpu_context(render::d3d9::gpu_context());
    render::install_ui_backend(render::d3d9::ui_backend());
}
#endif

// NOT ORIGINAL - the time of day (engine/gfx/rt_daylight.h, day_clock.h): [Graphics] tod (level - the default, the level
// as it is -, dawn, morning, noon, afternoon, dusk, night or HH:MM), tod_cycle (off / on: the clock runs),
// tod_cycle_minutes (real minutes a day, 24) and tod_levels (NO:night,AU:dusk - a level's own). Any renderer: on Direct3D
// 9 the original's time of day gives the look (its states blended), with Vulkan's effects the sky, the sun and relit
// follow it too. Anything else is the default, logged.
void read_time_of_day(const Resolution& in_effect)
{
    render::rt::TodSettings settings;
    const std::string& tod = in_effect.value("tod");
    if (!render::rt::parse_tod(tod, &settings.choice)) {
        get_log("[Graphics] tod=%s isn't level, dawn, morning, noon, afternoon, dusk, night or HH:MM - level",
                tod.c_str());
    }
    settings.cycle = in_effect.value("tod_cycle") == "on";
    if (in_effect.at("tod_cycle_minutes").in_effect) {
        settings.cycle_minutes = in_effect.number("tod_cycle_minutes", settings.cycle_minutes);
    }
    const std::string& levels = in_effect.value("tod_levels");
    if (!render::rt::parse_tod_levels(levels, &settings.levels)) {
        get_log("[Graphics] tod_levels=%s: an entry isn't <level>:<tod> - those before it used", levels.c_str());
    }
    render::rt::set_tod_settings(settings);
    if (render::rt::tod_may_run(settings)) {
        get_log("[Graphics] tod=%s%s%s", render::rt::tod_text(settings.choice).c_str(),
                settings.cycle ? text::format(" - the clock running, a day in %.0f minutes",
                                              static_cast<double>(render::rt::tod_settings().cycle_minutes))
                                     .c_str()
                               : "",
                settings.levels.empty() ? "" : " (some levels their own: tod_levels)");
    }
}

#ifdef THUG2_VULKAN
// A count read from the model: at least 1.
unsigned at_least_one(const Resolution& in_effect, const char* key, unsigned fallback)
{
    return static_cast<unsigned>(std::max(1, in_effect.integer(key, static_cast<int>(fallback))));
}

// (added by the scene's sun) [Graphics] rt_sun_source: the one sun every effect lights by (engine/gfx/rt_scene.h
// scene_sun) - level (the default: the sun the level's lighting was baked with), game (the sun the game shows, the
// level relit by it) or x,y,z / heading,elevation (a direction, for experiments); anything else is the default,
// logged. The per-effect keys it replaces (rt_shadow_sun, rt_relit_sun, rt_sun_place) are read no more - a line in the
// log says so. One sun for the whole frame: the split comparison's sides share it.
constexpr const char* kDefaultSunSource = "level";
void read_sun_source(const Resolution& in_effect)
{
    const std::string& text = in_effect.value("rt_sun_source");
    render::rt::SunChoice choice;
    if (!render::rt::parse_sun_source(text, &choice)) {
        get_log("[Graphics] rt_sun_source=%s isn't game, level, x,y,z or heading,elevation - %s used", text.c_str(),
                kDefaultSunSource);
        render::rt::parse_sun_source(kDefaultSunSource, &choice);
    }
    render::rt::set_sun_choice(choice);
    static bool retired_logged = false;
    if (!retired_logged) {
        retired_logged = true;
        for (const char* retired : {"rt_shadow_sun", "rt_relit_sun", "rt_sun_place"}) {
            constexpr std::size_t kLongest = 64;
            std::array<char, kLongest> old{};
            build_info::fetch_text(build_info::kGraphics, retired, "", old.data(), old.size());
            if (old.front() != '\0') {
                get_log("[Graphics] %s=%s is retired: rt_sun_source=%s sets the sun of every effect", retired,
                        old.data(), text.c_str());
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

// What the device was made for: rt_hdr as the game started (its colour targets' format - a start-up setting, so the
// running model keeps the value the game started with).
bool hdr_on(const Resolution& in_effect) { return in_effect.on("rt_hdr"); }

// NOT ORIGINAL - ray-traced sun shadows (gpu_vulkan/vulkan_rt_shadows.h)
render::vulkan::RtShadowSettings make_rt_shadows(const Resolution& in_effect)
{
    const render::vulkan::RtShadowSettings defaults;
    return render::vulkan::RtShadowSettings{
        .on = in_effect.on("rt_shadows"),
        .strength = in_effect.number("rt_shadow_strength", defaults.strength),
        .softness = in_effect.number("rt_shadow_softness", defaults.softness),
        .rays = at_least_one(in_effect, "rt_shadow_rays", defaults.rays),
        // (1 by default since the water and glass work: the skater's own projected shadow - a blocky 256 x 256
        // texture - is hidden while the ray-traced one is drawn; rt::object_shadows_replaced)
        .objects = in_effect.on("rt_shadow_objects"),
        .covered = in_effect.on("rt_shadow_covered"),
        .sky_rays = at_least_one(in_effect, "rt_shadow_sky_rays", render::vulkan::kRtShadowSkyRays),
        .match_bake = in_effect.on("rt_shadow_match_bake")};
}

// NOT ORIGINAL - ray-traced ambient occlusion (gpu_vulkan/vulkan_rt_effects.h)
render::vulkan::RtAoSettings make_rt_ao(const Resolution& in_effect)
{
    const render::vulkan::RtAoSettings defaults;
    return render::vulkan::RtAoSettings{
        .on = in_effect.on("rt_ao"),
        .strength = in_effect.number("rt_ao_strength", defaults.strength),
        .radius = in_effect.number("rt_ao_radius", defaults.radius),
        .rays = at_least_one(in_effect, "rt_ao_rays", defaults.rays),
        .denoise = in_effect.on("rt_ao_denoise"),
        // (added for the characters) the occlusion on the skinned characters x this
        .characters = std::clamp(in_effect.number("rt_ao_characters", defaults.characters), 0.0F, 1.0F)};
}

// NOT ORIGINAL - one-bounce ray-traced indirect light (gpu_vulkan/vulkan_rt_effects.h)
render::vulkan::RtGiSettings make_rt_gi(const Resolution& in_effect)
{
    const render::vulkan::RtGiSettings defaults;
    return render::vulkan::RtGiSettings{.on = in_effect.on("rt_gi"),
                                        .strength = in_effect.number("rt_gi_strength", defaults.strength),
                                        .distance = in_effect.number("rt_gi_distance", defaults.distance),
                                        .rays = at_least_one(in_effect, "rt_gi_rays", defaults.rays),
                                        .denoise = in_effect.on("rt_gi_denoise")};
}

// NOT ORIGINAL - the night lighting (gpu_vulkan/vulkan_rt_lights.h): rt_lights=auto, the default, lit only while it is
// dark - the time of day's night, or a dark level; 1 always, 0 never
render::vulkan::RtLightSettings make_rt_lights(const Resolution& in_effect)
{
    const render::vulkan::RtLightSettings defaults;
    const std::string& value = in_effect.value("rt_lights");
    return render::vulkan::RtLightSettings{.on = in_effect.on("rt_lights"),
                                           .automatic = value == "auto",
                                           .strength = in_effect.number("rt_lights_strength", defaults.strength),
                                           .shadowed = at_least_one(in_effect, "rt_lights_shadowed", defaults.shadowed),
                                           .max_lights = at_least_one(in_effect, "rt_lights_max", defaults.max_lights),
                                           .emissive = in_effect.number("rt_lights_emissive", defaults.emissive),
                                           .level = in_effect.number("rt_lights_level", defaults.level),
                                           .log = in_effect.on("rt_lights_log")};
}

// NOT ORIGINAL - ray-traced reflections (gpu_vulkan/vulkan_rt_reflections.h)
render::vulkan::RtReflectionSettings make_rt_reflections(const Resolution& in_effect)
{
    return render::vulkan::RtReflectionSettings{
        .on = in_effect.on("rt_reflections"),
        .strength = in_effect.number("rt_reflection_strength", render::vulkan::RtReflectionSettings{}.strength),
        .fresnel = in_effect.on("rt_reflection_fresnel"),
        .glass_roughness = in_effect.number("rt_glass_roughness", render::vulkan::kRtGlassRoughness),
        .glass_reflectance = in_effect.number("rt_glass_reflectance", render::vulkan::kRtGlassReflectance),
        // (out of effect without rt_pbr: no surface maps to say which surfaces are glossy)
        .surfaces = in_effect.at("rt_reflection_surfaces").in_effect
                        ? in_effect.number("rt_reflection_surfaces", render::vulkan::kRtReflectionSurfaces)
                        : 0.0F};
}

// NOT ORIGINAL - HDR rendering and tone mapping (gpu_vulkan/vulkan_hdr.h). The grading table's path is the game
// folder's unless it is a full one.
render::vulkan::HdrSettings make_hdr(const Resolution& in_effect)
{
    std::string lut_path = in_effect.at("rt_grade_lut").in_effect ? in_effect.value("rt_grade_lut") : std::string();
    const bool full = lut_path.find(':') == 1 || lut_path.starts_with('\\') || lut_path.starts_with('/');
    if (!lut_path.empty() && !full) {
        lut_path = build_info::executable_directory() + lut_path;
    }
    const render::vulkan::HdrSettings defaults;
    return render::vulkan::HdrSettings{.on = hdr_on(in_effect),
                                       .exposure = in_effect.number("rt_exposure", defaults.exposure),
                                       .tonemap =
                                           static_cast<unsigned>(std::max(0, in_effect.integer("rt_tonemap", 0))),
                                       .saturation = in_effect.number("rt_saturation", defaults.saturation),
                                       .contrast = in_effect.number("rt_contrast", defaults.contrast),
                                       .white_point = in_effect.number("rt_white_point", defaults.white_point),
                                       .grade_lut = lut_path};
}

// NOT ORIGINAL - volumetric fog, its light shafts and the haze of a level without fog (gpu_vulkan/vulkan_rt_fog.h)
render::vulkan::RtFogSettings make_rt_fog(const Resolution& in_effect)
{
    const render::vulkan::RtFogSettings defaults;
    return render::vulkan::RtFogSettings{.on = in_effect.on("rt_fog"),
                                         .density = in_effect.number("rt_fog_density", defaults.density),
                                         .height = in_effect.number("rt_fog_height", defaults.height),
                                         .anisotropy = in_effect.number("rt_fog_anisotropy", defaults.anisotropy),
                                         .steps = at_least_one(in_effect, "rt_fog_steps", render::vulkan::kRtFogSteps),
                                         .mode = in_effect.integer("rt_fog_mode", 0) == 1
                                                     ? render::vulkan::RtFogMode::kShafts
                                                     : render::vulkan::RtFogMode::kReplace,
                                         .hdr = hdr_on(in_effect),
                                         .haze = in_effect.on("rt_fog_haze"),
                                         .haze_density = in_effect.number("rt_fog_haze_density", defaults.haze_density),
                                         .haze_height = in_effect.number("rt_fog_haze_height", defaults.haze_height)};
}

// NOT ORIGINAL - the sun's disc and glare, screen-space rays, the bloom and the sky model (gpu_vulkan/vulkan_rt_sun.h,
// vulkan_rt_sky.h). rt_sun_colour: auto (the sky model's colour for the sun's elevation) or r,g,b (0 - 1 each).
render::vulkan::RtSunSettings make_rt_sun(const Resolution& in_effect)
{
    render::vulkan::RtSunSettings sun{
        .sun = in_effect.on("rt_sun"),
        .sun_size = in_effect.number("rt_sun_size", render::vulkan::RtSunSettings{}.sun_size),
        .bloom = in_effect.on("rt_bloom"),
        .bloom_strength = in_effect.number("rt_bloom_strength", render::vulkan::kRtBloomStrength),
        .shafts = in_effect.on("rt_shafts"),
        .shaft_strength = in_effect.number("rt_shaft_strength", render::vulkan::kRtShaftStrength),
        .sky_model = in_effect.on("rt_sky_model"),
        .lens_dirt = in_effect.on("rt_lens_dirt"),
        .hdr = hdr_on(in_effect)};
    const config::Resolved& colour_setting = in_effect.at("rt_sun_colour");
    std::array<float, 3> colour{};
    if (colour_setting.in_effect && parse_rgb(colour_setting.value, &colour)) {
        sun.colour_set = true;
        sun.colour = colour;
    } else if (colour_setting.in_effect && colour_setting.value != "auto") {
        get_log("[Graphics] rt_sun_colour=%s isn't auto or r,g,b - the sky model's colour used",
                colour_setting.value.c_str());
    }
    return sun;
}

// NOT ORIGINAL - a modern sky: the atmosphere and volumetric clouds (gpu_vulkan/vulkan_rt_sky_pass.h).
// rt_cloud_coverage: auto (the level's own sky) or 0 - 1.
render::vulkan::RtSkySettings make_rt_sky(const Resolution& in_effect)
{
    float cover = render::vulkan::kRtCloudCoverageAuto;
    const std::string& cover_text = in_effect.value("rt_cloud_coverage");
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
        .on = in_effect.on("rt_sky"),
        .match = in_effect.number("rt_sky_match", render::vulkan::kRtSkyMatch),
        .clouds = in_effect.on("rt_clouds"),
        .coverage = cover,
        .speed = in_effect.number("rt_cloud_speed", 1.0F),
        .quality = static_cast<unsigned>(std::max(0, in_effect.integer("rt_cloud_quality", 1))),
        .cloud_shadows = in_effect.on("rt_cloud_shadows"),
        .hdr = hdr_on(in_effect)};
}

// NOT ORIGINAL - the relit mode (engine/gfx/rt_relit.h, render_pc/rt_relit_world.h): on when asked for, or when the
// model forces it (a sun other than the baked one, the time of day's, or a lighting pass needs the level relit); it
// needs ray tracing and the level's relit data (the level's facts: engine/gfx/relit_data.h)
void read_rt_relit(const Resolution& in_effect)
{
    const render::rt::RelitSettings defaults;
    render::rt::set_relit_settings(
        {.on = in_effect.on("rt_relit"),
         .strength = in_effect.number("rt_relit_strength", defaults.strength),
         .residual = in_effect.on("rt_relit_residual"),
         .sky_rays = at_least_one(in_effect, "rt_relit_sky_rays", render::rt::kRelitSkyRays),
         .sun_rays = at_least_one(in_effect, "rt_relit_sun_rays", render::rt::kRelitSunRays)});
}

// NOT ORIGINAL - frame generation and Reflex (gpu_vulkan/vulkan_framegen.h): DLSS-G / FSR 3 from beside the exe. Read
// at start-up only (the swapchain and the device's queues are made for it).
void read_frame_generation(const Resolution& in_effect)
{
    render::vulkan::FrameGenSettings settings;
    if (!render::vulkan::parse_frame_generation(in_effect.value("frame_generation"), &settings.mode)) {
        settings.mode = render::vulkan::FrameGeneration::kOff;
    }
    if (!render::vulkan::parse_frame_generation_source(in_effect.value("frame_generation_provider"),
                                                       &settings.source)) {
        settings.source = render::vulkan::FrameGenSource::kAuto;
    }
    if (!render::vulkan::parse_reflex(in_effect.value("reflex"), &settings.reflex)) {
        settings.reflex = render::vulkan::Reflex::kOff;
    }
    render::vulkan::set_frame_generation_settings(settings);
}

// NOT ORIGINAL - upscaling (gpu_vulkan/vulkan_upscale.h): DLSS / FSR from beside the exe. Read at start-up only (the
// device's targets and the provider are made for it). Under frame generation with no upscaler asked for, the model has
// it on at native size (DLAA / FSR's anti-aliasing: frame generation makes its frames from its motion vectors).
void read_upscale(const Resolution& in_effect)
{
    render::vulkan::UpscaleSettings upscale;
    if (!render::vulkan::parse_upscaler(in_effect.value("upscaler"), &upscale.upscaler)) {
        upscale.upscaler = render::vulkan::Upscaler::kOff;
    }
    if (!render::vulkan::parse_upscale_quality(in_effect.value("upscale_quality"), &upscale.quality)) {
        upscale.quality = render::vulkan::UpscaleQuality::kQuality;
    }
    upscale.ray_reconstruction = in_effect.on("ray_reconstruction");
    upscale.sharpness = in_effect.number("upscale_sharpness", 0.0F);
    upscale.fsr_major = static_cast<unsigned>(std::max(0, in_effect.integer("fsr_version", 0)));
    render::vulkan::set_upscale_settings(upscale);
}

// The knobs' ranges: relief depth up to 4x, the characters' and the detail's strengths up to 2x.
constexpr float kMostRelief = 4.0F;
constexpr float kMostStrength = 2.0F;

// NOT ORIGINAL - per-pixel lighting and the surface maps beside the textures (gpu_vulkan/vulkan_surface.h)
void read_rt_pbr(const Resolution& in_effect)
{
    const render::vulkan::RtPbrSettings defaults;
    render::vulkan::set_rt_pbr_settings(
        {.on = in_effect.on("rt_pbr"),
         .specular = std::max(0.0F, in_effect.number("rt_pbr_specular", defaults.specular)),
         .normal_strength = std::max(0.0F, in_effect.number("rt_pbr_normal_strength", defaults.normal_strength)),
         .objects = in_effect.on("rt_pbr_objects"),
         // (added by surface relief - parallax occlusion mapping on the surface-mapped brick, stone, gravel...)
         .relief = in_effect.on("rt_relief"),
         .relief_strength =
             std::clamp(in_effect.number("rt_relief_strength", defaults.relief_strength), 0.0F, kMostRelief),
         // (added for the characters - their surface maps and skin / cloth / hair shading; on top of rt_pbr)
         .characters = in_effect.on("rt_characters"),
         .skin = std::clamp(in_effect.number("rt_characters_skin", defaults.skin), 0.0F, kMostStrength),
         .cloth = std::clamp(in_effect.number("rt_characters_cloth", defaults.cloth), 0.0F, kMostStrength),
         .hair = std::clamp(in_effect.number("rt_characters_hair", defaults.hair), 0.0F, kMostStrength)});
}

// NOT ORIGINAL - detail materials (gpu_vulkan/vulkan_detail.h): on the draws rt_pbr lights
void read_rt_detail(const Resolution& in_effect)
{
    const render::vulkan::RtDetailSettings defaults;
    render::vulkan::set_rt_detail_settings(
        {.on = in_effect.on("rt_detail"),
         .strength = std::clamp(in_effect.number("rt_detail_strength", defaults.strength), 0.0F, kMostStrength)});
}

// [Graphics] rt_style_noir_dark / _light: r,g,b; anything else is the default (logged when it isn't empty).
std::array<float, 3> style_colour(const Resolution& in_effect, const char* key, const std::array<float, 3>& fallback)
{
    const std::string& text = in_effect.value(key);
    std::array<float, 3> colour = fallback;
    if (!text.empty() && !parse_rgb(text, &colour)) {
        get_log("[Graphics] %s=%s isn't r,g,b - the default used", key, text.c_str());
    }
    return colour;
}

// [Graphics] rt_style as a name (off when it is out of effect).
render::vulkan::ArtStyle art_style_of(const Resolution& in_effect)
{
    render::vulkan::ArtStyle style = render::vulkan::ArtStyle::kOff;
    if (!render::vulkan::parse_art_style(in_effect.value("rt_style"), &style)) {
        style = render::vulkan::ArtStyle::kOff;
    }
    return style;
}

// NOT ORIGINAL - the vhs, retro and watercolour art styles' knobs (gpu_vulkan/vulkan_style_film.h), with rt_style as the
// toon's reading set it (the retro geometry is on with it)
void read_style_film(const Resolution& in_effect)
{
    const render::vulkan::VhsSettings vhs;
    const render::vulkan::RetroSettings retro;
    const render::vulkan::WatercolourSettings watercolour;
    render::vulkan::set_film_settings(
        {.style = render::vulkan::style_settings().style,
         .vhs = {.fisheye = in_effect.number("rt_style_vhs_fisheye", vhs.fisheye),
                 .bleed = in_effect.number("rt_style_vhs_bleed", vhs.bleed),
                 .wobble = in_effect.number("rt_style_vhs_wobble", vhs.wobble),
                 .noise = in_effect.number("rt_style_vhs_noise", vhs.noise),
                 .scanlines = in_effect.number("rt_style_vhs_scanlines", vhs.scanlines),
                 .saturation = in_effect.number("rt_style_vhs_saturation", vhs.saturation),
                 .overlay = in_effect.on("rt_style_vhs_overlay")},
         .retro = {.lines = in_effect.number("rt_style_retro_lines", retro.lines),
                   .bits = in_effect.number("rt_style_retro_bits", retro.bits),
                   .dither = in_effect.number("rt_style_retro_dither", retro.dither),
                   .snap = in_effect.number("rt_style_retro_snap", retro.snap),
                   .affine = in_effect.number("rt_style_retro_affine", retro.affine)},
         .watercolour = {.bleed = in_effect.number("rt_style_watercolour_bleed", watercolour.bleed),
                         .flow = in_effect.number("rt_style_watercolour_flow", watercolour.flow),
                         .granulation = in_effect.number("rt_style_watercolour_granulation", watercolour.granulation),
                         .edges = in_effect.number("rt_style_watercolour_edges", watercolour.edges),
                         .paper = in_effect.number("rt_style_watercolour_paper", watercolour.paper),
                         .outlines = in_effect.number("rt_style_watercolour_outlines", watercolour.outlines)}});
}

// NOT ORIGINAL - cel shading (engine/gfx/rt_cel.h, gpu_vulkan/vulkan_cel.h): on with rt_style=cel and only then (an
// older ini's rt_cel line is retired in the model: it turned cel on under Art style Off, which nothing in the Options
// screen could undo).
void read_rt_cel(const Resolution& in_effect)
{
    const render::rt::CelSettings defaults;
    render::rt::CelSettings cel{.on = art_style_of(in_effect) == render::vulkan::ArtStyle::kCel,
                                .bands = static_cast<uint32_t>(
                                    std::max(0, in_effect.integer("rt_cel_bands", static_cast<int>(defaults.bands)))),
                                .shadow_colour = defaults.shadow_colour,
                                .rim = in_effect.number("rt_cel_rim", defaults.rim),
                                .outline_width = in_effect.number("rt_cel_outline_width", defaults.outline_width),
                                .flatten = in_effect.number("rt_cel_flatten", defaults.flatten)};
    const std::string& shadow = in_effect.value("rt_cel_shadow_colour");
    if (!shadow.empty() && !render::rt::parse_cel_colour(shadow, &cel.shadow_colour)) {
        get_log("[Graphics] rt_cel_shadow_colour=%s isn't r,g,b or #rrggbb - the default kept", shadow.c_str());
    }
    render::vulkan::set_cel_settings(cel);
}

// NOT ORIGINAL - an art style (gpu_vulkan/vulkan_style.h): no ray tracing or HDR needed
void read_style(const Resolution& in_effect)
{
    const render::vulkan::StyleSettings defaults;
    render::vulkan::set_style_settings(
        {.style = art_style_of(in_effect),
         .strength = in_effect.number("rt_style_strength", defaults.strength),
         .smooth = in_effect.number("rt_style_smooth", defaults.smooth),
         .bands = in_effect.number("rt_style_bands", defaults.bands),
         .outlines = in_effect.number("rt_style_outlines", defaults.outlines),
         .outline_width = in_effect.number("rt_style_outline_width", defaults.outline_width),
         .saturation = in_effect.number("rt_style_saturation", defaults.saturation),
         .halftone = in_effect.number("rt_style_comic_halftone", defaults.halftone),
         .hatch = in_effect.number("rt_style_sketch_hatch", defaults.hatch),
         .wash = in_effect.number("rt_style_sketch_wash", defaults.wash),
         .paper = in_effect.number("rt_style_paper", defaults.paper),
         .grain = in_effect.number("rt_style_grain", defaults.grain),
         .vignette = in_effect.number("rt_style_vignette", defaults.vignette),
         .contrast = in_effect.number("rt_style_noir_contrast", defaults.contrast),
         .focus = in_effect.number("rt_style_thermal_focus", defaults.focus),
         .noir_dark = style_colour(in_effect, "rt_style_noir_dark", defaults.noir_dark),
         .noir_light = style_colour(in_effect, "rt_style_noir_light", defaults.noir_light)});
    read_style_film(in_effect);
}

// NOT ORIGINAL - grass (gpu_vulkan/vulkan_grass.h): blades where the level's collision says grass
void read_grass(const Resolution& in_effect)
{
    const render::vulkan::GrassSettings defaults;
    render::vulkan::set_grass_settings({.on = in_effect.on("grass"),
                                        .density = in_effect.number("grass_density", defaults.density),
                                        .height = in_effect.number("grass_height", defaults.height),
                                        .distance = in_effect.number("grass_distance", defaults.distance)});
}

// NOT ORIGINAL - snow (gpu_vulkan/vulkan_snow.h): a layer over every outdoor level's open ground, its trails and spray
void read_snow(const Resolution& in_effect)
{
    const render::vulkan::SnowSettings defaults;
    render::vulkan::set_snow_settings({.on = in_effect.on("snow"),
                                       .depth = in_effect.number("snow_depth", defaults.depth),
                                       .quality = static_cast<int>(std::lround(in_effect.number(
                                           "snow_trail_quality", static_cast<float>(defaults.quality)))),
                                       .falling = in_effect.on("snowfall")});
}

// NOT ORIGINAL - paint trails (gpu_vulkan/vulkan_paint.h): every skater's line of paint, widening with its combo
void read_paint(const Resolution& in_effect)
{
    const render::vulkan::PaintSettings defaults;
    render::vulkan::set_paint_settings(
        {.on = in_effect.on("paint_trails"),
         .scale = in_effect.number("paint_trail_scale", defaults.scale),
         .colour = render::paint::colour_from_text(in_effect.value("paint_trail_colour").c_str())});
}

// NOT ORIGINAL - decals and clutter (gpu_vulkan/vulkan_scatter.h): marks and small props over the level's surfaces
void read_scatter(const Resolution& in_effect)
{
    const render::vulkan::ScatterSettings defaults;
    render::vulkan::set_scatter_settings(
        {.decals = in_effect.on("rt_decals"),
         .decal_density = in_effect.number("rt_decals_density", defaults.decal_density),
         .clutter = in_effect.on("clutter"),
         .clutter_density = in_effect.number("clutter_density", defaults.clutter_density)});
}

// NOT ORIGINAL - water shading (gpu_vulkan/vulkan_water.h). rt_water_wind_heading: auto (the level's) or degrees;
// rt_water_tint: auto (the water's own colour as the game draws it) or r,g,b.
void read_water(const Resolution& in_effect)
{
    const render::vulkan::WaterSettings defaults;
    render::vulkan::WaterSettings water{.on = in_effect.on("rt_water"),
                                        .waves = in_effect.number("rt_water_waves", defaults.waves),
                                        .wind = in_effect.number("rt_water_wind", defaults.wind),
                                        .heading = render::vulkan::kWaterHeadingAuto,
                                        .clarity = in_effect.number("rt_water_clarity", defaults.clarity),
                                        .foam = in_effect.number("rt_water_foam", defaults.foam),
                                        .hdr = hdr_on(in_effect),
                                        .screen_reflections = in_effect.on("rt_water_screen_reflections"),
                                        .debug_view = in_effect.on("rt_water_view"),
                                        .geometry = in_effect.on("rt_water_geometry")};
    const std::string& heading = in_effect.value("rt_water_wind_heading");
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
    const std::string& tint = in_effect.value("rt_water_tint");
    std::array<float, 3> colour{};
    if (parse_rgb(tint, &colour)) {
        water.tint_auto = false;
        water.tint = colour;
    } else if (!tint.empty() && tint != "auto") {
        get_log("[Graphics] rt_water_tint=%s isn't auto or r,g,b - the water's own colour used", tint.c_str());
    }
    render::vulkan::set_water_settings(water);
}

// NOT ORIGINAL - the camera effects (gpu_vulkan/vulkan_camera_fx.h): motion blur, depth of field, the colour grade, the
// fisheye lens' knobs (vulkan_fisheye.h - the lens itself is the art styles fisheye and fisheye_vhs: rt_style).
// An older ini's rt_fisheye line (the lens' own switch before it became an art style) does nothing - logged once.
void read_camera_fx(const Resolution& in_effect)
{
    static bool old_switch_told = false;
    if (!old_switch_told) {
        constexpr std::size_t kLongest = 8;
        std::array<char, kLongest> old_switch{};
        build_info::fetch_text(build_info::kGraphics, "rt_fisheye", "", old_switch.data(), old_switch.size());
        if (old_switch.front() != '\0') {
            old_switch_told = true;
            get_log("[Graphics] rt_fisheye=%s does nothing: the fisheye lens is an art style now (rt_style=fisheye or "
                    "fisheye_vhs)",
                    old_switch.data());
        }
    }
    const render::vulkan::CameraFxSettings defaults;
    render::vulkan::CameraFxSettings settings{
        .motion_blur = in_effect.on("rt_motion_blur"),
        .motion_blur_strength = in_effect.number("rt_motion_blur_strength", defaults.motion_blur_strength),
        .dof = in_effect.on("rt_dof"),
        .dof_strength = in_effect.number("rt_dof_strength", defaults.dof_strength),
        .grade = render::vulkan::GradeLook::kOff,
        .grade_strength = in_effect.number("rt_grade_strength", defaults.grade_strength),
        // (out of effect under an art style with its own: the style's is used)
        .grade_vignette = in_effect.at("rt_grade_vignette").in_effect
                              ? in_effect.number("rt_grade_vignette", defaults.grade_vignette)
                              : 0.0F,
        .grade_grain =
            in_effect.at("rt_grade_grain").in_effect ? in_effect.number("rt_grade_grain", defaults.grade_grain) : 0.0F,
        .grade_levels = in_effect.value("rt_grade_levels"),
        .fisheye_strength = in_effect.number("rt_fisheye_strength", defaults.fisheye_strength),
        .fisheye_vignette = in_effect.number("rt_fisheye_vignette", defaults.fisheye_vignette),
        .fisheye_circle = in_effect.number("rt_fisheye_circle", defaults.fisheye_circle)};
    if (!render::vulkan::parse_grade_look(in_effect.value("rt_grade"), &settings.grade)) {
        settings.grade = render::vulkan::GradeLook::kOff;
    }
    render::vulkan::set_camera_fx_settings(settings);
}

// Each part of the renderer the model's effects name, read from a resolution (config::Effect's order: the sun's source
// before the effects lit by it, the time of day before the relit mode, the shadows before the bounce light).
void read_effect(config::Effect effect, const Resolution& in_effect)
{
    using config::Effect;
    switch (effect) {
    case Effect::kSunSource:
        read_sun_source(in_effect);
        break;
    case Effect::kTimeOfDay:
        read_time_of_day(in_effect);
        break;
    case Effect::kShadows:
        render::vulkan::set_rt_shadow_settings(make_rt_shadows(in_effect));
        break;
    case Effect::kAo:
        render::vulkan::set_rt_ao_settings(make_rt_ao(in_effect));
        break;
    case Effect::kGi:
        render::vulkan::set_rt_gi_settings(make_rt_gi(in_effect));
        break;
    case Effect::kLights:
        render::vulkan::set_rt_light_settings(make_rt_lights(in_effect));
        break;
    case Effect::kReflections:
        render::vulkan::set_rt_reflection_settings(make_rt_reflections(in_effect));
        break;
    case Effect::kHdr:
        render::vulkan::set_hdr_settings(make_hdr(in_effect));
        break;
    case Effect::kFog:
        render::vulkan::set_rt_fog_settings(make_rt_fog(in_effect));
        break;
    case Effect::kSun:
        render::vulkan::set_rt_sun_settings(make_rt_sun(in_effect));
        break;
    case Effect::kSky:
        render::vulkan::set_rt_sky_settings(make_rt_sky(in_effect));
        break;
    case Effect::kRelit:
        read_rt_relit(in_effect);
        break;
    case Effect::kPbr:
        read_rt_pbr(in_effect);
        break;
    case Effect::kDetail:
        read_rt_detail(in_effect);
        break;
    case Effect::kStyle:
        read_style(in_effect);
        read_rt_cel(in_effect); // (rt_style=cel is cel shading)
        break;
    case Effect::kCel:
        read_rt_cel(in_effect);
        break;
    case Effect::kGrass:
        read_grass(in_effect);
        break;
    case Effect::kScatter:
        read_scatter(in_effect);
        break;
    case Effect::kWater:
        read_water(in_effect);
        break;
    case Effect::kSnow:
        read_snow(in_effect);
        break;
    case Effect::kPaint:
        read_paint(in_effect);
        break;
    case Effect::kCameraFx:
        read_camera_fx(in_effect);
        break;
    case Effect::kNone:
    default:
        break;
    }
}

// The running settings the renderer was last given (a change read again is compared with them: every part whose
// settings' effective values changed is read again - the key's own, and any another rule moved with it).
struct LiveState {
    Resolution applied;
};

LiveState& live_state()
{
    static LiveState state;
    return state;
}

void read_all_effects(const Resolution& in_effect)
{
    for (int effect = 1; effect < config::kEffectCount; ++effect) {
        read_effect(static_cast<config::Effect>(effect), in_effect);
    }
    live_state().applied = in_effect;
}

// --- a split comparison (engine/gfx/graphics_options.h CompareBackend, devtools/compare_split.h): the effects' settings
// of a side - A, the running model's; B, the model resolved with the comparison's overrides over the ini ---

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

// Which effects can differ per side (EffectSet's members, one bit each - its Effect's; 0: an effect both sides share:
// the relit mode changes how the world itself is drawn and keeps a cache).
uint32_t effect_bit(config::Effect effect)
{
    using config::Effect;
    constexpr std::array kSplit = {Effect::kShadows, Effect::kAo,  Effect::kGi,  Effect::kReflections,
                                   Effect::kHdr,     Effect::kFog, Effect::kSun, Effect::kSky};
    return std::ranges::find(kSplit, effect) != kSplit.end() ? 1U << static_cast<uint32_t>(effect) : 0U;
}

EffectSet make_effects(const Resolution& in_effect)
{
    return {.shadows = make_rt_shadows(in_effect),
            .ao = make_rt_ao(in_effect),
            .gi = make_rt_gi(in_effect),
            .reflections = make_rt_reflections(in_effect),
            .hdr = make_hdr(in_effect),
            .fog = make_rt_fog(in_effect),
            .sun = make_rt_sun(in_effect),
            .sky = make_rt_sky(in_effect)};
}

struct CompareState {
    bool on = false;
    std::array<EffectSet, 2> sides{};
    render::graphics_options::CompareSettings overrides; // (the ones that can differ)
    uint32_t differ = 0;                                 // (the effects set again at a swap)
    bool wants_baked_sun = false;                        // (either side's shadows from the bake, or its bounce)
    int applied = 0;
};

CompareState& compare_state()
{
    static CompareState state;
    return state;
}

// Side `side`'s settings for the effects that differ (a swap: rt::settings_swap - the histories kept). The shadows
// before the bounce.
void set_effects(const EffectSet& set, uint32_t which)
{
    render::rt::set_settings_swap(true);
    const auto wanted = [which](config::Effect effect) { return (which & effect_bit(effect)) != 0U; };
    if (wanted(config::Effect::kShadows)) {
        render::vulkan::set_rt_shadow_settings(set.shadows);
    }
    if (wanted(config::Effect::kAo)) {
        render::vulkan::set_rt_ao_settings(set.ao);
    }
    if (wanted(config::Effect::kGi)) {
        render::vulkan::set_rt_gi_settings(set.gi);
    }
    if (wanted(config::Effect::kReflections)) {
        render::vulkan::set_rt_reflection_settings(set.reflections);
    }
    if (wanted(config::Effect::kHdr)) {
        render::vulkan::set_hdr_settings(set.hdr);
    }
    if (wanted(config::Effect::kFog)) {
        render::vulkan::set_rt_fog_settings(set.fog);
    }
    if (wanted(config::Effect::kSun)) {
        render::vulkan::set_rt_sun_settings(set.sun);
    }
    if (wanted(config::Effect::kSky)) {
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
        const int found = config::find(build_info::kGraphics, key.c_str());
        const uint32_t bit = found >= 0 && core::at(config::settings(), found).apply == config::Apply::kLive
                                 ? effect_bit(core::at(config::settings(), found).effect)
                                 : 0U;
        if (bit == 0U) {
            if (both_sides != nullptr) {
                both_sides->push_back(key);
            }
            continue;
        }
        state.differ |= bit;
        state.overrides.emplace_back(key, value);
    }
    std::get<0>(state.sides) = make_effects(config::running());
    std::get<1>(state.sides) = make_effects(config::running_with(state.overrides));
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

// A [Graphics] key changed while comparing: side A made again from the running model (B keeps its overrides).
void refresh_compare()
{
    const CompareState& state = compare_state();
    if (!state.on) {
        return;
    }
    const render::graphics_options::CompareSettings overrides = state.overrides;
    compare_prepare(overrides, nullptr);
}

// A [Graphics] key changed while the game runs (the running model already read again - graphics_options::reload_live):
// every part of the renderer whose settings' effective values moved is read again - the key's own, and any another
// setting's rule moved with it (a time of day turning the relit mode on). False when the key isn't one the renderer
// changes while it runs (it waits for the restart).
bool reload_live_key(const char* key)
{
    const int index = config::find(build_info::kGraphics, key);
    if (index < 0) {
        return false;
    }
    const config::Setting& setting = core::at(config::settings(), index);
    if (setting.apply != config::Apply::kLive || setting.effect == config::Effect::kNone) {
        return false;
    }
    const Resolution& now = config::running();
    const Resolution& before = live_state().applied;
    std::array<bool, config::kEffectCount> moved{};
    core::at(moved, static_cast<std::size_t>(setting.effect)) = true;
    for (std::size_t i = 0; i < now.items.size() && i < before.items.size(); ++i) {
        const config::Resolved& was = core::at(before.items, i);
        const config::Resolved& is = core::at(now.items, i);
        if (was.value != is.value || was.in_effect != is.in_effect) {
            core::at(moved, static_cast<std::size_t>(core::at(config::settings(), i).effect)) = true;
        }
    }
    for (int effect = 1; effect < config::kEffectCount; ++effect) {
        if (core::at(moved, static_cast<std::size_t>(effect))) {
            read_effect(static_cast<config::Effect>(effect), now);
        }
    }
    live_state().applied = now;
    get_log("renderer: [Graphics] %s changed - put to use at once", key);
    const std::string changes = config::describe(now);
    if (!changes.empty()) {
        get_log("settings: in effect now -\n%s", changes.c_str());
    }
    refresh_compare();
    return true;
}
#endif

// The Vulkan backend: render_pc's device over Vulkan's Gpu and GpuContext, and its UI backend (src/gpu_vulkan; the
// ImGui parts have none on Vulkan yet). The shader and pipeline caches are kept beside the exe. The effects' settings
// are read here from the running model, and again, part by part, when the Options screen changes one (reload_live_key).
void install_vulkan(const Resolution& in_effect)
{
#ifdef THUG2_VULKAN
    render::vulkan::configure({.device_index = in_effect.integer("vulkan_device", -1),
                               .cache_folder = build_info::executable_directory(),
                               .validation = in_effect.on("vulkan_validation"),
                               .ray_tracing = in_effect.on("ray_tracing")});
    read_frame_generation(in_effect);
    read_upscale(in_effect);
    read_all_effects(in_effect);
    render::graphics_options::set_live_reload(&reload_live_key);
    render::graphics_options::set_compare_backend(
        {.prepare = &compare_prepare, .apply = &compare_apply, .end = &compare_end});
    render::install_device(render::d3d9::device());
    render::install_gpu(render::vulkan::gpu());
    render::install_gpu_context(render::vulkan::gpu_context());
    render::install_ui_backend(render::vulkan::ui_backend());
#else
    (void)in_effect;
#ifndef THUG2_PORTABLE
    install_direct3d9();
#endif
#endif
}

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

// [Graphics] renderer as the settings model has it: Vulkan (the default) where this machine can draw with it, else
// Direct3D 9 (the model's choice need, with its reason). The editor mode (engine/sys/editor_mode.h) runs on Vulkan
// whatever the key says - its panels' own windows are Vulkan's - Direct3D 9 only when Vulkan can't draw here.
Renderer read_choice()
{
    // (main runs before the PC set-up that finds the settings file; finding it again there changes nothing)
    build_info::prepare_all();
    const FoundCapabilities& found = find_capabilities(); // (the device probed, the model told what it has)
    const config::Resolved& renderer = config::running().at("renderer");
    if (found.caps.vulkan) {
        get_log("renderer: Vulkan can draw on %s", found.device.c_str());
    } else {
        get_log("renderer: Vulkan can't draw here - %s%s%s", found.device.c_str(), found.device.empty() ? "" : ": ",
                found.problem.c_str());
    }
    if (sys::editor_mode::on()) {
        if (renderer.asked == "d3d9") {
            get_log("renderer: renderer=d3d9 is the game's - the editor mode runs on Vulkan");
        }
        return found.caps.vulkan ? Renderer::kVulkan : Renderer::kDirect3D9;
    }
    if (renderer.value == "vulkan") {
        return Renderer::kVulkan;
    }
    const std::string why = renderer.reason.empty() ? std::string(" (renderer=d3d9)") : " - " + renderer.reason;
    get_log("renderer: Direct3D 9%s", why.c_str());
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
    const Renderer renderer = chosen_renderer(); // (first: it finds the settings file and this machine's capabilities)
    const Resolution& in_effect = config::running();
    const std::string changes = config::describe(in_effect);
    if (!changes.empty()) {
        get_log("settings: what the ini asks for that isn't so here -\n%s", changes.c_str());
    }
    if (renderer == Renderer::kVulkan) {
        install_vulkan(in_effect);
    } else {
        read_time_of_day(in_effect); // (the time of day: either renderer)
#ifndef THUG2_PORTABLE
        install_direct3d9();
#else
        // NOT ORIGINAL (macOS port) - there is no Direct3D 9 here, and Vulkan is the only backend: a machine that can't
        // draw with Vulkan has no renderer at all (the log above says why). The game stops here rather than fault in the
        // renderer's first call.
        get_log("renderer: no graphics backend on this platform but Vulkan: stopping");
        ExitProcess(2);
#endif
    }
}

} // namespace app
