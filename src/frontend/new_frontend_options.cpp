// NOT ORIGINAL - the new front end's Options screen's settings and the ini text (ours): see new_frontend_options.h.

#include "frontend/new_frontend_options.h"
#include "frontend/new_frontend_text.h"
#include "base/core/checked_index.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace frontend::new_frontend::options {

namespace {

std::string get_trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool get_same(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

// A number's value (false if the text isn't one).
bool get_number(const std::string& text, double* out)
{
    const std::string trimmed = get_trim(text);
    if (trimmed.empty()) {
        return false;
    }
    char* end = nullptr;
    *out = std::strtod(trimmed.c_str(), &end);
    return (end != nullptr) && *end == '\0';
}

const std::vector<Choice>& off_on()
{
    static const std::vector<Choice> kValue = {{.value = "0", .label = text_tools::kOff}, {.value = "1", .label = text_tools::kOn}};
    return kValue;
}

// One line of the text: where it starts, ends (before its line break) and what it is.
struct Seg {
    size_t start, end, next; // next: after its line break
};
std::vector<Seg> get_lines(const std::string& text)
{
    std::vector<Seg> lines;
    size_t at = 0;
    while (at < text.size()) {
        const size_t nl = text.find('\n', at);
        const size_t end = nl == std::string::npos ? text.size() : nl;
        const size_t content = end > at && core::at(text, end - 1) == '\r' ? end - 1 : end;
        lines.push_back({.start = at, .end = content, .next = nl == std::string::npos ? text.size() : nl + 1});
        at = nl == std::string::npos ? text.size() : nl + 1;
    }
    return lines;
}

// The line's section name if it is a section header ("[name]"), else false.
bool section_of(const std::string& line, std::string* name)
{
    std::string trimmed = get_trim(line);
    if (trimmed.size() < 2 || trimmed.front() != '[') {
        return false;
    }
    const size_t close = trimmed.find(']');
    if (close == std::string::npos) {
        return false;
    }
    *name = get_trim(trimmed.substr(1, close - 1));
    return true;
}

// The line's key if it is a key=value line (not a comment), and where its '=' is.
bool get_key_of(const std::string& line, std::string* key, size_t* equals)
{
    std::string trimmed = get_trim(line);
    if (trimmed.empty() || core::at(trimmed, 0) == ';' || core::at(trimmed, 0) == '#' || core::at(trimmed, 0) == '[') {
        return false;
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    *key = get_trim(line.substr(0, eq));
    *equals = eq;
    return !key->empty();
}

// A [Graphics] setting of the renderer's: Off / On, or the choices given (`fallback`: the default's index - README's
// settings table).
Spec graphics_switch(const char* key, const char* label, const char* help, int fallback, Apply apply)
{
    return {.section = "Graphics", .key = key, .label = label, .help = help, .choices = off_on(), .fallback = fallback, .apply = apply};
}
Spec graphics_choice(const char* key, const char* label, const char* help, std::vector<Choice> choices, int fallback,
                     Apply apply)
{
    return {.section = "Graphics",
            .key = key,
            .label = label,
            .help = help,
            .choices = std::move(choices),
            .fallback = fallback,
            .apply = apply};
}

// The settings of each group, by key (resolved to get_specs()'s indices once, by bunches()).
struct GroupKeys {
    const char* id;
    const char* name;
    std::vector<const char*> keys;
};
const std::vector<GroupKeys>& group_keys()
{
    static const std::vector<GroupKeys> kGroups = {
        {.id = "graphics",
         .name = text_tools::kGroupGraphics,
         .keys = {"graphics_preset", "renderer", "ray_tracing", "texture_set", "anisotropy", "msaa", "draw_distance_scale",
                  "lod_distance_scale", "rt_tessellation", "rt_tess_max_deviation"}},
        {.id = "lighting",
         .name = text_tools::kGroupLighting,
         .keys = {"rt_sun_source", "rt_shadows", "rt_shadow_strength", "rt_shadow_softness", "rt_shadow_objects", "rt_ao",
                  "rt_ao_strength", "rt_ao_radius", "rt_ao_rays", "rt_gi", "rt_gi_strength", "rt_gi_rays"}},
        {.id = "surfaces",
         .name = text_tools::kGroupSurfaces,
         .keys = {"rt_relit", "rt_relit_strength", "rt_pbr", "rt_pbr_specular", "rt_pbr_normal_strength",
                  "rt_reflections", "rt_reflection_strength", "rt_reflection_fresnel", "rt_glass_reflectance",
                  "rt_glass_roughness"}},
        {.id = "sky",
         .name = text_tools::kGroupSky,
         .keys = {"rt_sky", "rt_clouds", "rt_cloud_coverage", "rt_cloud_quality", "rt_sky_model", "rt_sun", "rt_sun_colour",
                  "rt_shafts", "rt_fog", "rt_fog_haze", "rt_fog_haze_density"}},
        {.id = "colour",
         .name = text_tools::kGroupColour,
         .keys = {"rt_hdr", "rt_exposure", "rt_tonemap", "rt_white_point", "rt_saturation", "rt_contrast", "rt_bloom",
                  "rt_bloom_strength", "rt_style", "rt_style_strength", "rt_style_smooth", "rt_style_bands",
                  "rt_style_outlines", "rt_style_outline_width", "rt_style_saturation"}},
        {.id = "upscaling", .name = text_tools::kGroupUpscaling, .keys = {"upscaler", "upscale_quality", "ray_reconstruction"}},
        {.id = "interface", .name = text_tools::kGroupInterface, .keys = {"new_frontend", "hd_ui", "mouse_menus", "loading_console"}},
        {.id = "gameplay",
         .name = text_tools::kGroupGameplay,
         .keys = {"live_pause", "skip_intro_movies", "restart_tester", "screenshots", "respect_locks"}},
        {.id = "online", .name = text_tools::kGroupOnline, .keys = {"OnlineGameName", "thugpro_peer", "thugpro_parts"}},
        {.id = "mods", .name = text_tools::kGroupMods, .keys = {"anteara_mod", "thaw_bmx", "debug_mode"}},
        {.id = "sound", .name = text_tools::kGroupSound, .keys = {"continue_music"}},
    };
    return kGroups;
}

// The keys a graphics preset sets, and each preset's values for them (in that order): Original - every effect off, the
// game as it was; High - the cheaper effects (shadows, occlusion, reflections, the sky and sun, HDR, bloom, smooth
// curves); Ultra - every effect on. Upscaling is left as it is (it trades sharpness for speed, whatever the preset).
constexpr std::array kPresetKeys = {"ray_tracing", "rt_shadows",   "rt_ao", "rt_gi",  "rt_reflections", "rt_relit",
                                    "rt_pbr",      "rt_fog",       "rt_sky", "rt_sky_model", "rt_clouds", "rt_sun",
                                    "rt_shafts",   "rt_bloom",     "rt_hdr", "rt_tessellation"};
constexpr std::array<const char*, kPresetKeys.size()> kPresetOriginal = {"0", "0", "0", "0", "0", "0", "0", "0",
                                                                         "0", "0", "0", "0", "0", "0", "0", "0"};
constexpr std::array<const char*, kPresetKeys.size()> kPresetHigh = {"1", "1", "1", "0", "1", "0", "0", "0",
                                                                     "1", "1", "0", "1", "1", "1", "1", "1"};
constexpr std::array<const char*, kPresetKeys.size()> kPresetUltra = {"1", "1", "1", "1", "1", "1", "1", "1",
                                                                      "1", "1", "1", "1", "1", "1", "1", "1"};

std::vector<PresetValue> preset_list(const std::array<const char*, kPresetKeys.size()>& values)
{
    std::vector<PresetValue> list;
    list.reserve(kPresetKeys.size());
    for (size_t i = 0; i < kPresetKeys.size(); ++i) {
        list.push_back({.key = core::at(kPresetKeys, i), .value = core::at(values, i)});
    }
    return list;
}

// The setting whose key is `key` (keys are one setting's each - check_tables): its index, -1 if none.
int find_key(const char* key)
{
    const std::vector<Spec>& specs = get_specs();
    for (size_t i = 0; i < specs.size(); ++i) {
        if (get_same(core::at(specs, i).key, key)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The choice `value` is (numbers compared as numbers), -1 if none.
int match_choice(const Spec& spec, const std::string& value)
{
    const std::string v = get_trim(value);
    double number = 0.0;
    const bool is_number = get_number(v, &number);
    for (size_t i = 0; i < spec.choices.size(); ++i) {
        const char* choice = core::at(spec.choices, i).value;
        double number_value = 0.0;
        if (is_number && get_number(choice, &number_value) ? number == number_value : get_same(v, choice)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The rows that matter only while another setting is on: each one's effect does nothing without its switch (the
// readers in app/renderer_choice.cpp, the passes' own `on` checks). Left out: rt_relit_strength (the relit mode is also
// on whenever rt_sun_source isn't level, with rt_relit off). A rule's parent is an earlier row of the same group.
const std::vector<ShownWhen>& shown_table()
{
    static const std::vector<ShownWhen> kRules = {
        {.key = "rt_tess_max_deviation", .parent = "rt_tessellation", .values = {"1"}},
        {.key = "rt_shadow_strength", .parent = "rt_shadows", .values = {"1"}},
        {.key = "rt_shadow_softness", .parent = "rt_shadows", .values = {"1"}},
        {.key = "rt_shadow_objects", .parent = "rt_shadows", .values = {"1"}},
        {.key = "rt_ao_strength", .parent = "rt_ao", .values = {"1"}},
        {.key = "rt_ao_radius", .parent = "rt_ao", .values = {"1"}},
        {.key = "rt_ao_rays", .parent = "rt_ao", .values = {"1"}},
        {.key = "rt_gi_strength", .parent = "rt_gi", .values = {"1"}},
        {.key = "rt_gi_rays", .parent = "rt_gi", .values = {"1"}},
        {.key = "rt_pbr_specular", .parent = "rt_pbr", .values = {"1"}},
        {.key = "rt_pbr_normal_strength", .parent = "rt_pbr", .values = {"1"}},
        {.key = "rt_reflection_strength", .parent = "rt_reflections", .values = {"1"}},
        {.key = "rt_reflection_fresnel", .parent = "rt_reflections", .values = {"1"}},
        {.key = "rt_glass_reflectance", .parent = "rt_reflections", .values = {"1"}},
        {.key = "rt_glass_roughness", .parent = "rt_reflections", .values = {"1"}},
        {.key = "rt_cloud_coverage", .parent = "rt_clouds", .values = {"1"}},
        {.key = "rt_cloud_quality", .parent = "rt_clouds", .values = {"1"}},
        {.key = "rt_fog_haze", .parent = "rt_fog", .values = {"1"}},
        {.key = "rt_fog_haze_density", .parent = "rt_fog", .values = {"1"}},
        {.key = "rt_bloom_strength", .parent = "rt_bloom", .values = {"1"}},
        {.key = "rt_style_strength", .parent = "rt_style", .values = {"1"}},
        {.key = "rt_style_smooth", .parent = "rt_style", .values = {"1"}},
        {.key = "rt_style_bands", .parent = "rt_style", .values = {"1"}},
        {.key = "rt_style_outlines", .parent = "rt_style", .values = {"1"}},
        {.key = "rt_style_outline_width", .parent = "rt_style", .values = {"1"}},
        {.key = "rt_style_saturation", .parent = "rt_style", .values = {"1"}},
        {.key = "upscale_quality", .parent = "upscaler", .values = {"auto", "dlss", "fsr"}},
        {.key = "ray_reconstruction", .parent = "upscaler", .values = {"auto", "dlss", "fsr"}},
    };
    return kRules;
}

// The rule for setting `option` (by index), nullptr if none.
const ShownWhen* rule_for(int option)
{
    const std::vector<Spec>& specs = get_specs();
    if (option < 0 || std::cmp_greater_equal(option, specs.size())) {
        return nullptr;
    }
    for (const ShownWhen& rule : shown_table()) {
        if (get_same(rule.key, core::at(specs, option).key)) {
            return &rule;
        }
    }
    return nullptr;
}

// Whether `parent`'s choice is one of the rule's values.
bool parent_matches(const ShownWhen& rule, const Spec& parent, int choice)
{
    return std::ranges::any_of(rule.values, [&](const char* value) { return match_choice(parent, value) == choice; });
}

} // namespace

const std::vector<ShownWhen>& shown_rules() { return shown_table(); }

bool row_shown(int option, const std::vector<int>& choices)
{
    const ShownWhen* rule = rule_for(option);
    if (rule == nullptr) {
        return true;
    }
    const int parent = find_key(rule->parent);
    if (parent < 0) {
        return true;
    }
    const Spec& spec = core::at(get_specs(), parent);
    const int choice = std::cmp_less(parent, choices.size()) ? core::at(choices, parent) : spec.fallback;
    return parent_matches(*rule, spec, choice);
}

const std::vector<Spec>& get_specs()
{
    // (the words: new_frontend_text.h; the groups name them by key - group_keys())
    using text_tools::kOff;
    static const std::vector<Spec> kSpecs = {
        // Graphics: the preset, the renderer (app/renderer_choice.h), engine/gfx/graphics_options.h, smooth curves
        // (render_pc/mesh_smoothing.h)
        graphics_choice("graphics_preset", text_tools::kOptPreset, text_tools::kOptPresetHelp,
                        {{.value = "", .label = text_tools::kPresetCustom},
                         {.value = "original", .label = text_tools::kPresetOriginal},
                         {.value = "high", .label = text_tools::kPresetHigh},
                         {.value = "ultra", .label = text_tools::kPresetUltra}},
                        0, Apply::kPreset),
        graphics_choice("renderer", text_tools::kOptRenderer, text_tools::kOptRendererHelp,
                        {{.value = "vulkan", .label = "Vulkan"}, {.value = "d3d9", .label = "Direct3D 9"}}, 0, Apply::kRelaunch),
        graphics_switch("ray_tracing", text_tools::kOptRayTracing, text_tools::kOptRayTracingHelp, 0, Apply::kRelaunch),
        // (engine/gfx/texture_sets.h: the upscaled sets tools/pipeline/hd_textures/base.ts installs - the screen lists
        // the ones installed, set_choice_filter)
        graphics_choice("texture_set", text_tools::kOptTextureSet, text_tools::kOptTextureSetHelp,
                        {{.value = "original", .label = text_tools::kTextureSetOriginal},
                         {.value = "pbrify", .label = "PBRify"},
                         {.value = "seedvr2", .label = "SeedVR2"}},
                        0, Apply::kLive),
        {.section = "Graphics",
         .key = "anisotropy",
         .label = text_tools::kOptAnisotropy,
         .help = text_tools::kOptAnisotropyHelp,
         .choices = {{.value = "0", .label = kOff}, {.value = "2", .label = "2x"}, {.value = "4", .label = "4x"}, {.value = "8", .label = "8x"}, {.value = "16", .label = "16x"}},
         .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "Graphics",
         .key = "msaa",
         .label = text_tools::kOptMsaa,
         .help = text_tools::kOptMsaaHelp,
         .choices = {{.value = "0", .label = kOff}, {.value = "2", .label = "2x"}, {.value = "4", .label = "4x"}, {.value = "8", .label = "8x"}},
         .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "Graphics",
         .key = "draw_distance_scale",
         .label = text_tools::kOptDrawDistance,
         .help = text_tools::kOptDrawDistanceHelp,
         .choices = {{.value = "1", .label = "1x"}, {.value = "1.5", .label = "1.5x"}, {.value = "2", .label = "2x"}, {.value = "3", .label = "3x"}},
         .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "Graphics",
         .key = "lod_distance_scale",
         .label = text_tools::kOptLodDistance,
         .help = text_tools::kOptLodDistanceHelp,
         .choices = {{.value = "1", .label = "1x"}, {.value = "1.5", .label = "1.5x"}, {.value = "2", .label = "2x"}, {.value = "3", .label = "3x"}},
         .fallback = 0,
         .apply = Apply::kRelaunch},
        graphics_switch("rt_tessellation", text_tools::kOptTessellation, text_tools::kOptTessellationHelp, 0, Apply::kRelaunch),
        graphics_choice("rt_tess_max_deviation", text_tools::kOptTessDeviation, text_tools::kOptTessDeviationHelp,
                        {{.value = "1", .label = "1"}, {.value = "2", .label = "2"}, {.value = "3", .label = "3"}, {.value = "4", .label = "4"}},
                        1, Apply::kRelaunch),
        // Lighting: the sun every effect lights by (engine/gfx/rt_scene.h scene_sun - added by the scene's sun), shadows
        // (gpu_vulkan/vulkan_rt_shadows.h), occlusion and bounce light (vulkan_rt_effects.h)
        graphics_choice("rt_sun_source", text_tools::kOptSunSource, text_tools::kOptSunSourceHelp,
                        {{.value = "game", .label = text_tools::kOptSunSky}, {.value = "level", .label = text_tools::kOptSunLevel}},
                        1, Apply::kLive),
        graphics_switch("rt_shadows", text_tools::kOptShadows, text_tools::kOptShadowsHelp, 0, Apply::kLive),
        graphics_choice("rt_shadow_strength", text_tools::kOptShadowStrength, "",
                        {{.value = "0.25", .label = "25%"}, {.value = "0.5", .label = "50%"}, {.value = "0.75", .label = "75%"}, {.value = "1", .label = "100%"}},
                        1, Apply::kLive),
        graphics_choice("rt_shadow_softness", text_tools::kOptShadowSoftness, "",
                        {{.value = "0", .label = "Hard"}, {.value = "0.5", .label = "0.5"}, {.value = "1", .label = "1"}, {.value = "2", .label = "2"}, {.value = "4", .label = "4"}},
                        0, Apply::kLive),
        graphics_switch("rt_shadow_objects", text_tools::kOptShadowObjects, text_tools::kOptShadowObjectsHelp, 1, Apply::kLive),
        graphics_switch("rt_ao", text_tools::kOptAo, text_tools::kOptAoHelp, 0, Apply::kLive),
        graphics_choice("rt_ao_strength", text_tools::kOptAoStrength, "",
                        {{.value = "0.3", .label = "30%"}, {.value = "0.45", .label = "45%"}, {.value = "0.6", .label = "60%"}, {.value = "0.8", .label = "80%"}, {.value = "1", .label = "100%"}},
                        2, Apply::kLive),
        graphics_choice("rt_ao_radius", text_tools::kOptAoRadius, "",
                        {{.value = "24", .label = "24"}, {.value = "48", .label = "48"}, {.value = "96", .label = "96"}, {.value = "192", .label = "192"}},
                        1, Apply::kLive),
        graphics_choice("rt_ao_rays", text_tools::kOptAoRays, "",
                        {{.value = "1", .label = "1"}, {.value = "2", .label = "2"}, {.value = "4", .label = "4"}, {.value = "8", .label = "8"}},
                        2, Apply::kLive),
        graphics_switch("rt_gi", text_tools::kOptGi, text_tools::kOptGiHelp, 0, Apply::kLive),
        graphics_choice("rt_gi_strength", text_tools::kOptGiStrength, "",
                        {{.value = "0.5", .label = "50%"}, {.value = "1", .label = "100%"}, {.value = "1.5", .label = "150%"}, {.value = "2", .label = "200%"}},
                        1, Apply::kLive),
        graphics_choice("rt_gi_rays", text_tools::kOptGiRays, "",
                        {{.value = "1", .label = "1"}, {.value = "2", .label = "2"}, {.value = "4", .label = "4"}, {.value = "8", .label = "8"}},
                        1, Apply::kLive),
        // Surfaces: relit levels (engine/gfx/rt_relit.h), per-pixel lighting (rt_pbr), reflections
        // (gpu_vulkan/vulkan_rt_reflections.h)
        graphics_switch("rt_relit", text_tools::kOptRelit, text_tools::kOptRelitHelp, 0, Apply::kLive),
        graphics_choice("rt_relit_strength", text_tools::kOptRelitStrength, "",
                        {{.value = "0.25", .label = "25%"}, {.value = "0.5", .label = "50%"}, {.value = "0.75", .label = "75%"}, {.value = "1", .label = "100%"}},
                        3, Apply::kLive),
        graphics_switch("rt_pbr", text_tools::kOptPbr, text_tools::kOptPbrHelp, 0, Apply::kLive),
        graphics_choice("rt_pbr_specular", text_tools::kOptPbrSpecular, "",
                        {{.value = "0", .label = kOff}, {.value = "0.5", .label = "50%"}, {.value = "1", .label = "100%"}, {.value = "1.5", .label = "150%"}, {.value = "2", .label = "200%"}},
                        2, Apply::kLive),
        graphics_choice("rt_pbr_normal_strength", text_tools::kOptPbrNormal, "",
                        {{.value = "0", .label = "Flat"}, {.value = "0.5", .label = "50%"}, {.value = "1", .label = "100%"}, {.value = "1.5", .label = "150%"}, {.value = "2", .label = "200%"}},
                        2, Apply::kLive),
        graphics_switch("rt_reflections", text_tools::kOptReflections, text_tools::kOptReflectionsHelp, 0, Apply::kLive),
        graphics_choice("rt_reflection_strength", text_tools::kOptReflectionStrength, "",
                        {{.value = "0.25", .label = "25%"}, {.value = "0.5", .label = "50%"}, {.value = "0.75", .label = "75%"}, {.value = "1", .label = "100%"}},
                        1, Apply::kLive),
        graphics_switch("rt_reflection_fresnel", text_tools::kOptFresnel, text_tools::kOptFresnelHelp, 1, Apply::kLive),
        graphics_choice("rt_glass_reflectance", text_tools::kOptGlassReflectance, "",
                        {{.value = "0.04", .label = "Real"}, {.value = "0.1", .label = "10%"}, {.value = "0.2", .label = "20%"}, {.value = "0.35", .label = "35%"}},
                        2, Apply::kLive),
        graphics_choice("rt_glass_roughness", text_tools::kOptGlassRoughness, "",
                        {{.value = "0", .label = "Mirror"}, {.value = "0.05", .label = text_tools::kLow}, {.value = "0.1", .label = text_tools::kMedium}, {.value = "0.2", .label = text_tools::kHigh}},
                        1, Apply::kLive),
        // Sky: the sky and clouds (gpu_vulkan/vulkan_rt_sky_pass.h), the sun, its rays and the sky model
        // (vulkan_rt_sun.h), the fog and haze (vulkan_rt_fog.h)
        graphics_switch("rt_sky", text_tools::kOptSky, text_tools::kOptSkyHelp, 0, Apply::kLive),
        graphics_switch("rt_clouds", text_tools::kOptClouds, text_tools::kOptCloudsHelp, 0, Apply::kLive),
        graphics_choice("rt_cloud_coverage", text_tools::kOptCloudCoverage, "",
                        {{.value = "auto", .label = text_tools::kAuto}, {.value = "0", .label = "Clear"}, {.value = "0.25", .label = "25%"}, {.value = "0.5", .label = "50%"}, {.value = "0.75", .label = "75%"}, {.value = "1", .label = "Overcast"}},
                        0, Apply::kLive),
        graphics_choice("rt_cloud_quality", text_tools::kOptCloudQuality, "",
                        {{.value = "0", .label = text_tools::kLow}, {.value = "1", .label = text_tools::kMedium}, {.value = "2", .label = text_tools::kHigh}},
                        1, Apply::kLive),
        graphics_switch("rt_sky_model", text_tools::kOptSkyModel, text_tools::kOptSkyModelHelp, 0, Apply::kLive),
        graphics_switch("rt_sun", text_tools::kOptSun, text_tools::kOptSunHelp, 0, Apply::kLive),
        graphics_choice("rt_sun_colour", text_tools::kOptSunColour, "",
                        {{.value = "auto", .label = text_tools::kAuto}, {.value = "1,0.85,0.7", .label = "Warm"}, {.value = "1,1,1", .label = "White"}, {.value = "0.85,0.92,1", .label = "Cool"}},
                        0, Apply::kLive),
        graphics_switch("rt_shafts", text_tools::kOptShafts, text_tools::kOptShaftsHelp, 0, Apply::kLive),
        graphics_switch("rt_fog", text_tools::kOptFog, text_tools::kOptFogHelp, 0, Apply::kLive),
        graphics_switch("rt_fog_haze", text_tools::kOptHaze, text_tools::kOptHazeHelp, 1, Apply::kLive),
        graphics_choice("rt_fog_haze_density", text_tools::kOptHazeDensity, "",
                        {{.value = "0.15", .label = text_tools::kLow}, {.value = "0.3", .label = text_tools::kMedium}, {.value = "0.6", .label = text_tools::kHigh}, {.value = "1", .label = "Thick"}},
                        1, Apply::kLive),
        // Colour: HDR and its tone mapping (gpu_vulkan/vulkan_hdr.h; rt_hdr itself the device's targets' format), the
        // bloom (vulkan_rt_sun.h)
        graphics_switch("rt_hdr", text_tools::kOptHdr, text_tools::kOptHdrHelp, 0, Apply::kRelaunch),
        graphics_choice("rt_exposure", text_tools::kOptExposure, "",
                        {{.value = "-1", .label = "-1"}, {.value = "-0.5", .label = "-0.5"}, {.value = "0", .label = "0"}, {.value = "0.5", .label = "+0.5"}, {.value = "1", .label = "+1"}},
                        2, Apply::kLive),
        graphics_choice("rt_tonemap", text_tools::kOptTonemap, "",
                        {{.value = "0", .label = "Game"}, {.value = "1", .label = "Filmic"}}, 0, Apply::kLive),
        graphics_choice("rt_white_point", text_tools::kOptWhitePoint, "",
                        {{.value = "1", .label = "1"}, {.value = "1.25", .label = "1.25"}, {.value = "2", .label = "2"}, {.value = "4", .label = "4"}},
                        1, Apply::kLive),
        graphics_choice("rt_saturation", text_tools::kOptSaturation, "",
                        {{.value = "0.8", .label = "80%"}, {.value = "1", .label = "100%"}, {.value = "1.2", .label = "120%"}, {.value = "1.4", .label = "140%"}},
                        1, Apply::kLive),
        graphics_choice("rt_contrast", text_tools::kOptContrast, "",
                        {{.value = "0.9", .label = "90%"}, {.value = "1", .label = "100%"}, {.value = "1.1", .label = "110%"}, {.value = "1.2", .label = "120%"}},
                        1, Apply::kLive),
        graphics_switch("rt_bloom", text_tools::kOptBloom, text_tools::kOptBloomHelp, 0, Apply::kLive),
        graphics_choice("rt_bloom_strength", text_tools::kOptBloomStrength, "",
                        {{.value = "0.02", .label = text_tools::kLow}, {.value = "0.05", .label = text_tools::kMedium}, {.value = "0.1", .label = text_tools::kHigh}, {.value = "0.2", .label = "Max"}},
                        1, Apply::kLive),
        // (the art style: gpu_vulkan/vulkan_style.h - the toon filter and its knobs)
        graphics_choice("rt_style", text_tools::kOptStyle, text_tools::kOptStyleHelp,
                        {{.value = "0", .label = kOff}, {.value = "1", .label = text_tools::kStyleToon}}, 0, Apply::kLive),
        graphics_choice("rt_style_strength", text_tools::kOptStyleStrength, "",
                        {{.value = "0.5", .label = "50%"}, {.value = "0.75", .label = "75%"}, {.value = "1", .label = "100%"}},
                        2, Apply::kLive),
        graphics_choice("rt_style_smooth", text_tools::kOptStyleSmooth, "",
                        {{.value = "0", .label = kOff}, {.value = "2", .label = text_tools::kLow}, {.value = "4", .label = text_tools::kMedium}, {.value = "6", .label = text_tools::kHigh}},
                        2, Apply::kLive),
        graphics_choice("rt_style_bands", text_tools::kOptStyleBands, "",
                        {{.value = "0", .label = kOff}, {.value = "3", .label = "3"}, {.value = "4", .label = "4"}, {.value = "5", .label = "5"}, {.value = "7", .label = "7"}},
                        3, Apply::kLive),
        graphics_choice("rt_style_outlines", text_tools::kOptStyleOutlines, "",
                        {{.value = "0", .label = kOff}, {.value = "0.5", .label = "50%"}, {.value = "0.75", .label = "75%"}, {.value = "1", .label = "100%"}},
                        3, Apply::kLive),
        graphics_choice("rt_style_outline_width", text_tools::kOptStyleOutlineWidth, "",
                        {{.value = "0.5", .label = "Thin"}, {.value = "1", .label = text_tools::kMedium}, {.value = "1.5", .label = "Bold"}, {.value = "2", .label = "Heavy"}},
                        1, Apply::kLive),
        graphics_choice("rt_style_saturation", text_tools::kOptStyleSaturation, "",
                        {{.value = "1", .label = "100%"}, {.value = "1.15", .label = "115%"}, {.value = "1.3", .label = "130%"}, {.value = "1.5", .label = "150%"}},
                        1, Apply::kLive),
        // Upscaling (gpu_vulkan/vulkan_upscale.h): the device's targets and the provider are made for it
        graphics_choice("upscaler", text_tools::kOptUpscaler, text_tools::kOptUpscalerHelp,
                        {{.value = "off", .label = kOff}, {.value = "auto", .label = text_tools::kAuto}, {.value = "dlss", .label = "DLSS"}, {.value = "fsr", .label = "FSR"}},
                        0, Apply::kRelaunch),
        graphics_choice("upscale_quality", text_tools::kOptUpscaleQuality, text_tools::kOptUpscaleQualityHelp,
                        {{.value = "dlaa", .label = "DLAA"}, {.value = "quality", .label = "Quality"}, {.value = "balanced", .label = "Balanced"}, {.value = "performance", .label = "Perf"}, {.value = "ultra", .label = "Ultra perf"}},
                        1, Apply::kRelaunch),
        graphics_switch("ray_reconstruction", text_tools::kOptRayReconstruction, text_tools::kOptRayReconstructionHelp, 0,
                        Apply::kRelaunch),
        // Interface
        {.section = "features", .key = "new_frontend", .label = text_tools::kOptNewFrontend, .help = text_tools::kOptNewFrontendHelp, .choices = off_on(), .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "features", .key = "hd_ui", .label = text_tools::kOptHdUi, .help = text_tools::kOptHdUiHelp, .choices = off_on(), .fallback = 0, .apply = Apply::kRelaunch},
        {.section = "features", .key = "mouse_menus", .label = text_tools::kOptMouse, .help = text_tools::kOptMouseHelp, .choices = off_on(), .fallback = 1, .apply = Apply::kLive},
        {.section = "features", .key = "loading_console", .label = text_tools::kOptLoadingConsole, .help = text_tools::kOptLoadingConsoleHelp, .choices = off_on(), .fallback = 1,
         .apply = Apply::kRelaunch},
        // Gameplay (game/front_end/live_pause.h)
        {.section = "features",
         .key = "live_pause",
         .label = text_tools::kOptLivePause,
         .help = text_tools::kOptLivePauseHelp,
         .choices = {{.value = "0", .label = text_tools::kOptLivePauseFreeze}, {.value = "1", .label = text_tools::kOptLivePauseLive}},
         .fallback = 1,
         .apply = Apply::kLive},
        {.section = "features", .key = "skip_intro_movies", .label = text_tools::kOptSkipIntro, .help = text_tools::kOptSkipIntroHelp, .choices = off_on(), .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "features", .key = "restart_tester", .label = text_tools::kOptRestartTester, .help = text_tools::kOptRestartTesterHelp, .choices = off_on(), .fallback = 1,
         .apply = Apply::kLive},
        {.section = "features", .key = "screenshots", .label = text_tools::kOptScreenshots, .help = text_tools::kOptScreenshotsHelp, .choices = off_on(), .fallback = 1,
         .apply = Apply::kRelaunch},
        {.section = "frontend",
         .key = "respect_locks",
         .label = text_tools::kOptLocks,
         .help = text_tools::kOptLocksHelp,
         .choices = {{.value = "0", .label = text_tools::kOptLocksAll}, {.value = "1", .label = text_tools::kOptLocksUnlocked}},
         .fallback = 0,
         .apply = Apply::kLive},
        // Online
        {.section = "Miscellaneous",
         .key = "OnlineGameName",
         .label = text_tools::kOptIdentity,
         .help = text_tools::kOptIdentityHelp,
         .choices = {{.value = "", .label = text_tools::kIdentityThug2}, {.value = "thugpro", .label = text_tools::kIdentityThugpro}},
         .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "online", .key = "thugpro_peer", .label = text_tools::kOptThugproChat, .help = text_tools::kOptThugproChatHelp, .choices = off_on(), .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "features",
         .key = "thugpro_parts",
         .label = text_tools::kOptThugproParts,
         .help = text_tools::kOptThugproPartsHelp,
         .choices = {{.value = "0", .label = kOff}, {.value = "1", .label = text_tools::kOptThugproPartsHidden}, {.value = "2", .label = text_tools::kOptThugproPartsShown}},
         .fallback = 1,
         .apply = Apply::kRelaunch},
        // Mods
        {.section = "features", .key = "anteara_mod", .label = text_tools::kOptAnteara, .help = text_tools::kOptAntearaHelp, .choices = off_on(), .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "features", .key = "thaw_bmx", .label = text_tools::kOptBmx, .help = text_tools::kOptBmxHelp, .choices = off_on(), .fallback = 0, .apply = Apply::kRelaunch},
        {.section = "features", .key = "debug_mode", .label = text_tools::kOptDebug, .help = text_tools::kOptDebugHelp, .choices = off_on(), .fallback = 0, .apply = Apply::kRelaunch},
        // Sound (engine/audio/continue_music.h)
        {.section = "features", .key = "continue_music", .label = text_tools::kOptContinueMusic, .help = text_tools::kOptContinueMusicHelp, .choices = off_on(), .fallback = 0,
         .apply = Apply::kLive},
    };
    return kSpecs;
}

std::string check_tables()
{
    const std::vector<Spec>& specs = get_specs();
    std::string problems;
    const auto add = [&problems](const std::string& line) { problems += line + "\n"; };
    std::vector<int> groups_of(specs.size(), 0);
    for (const GroupKeys& group : group_keys()) {
        for (const char* key : group.keys) {
            const int found = find_key(key);
            if (found < 0) {
                add(std::string("group ") + group.id + ": " + key + " is no setting");
            } else {
                ++core::at(groups_of, found);
            }
        }
    }
    for (size_t i = 0; i < specs.size(); ++i) {
        const Spec& spec = core::at(specs, i);
        const std::string name = std::string("[") + spec.section + "] " + spec.key;
        if (core::at(groups_of, i) != 1) {
            add(name + " is in " + std::to_string(core::at(groups_of, i)) + " groups");
        }
        if (find_key(spec.key) != static_cast<int>(i)) {
            add(name + ": another setting has its key");
        }
        if (spec.label == nullptr || *spec.label == '\0' || spec.help == nullptr) {
            add(name + " has no label");
        }
        if (spec.fallback < 0 || std::cmp_greater_equal(spec.fallback, spec.choices.size())) {
            add(name + ": its fallback is no choice");
        }
        for (size_t at = 0; at < spec.choices.size(); ++at) {
            const int first = match_choice(spec, core::at(spec.choices, at).value);
            if (std::cmp_not_equal(first, at)) {
                add(name + ": choices " + std::to_string(first) + " and " + std::to_string(at) + " are one value");
            }
        }
    }
    for (int choice = 1; std::cmp_less(choice, core::at(specs, preset_option()).choices.size()); ++choice) {
        for (const PresetValue& set : preset_values(choice)) {
            const int found = find_key(set.key);
            if (found < 0 || !get_same(core::at(specs, found).section, "Graphics")) {
                add(std::string("preset ") + std::to_string(choice) + ": " + set.key + " is no [Graphics] setting");
                continue;
            }
            if (match_choice(core::at(specs, found), set.value) < 0) {
                add(std::string("preset ") + std::to_string(choice) + ": " + set.key + "=" + set.value + " is no choice");
            }
        }
    }
    problems += check_shown_rules();
    return problems;
}

std::string check_shown_rules()
{
    const std::vector<Spec>& specs = get_specs();
    std::string problems;
    const auto add = [&problems](const std::string& line) { problems += line + "\n"; };
    // (each setting's group and place in it)
    std::vector<std::pair<int, int>> place(specs.size(), {-1, -1});
    int group_at = 0;
    for (const GroupKeys& group : group_keys()) {
        int row = 0;
        for (const char* key : group.keys) {
            const int found = find_key(key);
            if (found >= 0) {
                core::at(place, found) = {group_at, row};
            }
            ++row;
        }
        ++group_at;
    }
    for (const ShownWhen& rule : shown_table()) {
        const std::string name = std::string("shown-when ") + rule.key + " by " + rule.parent;
        const int child = find_key(rule.key);
        const int parent = find_key(rule.parent);
        if (child < 0 || parent < 0) {
            add(name + ": a key is no setting");
            continue;
        }
        if (rule.values.empty()) {
            add(name + ": no values");
        }
        const Spec& parent_spec = core::at(specs, parent);
        for (const char* value : rule.values) {
            if (match_choice(parent_spec, value) < 0) {
                add(name + ": " + value + " is no choice of " + rule.parent);
            }
        }
        const auto [child_group, child_row] = core::at(place, child);
        const auto [parent_group, parent_row] = core::at(place, parent);
        if (child_group != parent_group || parent_row >= child_row) {
            add(name + ": the parent isn't an earlier row of the child's group");
        }
        if (rule_for(parent) != nullptr) {
            add(name + ": the parent is itself hidden by a rule");
        }
    }
    return problems;
}

const std::vector<GroupSet>& bunches()
{
    // (resolved from the keys once: a setting added or removed in get_specs() moves no other group's rows)
    static const std::vector<GroupSet> kGroups = [] {
        std::vector<GroupSet> groups;
        for (const GroupKeys& keys : group_keys()) {
            GroupSet group{.id = keys.id, .name = keys.name, .rows = {}};
            for (const char* key : keys.keys) {
                const int found = find_key(key);
                if (found >= 0) {
                    group.rows.push_back(found);
                }
            }
            groups.push_back(std::move(group));
        }
        assert(check_tables().empty() && "the options' tables: see check_tables()");
        return groups;
    }();
    return kGroups;
}

namespace {
ChoiceFilter& choice_filter()
{
    static ChoiceFilter filter = nullptr;
    return filter;
}
} // namespace

void set_choice_filter(ChoiceFilter filter) { choice_filter() = filter; }

bool choice_shown(const Spec& spec, size_t choice)
{
    if (choice >= spec.choices.size()) {
        return false;
    }
    const ChoiceFilter filter = choice_filter();
    return filter == nullptr || filter(spec, core::at(spec.choices, choice).value);
}

int preset_option()
{
    static const int kIndex = find_key("graphics_preset");
    return kIndex;
}

const std::vector<PresetValue>& preset_values(int choice)
{
    static const std::vector<PresetValue> kNone;
    static const std::vector<PresetValue> kOriginal = preset_list(kPresetOriginal);
    static const std::vector<PresetValue> kHigh = preset_list(kPresetHigh);
    static const std::vector<PresetValue> kUltra = preset_list(kPresetUltra);
    switch (choice) {
    case 1:
        return kOriginal;
    case 2:
        return kHigh;
    case 3:
        return kUltra;
    default:
        return kNone;
    }
}

int preset_of(const std::string& text)
{
    const std::vector<Spec>& specs = get_specs();
    const int choices = static_cast<int>(core::at(specs, preset_option()).choices.size());
    for (int choice = 1; choice < choices; ++choice) {
        bool all = true;
        for (const PresetValue& set : preset_values(choice)) {
            const int found = find_key(set.key);
            if (found < 0) {
                all = false;
                break;
            }
            const Spec& spec = core::at(specs, found);
            std::string value;
            const bool present = ini_get(text, {.section = spec.section, .key = spec.key}, &value);
            if (choice_of(spec, value, present) != choice_of(spec, set.value, true)) {
                all = false;
                break;
            }
        }
        if (all) {
            return choice;
        }
    }
    return 0;
}

int get_find(const std::string& section, const std::string& key)
{
    const std::vector<Spec>& specs = get_specs();
    for (size_t i = 0; i < specs.size(); ++i) {
        if (get_same(core::at(specs, i).section, section) && get_same(core::at(specs, i).key, key)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int choice_of(const Spec& spec, const std::string& value, bool present)
{
    if (!present) {
        return spec.fallback;
    }
    // (an empty value is the game's default; a number between the choices: the fallback as well)
    const int matched = match_choice(spec, value);
    return matched >= 0 ? matched : spec.fallback;
}

bool ini_get(const std::string& text, const IniKey& at, std::string* value)
{
    const std::string& section = at.section;
    const std::string& key = at.key;
    bool in_section = false;
    for (const Seg& segment : get_lines(text)) {
        const std::string line = text.substr(segment.start, segment.end - segment.start);
        std::string name;
        if (section_of(line, &name)) {
            if (in_section) {
                return false; // (only the first section of the name: Windows reads no other - a second [Graphics] is ignored)
            }
            in_section = get_same(name, section);
            continue;
        }
        size_t eq = 0;
        if (in_section && get_key_of(line, &name, &eq) && get_same(name, key)) {
            if (value) {
                *value = get_trim(line.substr(eq + 1));
            }
            return true;
        }
    }
    return false;
}

std::string ini_set(const std::string& text, const IniKey& at, const std::string& value)
{
    const std::string& section = at.section;
    const std::string& key = at.key;
    const std::string eol =
        text.find("\r\n") != std::string::npos || text.find('\n') == std::string::npos ? "\r\n" : "\n";
    const std::vector<Seg> lines = get_lines(text);
    bool in_section = false;
    bool found = false;
    size_t insert_at = std::string::npos; // after the section's last line that isn't blank
    for (const Seg& segment : lines) {
        const std::string line = text.substr(segment.start, segment.end - segment.start);
        std::string name;
        if (section_of(line, &name)) {
            if (in_section) {
                break; // (the section ended)
            }
            in_section = get_same(name, section);
            if (in_section) {
                found = true;
                insert_at = segment.next;
            }
            continue;
        }
        if (!in_section) {
            continue;
        }
        size_t eq = 0;
        if (get_key_of(line, &name, &eq) && get_same(name, key)) {
            // (the value replaced; whatever stood after the line - its break - kept)
            return text.substr(0, segment.start + eq + 1) + value + text.substr(segment.end);
        }
        if (!get_trim(line).empty()) {
            insert_at = segment.next;
        }
    }
    const std::string added = key + "=" + value;
    if (found) {
        std::string before = text.substr(0, insert_at);
        // (the line before has no break when it is the file's last)
        if (!before.empty() && before.back() != '\n') {
            before += eol;
        }
        return before + added + eol + text.substr(insert_at);
    }
    std::string out = text;
    if (!out.empty() && out.back() != '\n') {
        out += eol;
    }
    if (!out.empty()) {
        out += eol;
    }
    return out + "[" + section + "]" + eol + added + eol;
}

} // namespace frontend::new_frontend::options
