#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/config/ini_text.h"
#include "engine/config/settings_model.h"

// NOT ORIGINAL - the new front end's Options screen (ours; see new_frontend.h): the settings of thug2-decomp.ini it
// offers - the settings model's (engine/config/settings_model.h: every setting with a tab, in its order) - and how each
// row stands: a setting that isn't in effect is greyed with the model's reason ("Needs Ray tracing"), one another
// setting sets shows the value in use, one that works against another says so. Pure (no files, no game).
namespace frontend::new_frontend::options {

using Apply = config::Apply;
using Choice = config::Choice;
using IniKey = config::IniKey;
using config::ini_get;
using config::ini_set;

struct Spec {
    const char* section; // the ini section ("features", "Graphics", ...)
    const char* key;
    const char* label;
    const char* help;
    std::vector<Choice> choices;
    int fallback; // the choice when the key isn't there (the setting's default)
    Apply apply;
    int setting; // its index in config::settings()
};

struct GroupSet {
    const char* id;        // "graphics", ... (automation)
    const char* name;      // "Graphics"
    std::vector<int> rows; // indices into get_specs(), in the model's order
};

// Every option offered, and the groups (tabs) they are shown in.
const std::vector<Spec>& get_specs();
const std::vector<GroupSet>& bunches();
// What is wrong with the model or the screen's tables, one line each ("" when nothing): config::check_model()'s lines,
// a tab with no rows, a preset's key that isn't offered. (bunches() asserts it is empty; the options check prints it.)
std::string check_tables();

// The option `section`/`key` (any case): its index, -1 if none.
int get_find(const std::string& section, const std::string& key);

// The choice an ini value is (numbers compared as numbers: "1.50" is "1.5"); `present` false: the key isn't in the
// file - the fallback. A value matching no choice: the fallback too (the screen shows what the game uses for it ... as
// near as the choices go).
int choice_of(const Spec& spec, const std::string& value, bool present);

// --- each row as the settings model resolves it ---

struct RowState {
    bool in_effect = true; // greyed when not
    bool forced = false;   // another setting sets its value (`shown` is the one in use)
    std::string reason;    // why it is greyed, forced or not what was asked ("" none)
    std::string conflict;  // what works against it ("" none)
    int shown = -1;        // the choice in use when it isn't the one asked (forced, or not possible here); -1: as asked
    std::vector<bool> possible; // one per choice: offered here (a texture set installed, 3x on an RTX 50)
};
// One per get_specs() entry.
std::vector<RowState> row_states(const config::Resolution& resolution);

// --- the graphics preset (the Graphics group's first row: Custom, Original, High, Ultra) ---

using PresetValue = config::PresetValue;
// The preset setting's index in get_specs().
int preset_option();
// What choosing the preset's `choice` writes ([Graphics] keys and values; none for Custom).
const std::vector<PresetValue>& preset_values(int choice);
// The preset the ini text's [Graphics] keys are (Custom, 0, when they are no preset's).
int preset_of(const std::string& text);

} // namespace frontend::new_frontend::options
