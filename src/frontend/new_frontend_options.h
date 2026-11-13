#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// NOT ORIGINAL - the new front end's Options screen (ours; see new_frontend.h): the settings of thug2-decomp.ini it
// offers (docs/design/frontend/Options.dc.html's groups and keys, plus new_frontend and thaw_bmx), and the ini text
// read and changed in place - pure (no files, no game).
namespace frontend::new_frontend::options {

// When a change is seen.
enum class Apply : uint8_t {
    kLive,     // at once (the feature's switch is set as the ini is written)
    kRelaunch, // the next time the game starts (read once at start-up)
    kPreset,   // not a key of its own: a choice sets several others (graphics_preset - preset_values)
};

struct Choice {
    const char* value; // as written to the ini ("" written as an empty value: the game's own default)
    const char* label; // the segmented control's text
};

struct Spec {
    const char* section; // the ini section ("features", "Graphics", ...)
    const char* key;
    const char* label;
    const char* help;
    std::vector<Choice> choices;
    int fallback; // the choice when the key isn't there (the feature's own default)
    Apply apply;
};

struct GroupSet {
    const char* id;        // "graphics", ... (automation)
    const char* name;      // "Graphics"
    std::vector<int> rows; // indices into get_specs() (named by key in the groups' table; resolved once)
};

// Every option offered, and the groups they are shown in (Graphics, Lighting, Surfaces, Sky, Colour, Upscaling,
// Interface, Gameplay, Online, Mods, Sound).
const std::vector<Spec>& get_specs();
const std::vector<GroupSet>& bunches();
// What is wrong with the tables, one line each ("" when nothing): a group's key that is no setting, a setting in no
// group or in two, a fallback that is no choice, two choices with one value, a preset's key or value that is no
// setting's choice. (bunches() asserts it is empty; the options check prints it.)
std::string check_tables();
// --- rows shown only while another setting is set a certain way (a toon knob under Art style: Toon) ---

// `key` is listed only while `parent` (another setting's key, an earlier row of the same group) has one of `values`
// (as written to the ini). The hidden row's own value is kept - and still written by a graphics preset.
struct ShownWhen {
    const char* key;
    const char* parent;
    std::vector<const char*> values;
};
const std::vector<ShownWhen>& shown_rules();
// Whether the row `option` is listed, given every setting's chosen index (`choices`, one per get_specs() entry - a
// missing or short list reads the fallbacks). A setting with no rule is always listed.
bool row_shown(int option, const std::vector<int>& choices);
// What is wrong with the rules, one line each: a key or parent that is no setting, a value that is no choice of the
// parent, a parent that isn't an earlier row of the child's group, a parent that is itself hidden by a rule.
// (check_tables includes it.)
std::string check_shown_rules();

// The option `section`/`key` (any case): its index, -1 if none.
int get_find(const std::string& section, const std::string& key);

// The choice an ini value is (numbers compared as numbers: "1.50" is "1.5"); `present` false: the key isn't in the
// file - the fallback. A value matching no choice: the fallback too (the screen shows what the game uses for it ...
// as near as the choices go).
int choice_of(const Spec& spec, const std::string& value, bool present);

// --- choices the game can't offer now (a texture set that isn't installed) ---

// Whether a setting's choice is offered: the filter the game installs (new_frontend.cpp - the texture sets installed);
// without one every choice is (as the options check sees them).
using ChoiceFilter = bool (*)(const Spec& spec, const char* value);
void set_choice_filter(ChoiceFilter filter);
[[nodiscard]] bool choice_shown(const Spec& spec, size_t choice);

// --- the graphics preset (the Graphics group's first row: Custom, Original, High, Ultra) ---

struct PresetValue {
    const char* key; // a [Graphics] key
    const char* value;
};
// The preset setting's index in get_specs().
int preset_option();
// What choosing the preset's `choice` writes ([Graphics] keys and values; none for Custom).
const std::vector<PresetValue>& preset_values(int choice);
// The preset the ini text's [Graphics] keys are (Custom, 0, when they are no preset's).
int preset_of(const std::string& text);

// --- ini text (the file as GetPrivateProfileString reads it: [section] lines, key=value lines, ';' / '#' comment
// lines; names in any case, spaces around them ignored) ---

// A setting's place in an ini file: its [section] and its key.
struct IniKey {
    std::string section;
    std::string key;
};

// The value of `at`'s key in its section (the first such line of the first such section, as Windows reads it). False if
// there is none.
bool ini_get(const std::string& text, const IniKey& at, std::string* value);
// The text with `at`'s key = `value` in its section: the first such line's value replaced (its spacing before the '=' kept), or
// a line added after the section's last line that isn't blank (a missing section added at the end) - every other
// line, comment and unknown key as it was, the file's line ending kept (\r\n unless it only uses \n).
std::string ini_set(const std::string& text, const IniKey& at, const std::string& value);

} // namespace frontend::new_frontend::options
