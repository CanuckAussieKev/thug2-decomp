// NOT ORIGINAL - thug2_options_check.exe: the new front end's Options screen's tables (frontend/new_frontend_options.h)
// checked without the game: every setting in exactly one group (the groups name their rows by key), each with a label
// and choices that include its default, the graphics settings' defaults and when they apply as the renderer reads them
// (README's [Graphics] table), every choice written to an ini text and read back as itself with the rest of the text
// kept, and the graphics presets recognised once written. Exit code 0 when everything holds.

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
        {.key = "anisotropy", .fallback = "0", .live = false},
        {.key = "msaa", .fallback = "0", .live = false},
        {.key = "draw_distance_scale", .fallback = "1", .live = false},
        {.key = "lod_distance_scale", .fallback = "1", .live = false},
        {.key = "rt_tessellation", .fallback = "0", .live = false},
        {.key = "rt_tess_max_deviation", .fallback = "2", .live = false},
        {.key = "rt_shadows", .fallback = "0", .live = true},
        {.key = "rt_shadow_strength", .fallback = "0.5", .live = true},
        {.key = "rt_shadow_softness", .fallback = "0", .live = true},
        {.key = "rt_shadow_objects", .fallback = "1", .live = true},
        {.key = "rt_shadow_sun", .fallback = "bake", .live = true},
        {.key = "rt_ao", .fallback = "0", .live = true},
        {.key = "rt_ao_strength", .fallback = "0.6", .live = true},
        {.key = "rt_ao_radius", .fallback = "48", .live = true},
        {.key = "rt_ao_rays", .fallback = "4", .live = true},
        {.key = "rt_gi", .fallback = "0", .live = true},
        {.key = "rt_gi_strength", .fallback = "1", .live = true},
        {.key = "rt_gi_rays", .fallback = "2", .live = true},
        {.key = "rt_relit", .fallback = "0", .live = true},
        {.key = "rt_relit_strength", .fallback = "1", .live = true},
        {.key = "rt_relit_sun", .fallback = "bake", .live = true},
        {.key = "rt_pbr", .fallback = "0", .live = true},
        {.key = "rt_pbr_specular", .fallback = "1", .live = true},
        {.key = "rt_pbr_normal_strength", .fallback = "1", .live = true},
        {.key = "rt_reflections", .fallback = "0", .live = true},
        {.key = "rt_reflection_strength", .fallback = "0.5", .live = true},
        {.key = "rt_reflection_fresnel", .fallback = "1", .live = true},
        {.key = "rt_glass_reflectance", .fallback = "0.2", .live = true},
        {.key = "rt_glass_roughness", .fallback = "0.05", .live = true},
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
        {.key = "rt_style", .fallback = "0", .live = true},
        {.key = "rt_style_strength", .fallback = "1", .live = true},
        {.key = "rt_style_smooth", .fallback = "4", .live = true},
        {.key = "rt_style_bands", .fallback = "5", .live = true},
        {.key = "rt_style_outlines", .fallback = "1", .live = true},
        {.key = "rt_style_outline_width", .fallback = "1", .live = true},
        {.key = "rt_style_saturation", .fallback = "1.15", .live = true},
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

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_tables();
    check_graphics();
    check_ini();
    check_presets();
    std::printf("options check: %d checks, %d failed\n", check_state().checked, check_state().failures);
    return check_state().failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
