#pragma once

#include <string>
#include <vector>

// NOT ORIGINAL - the new front end's Options screen (ours; see new_frontend.h): the settings of thug2-decomp.ini it
// offers (docs/design/frontend/Options.dc.html's groups and keys, plus new_frontend and thaw_bmx), and the ini text
// read and changed in place - pure (no files, no game), so the self-tests run them offline
// (src/hook/selftests_frontend.cpp).
namespace Front::NewFrontend::Options {

// When a change is seen.
enum class Apply {
    Live,    // at once (the feature's switch is set as the ini is written)
    Restart, // the next time the game starts (read once at start-up)
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
    int fallback;        // the choice when the key isn't there (the feature's own default)
    Apply apply;
};

struct Group {
    const char* id;    // "graphics", ... (automation)
    const char* name;  // "Graphics"
    std::vector<int> rows; // indices into Specs()
};

// Every option offered, and the groups they are shown in (Graphics, Interface, Gameplay, Online, Mods).
const std::vector<Spec>& Specs();
const std::vector<Group>& Groups();
// The option `section`/`key` (any case): its index, -1 if none.
int Find(const std::string& section, const std::string& key);

// The choice an ini value is (numbers compared as numbers: "1.50" is "1.5"); `present` false: the key isn't in the
// file - the fallback. A value matching no choice: the fallback too (the screen shows what the game uses for it ...
// as near as the choices go).
int ChoiceOf(const Spec& spec, const std::string& value, bool present);

// --- ini text (the file as GetPrivateProfileString reads it: [section] lines, key=value lines, ';' / '#' comment
// lines; names in any case, spaces around them ignored) ---

// The value of `key` in `section` (the first such line, as Windows reads it). False if there is none.
bool IniGet(const std::string& text, const std::string& section, const std::string& key, std::string* value);
// The text with `key`=`value` in `section`: the first such line's value replaced (its spacing before the '=' kept), or
// a line added after the section's last line that isn't blank (a missing section added at the end) - every other
// line, comment and unknown key as it was, the file's line ending kept (\r\n unless it only uses \n).
std::string IniSet(const std::string& text, const std::string& section, const std::string& key,
                   const std::string& value);

} // namespace Front::NewFrontend::Options
