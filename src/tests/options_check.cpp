// NOT ORIGINAL - thug2_options_check.exe: the settings model (engine/config/settings_model.h) and the new front end's
// Options screen over it (frontend/new_frontend_options.h), checked without the game:
//   - the model's own checks (check_model) and the screen's tables: every offered setting in one tab, the Style and
//     Camera tabs, every choice written to an ini text and read back with the rest of the text kept, the presets;
//   - README's settings tables against the model: every declared key has a row and every row's key is declared, with
//     the same default;
//   - every rule: each need on another setting (broken: the setting out of effect; met: in effect), each capability
//     (taken away: out of effect), each choice's need and its stand-in, each force and each conflict - and the resolver
//     on several machines (no ray tracing GPU, the 32-bit game, AMD, an RTX 40, an RTX 50, no Vulkan).
// Exit code 0 when everything holds. The README's path: the first argument, else the source tree's.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tests/no_error_dialogs.h"
#include "base/core/checked_index.h"
#include "engine/config/ini_text.h"
#include "engine/config/settings_model.h"
#include "engine/config/settings_text.h"
#include "frontend/new_frontend_options.h"

#ifndef THUG2_README
#define THUG2_README "README.md"
#endif

namespace {

namespace options = frontend::new_frontend::options;
namespace t = config::text;

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

bool same_value(const std::string& a, const std::string& b)
{
    char* end_a = nullptr;
    char* end_b = nullptr;
    const double x = std::strtod(a.c_str(), &end_a);
    const double y = std::strtod(b.c_str(), &end_b);
    if (!a.empty() && !b.empty() && *end_a == '\0' && *end_b == '\0') {
        return x == y;
    }
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

// --- the machines ---

config::Capabilities rtx50()
{
    constexpr int kSeries = 50;
    constexpr int kRefresh = 144;
    return {.vulkan = true,
            .ray_queries = true,
            .x64 = true,
            .vendor = config::kVendorNvidia,
            .rtx = kSeries,
            .surface_maps = true,
            .material_library = true,
            .height_maps = false,
            .texture_sets = {"original", "pbrify"},
            .refresh_hz = kRefresh,
            .gpu = "NVIDIA GeForce RTX 5070 Ti"};
}
config::Capabilities rtx40()
{
    config::Capabilities caps = rtx50();
    caps.rtx = config::rtx_series("NVIDIA GeForce RTX 4080");
    caps.gpu = "NVIDIA GeForce RTX 4080";
    return caps;
}
config::Capabilities amd()
{
    config::Capabilities caps = rtx50();
    caps.vendor = config::kVendorAmd;
    caps.rtx = 0;
    caps.gpu = "AMD Radeon RX 7900 XT";
    return caps;
}
config::Capabilities no_rt_gpu()
{
    constexpr uint32_t kIntel = 0x8086;
    config::Capabilities caps = rtx50();
    caps.vendor = kIntel;
    caps.rtx = 0;
    caps.ray_queries = false;
    caps.gpu = "Intel(R) UHD Graphics 630";
    return caps;
}
config::Capabilities x86_build()
{
    config::Capabilities caps = rtx50();
    caps.x64 = false;
    return caps;
}
config::Capabilities no_vulkan()
{
    config::Capabilities caps;
    caps.x64 = true;
    caps.texture_sets = {"original"};
    return caps;
}

// The machine `caps` without capability `cap`.
config::Capabilities without(config::Capabilities caps, config::Cap cap)
{
    switch (cap) {
    case config::Cap::kVulkan:
        caps.vulkan = false;
        break;
    case config::Cap::kRayQueries:
        caps.ray_queries = false;
        break;
    case config::Cap::k64Bit:
        caps.x64 = false;
        break;
    case config::Cap::kNvidia:
        caps.vendor = config::kVendorAmd;
        caps.rtx = 0;
        break;
    case config::Cap::kRtx:
        caps.rtx = 0;
        break;
    case config::Cap::kRtx40:
        caps.rtx = config::rtx_series("RTX 3080");
        break;
    case config::Cap::kRtx50:
        caps.rtx = config::rtx_series("RTX 4080");
        break;
    case config::Cap::kSurfaceMaps:
        caps.surface_maps = false;
        break;
    case config::Cap::kMaterialLibrary:
        caps.material_library = false;
        break;
    case config::Cap::kHeightMaps:
        caps.height_maps = false;
        break;
    case config::Cap::kLevelWithoutFog:
    default:
        break;
    }
    return caps;
}

using Ini = std::vector<std::pair<std::string, std::string>>; // ("section/key" or a [Graphics] key, value)

// A key's slot: "section/key", or a [Graphics] key.
std::string slot_of(const std::string& key)
{
    const size_t slash = key.find('/');
    return slash == std::string::npos ? config::ini_slot("Graphics", key.c_str())
                                      : config::ini_slot(key.substr(0, slash).c_str(), key.substr(slash + 1).c_str());
}

config::Inputs inputs_of(const config::Capabilities& caps, const Ini& ini)
{
    config::Inputs inputs{.ini = {}, .caps = caps, .level = {}};
    for (const auto& [key, value] : ini) {
        inputs.ini[slot_of(key)] = value;
    }
    return inputs;
}

config::Resolution resolve(const config::Capabilities& caps, const Ini& ini) { return config::resolve(inputs_of(caps, ini)); }

// Every effect on (as far as they go together: no art style, no upscaler, msaa - the settings that need those are the
// exclusive ones below).
Ini everything_on()
{
    Ini ini;
    for (const config::Setting& setting : config::settings()) {
        if (setting.kind == config::Kind::kSwitch && setting.retired == nullptr) {
            ini.emplace_back(config::ini_slot(setting.section, setting.key), "1");
        }
    }
    const Ini extra = {{"renderer", "vulkan"},
                       {"msaa", "4"},
                       {"rt_shadow_softness", "1"},
                       {"rt_displacement", "1"},
                       {"tod", "noon"},
                       {"tod_cycle", "on"},
                       {"rt_lights", "1"},
                       {"rt_grade", "level"},
                       {"rt_style", "off"},
                       {"upscaler", "off"},
                       {"frame_generation", "off"},
                       {"reflex", "on"},
                       {"present_mode", "mailbox"},
                       {"features/start_level", "TR"},
                       {"Miscellaneous/OnlineGameName", "thugpro"}};
    for (const auto& [key, value] : extra) {
        ini.emplace_back(slot_of(key), value);
    }
    // (AntiAliasing: out of effect with msaa set - one exclusive pair)
    return ini;
}

Ini with(Ini ini, const std::string& key, const std::string& value)
{
    const std::string slot = slot_of(key);
    std::erase_if(ini, [&](const auto& each) { return slot_of(each.first) == slot; });
    ini.emplace_back(slot, value);
    return ini;
}

// The settings out of effect with every effect on: each needs a choice another excludes (an art style's knobs, the
// upscaler's, AntiAliasing beside msaa), or is retired.
bool exclusive(const config::Setting& setting)
{
    const std::string key = setting.key;
    if (setting.retired != nullptr) {
        return true;
    }
    constexpr std::array kStyleKnobs = {"rt_style_", "rt_fisheye_", "rt_cel_"};
    constexpr std::array kOthers = {"AntiAliasing",      "upscale_quality", "ray_reconstruction",
                                    "upscale_sharpness", "fsr_version",     "frame_generation_provider"};
    // (the light energy audit: the bounce pass and the relit mode both light the bounce - one owner, relit's)
    if (key.starts_with("rt_gi")) {
        return true;
    }
    return std::ranges::any_of(kStyleKnobs, [&key](const char* each) { return key.starts_with(each); }) ||
           std::ranges::any_of(kOthers, [&key](const char* each) { return key == each; });
}

std::string name_of(const config::Setting& setting) { return std::string("[") + setting.section + "] " + setting.key; }

// --- the model and the screen's tables ---

void check_tables()
{
    const std::string problems = options::check_tables();
    expect(problems.empty(), "the model and the tables:\n" + problems);
    std::set<int> seen;
    size_t rows = 0;
    for (const options::GroupSet& group : options::bunches()) {
        expect(!group.rows.empty(), std::string("group ") + group.id + " has rows");
        for (const int row : group.rows) {
            seen.insert(row);
            ++rows;
        }
    }
    expect(rows == options::get_specs().size() && seen.size() == rows, "every offered setting in one tab, once");
    // (the Style tab: the art style first, every style's knob - rt_style_*, cel shading's rt_cel_* and the fisheye
    // styles' rt_fisheye_* - in it and in no other tab)
    bool style_tab = false;
    for (const options::GroupSet& group : options::bunches()) {
        const bool is_style = std::strcmp(group.id, "style") == 0;
        style_tab = style_tab || is_style;
        if (is_style) {
            expect(!group.rows.empty() && std::strcmp(core::at(options::get_specs(), group.rows.front()).key, "rt_style") == 0,
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
    expect(options::get_find("Graphics", "rt_fisheye") < 0, "no row writes rt_fisheye (the lens is rt_style=fisheye)");
    // (the Camera tab, right after Colour: the camera effects and nothing else)
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
    }
    // (a live [Graphics] setting names the part of the renderer it changes; one read at start-up names none it reads
    // again)
    for (const config::Setting& setting : config::settings()) {
        if (std::strcmp(setting.section, "Graphics") == 0 && setting.apply == config::Apply::kLive &&
            setting.group == config::Group::kNone) {
            expect(setting.effect != config::Effect::kNone, name_of(setting) + " is live: it names its renderer part");
        }
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
            // (and the model reads it as that choice)
            const config::Resolution resolution = config::resolve({.ini = config::ini_values(text), .caps = rtx50(), .level = {}});
            expect(same_value(core::at(resolution.items, spec.setting).asked, written), name + " is what the model reads");
        }
        expect(before >= 0 && std::cmp_less(before, spec.choices.size()), std::string(spec.key) + " read from the sample");
    }
    // (a section named twice: only the first is read, as Windows reads it, and a key is written there)
    const std::string twice = "[Graphics]\r\nmsaa=2\r\n[Graphics]\r\nrt_ao=1\r\n";
    expect(!options::ini_get(twice, {.section = "Graphics", .key = "rt_ao"}, nullptr), "a second [Graphics] isn't read");
    expect(!config::ini_values(twice).contains("graphics/rt_ao"), "the model doesn't read a second [Graphics] either");
    std::string value;
    const std::string set = options::ini_set(twice, {.section = "Graphics", .key = "rt_ao"}, "0");
    expect(options::ini_get(set, {.section = "Graphics", .key = "rt_ao"}, &value) && value == "0",
           "a key written into the first [Graphics]");
    // (old spellings: rt_style=1 is toon, tod_cycle=1 is on, upscale_quality=native is dlaa; a switch's 2 is on)
    const config::Resolution old = resolve(rtx50(), {{"rt_style", "1"}, {"tod_cycle", "1"}, {"upscale_quality", "native"}, {"rt_bloom", "2"}});
    expect(old.value("rt_style") == "toon" && old.value("tod_cycle") == "on" && old.at("upscale_quality").asked == "dlaa" &&
               old.on("rt_bloom"),
           "older spellings read as their choices");
    const config::Resolution bad = resolve(rtx50(), {{"rt_grade", "sepia"}, {"rt_shadow_strength", "dark"}});
    expect(bad.value("rt_grade") == "off" && bad.at("rt_grade").reason == t::kNotAChoice, "a value that is no choice: the default, said so");
    expect(same_value(bad.at("rt_shadow_strength").asked, "0.5"), "a number that isn't one: the default");
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
    expect(options::preset_of("") == 1, "an empty ini is Original");
    for (int choice = 1; std::cmp_less(choice, spec.choices.size()); ++choice) {
        std::string text = "[Graphics]\r\nrt_ao_strength=0.8\r\n";
        for (const options::PresetValue& set : options::preset_values(choice)) {
            text = options::ini_set(text, {.section = "Graphics", .key = set.key}, set.value);
        }
        expect(options::preset_of(text) == choice,
               std::string("the ") + core::at(spec.choices, choice).label + " preset recognised once written");
        const std::string changed =
            options::ini_set(text, {.section = "Graphics", .key = "rt_relit"}, options::preset_of(text) == 3 ? "0" : "1");
        expect(options::preset_of(changed) != choice, std::string("the ") + core::at(spec.choices, choice).label + " preset left by a change");
    }
    // (Ultra on an RTX 50: every effect it sets in effect)
    Ini ultra;
    for (const options::PresetValue& set : options::preset_values(3)) {
        ultra.emplace_back(set.key, set.value);
    }
    const config::Resolution resolution = resolve(rtx50(), ultra);
    for (const options::PresetValue& set : options::preset_values(3)) {
        if (std::string_view(set.value) != "0") {
            expect(resolution.on(set.key), std::string("Ultra on an RTX 50: ") + set.key + " in effect");
        }
    }
}

// --- README's settings tables ---

struct ReadmeRow {
    std::string section;
    std::string key;
    std::string fallback;
    int line;
};

std::string readme_default(std::string text)
{
    std::erase(text, '`');
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    while (!text.empty() && text.front() == ' ') {
        text.erase(text.begin());
    }
    if (text == "(none)" || text == "(empty)" || text == "empty" || text == "(the game's)") {
        return {};
    }
    return text;
}

std::vector<std::string> split(const std::string& text, const std::string& by)
{
    std::vector<std::string> parts;
    size_t at = 0;
    while (true) {
        const size_t next = text.find(by, at);
        parts.push_back(text.substr(at, next == std::string::npos ? std::string::npos : next - at));
        if (next == std::string::npos) {
            break;
        }
        at = next + by.size();
    }
    return parts;
}

// The rows of README's settings tables in the sections the model declares.
std::vector<ReadmeRow> readme_rows(const std::string& path, bool* found)
{
    std::ifstream file(path);
    *found = file.good();
    std::vector<ReadmeRow> rows;
    std::set<std::string> sections;
    for (const config::Setting& setting : config::settings()) {
        std::string lower = setting.section;
        std::ranges::transform(lower, lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        sections.insert(lower);
    }
    const std::regex heading(R"(^\*\*`\[([^\]]+)\]`\*\*)");
    const std::regex token("`([^`]*)`");
    std::string line;
    bool in_settings = false;
    std::string section;
    int number = 0;
    while (std::getline(file, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.starts_with("## ")) {
            in_settings = line.starts_with("## Settings");
            continue;
        }
        std::smatch match;
        if (!in_settings) {
            continue;
        }
        if (std::regex_search(line, match, heading)) {
            section = match[1].str();
            continue;
        }
        if (!line.starts_with("| `")) {
            continue;
        }
        const std::vector<std::string> cells = split(line, "|");
        if (cells.size() < 4) {
            continue;
        }
        const std::string& keys = core::at(cells, 1);
        // (each `token`: a key, "[section] key", or "[section]" alone - a place holder for its default)
        std::vector<std::pair<std::string, std::string>> tokens;
        std::string row_section = section;
        for (std::sregex_iterator it(keys.begin(), keys.end(), token), end; it != end; ++it) {
            std::string text = (*it)[1].str();
            if (text.starts_with("[")) {
                const size_t close = text.find(']');
                row_section = text.substr(1, close - 1);
                text = close + 1 < text.size() ? text.substr(close + 2) : std::string();
            }
            tokens.emplace_back(row_section, text);
        }
        std::vector<std::string> defaults = split(core::at(cells, 2), " / ");
        if (defaults.size() != tokens.size() && defaults.size() > 1) {
            std::vector<std::string> more = split(defaults.back(), ", ");
            defaults.pop_back();
            defaults.insert(defaults.end(), more.begin(), more.end());
        }
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& [row_of, key] = core::at(tokens, i);
            std::string lower = row_of;
            std::ranges::transform(lower, lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (key.empty() || key.find(' ') != std::string::npos || !sections.contains(lower)) {
                continue;
            }
            const std::string fallback = defaults.size() == tokens.size() ? core::at(defaults, i) : defaults.front();
            rows.push_back({.section = row_of, .key = key, .fallback = readme_default(fallback), .line = number});
        }
    }
    return rows;
}

void check_readme(const std::string& path)
{
    bool found = false;
    const std::vector<ReadmeRow> rows = readme_rows(path, &found);
    expect(found, "README found at " + path);
    if (!found) {
        return;
    }
    expect(rows.size() > 200, "README's settings tables read (" + std::to_string(rows.size()) + " rows)");
    std::set<int> documented;
    for (const ReadmeRow& row : rows) {
        const int index = config::find(row.section.c_str(), row.key.c_str());
        const std::string where = "README line " + std::to_string(row.line) + ": [" + row.section + "] " + row.key;
        expect(index >= 0, where + " is declared in the settings model");
        if (index < 0) {
            continue;
        }
        documented.insert(index);
        const config::Setting& setting = core::at(config::settings(), index);
        expect(same_value(row.fallback, setting.fallback),
               where + ": README's default " + row.fallback + ", the model's " + setting.fallback);
    }
    const std::vector<config::Setting>& all = config::settings();
    for (size_t i = 0; i < all.size(); ++i) {
        if (core::at(all, i).apply != config::Apply::kPreset) {
            expect(documented.contains(static_cast<int>(i)), name_of(core::at(all, i)) + " has a README row");
        }
    }
}

// --- the rules ---

// A setting a condition is about, and values that meet or break it (a plain is / is-not on one setting).
struct Toggle {
    std::string key;
    std::string meets;
    std::string breaks;
};
bool toggle_of(const config::Cond& cond, Toggle* out)
{
    const bool negated = cond.op == config::Cond::Op::kNot && cond.parts.size() == 1 && cond.parts.front().op == config::Cond::Op::kIs;
    const config::Cond& is = negated ? cond.parts.front() : cond;
    if (is.op != config::Cond::Op::kIs || is.values.empty()) {
        return false;
    }
    const int index = config::find(is.key);
    if (index < 0) {
        return false;
    }
    const config::Setting& other = core::at(config::settings(), index);
    const auto listed = [&is](const std::string& value) {
        return std::ranges::any_of(is.values, [&](const char* each) { return same_value(value, each); });
    };
    // (a value of the other setting that isn't listed: its off value, a choice, or any text)
    std::string unlisted;
    if (other.off != nullptr && !listed(other.off)) {
        unlisted = other.off;
    } else {
        for (const config::Choice& choice : other.choices) {
            if (!listed(choice.value) && choice.needs.empty()) {
                unlisted = choice.value;
                break;
            }
        }
    }
    if (unlisted.empty() && (other.kind == config::Kind::kText || other.kind == config::Kind::kNumber)) {
        unlisted = other.kind == config::Kind::kText ? "XX" : "7";
    }
    if (unlisted.empty()) {
        return false;
    }
    out->key = config::ini_slot(other.section, other.key);
    out->meets = negated ? unlisted : is.values.front();
    out->breaks = negated ? is.values.front() : unlisted;
    return true;
}

void check_everything_on()
{
    const config::Resolution resolution = resolve(rtx50(), everything_on());
    const std::vector<config::Setting>& all = config::settings();
    for (size_t i = 0; i < all.size(); ++i) {
        const config::Setting& setting = core::at(all, i);
        if (setting.apply == config::Apply::kPreset) {
            continue;
        }
        const config::Resolved& item = core::at(resolution.items, i);
        if (exclusive(setting)) {
            expect(!item.in_effect || setting.retired == nullptr,
                   name_of(setting) + ": retired, never in effect");
            continue;
        }
        expect(item.in_effect, "RTX 50, every effect on: " + name_of(setting) + " in effect (" + item.reason + ")");
    }
}

// Each need on another setting: broken, the setting is out of effect; met (with every effect on), it is in effect.
void check_needs()
{
    const Ini base = everything_on();
    int toggled = 0;
    int held = 0;
    const std::vector<config::Setting>& all = config::settings();
    for (size_t i = 0; i < all.size(); ++i) {
        const config::Setting& setting = core::at(all, i);
        for (const config::Need& need : setting.needs) {
            Toggle toggle;
            if (!toggle_of(need.cond, &toggle)) {
                continue;
            }
            const std::string rule = name_of(setting) + " needs " + toggle.key;
            const config::Resolution broken = resolve(rtx50(), with(base, toggle.key, toggle.breaks));
            // (a setting another rule holds where it is - the relit mode under the time of day, Ray Reconstruction with
            // no DLSS - can't be broken this way: its own checks below)
            const int other = config::find(toggle.key.substr(toggle.key.find('/') + 1).c_str());
            if (other < 0 || !same_value(core::at(broken.items, other).value, toggle.breaks)) {
                ++held;
                continue;
            }
            ++toggled;
            const config::Resolved& off = core::at(broken.items, i);
            expect(!off.in_effect && !off.reason.empty(), rule + ": out of effect with " + toggle.key + "=" + toggle.breaks);
            if (setting.off != nullptr) {
                expect(same_value(off.value, setting.off), rule + ": its off value while out of effect");
            }
            const config::Resolution met = resolve(rtx50(), with(base, toggle.key, toggle.meets));
            if (!exclusive(setting)) {
                expect(core::at(met.items, i).in_effect, rule + ": in effect with " + toggle.key + "=" + toggle.meets);
            }
        }
    }
    expect(toggled > 150 && held < 15,
           "the needs on other settings toggled (" + std::to_string(toggled) + ", " + std::to_string(held) + " held)");
    // (the ones held above: the relit mode's knobs follow it - out of effect with it off and nothing forcing it)
    const config::Resolution relit_off = resolve(rtx50(), {{"ray_tracing", "1"}, {"rt_relit", "0"}});
    for (const char* key : {"rt_relit_strength", "rt_relit_residual", "rt_relit_detail", "rt_relit_sky_rays", "rt_relit_sun_rays", "rt_relit_cache"}) {
        expect(!relit_off.at(key).in_effect, std::string(key) + " out of effect with the relit mode off");
    }
}

// Each capability a setting needs: without it, the setting is out of effect with the capability's words (or its root's).
void check_capabilities()
{
    int taken = 0;
    const config::Resolution base = resolve(rtx50(), everything_on());
    const std::vector<config::Setting>& all = config::settings();
    for (size_t i = 0; i < all.size(); ++i) {
        const config::Setting& setting = core::at(all, i);
        for (const config::Need& need : setting.needs) {
            if (need.cond.op != config::Cond::Op::kHas || need.cond.cap == config::Cap::kLevelWithoutFog ||
                need.cond.cap == config::Cap::kLevelOutdoor) { // (the level's facts: own cases below)
                continue;
            }
            ++taken;
            const config::Resolution lacking = resolve(without(rtx50(), need.cond.cap), everything_on());
            const config::Resolved& item = core::at(lacking.items, i);
            expect(!item.in_effect && !item.reason.empty(), name_of(setting) + ": out of effect without its capability");
            expect(core::at(base.items, i).in_effect || exclusive(setting), name_of(setting) + ": in effect with it");
        }
    }
    expect(taken >= 7, "the capabilities taken away (" + std::to_string(taken) + ")");
    // (the level's fog: the haze is for levels without)
    config::Inputs foggy = inputs_of(rtx50(), everything_on());
    foggy.level = {.known = true, .fog = true};
    const config::Resolution fog = config::resolve(foggy);
    expect(!fog.at("rt_fog_haze").in_effect && fog.at("rt_fog_haze").reason == t::kLevelHasFog,
           "a level with fog of its own: no haze");
    expect(!fog.at("rt_fog_haze_density").in_effect, "a level with fog of its own: the haze's air out of effect");
    expect(fog.on("rt_fog"), "a level with fog of its own: the volumetric fog still on");
    // (added by the snow) snow on outdoor levels only - the level's fact; its three knobs with it; Vulkan, no ray tracing
    const Ini snowy = {{"snow", "1"}, {"snow_depth", "6"}, {"snow_trail_quality", "3"}, {"snowfall", "1"}};
    const config::Resolution outside = resolve(no_rt_gpu(), snowy);
    expect(outside.on("snow") && outside.on("snowfall") && outside.value("snow_depth") == "6" &&
               outside.value("snow_trail_quality") == "3",
           "snow on an outdoor level without a ray tracing GPU");
    config::Inputs indoor_inputs = inputs_of(rtx50(), snowy);
    indoor_inputs.level.outdoor = false;
    const config::Resolution indoor = config::resolve(indoor_inputs);
    expect(!indoor.on("snow") && indoor.at("snow").reason == t::kLevelIndoors, "an indoor level: no snow, and why");
    expect(!indoor.at("snow_depth").in_effect && !indoor.at("snowfall").in_effect, "an indoor level: snow's knobs out");
    expect(!resolve(no_vulkan(), snowy).on("snow") && resolve(no_vulkan(), snowy).at("snow").reason == t::kNoVulkanGpu,
           "no Vulkan: no snow, at the root");
    expect(!resolve(rtx50(), {}).on("snow"), "snow off by default");
    // (added by the de-lit texture sets) rt_delight in effect whatever texture set is chosen: a de-lit set's textures
    // that came flat are left alone by the renderer one by one (gpu_vulkan/vulkan_detail.h), its others keep it
    config::Capabilities sets = rtx50();
    sets.texture_sets = {"original", "pbrify", "seedvr2"};
    for (const char* set : {"seedvr2", "pbrify", "original"}) {
        const config::Resolution with_set = resolve(sets, {{"rt_pbr", "1"}, {"rt_delight", "1"}, {"texture_set", set}});
        expect(with_set.on("rt_delight") && with_set.at("rt_delight_strength").in_effect,
               std::string("texture_set=") + set + ": the draw-time de-lighting in effect (per texture in the renderer)");
    }
}

// The machines: what a typical ini resolves to on each.
void check_machines()
{
    const Ini ini = {{"ray_tracing", "1"}, {"rt_shadows", "1"},     {"rt_shadow_strength", "0.75"}, {"rt_gi", "1"},
                     {"upscaler", "dlss"}, {"frame_generation", "4x"}, {"ray_reconstruction", "1"}, {"rt_pbr", "1"},
                     {"msaa", "4"},        {"grass", "1"},          {"rt_relit", "1"},             {"texture_set", "seedvr2"}};
    // RTX 50: everything as asked; DLSS-G makes 4x, Reflex on with it
    const config::Resolution r50 = resolve(rtx50(), ini);
    expect(r50.on("rt_shadows") && r50.on("rt_relit") && r50.on("rt_pbr"), "RTX 50: the effects on");
    expect(!r50.on("rt_gi") && r50.at("rt_gi").reason == t::kRelitBouncesLight,
           "RTX 50: the bounce pass out of effect - relit lights the bounce (one owner)");
    expect(r50.value("frame_generation") == "4x" && r50.value("upscaler") == "dlss", "RTX 50: 4x frame generation over DLSS");
    expect(r50.on("ray_reconstruction"), "RTX 50: Ray Reconstruction in effect");
    expect(r50.value("reflex") == "on" && r50.at("reflex").forced && r50.at("reflex").reason == t::kOnForFrameGeneration,
           "RTX 50: Reflex on - DLSS frame generation needs it");
    expect(!r50.at("msaa").in_effect && r50.value("msaa") == "0" && r50.at("msaa").reason == t::kOffWhileUpscaling,
           "RTX 50: msaa off while upscaling");
    expect(!r50.on("rt_ao_denoise") || !r50.on("rt_ao"), "RTX 50: no AO denoiser while Ray Reconstruction cleans it up");
    {
        Ini unrelit = ini;
        std::erase_if(unrelit, [](const auto& each) { return each.first == "rt_relit"; });
        const config::Resolution plain = resolve(rtx50(), unrelit);
        expect(plain.on("rt_gi") && !plain.on("rt_gi_denoise") &&
                   plain.at("rt_gi_denoise").reason == t::kRayReconstructionDenoises,
               "RTX 50 without relit: the bounce pass on, its denoiser left to Ray Reconstruction");
    }
    expect(r50.value("texture_set") == "original" && r50.at("texture_set").reason == t::kNotInstalled,
           "a texture set that isn't installed: the original");
    expect(r50.on("grass"), "RTX 50: grass");
    // RTX 40: DLSS-G at 2x - 3x and 4x need an RTX 50
    const config::Resolution r40 = resolve(rtx40(), ini);
    expect(r40.value("frame_generation") == "2x" && r40.at("frame_generation").reason == t::kMultiNeedsRtx50,
           "RTX 40: 4x asked, 2x made");
    expect(!core::at(r40.at("frame_generation").blocked, 3).empty() && core::at(r40.at("frame_generation").blocked, 1).empty(),
           "RTX 40: 4x not offered, 2x offered");
    expect(r40.value("reflex") == "on", "RTX 40: Reflex on with DLSS-G");
    expect(r40.on("ray_reconstruction"), "RTX 40: Ray Reconstruction");
    // AMD: FSR for both; no Reflex, no Ray Reconstruction
    const config::Resolution ramd = resolve(amd(), ini);
    expect(ramd.value("upscaler") == "fsr" && ramd.at("upscaler").reason == t::kDlssNeedsRtx, "AMD: DLSS asked, FSR used");
    expect(ramd.value("frame_generation") == "2x", "AMD: FSR frame generation, 2x");
    expect(!ramd.on("reflex") && !ramd.at("reflex").forced, "AMD: no Reflex");
    expect(!ramd.on("ray_reconstruction") && ramd.at("ray_reconstruction").reason == t::kNeedsDlss,
           "AMD: no Ray Reconstruction - it needs DLSS");
    expect(ramd.on("rt_shadows") && ramd.on("rt_relit"), "AMD: ray tracing works");
    {
        Ini unrelit = ini;
        std::erase_if(unrelit, [](const auto& each) { return each.first == "rt_relit"; });
        expect(resolve(amd(), unrelit).on("rt_gi_denoise"), "AMD without relit: the bounce's own denoiser");
    }
    // no ray tracing GPU: every ray-traced effect out, with the root reason
    const config::Resolution nort = resolve(no_rt_gpu(), ini);
    expect(!nort.on("ray_tracing") && nort.at("ray_tracing").reason == t::kNeedsRayGpu, "no RT GPU: ray tracing out");
    for (const char* key : {"rt_shadows", "rt_shadow_strength", "rt_gi", "rt_relit"}) {
        expect(!nort.at(key).in_effect && nort.at(key).reason == t::kNeedsRayGpu,
               std::string("no RT GPU: ") + key + " out - " + nort.at(key).reason);
    }
    expect(nort.on("rt_pbr") && nort.on("grass"), "no RT GPU: per-pixel lighting and grass still on");
    // the 32-bit game: no ray tracing, no upscaler or frame generation
    const config::Resolution r86 = resolve(x86_build(), ini);
    expect(!r86.on("ray_tracing") && r86.at("ray_tracing").reason == t::kNeeds64Bit, "x86: ray tracing out - 64-bit");
    expect(!r86.on("rt_shadows") && r86.at("rt_shadows").reason == t::kNeeds64Bit, "x86: shadows out, at the root");
    expect(r86.value("upscaler") == "off" && r86.value("frame_generation") == "off", "x86: no upscaler or frame generation");
    expect(r86.on("msaa"), "x86: msaa in effect (no upscaler)");
    // no Vulkan: Direct3D 9, its reason; the Vulkan effects out; the time of day and smooth curves still in effect
    const config::Resolution nvk = resolve(no_vulkan(), with(with(ini, "tod", "dusk"), "rt_tessellation", "1"));
    expect(nvk.value("renderer") == "d3d9" && nvk.at("renderer").reason == t::kNoVulkanGpu, "no Vulkan: Direct3D 9");
    expect(!nvk.on("rt_shadows") && nvk.at("rt_shadows").reason == t::kNoVulkanGpu, "no Vulkan: shadows out at the root");
    expect(!nvk.on("grass") && nvk.at("grass").reason == t::kNoVulkanGpu, "no Vulkan: grass out, at the root");
    expect(nvk.value("tod") == "dusk" && nvk.on("rt_tessellation"), "no Vulkan: the time of day and smooth curves");
    // renderer=d3d9 asked on a Vulkan machine
    const config::Resolution d3d = resolve(rtx50(), with(ini, "renderer", "d3d9"));
    expect(!d3d.on("rt_shadows") && d3d.at("rt_shadows").reason == t::kNeedsVulkan, "renderer=d3d9: Vulkan's effects out");
    expect(d3d.value("upscaler") == "off" && d3d.on("msaa"), "renderer=d3d9: no upscaler, msaa back");
    // rtx_series
    expect(config::rtx_series("NVIDIA GeForce RTX 5070 Ti") == 50 && config::rtx_series("NVIDIA GeForce RTX 4090") == 40 &&
               config::rtx_series("NVIDIA GeForce RTX 2060 SUPER") == 20 && config::rtx_series("NVIDIA GeForce GTX 1080") == 0 &&
               config::rtx_series("NVIDIA RTX A4000") == 30,
           "RTX series from the device's name");
}

// The choices' needs, the forces and the conflicts, one by one.
void check_forces_and_conflicts()
{
    // frame generation turns the upscaler on at native size when none is asked for
    const config::Resolution fg = resolve(rtx50(), {{"frame_generation", "2x"}});
    expect(fg.value("upscaler") == "auto" && fg.at("upscaler").forced && fg.at("upscaler").reason == t::kOnForFrameGeneration,
           "frame generation: the upscaler on");
    expect(fg.value("upscale_quality") == "dlaa" && fg.at("upscale_quality").forced, "frame generation: at native size");
    const config::Resolution fg_dlss = resolve(rtx50(), {{"frame_generation", "2x"}, {"upscaler", "dlss"}, {"upscale_quality", "balanced"}});
    expect(!fg_dlss.at("upscaler").forced && fg_dlss.value("upscale_quality") == "balanced",
           "frame generation with an upscaler asked for: as asked");
    // the provider: DLSS-G on an RTX 40 / 50, else FSR; told FSR, no Reflex forced
    const config::Resolution fsr_frames = resolve(rtx50(), {{"frame_generation", "4x"}, {"frame_generation_provider", "fsr"}});
    expect(fsr_frames.value("frame_generation") == "2x" && fsr_frames.value("reflex") == "off",
           "FSR's frame generation: 2x, Reflex as asked");
    const config::Resolution dlssg_amd = resolve(amd(), {{"frame_generation", "2x"}, {"frame_generation_provider", "dlss"}});
    expect(dlssg_amd.value("frame_generation_provider") == "fsr" && dlssg_amd.at("frame_generation_provider").reason == t::kDlssgNeedsRtx40,
           "DLSS-G asked on AMD: FSR's");
    const config::Resolution no_fg = resolve(rtx50(), {{"frame_generation_provider", "dlss"}});
    expect(!no_fg.at("frame_generation_provider").in_effect, "a provider without frame generation: out of effect");
    // the relit mode: forced on by a sun other than the level's, by the time of day; its knobs in effect with it
    const config::Resolution sun = resolve(rtx50(), {{"ray_tracing", "1"}, {"rt_sun_source", "game"}});
    expect(sun.on("rt_relit") && sun.at("rt_relit").forced && sun.at("rt_relit").reason == t::kOnForSun,
           "rt_sun_source=game: relit on");
    expect(sun.at("rt_relit_strength").in_effect, "relit forced on: its strength in effect");
    const config::Resolution tod = resolve(rtx50(), {{"ray_tracing", "1"}, {"tod", "night"}});
    expect(tod.on("rt_relit") && tod.at("rt_relit").reason == t::kOnForTimeOfDay, "tod=night: relit on");
    const config::Resolution cycle = resolve(rtx50(), {{"ray_tracing", "1"}, {"tod_cycle", "on"}});
    expect(cycle.on("rt_relit") && cycle.at("tod_cycle_minutes").in_effect, "the day cycle: relit on, its length in effect");
    const config::Resolution levels = resolve(rtx50(), {{"ray_tracing", "1"}, {"tod_levels", "NO:night,AU:level"}});
    expect(levels.on("rt_relit"), "tod_levels with a time: relit on");
    const config::Resolution level_only = resolve(rtx50(), {{"ray_tracing", "1"}, {"tod_levels", "AU:level"}});
    expect(!level_only.on("rt_relit"), "tod_levels with the levels' own: relit as asked (off)");
    const config::Resolution tod_nort = resolve(no_rt_gpu(), {{"ray_tracing", "1"}, {"tod", "night"}});
    expect(!tod_nort.on("rt_relit") && tod_nort.at("rt_relit").reason == t::kNeedsRayGpu, "tod without RT: relit asked, out");
    expect(!tod_nort.at("tod_cycle_minutes").in_effect, "no day cycle: its length out of effect");
    // the clock's sun beats rt_sun_source=game; a fixed direction beats the clock (a conflict)
    const config::Resolution clock = resolve(rtx50(), {{"ray_tracing", "1"}, {"rt_sun_source", "game"}, {"tod", "dusk"}});
    expect(!clock.at("rt_sun_source").in_effect && clock.at("rt_sun_source").reason == t::kClockSetsSun &&
               clock.value("rt_sun_source") == "level",
           "rt_sun_source=game under a time of day: the clock's sun");
    const config::Resolution fixed = resolve(rtx50(), {{"ray_tracing", "1"}, {"rt_sun_source", "0.3,0.8,0.5"}, {"tod", "dusk"}});
    expect(fixed.value("rt_sun_source") == "0.3,0.8,0.5" && !fixed.at("tod").conflicts.empty() &&
               fixed.at("tod").conflicts.front() == t::kFixedSunStopsClock,
           "a fixed sun and a time of day: a conflict");
    // THUGPRO's lobbies speak its messages
    const config::Resolution thugpro = resolve(rtx50(), {{"Miscellaneous/OnlineGameName", "thugpro"}});
    expect(thugpro.on("thugpro_peer") && thugpro.at("thugpro_peer").forced, "THUGPRO lobbies: its chat on");
    // conflicts: frame generation under V-sync; more frames than the display shows
    const config::Resolution vsync = resolve(rtx50(), {{"frame_generation", "2x"}});
    expect(!vsync.at("present_mode").conflicts.empty() && vsync.at("present_mode").conflicts.front() == t::kVsyncHoldsFrames,
           "frame generation with V-sync: a conflict");
    const config::Resolution mailbox = resolve(rtx50(), {{"frame_generation", "2x"}, {"present_mode", "mailbox"}});
    expect(mailbox.active.empty(), "frame generation, 60 x 2 under 144 Hz, mailbox: no conflict");
    const config::Resolution fast = resolve(rtx50(), {{"frame_generation", "4x"}, {"present_mode", "immediate"}, {"max_fps", "120"}});
    expect(!fast.at("max_fps").conflicts.empty() && fast.at("max_fps").conflicts.front() == t::kPastRefresh,
           "120 x 4 on a 144 Hz display: a conflict");
    config::Capabilities fast_display = rtx50();
    fast_display.refresh_hz = 480;
    expect(resolve(fast_display, {{"frame_generation", "4x"}, {"present_mode", "immediate"}, {"max_fps", "120"}}).active.empty(),
           "120 x 4 on a 480 Hz display: none");
    // the haze, its air and the sky model; smooth curves' strength; the grade under an art style with its own
    const config::Resolution haze = resolve(rtx50(), {{"ray_tracing", "1"}, {"rt_fog", "1"}});
    expect(haze.on("rt_fog_haze") && !haze.at("rt_fog_haze_density").in_effect &&
               haze.at("rt_fog_haze_density").reason == t::kNeedsHazeAndSkyModel,
           "the haze without the sky model: its density out of effect");
    expect(!resolve(rtx50(), {{"ray_tracing", "1"}, {"rt_sky_model", "1"}}).at("rt_fog_haze").in_effect,
           "the haze without volumetric fog: out of effect (the fog pass draws it)");
    const config::Resolution tess = resolve(rtx50(), {{"rt_tessellation", "1"}});
    expect(!tess.at("rt_tess_smooth_strength").in_effect, "smooth curves without smooth shapes: its strength out");
    const config::Resolution noir = resolve(rtx50(), {{"rt_style", "noir"}, {"rt_grade", "warm"}, {"rt_grade_vignette", "0.65"}});
    expect(!noir.at("rt_grade_vignette").in_effect && noir.at("rt_grade_vignette").reason == t::kStyleOwnVignette,
           "noir: the grade's vignette out - the style's own");
    expect(noir.at("rt_style_vignette").in_effect && noir.at("rt_style_noir_contrast").in_effect, "noir: its own knobs in");
    // ray reconstruction: DLSS upscaling alone (auto on an RTX is DLSS)
    expect(resolve(rtx50(), {{"ray_tracing", "1"}, {"upscaler", "auto"}, {"ray_reconstruction", "1"}}).on("ray_reconstruction"),
           "auto on an RTX: DLSS, Ray Reconstruction");
    expect(!resolve(rtx50(), {{"ray_tracing", "1"}, {"upscaler", "fsr"}, {"ray_reconstruction", "1"}}).on("ray_reconstruction"),
           "FSR: no Ray Reconstruction");
    const config::Resolution fsr = resolve(amd(), {{"upscaler", "auto"}, {"upscale_sharpness", "0.5"}});
    expect(fsr.at("upscale_sharpness").in_effect, "auto on AMD is FSR: its sharpening in effect");
    expect(!resolve(rtx50(), {{"upscaler", "auto"}, {"upscale_sharpness", "0.5"}}).at("upscale_sharpness").in_effect,
           "auto on an RTX is DLSS: no FSR sharpening");
    // rt_displacement: height maps or the test pattern
    expect(resolve(rtx50(), {{"rt_tessellation", "1"}, {"rt_displacement", "2"}}).value("rt_displacement") == "0",
           "displacement without height maps: 0");
    expect(resolve(rtx50(), {{"rt_tessellation", "1"}, {"rt_displacement", "2"}, {"rt_tess_test_heights", "1"}}).on("rt_displacement"),
           "displacement with the test pattern");
    // the sun's colour: with the sun, its rays, the bloom or the sky model
    expect(!resolve(rtx50(), {{"rt_sun_colour", "1,1,1"}}).at("rt_sun_colour").in_effect, "the sun's colour alone: out");
    expect(resolve(rtx50(), {{"rt_bloom", "1"}, {"rt_sun_colour", "1,1,1"}}).at("rt_sun_colour").in_effect, "with the bloom: in");
    // retired: rt_cel
    expect(!resolve(rtx50(), {{"rt_cel", "1"}}).at("rt_cel").in_effect, "rt_cel is retired");
    // the screen's rows: greyed with the reason, the value in use shown
    const std::vector<options::RowState> rows = options::row_states(fg);
    const int upscaler = options::get_find("Graphics", "upscaler");
    expect(upscaler >= 0 && core::at(rows, upscaler).forced &&
               core::at(core::at(options::get_specs(), upscaler).choices, core::at(rows, upscaler).shown).value == std::string("auto"),
           "the Options screen: the forced upscaler shows Auto");
    const std::vector<options::RowState> nort_rows = options::row_states(resolve(no_rt_gpu(), {{"ray_tracing", "1"}, {"rt_shadows", "1"}}));
    const int shadows = options::get_find("Graphics", "rt_shadows");
    expect(shadows >= 0 && !core::at(nort_rows, shadows).in_effect && core::at(nort_rows, shadows).reason == t::kNeedsRayGpu,
           "the Options screen: Shadows greyed - needs a ray tracing GPU");
    const int textures = options::get_find("Graphics", "texture_set");
    expect(textures >= 0 && core::at(core::at(nort_rows, textures).possible, 1) && !core::at(core::at(nort_rows, textures).possible, 2),
           "the Options screen: the texture sets installed offered");
}

// --resolve <ini> [machine] [surface-maps] [texture sets,...] [refresh Hz]: an ini file resolved on one of the machines
// above (rtx50, rtx40, amd, no-rt, x86, no-vulkan): every setting the ini sets that is out of effect, set by another or
// not what was asked, with the reason, and the conflicts - a read-only look at what a player's ini does on a machine.
int resolve_file(const std::span<char*>& args)
{
    std::ifstream file(core::at(args, 2), std::ios::binary);
    if (!file.good()) {
        std::printf("can't read %s\n", core::at(args, 2));
        return EXIT_FAILURE;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    enum Arg : uint8_t { kFile = 2, kMachine, kSurfaceMaps, kSets, kRefresh };
    const std::string machine = args.size() > kMachine ? core::at(args, kMachine) : "rtx50";
    const std::map<std::string, config::Capabilities (*)()> machines = {{"rtx50", &rtx50}, {"rtx40", &rtx40}, {"amd", &amd},
                                                                        {"no-rt", &no_rt_gpu}, {"x86", &x86_build},
                                                                        {"no-vulkan", &no_vulkan}};
    const auto made = machines.find(machine);
    config::Capabilities caps = made != machines.end() ? made->second() : rtx50();
    if (args.size() > kSurfaceMaps) {
        caps.surface_maps = std::string(core::at(args, kSurfaceMaps)) == "1";
    }
    if (args.size() > kSets) {
        caps.texture_sets = {"original"};
        for (const std::string& set : split(core::at(args, kSets), ",")) {
            caps.texture_sets.push_back(set);
        }
    }
    if (args.size() > kRefresh) {
        caps.refresh_hz = std::atoi(core::at(args, kRefresh));
    }
    const config::Resolution resolution = config::resolve({.ini = config::ini_values(text), .caps = caps, .level = {}});
    std::printf("%s on %s (%s, surface maps %s, %d Hz)\n", core::at(args, 2), caps.gpu.empty() ? "no Vulkan GPU" : caps.gpu.c_str(),
                caps.x64 ? "64-bit" : "32-bit", caps.surface_maps ? "yes" : "no", caps.refresh_hz);
    const std::vector<config::Setting>& all = config::settings();
    for (const char* heading : {"dead (set, not in effect)", "set by another setting / not as asked", "in conflict"}) {
        std::printf("\n%s:\n", heading);
        for (size_t i = 0; i < all.size(); ++i) {
            const config::Setting& setting = core::at(all, i);
            const config::Resolved& item = core::at(resolution.items, i);
            const bool dead = item.set && !item.in_effect;
            const bool moved = item.in_effect && item.changed;
            const bool fight = !item.conflicts.empty();
            const std::string which = heading;
            if ((which.starts_with("dead") && dead) || (which.starts_with("set by") && moved)) {
                std::printf("  %s=%s -> %s (%s)\n", name_of(setting).c_str(), item.asked.c_str(),
                            item.in_effect ? item.value.c_str() : "off", item.reason.c_str());
            } else if (which.starts_with("in conflict") && fight) {
                std::printf("  %s=%s: %s\n", name_of(setting).c_str(), item.value.c_str(), item.conflicts.front().c_str());
            }
        }
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const std::span<char*> args(argv, static_cast<size_t>(argc));
    if (args.size() > 2 && std::string(core::at(args, 1)) == "--resolve") {
        return resolve_file(args);
    }
    const std::string readme = args.size() > 1 ? args[1] : THUG2_README;
    check_tables();
    check_ini();
    check_presets();
    check_readme(readme);
    check_everything_on();
    check_needs();
    check_capabilities();
    check_machines();
    check_forces_and_conflicts();
    std::printf("options check: %d checks, %d failed\n", check_state().checked, check_state().failures);
    return check_state().failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
