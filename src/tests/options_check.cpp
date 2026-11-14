// NOT ORIGINAL - thug2_options_check.exe: the new front end's Options screen's tables (frontend/new_frontend_options.h)
// checked without the game: every setting in exactly one group (the groups name their rows by key), each with a label
// and choices that include its default, the graphics settings' defaults and when they apply as the renderer reads them
// (README's [Graphics] table), every choice written to an ini text and read back as itself with the rest of the text
// kept, and the graphics presets recognised once written. Exit code 0 when everything holds.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "tests/no_error_dialogs.h"
#include "base/core/checked_index.h"
#include "frontend/new_frontend_options.h"

namespace {

namespace options = frontend::new_frontend::options;

struct CheckState {
    int failures = 0;
    int checked = 0;
};

CheckState& check_state()
{
    static CheckState state;
    return state;
}

void expect(bool ok, const std::string& what)
{
    ++check_state().checked;
    if (!ok) {
        ++check_state().failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

// A [Graphics] setting as the renderer reads it: its default (README's settings table) and whether the Options screen
// changes it while the game runs.
struct Expected {
    const char* key;
    const char* fallback;
    bool live;
};

// (the graphics settings the Options screen offers - a new one is added here too, or the check fails)
const std::vector<Expected>& expected()
{
    static const std::vector<Expected> kExpected = {
        {.key = "graphics_preset", .fallback = "", .live = false},
        {.key = "renderer", .fallback = "vulkan", .live = false},
        {.key = "ray_tracing", .fallback = "0", .live = false},
        {.key = "texture_set", .fallback = "original", .live = true},
        {.key = "max_fps", .fallback = "0", .live = true},
        {.key = "present_mode", .fallback = "vsync", .live = true},
        {.key = "show_fps", .fallback = "0", .live = true},
        {.key = "anisotropy", .fallback = "0", .live = false},
        {.key = "msaa", .fallback = "0", .live = false},
        {.key = "draw_distance_scale", .fallback = "1", .live = false},
        {.key = "lod_distance_scale", .fallback = "1", .live = false},
        {.key = "rt_tessellation", .fallback = "0", .live = false},
        {.key = "rt_tess_max_deviation", .fallback = "2", .live = false},
        {.key = "rt_sun_source", .fallback = "level", .live = true},
        {.key = "rt_shadows", .fallback = "0", .live = true},
        {.key = "rt_shadow_strength", .fallback = "0.5", .live = true},
        {.key = "rt_shadow_softness", .fallback = "0", .live = true},
        {.key = "rt_shadow_objects", .fallback = "1", .live = true},
        {.key = "rt_ao", .fallback = "0", .live = true},
        {.key = "rt_ao_strength", .fallback = "0.6", .live = true},
        {.key = "rt_ao_radius", .fallback = "48", .live = true},
        {.key = "rt_ao_rays", .fallback = "4", .live = true},
        {.key = "rt_gi", .fallback = "0", .live = true},
        {.key = "rt_gi_strength", .fallback = "1", .live = true},
        {.key = "rt_gi_rays", .fallback = "2", .live = true},
        {.key = "rt_lights", .fallback = "0", .live = true},
        {.key = "rt_lights_strength", .fallback = "0.35", .live = true},
        {.key = "rt_lights_shadowed", .fallback = "4", .live = true},
        {.key = "rt_cel_bands", .fallback = "3", .live = true},
        {.key = "rt_cel_shadow_colour", .fallback = "0.62,0.66,0.86", .live = true},
        {.key = "rt_cel_rim", .fallback = "0.5", .live = true},
        {.key = "rt_cel_outline_width", .fallback = "1", .live = true},
        {.key = "rt_cel_flatten", .fallback = "0.5", .live = true},
        {.key = "rt_relit", .fallback = "0", .live = true},
        {.key = "rt_relit_strength", .fallback = "1", .live = true},
        {.key = "rt_pbr", .fallback = "0", .live = true},
        {.key = "rt_pbr_specular", .fallback = "1", .live = true},
        {.key = "rt_pbr_normal_strength", .fallback = "1", .live = true},
        {.key = "rt_detail", .fallback = "0", .live = true},
        {.key = "rt_detail_strength", .fallback = "1", .live = true},
        {.key = "rt_delight", .fallback = "0", .live = true},
        {.key = "rt_delight_strength", .fallback = "0.5", .live = true},
        {.key = "rt_reflections", .fallback = "0", .live = true},
        {.key = "rt_reflection_strength", .fallback = "0.5", .live = true},
        {.key = "rt_reflection_fresnel", .fallback = "1", .live = true},
        {.key = "rt_glass_reflectance", .fallback = "0.2", .live = true},
        {.key = "rt_glass_roughness", .fallback = "0.05", .live = true},
        {.key = "grass", .fallback = "0", .live = true},
        {.key = "grass_density", .fallback = "1", .live = true},
        {.key = "grass_height", .fallback = "1", .live = true},
        {.key = "grass_distance", .fallback = "50", .live = true},
        {.key = "rt_water", .fallback = "0", .live = true},
        {.key = "rt_water_waves", .fallback = "1", .live = true},
        {.key = "rt_water_wind", .fallback = "1", .live = true},
        {.key = "rt_water_clarity", .fallback = "1", .live = true},
        {.key = "rt_water_foam", .fallback = "1", .live = true},
        {.key = "rt_water_tint", .fallback = "auto", .live = true},
        {.key = "rt_water_geometry", .fallback = "1", .live = true},
        {.key = "rt_sky", .fallback = "0", .live = true},
        {.key = "rt_clouds", .fallback = "0", .live = true},
        {.key = "rt_cloud_coverage", .fallback = "auto", .live = true},
        {.key = "rt_cloud_quality", .fallback = "1", .live = true},
        {.key = "rt_sky_model", .fallback = "0", .live = true},
        {.key = "rt_sun", .fallback = "0", .live = true},
        {.key = "rt_sun_colour", .fallback = "auto", .live = true},
        {.key = "rt_shafts", .fallback = "0", .live = true},
        {.key = "rt_fog", .fallback = "0", .live = true},
        {.key = "rt_fog_haze", .fallback = "1", .live = true},
        {.key = "rt_fog_haze_density", .fallback = "0.3", .live = true},
        {.key = "rt_hdr", .fallback = "0", .live = false},
        {.key = "rt_exposure", .fallback = "0", .live = true},
        {.key = "rt_tonemap", .fallback = "0", .live = true},
        {.key = "rt_white_point", .fallback = "1.25", .live = true},
        {.key = "rt_saturation", .fallback = "1", .live = true},
        {.key = "rt_contrast", .fallback = "1", .live = true},
        {.key = "rt_bloom", .fallback = "0", .live = true},
        {.key = "rt_bloom_strength", .fallback = "0.05", .live = true},
        {.key = "rt_motion_blur", .fallback = "0", .live = true},
        {.key = "rt_motion_blur_strength", .fallback = "0.5", .live = true},
        {.key = "rt_dof", .fallback = "0", .live = true},
        {.key = "rt_dof_strength", .fallback = "0.4", .live = true},
        {.key = "rt_grade", .fallback = "off", .live = true},
        {.key = "rt_grade_strength", .fallback = "1", .live = true},
        {.key = "rt_grade_vignette", .fallback = "0", .live = true},
        {.key = "rt_grade_grain", .fallback = "0", .live = true},
        {.key = "rt_fisheye_strength", .fallback = "0.75", .live = true},
        {.key = "rt_fisheye_vignette", .fallback = "0.5", .live = true},
        {.key = "rt_fisheye_circle", .fallback = "0.5", .live = true},
        {.key = "rt_style", .fallback = "off", .live = true},
        {.key = "rt_style_strength", .fallback = "1", .live = true},
        {.key = "rt_style_smooth", .fallback = "4", .live = true},
        {.key = "rt_style_bands", .fallback = "5", .live = true},
        {.key = "rt_style_outlines", .fallback = "1", .live = true},
        {.key = "rt_style_outline_width", .fallback = "1", .live = true},
        {.key = "rt_style_saturation", .fallback = "1.15", .live = true},
        {.key = "rt_style_comic_halftone", .fallback = "5", .live = true},
        {.key = "rt_style_sketch_hatch", .fallback = "5", .live = true},
        {.key = "rt_style_sketch_wash", .fallback = "0.35", .live = true},
        {.key = "rt_style_paper", .fallback = "0.5", .live = true},
        {.key = "rt_style_grain", .fallback = "0.5", .live = true},
        {.key = "rt_style_vignette", .fallback = "0.5", .live = true},
        {.key = "rt_style_noir_contrast", .fallback = "1.8", .live = true},
        {.key = "rt_style_noir_dark", .fallback = "0,0,0", .live = true},
        {.key = "rt_style_noir_light", .fallback = "0.95,0.92,0.84", .live = true},
        {.key = "rt_style_thermal_focus", .fallback = "0.6", .live = true},
        {.key = "rt_style_vhs_fisheye", .fallback = "0.35", .live = true},
        {.key = "rt_style_vhs_bleed", .fallback = "1", .live = true},
        {.key = "rt_style_vhs_wobble", .fallback = "1", .live = true},
        {.key = "rt_style_vhs_noise", .fallback = "1", .live = true},
        {.key = "rt_style_vhs_scanlines", .fallback = "0.5", .live = true},
        {.key = "rt_style_vhs_saturation", .fallback = "0.7", .live = true},
        {.key = "rt_style_vhs_overlay", .fallback = "1", .live = true},
        {.key = "rt_style_retro_lines", .fallback = "240", .live = true},
        {.key = "rt_style_retro_bits", .fallback = "5", .live = true},
        {.key = "rt_style_retro_dither", .fallback = "1", .live = true},
        {.key = "rt_style_retro_snap", .fallback = "1", .live = true},
        {.key = "rt_style_retro_affine", .fallback = "1", .live = true},
        {.key = "rt_style_watercolour_bleed", .fallback = "1", .live = true},
        {.key = "rt_style_watercolour_flow", .fallback = "1", .live = true},
        {.key = "rt_style_watercolour_granulation", .fallback = "1", .live = true},
        {.key = "rt_style_watercolour_edges", .fallback = "1", .live = true},
        {.key = "rt_style_watercolour_paper", .fallback = "0.6", .live = true},
        {.key = "rt_style_watercolour_outlines", .fallback = "0.35", .live = true},
        {.key = "upscaler", .fallback = "off", .live = false},
        {.key = "upscale_quality", .fallback = "quality", .live = false},
        {.key = "ray_reconstruction", .fallback = "0", .live = false},
    };
    return kExpected;
}

bool same_value(const char* a, const char* b)
{
    char* end_a = nullptr;
    char* end_b = nullptr;
    const double x = std::strtod(a, &end_a);
    const double y = std::strtod(b, &end_b);
    if (*a != '\0' && *b != '\0' && *end_a == '\0' && *end_b == '\0') {
        return x == y;
    }
    return std::strcmp(a, b) == 0;
}

void check_tables()
{
    const std::string problems = options::check_tables();
    expect(problems.empty(), "the tables:\n" + problems);
    // (bunches' rows: every setting once, no index twice)
    std::set<int> seen;
    size_t rows = 0;
    for (const options::GroupSet& group : options::bunches()) {
        expect(!group.rows.empty(), std::string("group ") + group.id + " has rows");
        for (const int row : group.rows) {
            seen.insert(row);
            ++rows;
        }
    }
    expect(rows == options::get_specs().size() && seen.size() == rows, "every setting in one group, once");
    // (the Style tab: the art style first, every style's knob - rt_style_*, cel shading's rt_cel_* and the fisheye
    // styles' rt_fisheye_* - in it and in no other tab)
    bool style_tab = false;
    for (const options::GroupSet& group : options::bunches()) {
        const bool is_style = std::strcmp(group.id, "style") == 0;
        style_tab = style_tab || is_style;
        if (is_style) {
            expect(!group.rows.empty() &&
                       std::strcmp(core::at(options::get_specs(), group.rows.front()).key, "rt_style") == 0,
                   "the Style tab starts with the art style");
        }
        for (const int row : group.rows) {
            const std::string key = core::at(options::get_specs(), row).key;
            const bool style_key = key.starts_with("rt_style") || key.starts_with("rt_cel") || key.starts_with("rt_fisheye");
            expect(style_key == is_style, key + (is_style ? " isn't a style's knob" : " belongs in the Style tab"));
        }
    }
    expect(style_tab, "there is a Style tab");
    expect(options::get_find("Graphics", "rt_cel") < 0, "no row writes rt_cel (cel shading is rt_style=cel)");
    expect(options::get_find("Graphics", "rt_fisheye") < 0,
           "no row writes rt_fisheye (the fisheye lens is rt_style=fisheye / fisheye_vhs)");
    // (the Camera tab, right after Colour: the camera effects - motion blur, depth of field, the grade - and nothing else)
    {
        const std::vector<options::GroupSet>& groups = options::bunches();
        int colour = -1;
        int camera = -1;
        for (size_t i = 0; i < groups.size(); ++i) {
            colour = std::strcmp(core::at(groups, i).id, "colour") == 0 ? static_cast<int>(i) : colour;
            camera = std::strcmp(core::at(groups, i).id, "camera") == 0 ? static_cast<int>(i) : camera;
        }
        expect(colour >= 0 && camera == colour + 1, "there is a Camera tab, right after Colour");
        if (camera >= 0) {
            for (const int row : core::at(groups, static_cast<size_t>(camera)).rows) {
                const std::string key = core::at(options::get_specs(), row).key;
                expect(key.starts_with("rt_motion_blur") || key.starts_with("rt_dof") || key.starts_with("rt_grade"),
                       key + " isn't a camera effect");
            }
            for (const char* key : {"rt_motion_blur", "rt_motion_blur_strength", "rt_dof", "rt_dof_strength", "rt_grade",
                                    "rt_grade_strength", "rt_grade_vignette", "rt_grade_grain"}) {
                const int index = options::get_find("Graphics", key);
                const std::vector<int>& rows = core::at(groups, static_cast<size_t>(camera)).rows;
                expect(std::find(rows.begin(), rows.end(), index) != rows.end(), std::string(key) + " is in the Camera tab");
            }
        }
    }
}

void check_graphics()
{
    const std::vector<options::Spec>& specs = options::get_specs();
    for (const Expected& want : expected()) {
        const int index = options::get_find("Graphics", want.key);
        expect(index >= 0, std::string(want.key) + " is offered");
        if (index < 0) {
            continue;
        }
        const options::Spec& spec = core::at(specs, index);
        expect(same_value(core::at(spec.choices, spec.fallback).value, want.fallback),
               std::string(want.key) + "'s default is " + want.fallback);
        expect((spec.apply == options::Apply::kLive) == want.live,
               std::string(want.key) + (want.live ? " applies at once" : " waits for the restart"));
        expect(spec.help != nullptr && spec.label != nullptr && *spec.label != '\0', std::string(want.key) + " has a label");
    }
    // (every [Graphics] setting offered is one the check knows)
    for (const options::Spec& spec : specs) {
        if (std::strcmp(spec.section, "Graphics") != 0) {
            continue;
        }
        bool known = false;
        for (const Expected& want : expected()) {
            known = known || std::strcmp(want.key, spec.key) == 0;
        }
        expect(known, std::string(spec.key) + " is in the check's table");
    }
}

void check_ini()
{
    const std::string sample = "; settings\r\n[features]\r\nnew_frontend=1\r\n\r\n[Graphics]\r\nResolutionX=1280\r\n"
                               "msaa = 4\r\n\r\n[Miscellaneous]\r\nOnlineGameName=\r\n";
    for (const options::Spec& spec : options::get_specs()) {
        if (spec.apply == options::Apply::kPreset) {
            continue; // (no key of its own)
        }
        const options::IniKey at{.section = spec.section, .key = spec.key};
        std::string value;
        const bool present = options::ini_get(sample, at, &value);
        const int before = options::choice_of(spec, value, present);
        for (size_t i = 0; i < spec.choices.size(); ++i) {
            const char* written = core::at(spec.choices, i).value;
            const std::string text = options::ini_set(sample, at, written);
            std::string read;
            const bool found = options::ini_get(text, at, &read);
            const std::string name = std::string("[") + spec.section + "] " + spec.key + "=" + written;
            expect(found && read == written, name + " written and read back");
            expect(options::choice_of(spec, read, found) == static_cast<int>(i), name + " is choice " + std::to_string(i));
            std::string kept;
            expect(options::ini_get(text, {.section = "Graphics", .key = "ResolutionX"}, &kept) && kept == "1280",
                   name + " keeps the other keys");
            expect(text.starts_with("; settings\r\n"), name + " keeps the comment");
        }
        expect(before >= 0 && std::cmp_less(before, spec.choices.size()), std::string(spec.key) + " read from the sample");
    }
    // (a section named twice: only the first is read, as Windows reads it, and a key is written there)
    const std::string twice = "[Graphics]\r\nmsaa=2\r\n[Graphics]\r\nrt_ao=1\r\n";
    expect(!options::ini_get(twice, {.section = "Graphics", .key = "rt_ao"}, nullptr), "a second [Graphics] isn't read");
    std::string value;
    const std::string set = options::ini_set(twice, {.section = "Graphics", .key = "rt_ao"}, "0");
    expect(options::ini_get(set, {.section = "Graphics", .key = "rt_ao"}, &value) && value == "0",
           "a key written into the first [Graphics]");
}

void check_presets()
{
    const int preset = options::preset_option();
    expect(preset >= 0, "the graphics preset is offered");
    if (preset < 0) {
        return;
    }
    const options::Spec& spec = core::at(options::get_specs(), preset);
    expect(options::preset_values(0).empty(), "Custom sets nothing");
    // (no [Graphics] keys: every effect at its default - off -, which is the Original preset)
    expect(options::preset_of("") == 1, "an empty ini is Original");
    for (int choice = 1; std::cmp_less(choice, spec.choices.size()); ++choice) {
        std::string text = "[Graphics]\r\nrt_ao_strength=0.8\r\n";
        for (const options::PresetValue& set : options::preset_values(choice)) {
            text = options::ini_set(text, {.section = "Graphics", .key = set.key}, set.value);
        }
        expect(options::preset_of(text) == choice,
               std::string("the ") + core::at(spec.choices, choice).label + " preset recognised once written");
        // (one effect changed after it: Custom - unless that is another preset)
        const std::string changed = options::ini_set(text, {.section = "Graphics", .key = "rt_gi"},
                                                     options::preset_of(text) == 3 ? "0" : "1");
        const int after = options::preset_of(changed);
        expect(after != choice, std::string("the ") + core::at(spec.choices, choice).label + " preset left by a change");
    }
    // (the presets differ from each other)
    expect(options::preset_values(1).size() == options::preset_values(3).size(), "the presets set the same keys");
}

// Rows shown only while another setting is set a certain way: hidden while it is off, listed while it is on, every
// rule's child and parent real settings (check_shown_rules), the Colour tab's toon knobs among them, no row hidden by a
// setting that doesn't control it (the rules are the table's: the parent's own key prefix names the effect), and a
// preset still writing the hidden rows' keys.
void check_shown_when()
{
    const std::vector<options::Spec>& specs = options::get_specs();
    expect(options::check_shown_rules().empty(), "the shown-when rules:\n" + options::check_shown_rules());
    expect(!options::shown_rules().empty(), "there are shown-when rules");
    const auto index_of = [](const char* key) { return options::get_find("Graphics", key); };
    for (const options::ShownWhen& rule : options::shown_rules()) {
        const std::string name = std::string(rule.key) + " by " + rule.parent;
        const int child = index_of(rule.key);
        const int parent = index_of(rule.parent);
        expect(child >= 0 && parent >= 0, name + ": both are settings");
        if (child < 0 || parent < 0) {
            continue;
        }
        const options::Spec& parent_spec = core::at(specs, parent);
        // (every choice of the parent: the child listed exactly while it is one of the rule's values)
        for (size_t choice = 0; choice < parent_spec.choices.size(); ++choice) {
            std::vector<int> choices(specs.size(), 0);
            for (size_t i = 0; i < specs.size(); ++i) {
                core::at(choices, i) = core::at(specs, i).fallback;
            }
            core::at(choices, parent) = static_cast<int>(choice);
            bool listed = false;
            for (const char* value : rule.values) {
                listed = listed || std::strcmp(value, core::at(parent_spec.choices, choice).value) == 0;
            }
            expect(options::row_shown(child, choices) == listed,
                   name + (listed ? ": listed" : ": hidden") + " while " + parent_spec.key + "=" +
                       core::at(parent_spec.choices, choice).value);
        }
        // (the parent itself is never hidden; no effect's knob is hidden by another effect's switch: the child's
        // key starts with its parent's, less a trailing letter or two - rt_shadow_* under rt_shadows)
        expect(options::row_shown(parent, {}), name + ": the parent is always listed");
        const std::string stem = std::string(rule.parent).substr(0, std::string(rule.parent).size() > 6 ? 6 : 4);
        const bool upscale = std::strcmp(rule.parent, "upscaler") == 0;
        // (cel shading is the art style cel: its rt_cel_* knobs under rt_style)
        // (and the fisheye styles' lens: its rt_fisheye_* knobs under rt_style)
        const bool cel = std::strcmp(rule.parent, "rt_style") == 0 &&
                         (std::string(rule.key).starts_with("rt_cel_") || std::string(rule.key).starts_with("rt_fisheye_"));
        expect(upscale || cel || std::string(rule.key).starts_with(stem) || std::string(rule.key).starts_with("rt_glass_"),
               name + ": the parent controls the child");
    }
    // (the six toon knobs, by name)
    for (const char* key : {"rt_style_strength", "rt_style_smooth", "rt_style_bands", "rt_style_outlines", "rt_style_outline_width",
                            "rt_style_saturation"}) {
        std::vector<int> choices(specs.size(), 0);
        for (size_t i = 0; i < specs.size(); ++i) {
            core::at(choices, i) = core::at(specs, i).fallback;
        }
        const int style = index_of("rt_style");
        core::at(choices, style) = 0;
        expect(!options::row_shown(index_of(key), choices), std::string(key) + " hidden while the art style is off");
        core::at(choices, style) = 1;
        expect(options::row_shown(index_of(key), choices), std::string(key) + " listed with the toon style");
    }
    // (the looks' own knobs: listed only under their looks)
    {
        struct LookRow {
            const char* key;
            const char* shown;  // a style it is listed under
            const char* hidden; // ... and one it isn't
        };
        constexpr std::array kLookRows{
            LookRow{.key = "rt_style_comic_halftone", .shown = "comic", .hidden = "toon"},
            LookRow{.key = "rt_style_sketch_hatch", .shown = "sketch", .hidden = "comic"},
            LookRow{.key = "rt_style_grain", .shown = "noir", .hidden = "toon"},
            LookRow{.key = "rt_style_noir_dark", .shown = "noir", .hidden = "thermal"},
            LookRow{.key = "rt_style_thermal_focus", .shown = "thermal", .hidden = "noir"},
            LookRow{.key = "rt_style_bands", .shown = "comic", .hidden = "sketch"},
            LookRow{.key = "rt_cel_bands", .shown = "cel", .hidden = "toon"},
            LookRow{.key = "rt_cel_flatten", .shown = "cel", .hidden = "comic"},
            LookRow{.key = "rt_style_vhs_fisheye", .shown = "vhs", .hidden = "retro"},
            LookRow{.key = "rt_style_vhs_fisheye", .shown = "vhs", .hidden = "fisheye_vhs"}, // (the lens replaces it)
            LookRow{.key = "rt_style_vhs_bleed", .shown = "fisheye_vhs", .hidden = "fisheye"},
            LookRow{.key = "rt_style_vhs_overlay", .shown = "fisheye_vhs", .hidden = "retro"},
            LookRow{.key = "rt_style_strength", .shown = "fisheye_vhs", .hidden = "fisheye"}, // (fisheye: no style pass)
            LookRow{.key = "rt_fisheye_strength", .shown = "fisheye", .hidden = "vhs"},
            LookRow{.key = "rt_fisheye_vignette", .shown = "fisheye_vhs", .hidden = "off"},
            LookRow{.key = "rt_fisheye_circle", .shown = "fisheye", .hidden = "toon"},
            LookRow{.key = "rt_style_retro_snap", .shown = "retro", .hidden = "vhs"},
            LookRow{.key = "rt_style_watercolour_paper", .shown = "watercolour", .hidden = "sketch"},
        };
        const int style = index_of("rt_style");
        const options::Spec& styles = core::at(specs, style);
        const auto choice_index = [&](const char* value) {
            for (size_t i = 0; i < styles.choices.size(); ++i) {
                if (std::strcmp(core::at(styles.choices, i).value, value) == 0) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        for (const LookRow& row : kLookRows) {
            std::vector<int> choices(specs.size(), 0);
            for (size_t i = 0; i < specs.size(); ++i) {
                core::at(choices, i) = core::at(specs, i).fallback;
            }
            core::at(choices, style) = choice_index(row.shown);
            expect(options::row_shown(index_of(row.key), choices), std::string(row.key) + " listed with " + row.shown);
            core::at(choices, style) = choice_index(row.hidden);
            expect(!options::row_shown(index_of(row.key), choices), std::string(row.key) + " hidden with " + row.hidden);
        }
    }
    // (the upscaler: off hides its knobs, any provider lists them)
    {
        std::vector<int> choices(specs.size(), 0);
        const int upscaler = index_of("upscaler");
        const options::Spec& spec = core::at(specs, upscaler);
        for (size_t choice = 0; choice < spec.choices.size(); ++choice) {
            core::at(choices, upscaler) = static_cast<int>(choice);
            const bool off = std::strcmp(core::at(spec.choices, choice).value, "off") == 0;
            expect(options::row_shown(index_of("upscale_quality"), choices) == !off,
                   std::string("upscale_quality with upscaler=") + core::at(spec.choices, choice).value);
        }
    }
    // (a preset writes the hidden rows' keys too: every key of the preset is a setting, hidden or not)
    for (int choice = 1; choice < 4; ++choice) {
        for (const options::PresetValue& set : options::preset_values(choice)) {
            expect(index_of(set.key) >= 0, std::string("preset key ") + set.key + " is a setting");
        }
    }
}

// The texture sets' row: Original always offered; a filter (the game's: the sets installed) hides the others.
void check_choice_filter()
{
    const int index = options::get_find("Graphics", "texture_set");
    expect(index >= 0, "texture_set is offered");
    if (index < 0) {
        return;
    }
    const options::Spec& spec = core::at(options::get_specs(), index);
    for (size_t i = 0; i < spec.choices.size(); ++i) {
        expect(options::choice_shown(spec, i), std::string("without a filter every choice is shown: ") + core::at(spec.choices, i).value);
    }
    options::set_choice_filter([](const options::Spec& filtered, const char* value) {
        return std::strcmp(filtered.key, "texture_set") != 0 || std::strcmp(value, "seedvr2") != 0;
    });
    expect(options::choice_shown(spec, 0) && options::choice_shown(spec, 1) && !options::choice_shown(spec, 2),
           "the filter hides a set that isn't installed");
    expect(!options::choice_shown(spec, spec.choices.size()), "no choice past the last");
    options::set_choice_filter(nullptr);
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_tables();
    check_graphics();
    check_ini();
    check_presets();
    check_choice_filter();
    check_shown_when();
    std::printf("options check: %d checks, %d failed\n", check_state().checked, check_state().failures);
    return check_state().failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
