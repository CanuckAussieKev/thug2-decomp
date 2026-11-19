#pragma once

// NOT ORIGINAL - (round CS) a setting changed while the game runs: the one path the console (`set`, `toggle`, `reset` -
// devtools/console.h), the automation channel (`console <command>`) and the Options screen all take. A change is
//   1. looked up in the settings model (settings_model.h: "section/key" or the key alone) and its value checked as the
//      Options screen offers it (a switch 0 / 1 / on / off, one of a choice's values, a number) - an unknown key or a
//      value that isn't one is refused;
//   2. written to thug2-decomp.ini by the one writer (write): the key's line replaced, added to its section or removed
//      (back at its default) - every other line, comment and line ending as it was, no byte-order mark added; written
//      to a new file and moved over the old one;
//   3. put to use as the model's Apply says, after running() is read again (settings_state.h):
//      - kLive: its owner (Setting::owner) does it now - the applier the game installs for each (set_applier;
//        app/settings_appliers.cpp, frontend/new_frontend_settings.cpp): the renderer's part read again, the display
//        made again (the back buffer and its samples, the window, the upscaler, frame generation), a switch set...;
//      - kReload: the level is loaded again and the skater put back where he stood, facing the same way (the game's own
//        level load - the reloader the game installs, set_level_reloader); offline only, and the level's run (score,
//        goals, peds, the clock's transitions) starts again;
//      - kRelaunch: nothing changes until the next start - the setting says why (Setting::restart).
// Nothing here changes the game's simulation: every owner is a drawing, a menu, a log or a load's.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/config/settings_model.h"

namespace config::live {

enum class Outcome : uint8_t {
    kNow,       // put to use (or nothing to do: the value it had)
    kReload,    // the level loads again with it
    kNextStart, // the next time the game starts
};

struct Result {
    bool ok = false;                  // false: refused or failed - `text` says why
    Outcome outcome = Outcome::kNow;
    std::string text;                 // one line for the console, the automation's answer and the log
};

// --- the owners' appliers (installed by the game: app/settings_appliers.cpp, frontend/new_frontend_settings.cpp) ---

// What puts a live setting to use: called after the ini is written and running() read again; true when it did now
// (false: nothing to change here now - no Vulkan renderer, no device yet).
using Applier = bool (*)(const Setting& setting);
void set_applier(Owner owner, Applier apply);
// Whether `owner` has one (the start-up logs every owner the model names without one).
[[nodiscard]] bool has_applier(Owner owner);
// The owners the model names that have no applier ("" none) - logged at start-up.
std::string missing_appliers();

// The level loaded again where the player stands (the game's: game/front_end/level_reload.h). True when it will
// happen; false - `why` - when it can't (no level, a network game, the level editor open).
using Reloader = bool (*)(std::string* why);
void set_level_reloader(Reloader reload);

// --- the steps ---

// The setting `name` names: "section/key", "[section]key" or the key alone (any case); -1 with `why` when none.
int find_named(const std::string& name, std::string* why);
// `value` as the setting takes it (`normal`: the choice's own spelling, a switch's 0 / 1); false with `why` when it
// takes no such value (or is retired, or is the preset's - set_preset).
bool check_value(const Setting& setting, const std::string& value, std::string* normal, std::string* why);
// The settings file's path, and the one writer: each [section] key set to its value (nullptr: removed - its default),
// in one write. False when the file couldn't be read or written. (The reads cached by the platform are forgotten:
// build_info::forget_cached_reads.)
std::string ini_path();
struct Write {
    int setting = -1;
    std::string value;
    bool remove = false;
};
bool write(const std::vector<Write>& writes);
// `setting` put to use as its Apply says (after write).
Result apply(int setting);

// --- the commands (the console's, the automation's and the Options screen's) ---

// [section] key = value: checked, written, put to use.
Result set(const std::string& name, const std::string& value);
// Several at once (the Options screen's preset): written in one write, each put to use (the level reloaded once).
Result set_all(const std::vector<std::pair<int, std::string>>& changes);
// An on / off setting turned over.
Result toggle(const std::string& name);
// Back at its default (its line removed).
Result reset(const std::string& name);
// Its value, its default, whether it is in effect and why (the settings report's words), and when a change is seen.
std::string get(const std::string& name);
// Every setting whose section / key holds `filter` (any case; "" all): key = value, * when it isn't the default, the
// effective value when it differs.
std::string list(const std::string& filter);
// Every setting read only at start-up, and why.
std::string restart_list();
// When a change of `setting` is seen, in words ("now", "reloads the level", "next start: <why>").
std::string when_text(const Setting& setting);

} // namespace config::live
