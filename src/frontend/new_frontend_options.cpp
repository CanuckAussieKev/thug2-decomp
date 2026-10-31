// NOT ORIGINAL - the new front end's Options screen's settings and the ini text (ours): see new_frontend_options.h.

#include "frontend/new_frontend_options.h"
#include "frontend/new_frontend_text.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace frontend::new_frontend::options {

namespace {

std::string get_trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool get_same(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

// A number's value (false if the text isn't one).
bool get_number(const std::string& text, double* out)
{
    std::string t = get_trim(text);
    if (t.empty())
        return false;
    char* end = nullptr;
    *out = std::strtod(t.c_str(), &end);
    return end && *end == '\0';
}

const std::vector<Choice> kOffOn = {{"0", text_tools::kOff}, {"1", text_tools::kOn}};

// One line of the text: where it starts, ends (before its line break) and what it is.
struct Seg {
    size_t start, end, next; // next: after its line break
};
std::vector<Seg> get_lines(const std::string& text)
{
    std::vector<Seg> lines;
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        size_t end = nl == std::string::npos ? text.size() : nl;
        size_t content = end > at && text[end - 1] == '\r' ? end - 1 : end;
        lines.push_back({at, content, nl == std::string::npos ? text.size() : nl + 1});
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
    size_t close = t.find(']');
    if (close == std::string::npos)
        return false;
    *name = get_trim(t.substr(1, close - 1));
    return true;
}

// The line's key if it is a key=value line (not a comment), and where its '=' is.
bool get_key_of(const std::string& line, std::string* key, size_t* equals)
{
    std::string t = get_trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[')
        return false;
    size_t eq = line.find('=');
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
        {"Graphics",
         "anisotropy",
         text_tools::kOptAnisotropy,
         text_tools::kOptAnisotropyHelp,
         {{"0", text_tools::kOff}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}},
         0,
         Apply::kRelaunch},
        {"Graphics",
         "msaa",
         text_tools::kOptMsaa,
         text_tools::kOptMsaaHelp,
         {{"0", text_tools::kOff}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}},
         0,
         Apply::kRelaunch},
        {"Graphics",
         "draw_distance_scale",
         text_tools::kOptDrawDistance,
         text_tools::kOptDrawDistanceHelp,
         {{"1", "1x"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}},
         0,
         Apply::kRelaunch},
        {"Graphics",
         "lod_distance_scale",
         text_tools::kOptLodDistance,
         text_tools::kOptLodDistanceHelp,
         {{"1", "1x"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}},
         0,
         Apply::kRelaunch},
        // Interface
        {"features", "new_frontend", text_tools::kOptNewFrontend, text_tools::kOptNewFrontendHelp, kOffOn, 0,
         Apply::kRelaunch},
        {"features", "hd_ui", text_tools::kOptHdUi, text_tools::kOptHdUiHelp, kOffOn, 0, Apply::kRelaunch},
        {"features", "mouse_menus", text_tools::kOptMouse, text_tools::kOptMouseHelp, kOffOn, 1, Apply::kLive},
        {"features", "loading_console", text_tools::kOptLoadingConsole, text_tools::kOptLoadingConsoleHelp, kOffOn, 1,
         Apply::kRelaunch},
        // Gameplay
        {"features", "skip_intro_movies", text_tools::kOptSkipIntro, text_tools::kOptSkipIntroHelp, kOffOn, 0,
         Apply::kRelaunch},
        {"features", "restart_tester", text_tools::kOptRestartTester, text_tools::kOptRestartTesterHelp, kOffOn, 1,
         Apply::kLive},
        {"features", "level_editor", text_tools::kOptLevelEditor, text_tools::kOptLevelEditorHelp, kOffOn, 1,
         Apply::kLive},
        {"features", "screenshots", text_tools::kOptScreenshots, text_tools::kOptScreenshotsHelp, kOffOn, 1,
         Apply::kRelaunch},
        {"frontend",
         "respect_locks",
         text_tools::kOptLocks,
         text_tools::kOptLocksHelp,
         {{"0", text_tools::kOptLocksAll}, {"1", text_tools::kOptLocksUnlocked}},
         0,
         Apply::kLive},
        // Online
        {"Miscellaneous",
         "OnlineGameName",
         text_tools::kOptIdentity,
         text_tools::kOptIdentityHelp,
         {{"", text_tools::kIdentityThug2}, {"thugpro", text_tools::kIdentityThugpro}},
         0,
         Apply::kRelaunch},
        {"online", "thugpro_peer", text_tools::kOptThugproChat, text_tools::kOptThugproChatHelp, kOffOn, 0,
         Apply::kRelaunch},
        {"features",
         "thugpro_parts",
         text_tools::kOptThugproParts,
         text_tools::kOptThugproPartsHelp,
         {{"0", text_tools::kOff}, {"1", text_tools::kOptThugproPartsHidden}, {"2", text_tools::kOptThugproPartsShown}},
         1,
         Apply::kRelaunch},
        // Mods
        {"features", "anteara_mod", text_tools::kOptAnteara, text_tools::kOptAntearaHelp, kOffOn, 0, Apply::kRelaunch},
        {"features", "thaw_bmx", text_tools::kOptBmx, text_tools::kOptBmxHelp, kOffOn, 0, Apply::kRelaunch},
        {"features", "debug_mode", text_tools::kOptDebug, text_tools::kOptDebugHelp, kOffOn, 0, Apply::kRelaunch},
    };
    return kSpecs;
}

const std::vector<GroupSet>& bunches()
{
    static const std::vector<GroupSet> kGroups = {
        {"graphics", text_tools::kGroupGraphics, {0, 1, 2, 3}},
        {"interface", text_tools::kGroupInterface, {4, 5, 6, 7}},
        {"gameplay", text_tools::kGroupGameplay, {8, 9, 10, 11, 12}},
        {"online", text_tools::kGroupOnline, {13, 14, 15}},
        {"mods", text_tools::kGroupMods, {16, 17, 18}},
    };
    return kGroups;
}
int get_find(const std::string& section, const std::string& key)
{
    const std::vector<Spec>& specs = get_specs();
    for (size_t i = 0; i < specs.size(); ++i)
        if (get_same(specs[i].section, section) && get_same(specs[i].key, key))
            return static_cast<int>(i);
    return -1;
}

int choice_of(const Spec& spec, const std::string& value, bool present)
{
    if (!present)
        return spec.fallback;
    std::string v = get_trim(value);
    double number = 0.0;
    bool is_number = get_number(v, &number);
    for (size_t i = 0; i < spec.choices.size(); ++i) {
        const char* choice = spec.choices[i].value;
        double c = 0.0;
        if (is_number && get_number(choice, &c) ? number == c : get_same(v, choice))
            return static_cast<int>(i);
    }
    // (an empty value is the game's default; a number between the choices: the fallback as well)
    return spec.fallback;
}

bool ini_get(const std::string& text, const std::string& section, const std::string& key, std::string* value)
{
    bool in_section = false;
    for (const Seg& l : get_lines(text)) {
        std::string line = text.substr(l.start, l.end - l.start);
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

std::string ini_set(const std::string& text, const std::string& section, const std::string& key,
                    const std::string& value)
{
    std::string eol = text.find("\r\n") != std::string::npos || text.find('\n') == std::string::npos ? "\r\n" : "\n";
    std::vector<Seg> lines = get_lines(text);
    bool in_section = false, found = false;
    size_t insert_at = std::string::npos; // after the section's last line that isn't blank
    for (const Seg& l : lines) {
        std::string line = text.substr(l.start, l.end - l.start);
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
    std::string added = key + "=" + value;
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
