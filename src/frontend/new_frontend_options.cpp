// NOT ORIGINAL - the new front end's Options screen's settings and the ini text (ours): see new_frontend_options.h.

#include "frontend/new_frontend_options.h"
#include "frontend/new_frontend_text.h"
#include "engine/core/checked_index.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace frontend::new_frontend::options {

namespace {

std::string get_trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool get_same(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

// A number's value (false if the text isn't one).
bool get_number(const std::string& text, double* out)
{
    const std::string t = get_trim(text);
    if (t.empty())
        return false;
    char* end = nullptr;
    *out = std::strtod(t.c_str(), &end);
    return end && *end == '\0';
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
    std::string t = get_trim(line);
    if (t.size() < 2 || t.front() != '[')
        return false;
    const size_t close = t.find(']');
    if (close == std::string::npos)
        return false;
    *name = get_trim(t.substr(1, close - 1));
    return true;
}

// The line's key if it is a key=value line (not a comment), and where its '=' is.
bool get_key_of(const std::string& line, std::string* key, size_t* equals)
{
    std::string t = get_trim(line);
    if (t.empty() || core::at(t, 0) == ';' || core::at(t, 0) == '#' || core::at(t, 0) == '[')
        return false;
    const size_t eq = line.find('=');
    if (eq == std::string::npos)
        return false;
    *key = get_trim(line.substr(0, eq));
    *equals = eq;
    return !key->empty();
}

} // namespace

const std::vector<Spec>& get_specs()
{
    // (the words: new_frontend_text.h)
    static const std::vector<Spec> kSpecs = {
        // Graphics (engine/gfx/graphics_options.h)
        {.section = "Graphics",
         .key = "anisotropy",
         .label = text_tools::kOptAnisotropy,
         .help = text_tools::kOptAnisotropyHelp,
         .choices = {{.value = "0", .label = text_tools::kOff}, {.value = "2", .label = "2x"}, {.value = "4", .label = "4x"}, {.value = "8", .label = "8x"}, {.value = "16", .label = "16x"}},
         .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "Graphics",
         .key = "msaa",
         .label = text_tools::kOptMsaa,
         .help = text_tools::kOptMsaaHelp,
         .choices = {{.value = "0", .label = text_tools::kOff}, {.value = "2", .label = "2x"}, {.value = "4", .label = "4x"}, {.value = "8", .label = "8x"}},
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
        // Interface
        {.section = "features", .key = "new_frontend", .label = text_tools::kOptNewFrontend, .help = text_tools::kOptNewFrontendHelp, .choices = off_on(), .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "features", .key = "hd_ui", .label = text_tools::kOptHdUi, .help = text_tools::kOptHdUiHelp, .choices = off_on(), .fallback = 0, .apply = Apply::kRelaunch},
        {.section = "features", .key = "mouse_menus", .label = text_tools::kOptMouse, .help = text_tools::kOptMouseHelp, .choices = off_on(), .fallback = 1, .apply = Apply::kLive},
        {.section = "features", .key = "loading_console", .label = text_tools::kOptLoadingConsole, .help = text_tools::kOptLoadingConsoleHelp, .choices = off_on(), .fallback = 1,
         .apply = Apply::kRelaunch},
        // Gameplay
        {.section = "features", .key = "skip_intro_movies", .label = text_tools::kOptSkipIntro, .help = text_tools::kOptSkipIntroHelp, .choices = off_on(), .fallback = 0,
         .apply = Apply::kRelaunch},
        {.section = "features", .key = "restart_tester", .label = text_tools::kOptRestartTester, .help = text_tools::kOptRestartTesterHelp, .choices = off_on(), .fallback = 1,
         .apply = Apply::kLive},
        {.section = "features", .key = "level_editor", .label = text_tools::kOptLevelEditor, .help = text_tools::kOptLevelEditorHelp, .choices = off_on(), .fallback = 1,
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
         .choices = {{.value = "0", .label = text_tools::kOff}, {.value = "1", .label = text_tools::kOptThugproPartsHidden}, {.value = "2", .label = text_tools::kOptThugproPartsShown}},
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

const std::vector<GroupSet>& bunches()
{
    static const std::vector<GroupSet> kGroups = {
        {.id = "graphics", .name = text_tools::kGroupGraphics, .rows = {0, 1, 2, 3}},
        {.id = "interface", .name = text_tools::kGroupInterface, .rows = {4, 5, 6, 7}},
        {.id = "gameplay", .name = text_tools::kGroupGameplay, .rows = {8, 9, 10, 11, 12}},
        {.id = "online", .name = text_tools::kGroupOnline, .rows = {13, 14, 15}},
        {.id = "mods", .name = text_tools::kGroupMods, .rows = {16, 17, 18}},
        {.id = "sound", .name = text_tools::kGroupSound, .rows = {19}},
    };
    return kGroups;
}
int get_find(const std::string& section, const std::string& key)
{
    const std::vector<Spec>& specs = get_specs();
    for (size_t i = 0; i < specs.size(); ++i)
        if (get_same(core::at(specs, i).section, section) && get_same(core::at(specs, i).key, key))
            return static_cast<int>(i);
    return -1;
}

int choice_of(const Spec& spec, const std::string& value, bool present)
{
    if (!present)
        return spec.fallback;
    const std::string v = get_trim(value);
    double number = 0.0;
    const bool is_number = get_number(v, &number);
    for (size_t i = 0; i < spec.choices.size(); ++i) {
        const char* choice = core::at(spec.choices, i).value;
        double c = 0.0;
        if (is_number && get_number(choice, &c) ? number == c : get_same(v, choice))
            return static_cast<int>(i);
    }
    // (an empty value is the game's default; a number between the choices: the fallback as well)
    return spec.fallback;
}

bool ini_get(const std::string& text, const IniKey& at, std::string* value)
{
    const std::string& section = at.section;
    const std::string& key = at.key;
    bool in_section = false;
    for (const Seg& l : get_lines(text)) {
        const std::string line = text.substr(l.start, l.end - l.start);
        std::string name;
        if (section_of(line, &name)) {
            in_section = get_same(name, section);
            continue;
        }
        size_t eq = 0;
        if (in_section && get_key_of(line, &name, &eq) && get_same(name, key)) {
            if (value)
                *value = get_trim(line.substr(eq + 1));
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
    bool in_section = false, found = false;
    size_t insert_at = std::string::npos; // after the section's last line that isn't blank
    for (const Seg& l : lines) {
        const std::string line = text.substr(l.start, l.end - l.start);
        std::string name;
        if (section_of(line, &name)) {
            if (in_section)
                break; // (the section ended)
            in_section = get_same(name, section);
            if (in_section) {
                found = true;
                insert_at = l.next;
            }
            continue;
        }
        if (!in_section)
            continue;
        size_t eq = 0;
        if (get_key_of(line, &name, &eq) && get_same(name, key)) {
            // (the value replaced; whatever stood after the line - its break - kept)
            return text.substr(0, l.start + eq + 1) + value + text.substr(l.end);
        }
        if (!get_trim(line).empty())
            insert_at = l.next;
    }
    const std::string added = key + "=" + value;
    if (found) {
        std::string before = text.substr(0, insert_at);
        // (the line before has no break when it is the file's last)
        if (!before.empty() && before.back() != '\n')
            before += eol;
        return before + added + eol + text.substr(insert_at);
    }
    std::string out = text;
    if (!out.empty() && out.back() != '\n')
        out += eol;
    if (!out.empty())
        out += eol;
    return out + "[" + section + "]" + eol + added + eol;
}

} // namespace frontend::new_frontend::options
