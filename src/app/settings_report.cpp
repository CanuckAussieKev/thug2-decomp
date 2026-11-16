// NOT ORIGINAL - the settings in effect, for the screenshot sidecar (settings_report.h).

#include "app/settings_report.h"

#include <windows.h>

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/renderer_choice.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/ext/extension.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/texture_sets.h"
#include "engine/config/settings_model.h"
#include "engine/config/settings_state.h"
#include "platform/platform_config.h"
#ifdef THUG2_VULKAN
#include "gpu_vulkan/gpu_vulkan.h"
#endif

namespace app {

namespace {

using Fields = std::vector<std::pair<std::string, std::string>>;
using IniSection = std::vector<std::pair<std::string, std::string>>; // (key, value) as the file has them

constexpr std::string_view kWhitespace = " \t\r\n";
constexpr DWORD kSectionBytes = 1U << 16;

std::string trimmed(std::string_view text)
{
    const size_t first = text.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    return std::string(text.substr(first, text.find_last_not_of(kWhitespace) - first + 1));
}

bool same_text(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

// Two ini values the same ("1.50" and "1.5" are).
bool same_value(const std::string& a, const std::string& b)
{
    if (same_text(a, b)) {
        return true;
    }
    char* end_a = nullptr;
    char* end_b = nullptr;
    const double number_a = std::strtod(a.c_str(), &end_a);
    const double number_b = std::strtod(b.c_str(), &end_b);
    return !a.empty() && !b.empty() && *end_a == '\0' && *end_b == '\0' && number_a == number_b;
}

// The file's keys of a section, in the file's order (comment lines left out by Windows).
IniSection read_section(const char* section)
{
    const std::string path = std::string(build_info::executable_directory()) + build_info::kFileName;
    std::vector<char> buffer(kSectionBytes);
    const DWORD length = GetPrivateProfileSectionA(section, buffer.data(), kSectionBytes, path.c_str());
    IniSection keys;
    for (size_t at = 0; at < length && core::at(buffer, at) != '\0';) {
        const std::string_view line(&core::at(buffer, at));
        at += line.size() + 1;
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        keys.emplace_back(trimmed(line.substr(0, equals)), trimmed(line.substr(equals + 1)));
    }
    return keys;
}

const std::string* find_key(const IniSection& keys, const std::string& key)
{
    for (const auto& entry : keys) {
        if (same_text(entry.first, key)) {
            return &entry.second;
        }
    }
    return nullptr;
}

struct StartSettings {
    bool taken = false;
    IniSection graphics;
    IniSection features;
};

StartSettings& start_settings()
{
    static core::NoDestroy<StartSettings> state;
    return state.get();
}

const char* on_off(bool on) { return on ? "on" : "off"; }

std::string shown(const std::string& value) { return value.empty() ? "(empty)" : value; }

// One setting of the model (engine/config/settings_model.h): its value as asked (the ini's, or its default - then
// marked), what is in effect when that differs - the effective value, or "not in effect" - with the model's reason, and
// for a setting read once at start-up that the ini has changed since, what the game started with.
void add_setting(Fields* out, const config::Setting& setting, const config::Resolved& now, const IniSection& at_start)
{
    if (setting.apply == config::Apply::kPreset) {
        return; // (graphics_preset: not a key of its own)
    }
    std::string text = shown(now.asked) + (now.set ? "" : " (default)");
    if (now.changed || !now.reason.empty()) {
        text += " -> " + (now.in_effect ? shown(now.value) : std::string("not in effect"));
        if (!now.reason.empty()) {
            text += " (" + now.reason + ")";
        }
    }
    for (const std::string& conflict : now.conflicts) {
        text += " [conflict: " + conflict + "]";
    }
    for (const std::string& limit : now.limits) {
        text += " [limited: " + limit + "]";
    }
    if (setting.apply == config::Apply::kRelaunch) {
        const std::string* started = find_key(at_start, setting.key);
        const std::string then = started != nullptr ? *started : std::string(setting.fallback);
        if (!same_value(now.set ? now.asked : std::string(setting.fallback), then) && started != nullptr) {
            text += " - changed since the start, running " + shown(then);
        }
    }
    out->emplace_back(std::string(setting.section) + " " + setting.key, text);
}

// [section]'s settings: the model's in its order, then the ini's keys the model doesn't know.
struct SectionKeys {
    const IniSection* now;      // the file's keys now
    const IniSection* at_start; // as the game started
};
void add_section(Fields* out, const char* section, const SectionKeys& keys, const config::Resolution& now)
{
    const IniSection& now_keys = *keys.now;
    const IniSection& at_start = *keys.at_start;
    const std::vector<config::Setting>& all = config::settings();
    for (size_t i = 0; i < all.size() && i < now.items.size(); ++i) {
        if (same_text(core::at(all, i).section, section)) {
            add_setting(out, core::at(all, i), core::at(now.items, i), at_start);
        }
    }
    for (const auto& [key, value] : now_keys) {
        if (config::find(section, key.c_str()) < 0) {
            out->emplace_back(std::string(section) + " " + key, shown(value) + " (not a setting the game knows)");
        }
    }
}

const char* sun_source_name(render::rt::SunSource source)
{
    switch (source) {
    case render::rt::SunSource::kGame:
        return "game";
    case render::rt::SunSource::kFixed:
        return "fixed direction";
    case render::rt::SunSource::kLevel:
    default:
        return "level";
    }
}

// What the game actually runs, where that can differ from the ini.
void add_running(Fields* out)
{
    const auto add = [out](const char* key, std::string text) { out->emplace_back(std::string("running ") + key, std::move(text)); };
    const std::string requested_renderer = [] {
        for (const auto& [key, value] : start_settings().graphics) {
            if (same_text(key, "renderer")) {
                return value;
            }
        }
        return std::string();
    }();
    const bool vulkan = chosen_renderer() == Renderer::kVulkan;
    add("renderer", std::string(vulkan ? "vulkan" : "d3d9") +
                        (!requested_renderer.empty() && !same_text(requested_renderer, vulkan ? "vulkan" : "d3d9")
                             ? " (ini asks for " + requested_renderer + ")"
                             : ""));
    const render::GpuContext& gpu = render::gpu_context();
    if (gpu.device_made()) {
        const render::GpuAdapterInfo& adapter = gpu.adapter();
        add("gpu", adapter.description.data());
        add("driver", core::at(adapter.driver, 0) != '\0' ? adapter.driver.data() : "(unknown)");
        const render::GpuSettings settings = gpu.settings();
        add("window", text::format("%s, back buffer %u x %u, %s", settings.windowed ? "windowed" : "full screen",
                                   settings.width, settings.height, settings.vsync ? "vsync" : "no vsync"));
        add("msaa samples", std::to_string(settings.samples));
    }
    const render::graphics_options::Options& options = render::graphics_options::get_pull();
    add("anisotropy / msaa asked",
        text::format("%d / %d (draw distance x%.3g, LOD distance x%.3g)", options.anisotropy, options.msaa,
                     static_cast<double>(options.draw_distance_scale_), static_cast<double>(options.lod_distance_scale_)));
    add("ray tracing", render::rt::ray_tracing() != nullptr ? "on" : "off (none on this renderer / device, or the ini has it off)");
    add("hdr output", on_off(render::rt::hdr_colour()));
    const render::rt::ViewScale scale = render::rt::view_scale();
#ifdef THUG2_VULKAN
    if (vulkan) {
        const render::vulkan::UpscaleSettings upscale = render::vulkan::upscale_settings();
        const char* provider = render::vulkan::upscale_provider_name();
        std::string text = provider != nullptr ? provider : "none";
        if (upscale.upscaler != render::vulkan::Upscaler::kOff && provider == nullptr) {
            text += " (asked for one - the libraries or the device don't run it: fell back)";
        }
        add("upscaler", text);
        // (rt_style as the renderer took it; cel is cel shading, on with rt_style=cel)
        const render::vulkan::ArtStyle style = render::vulkan::style_settings().style;
        const bool cel = render::rt::cel_settings().on;
        add("art style", render::vulkan::art_style_name(cel ? render::vulkan::ArtStyle::kCel : style));
    }
#endif
    if (scale.on && gpu.device_made()) {
        const render::GpuSettings settings = gpu.settings();
        add("render resolution",
            text::format("%.0f x %.0f (%.1f%% of the output, jitter %.2f, %.2f)",
                         static_cast<double>(static_cast<float>(settings.width) * scale.scale),
                         static_cast<double>(static_cast<float>(settings.height) * scale.scale),
                         static_cast<double>(scale.scale) * 100.0, static_cast<double>(core::at(scale.jitter, 0)),
                         static_cast<double>(core::at(scale.jitter, 1))));
    }
    add("sun source", sun_source_name(render::rt::sun_choice().source));
    const config::Resolved& relit = config::running().at("rt_relit");
    add("rt_relit", std::string(on_off(render::rt::relit_settings().on)) +
                        (relit.forced && relit.in_effect ? " (" + relit.reason + ")" : std::string()));
    add("texture set", render::texture_sets::active());
}

// The ini's [Graphics] and [features] as they are now, kept as what the game started with.
void note_start_settings()
{
    start_settings().graphics = read_section(build_info::kGraphics);
    start_settings().features = read_section("features");
    start_settings().taken = true;
}

class SettingsReportExtension final : public ext::Extension {
public:
    // The first frame: the ini as the game started with it (nothing has changed it yet; before this the ini isn't found)
    void menu_frame(modules::FrontEnd* /*front_end*/, bool /*game_owned*/) override
    {
        if (!start_settings().taken) {
            note_start_settings();
        }
    }
    bool describe(ext::Description* out) override
    {
        out->title = "settings";
        add_running(&out->fields);
        const StartSettings& start = start_settings();
        const IniSection graphics = read_section(build_info::kGraphics);
        const IniSection features = read_section("features");
        // (the ini as it is now through the settings model: what is in effect on this machine, and why not)
        const config::Resolution now = config::current();
        add_section(&out->fields, build_info::kGraphics, {.now = &graphics, .at_start = &start.graphics}, now);
        add_section(&out->fields, "features", {.now = &features, .at_start = &start.features}, now);
        // ([debug]: the keys that are on)
        for (const auto& [key, value] : read_section("debug")) {
            if (value != "0" && !value.empty()) {
                const config::Resolved& item = now.at("debug", key.c_str());
                out->fields.emplace_back("debug " + key, value + (item.set && !item.in_effect ? " -> not in effect (" + item.reason + ")" : ""));
            }
        }
        for (const int index : now.active) {
            const config::Conflict& conflict = core::at(config::conflicts(), index);
            std::string keys;
            for (const char* key : conflict.keys) {
                keys += (keys.empty() ? "" : " + ") + std::string(key);
            }
            out->fields.emplace_back("conflict " + keys, conflict.reason);
        }
        for (const int index : now.limited) {
            const config::Limit& limit = core::at(config::limits(), index);
            std::string keys;
            for (const char* key : limit.keys) {
                keys += (keys.empty() ? "" : " + ") + std::string(key);
            }
            out->fields.emplace_back("limited " + keys, limit.reason);
        }
        return true;
    }
};

} // namespace

ext::Extension& settings_report_extension()
{
    static SettingsReportExtension report;
    return report;
}

} // namespace app
