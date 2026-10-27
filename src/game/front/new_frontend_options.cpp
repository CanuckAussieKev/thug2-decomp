// NOT ORIGINAL - the new front end's Options screen's settings and the ini text (ours): see new_frontend_options.h.

#include "game/front/new_frontend_options.h"
#include "game/front/new_frontend_text.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Front::NewFrontend::Options {

namespace {

std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool Same(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

// A number's value (false if the text isn't one).
bool Number(const std::string& text, double* out)
{
    std::string t = Trim(text);
    if (t.empty())
        return false;
    char* end = nullptr;
    *out = std::strtod(t.c_str(), &end);
    return end && *end == '\0';
}

const std::vector<Choice> kOffOn = {{"0", Text::kOff}, {"1", Text::kOn}};

// One line of the text: where it starts, ends (before its line break) and what it is.
struct Line {
    size_t start, end, next; // next: after its line break
};
std::vector<Line> Lines(const std::string& text)
{
    std::vector<Line> lines;
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
bool SectionOf(const std::string& line, std::string* name)
{
    std::string t = Trim(line);
    if (t.size() < 2 || t.front() != '[')
        return false;
    size_t close = t.find(']');
    if (close == std::string::npos)
        return false;
    *name = Trim(t.substr(1, close - 1));
    return true;
}

// The line's key if it is a key=value line (not a comment), and where its '=' is.
bool KeyOf(const std::string& line, std::string* key, size_t* equals)
{
    std::string t = Trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[')
        return false;
    size_t eq = line.find('=');
    if (eq == std::string::npos)
        return false;
    *key = Trim(line.substr(0, eq));
    *equals = eq;
    return !key->empty();
}

} // namespace

const std::vector<Spec>& Specs()
{
    // (the words: new_frontend_text.h)
    static const std::vector<Spec> specs = {
        // Graphics (game/gfx/graphics_options.h)
        {"Graphics", "anisotropy", Text::kOptAnisotropy, Text::kOptAnisotropyHelp,
         {{"0", Text::kOff}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}}, 0, Apply::Restart},
        {"Graphics", "msaa", Text::kOptMsaa, Text::kOptMsaaHelp, {{"0", Text::kOff}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}}, 0,
         Apply::Restart},
        {"Graphics", "draw_distance_scale", Text::kOptDrawDistance, Text::kOptDrawDistanceHelp,
         {{"1", "1x"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}}, 0, Apply::Restart},
        {"Graphics", "lod_distance_scale", Text::kOptLodDistance, Text::kOptLodDistanceHelp,
         {{"1", "1x"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}}, 0, Apply::Restart},
        // Interface
        {"features", "new_frontend", Text::kOptNewFrontend, Text::kOptNewFrontendHelp, kOffOn, 0, Apply::Restart},
        {"features", "hd_ui", Text::kOptHdUi, Text::kOptHdUiHelp, kOffOn, 0, Apply::Restart},
        {"features", "mouse_menus", Text::kOptMouse, Text::kOptMouseHelp, kOffOn, 1, Apply::Live},
        {"features", "loading_console", Text::kOptLoadingConsole, Text::kOptLoadingConsoleHelp, kOffOn, 1, Apply::Restart},
        // Gameplay
        {"features", "skip_intro_movies", Text::kOptSkipIntro, Text::kOptSkipIntroHelp, kOffOn, 0, Apply::Restart},
        {"features", "restart_tester", Text::kOptRestartTester, Text::kOptRestartTesterHelp, kOffOn, 1, Apply::Live},
        {"features", "level_editor", Text::kOptLevelEditor, Text::kOptLevelEditorHelp, kOffOn, 1, Apply::Live},
        {"features", "screenshots", Text::kOptScreenshots, Text::kOptScreenshotsHelp, kOffOn, 1, Apply::Restart},
        {"frontend", "respect_locks", Text::kOptLocks, Text::kOptLocksHelp,
         {{"0", Text::kOptLocksAll}, {"1", Text::kOptLocksUnlocked}}, 0, Apply::Live},
        // Online
        {"Miscellaneous", "OnlineGameName", Text::kOptIdentity, Text::kOptIdentityHelp,
         {{"", Text::kIdentityThug2}, {"thugpro", Text::kIdentityThugpro}}, 0, Apply::Restart},
        {"online", "thugpro_peer", Text::kOptThugproChat, Text::kOptThugproChatHelp, kOffOn, 0, Apply::Restart},
        {"features", "thugpro_parts", Text::kOptThugproParts, Text::kOptThugproPartsHelp,
         {{"0", Text::kOff}, {"1", Text::kOptThugproPartsHidden}, {"2", Text::kOptThugproPartsShown}}, 1, Apply::Restart},
        // Mods
        {"features", "anteara_mod", Text::kOptAnteara, Text::kOptAntearaHelp, kOffOn, 0, Apply::Restart},
        {"features", "thaw_bmx", Text::kOptBmx, Text::kOptBmxHelp, kOffOn, 0, Apply::Restart},
        {"features", "debug_mode", Text::kOptDebug, Text::kOptDebugHelp, kOffOn, 0, Apply::Restart},
    };
    return specs;
}

const std::vector<Group>& Groups()
{
    static const std::vector<Group> groups = {
        {"graphics", Text::kGroupGraphics, {0, 1, 2, 3}},  {"interface", Text::kGroupInterface, {4, 5, 6, 7}},
        {"gameplay", Text::kGroupGameplay, {8, 9, 10, 11, 12}}, {"online", Text::kGroupOnline, {13, 14, 15}},
        {"mods", Text::kGroupMods, {16, 17, 18}},
    };
    return groups;
}
int Find(const std::string& section, const std::string& key)
{
    const std::vector<Spec>& specs = Specs();
    for (size_t i = 0; i < specs.size(); ++i)
        if (Same(specs[i].section, section) && Same(specs[i].key, key))
            return static_cast<int>(i);
    return -1;
}

int ChoiceOf(const Spec& spec, const std::string& value, bool present)
{
    if (!present)
        return spec.fallback;
    std::string v = Trim(value);
    double number = 0.0;
    bool isNumber = Number(v, &number);
    for (size_t i = 0; i < spec.choices.size(); ++i) {
        const char* choice = spec.choices[i].value;
        double c = 0.0;
        if (isNumber && Number(choice, &c) ? number == c : Same(v, choice))
            return static_cast<int>(i);
    }
    // (an empty value is the game's default; a number between the choices: the fallback as well)
    return spec.fallback;
}

bool IniGet(const std::string& text, const std::string& section, const std::string& key, std::string* value)
{
    bool inSection = false;
    for (const Line& l : Lines(text)) {
        std::string line = text.substr(l.start, l.end - l.start);
        std::string name;
        if (SectionOf(line, &name)) {
            inSection = Same(name, section);
            continue;
        }
        size_t eq = 0;
        if (inSection && KeyOf(line, &name, &eq) && Same(name, key)) {
            if (value)
                *value = Trim(line.substr(eq + 1));
            return true;
        }
    }
    return false;
}

std::string IniSet(const std::string& text, const std::string& section, const std::string& key,
                   const std::string& value)
{
    std::string eol = text.find("\r\n") != std::string::npos || text.find('\n') == std::string::npos ? "\r\n" : "\n";
    std::vector<Line> lines = Lines(text);
    bool inSection = false, found = false;
    size_t insertAt = std::string::npos; // after the section's last line that isn't blank
    for (const Line& l : lines) {
        std::string line = text.substr(l.start, l.end - l.start);
        std::string name;
        if (SectionOf(line, &name)) {
            if (inSection)
                break; // (the section ended)
            inSection = Same(name, section);
            if (inSection) {
                found = true;
                insertAt = l.next;
            }
            continue;
        }
        if (!inSection)
            continue;
        size_t eq = 0;
        if (KeyOf(line, &name, &eq) && Same(name, key)) {
            // (the value replaced; whatever stood after the line - its break - kept)
            return text.substr(0, l.start + eq + 1) + value + text.substr(l.end);
        }
        if (!Trim(line).empty())
            insertAt = l.next;
    }
    std::string added = key + "=" + value;
    if (found) {
        std::string before = text.substr(0, insertAt);
        // (the line before has no break when it is the file's last)
        if (!before.empty() && before.back() != '\n')
            before += eol;
        return before + added + eol + text.substr(insertAt);
    }
    std::string out = text;
    if (!out.empty() && out.back() != '\n')
        out += eol;
    if (!out.empty())
        out += eol;
    return out + "[" + section + "]" + eol + added + eol;
}

} // namespace Front::NewFrontend::Options
