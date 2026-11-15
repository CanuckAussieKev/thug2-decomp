// NOT ORIGINAL - the settings model at run time (settings_state.h).

#include "engine/config/settings_state.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/config/ini_text.h"
#include "engine/config/settings_model.h"
#include "platform/platform_config.h"

namespace config {

namespace {

struct SettingsState {
    Capabilities caps;
    LevelFacts level;
    bool started = false;
    IniValues at_start; // (the ini when the game started: the start-up settings keep these values)
    Inputs running_inputs;
    Resolution running;
    IniValues overrides; // (set_overrides)
};

SettingsState& settings_state()
{
    static core::NoDestroy<SettingsState> state;
    return state.get();
}

// The ini now, with the start-up settings' values as they were at the start.
Inputs make_running_inputs()
{
    SettingsState& state = settings_state();
    Inputs inputs = current_inputs();
    if (!state.started) {
        state.at_start = inputs.ini;
        state.started = true;
    }
    for (const Setting& setting : settings()) {
        if (setting.apply != Apply::kRelaunch) {
            continue;
        }
        const std::string slot = ini_slot(setting.section, setting.key);
        const auto then = state.at_start.find(slot);
        if (then == state.at_start.end()) {
            inputs.ini.erase(slot);
        } else {
            inputs.ini[slot] = then->second;
        }
    }
    return inputs;
}

} // namespace

void set_capabilities(const Capabilities& caps)
{
    const int refresh = settings_state().caps.refresh_hz;
    settings_state().caps = caps;
    if (caps.refresh_hz == 0) {
        settings_state().caps.refresh_hz = refresh;
    }
    if (settings_state().started) {
        refresh_running();
    }
}

const Capabilities& capabilities() { return settings_state().caps; }

void set_display_refresh(int hz)
{
    settings_state().caps.refresh_hz = hz;
    if (settings_state().started) {
        refresh_running();
    }
}

void set_level_facts(const LevelFacts& level)
{
    settings_state().level = level;
    if (settings_state().started) {
        refresh_running();
    }
}

bool set_level_outdoor(bool outdoor)
{
    if (settings_state().level.outdoor == outdoor) {
        return false;
    }
    settings_state().level.outdoor = outdoor;
    if (settings_state().started) {
        refresh_running();
    }
    return true;
}

bool set_level_light(LevelLight light)
{
    if (settings_state().level.light == light) {
        return false;
    }
    settings_state().level.light = light;
    if (settings_state().started) {
        refresh_running();
    }
    return true;
}

void set_overrides(IniValues values)
{
    settings_state().overrides = std::move(values);
    if (settings_state().started) {
        refresh_running();
    }
}

Inputs current_inputs()
{
    Inputs inputs{.ini = ini_values(build_info::file_text()), .caps = settings_state().caps, .level = settings_state().level};
    for (const auto& [slot, value] : settings_state().overrides) {
        inputs.ini[slot] = value;
    }
    return inputs;
}

Resolution current() { return resolve(current_inputs()); }

const Resolution& running()
{
    if (!settings_state().started) {
        refresh_running();
    }
    return settings_state().running;
}

const Inputs& running_inputs()
{
    if (!settings_state().started) {
        refresh_running();
    }
    return settings_state().running_inputs;
}

void refresh_running()
{
    SettingsState& state = settings_state();
    state.running_inputs = make_running_inputs();
    state.running = resolve(state.running_inputs);
}

Resolution running_with(const std::vector<std::pair<std::string, std::string>>& graphics_overrides)
{
    Inputs inputs = running_inputs();
    for (const auto& [key, value] : graphics_overrides) {
        inputs.ini[ini_slot("Graphics", key.c_str())] = value;
    }
    return resolve(inputs);
}

std::string describe(const Resolution& resolution)
{
    std::string lines;
    const std::vector<Setting>& all = settings();
    for (size_t i = 0; i < all.size() && i < resolution.items.size(); ++i) {
        const Resolved& item = core::at(resolution.items, i);
        const Setting& setting = core::at(all, i);
        // (only what the ini asks for: a default out of effect - most effects' knobs without their switch - is no news)
        if (!item.set || (!item.changed && item.reason.empty())) {
            continue;
        }
        lines += std::string("[") + setting.section + "] " + setting.key + "=" + item.asked + " -> " +
                 (item.in_effect ? item.value : std::string("not in effect")) +
                 (item.reason.empty() ? std::string() : " (" + item.reason + ")") + "\n";
    }
    for (const int index : resolution.active) {
        const Conflict& conflict = core::at(conflicts(), index);
        std::string keys;
        for (const char* key : conflict.keys) {
            keys += (keys.empty() ? "" : " + ") + std::string(key);
        }
        lines += "conflict: " + keys + " - " + conflict.reason + "\n";
    }
    return lines;
}

} // namespace config
