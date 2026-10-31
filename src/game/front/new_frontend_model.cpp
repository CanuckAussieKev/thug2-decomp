// NOT ORIGINAL - the new front end's pure parts (ours): see new_frontend_model.h.

#include "game/front/new_frontend_model.h"
#include "game/front/new_frontend_options.h"
#include "game/front/new_frontend_text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

namespace Front::NewFrontend {

namespace {

// --- a small JSON reader (the data file only: objects, arrays, strings, numbers, true / false / null) ---

struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* Get(const char* key) const
    {
        for (const auto& [name, value] : members)
            if (name == key)
                return &value;
        return nullptr;
    }
    std::string Str(const char* key) const
    {
        const Json* v = Get(key);
        return v && v->kind == String ? v->text : std::string();
    }
};

class Reader {
public:
    explicit Reader(const std::string& text) : m_text(text) {}
    bool Parse(Json* out)
    {
        if (!Value(out, 0))
            return false;
        Space();
        return m_at == m_text.size() || Fail("text after the value");
    }
    const std::string& Error() const { return m_error; }

private:
    bool Fail(const char* why)
    {
        if (m_error.empty())
            m_error = std::string(why) + " at character " + std::to_string(m_at);
        return false;
    }
    void Space()
    {
        while (m_at < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_at])))
            ++m_at;
    }
    bool Literal(const char* word)
    {
        size_t n = std::strlen(word);
        if (m_text.compare(m_at, n, word) != 0)
            return false;
        m_at += n;
        return true;
    }
    static void AppendUtf8(std::string* out, unsigned code)
    {
        if (code < 0x80) {
            out->push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out->push_back(static_cast<char>(0xC0 | (code >> 6)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out->push_back(static_cast<char>(0xE0 | (code >> 12)));
            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    bool StringValue(std::string* out)
    {
        ++m_at; // (the opening quote)
        while (m_at < m_text.size()) {
            char c = m_text[m_at++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (m_at >= m_text.size())
                break;
            char e = m_text[m_at++];
            switch (e) {
            case '"': case '\\': case '/': out->push_back(e); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            case 'u': {
                if (m_at + 4 > m_text.size())
                    return Fail("a short \\u escape");
                unsigned code = static_cast<unsigned>(std::strtoul(m_text.substr(m_at, 4).c_str(), nullptr, 16));
                m_at += 4;
                AppendUtf8(out, code);
                break;
            }
            default: return Fail("an unknown escape");
            }
        }
        return Fail("a string that doesn't end");
    }
    bool Value(Json* out, int depth)
    {
        if (depth > 32)
            return Fail("nesting too deep");
        Space();
        if (m_at >= m_text.size())
            return Fail("the end of the text where a value should be");
        char c = m_text[m_at];
        if (c == '{') {
            out->kind = Json::Object;
            ++m_at;
            Space();
            if (m_at < m_text.size() && m_text[m_at] == '}') {
                ++m_at;
                return true;
            }
            for (;;) {
                Space();
                if (m_at >= m_text.size() || m_text[m_at] != '"')
                    return Fail("an object's key isn't a string");
                std::string key;
                if (!StringValue(&key))
                    return false;
                Space();
                if (m_at >= m_text.size() || m_text[m_at] != ':')
                    return Fail("no ':' after a key");
                ++m_at;
                Json value;
                if (!Value(&value, depth + 1))
                    return false;
                out->members.emplace_back(std::move(key), std::move(value));
                Space();
                if (m_at < m_text.size() && m_text[m_at] == ',') {
                    ++m_at;
                    continue;
                }
                if (m_at < m_text.size() && m_text[m_at] == '}') {
                    ++m_at;
                    return true;
                }
                return Fail("an object not closed");
            }
        }
        if (c == '[') {
            out->kind = Json::Array;
            ++m_at;
            Space();
            if (m_at < m_text.size() && m_text[m_at] == ']') {
                ++m_at;
                return true;
            }
            for (;;) {
                Json value;
                if (!Value(&value, depth + 1))
                    return false;
                out->items.push_back(std::move(value));
                Space();
                if (m_at < m_text.size() && m_text[m_at] == ',') {
                    ++m_at;
                    continue;
                }
                if (m_at < m_text.size() && m_text[m_at] == ']') {
                    ++m_at;
                    return true;
                }
                return Fail("an array not closed");
            }
        }
        if (c == '"') {
            out->kind = Json::String;
            return StringValue(&out->text);
        }
        if (Literal("true")) {
            out->kind = Json::Bool;
            out->boolean = true;
            return true;
        }
        if (Literal("false")) {
            out->kind = Json::Bool;
            return true;
        }
        if (Literal("null"))
            return true;
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            const char* start = m_text.c_str() + m_at;
            char* end = nullptr;
            out->number = std::strtod(start, &end);
            if (end == start)
                return Fail("a bad number");
            out->kind = Json::Number;
            m_at += static_cast<size_t>(end - start);
            return true;
        }
        return Fail("an unexpected character");
    }

    const std::string& m_text;
    size_t m_at = 0;
    std::string m_error;
};

bool SameText(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

// --- the data ---------------------------------------------------------------------------------------------------

std::string Catalogue::MenuArt(const std::string& id) const
{
    for (const auto& [key, image] : menuArt)
        if (key == id)
            return image;
    return {};
}

bool Catalogue::Find(const std::string& load, int* section, int* level) const
{
    for (size_t s = 0; s < sections.size(); ++s)
        for (size_t l = 0; l < sections[s].levels.size(); ++l)
            if (SameText(sections[s].levels[l].load, load)) {
                if (section)
                    *section = static_cast<int>(s);
                if (level)
                    *level = static_cast<int>(l);
                return true;
            }
    return false;
}

uint32_t ParseColour(const std::string& text, uint32_t fallback)
{
    if (text.size() != 7 || text[0] != '#')
        return fallback;
    for (size_t i = 1; i < 7; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(text[i])))
            return fallback;
    return 0xFF000000u | static_cast<uint32_t>(std::strtoul(text.c_str() + 1, nullptr, 16));
}

bool ParseCatalogue(const std::string& json, Catalogue* out, std::string* error)
{
    auto fail = [error](const std::string& why) {
        if (error)
            *error = why;
        return false;
    };
    Json root;
    Reader reader(json);
    if (!reader.Parse(&root))
        return fail(reader.Error());
    if (root.kind != Json::Object)
        return fail("not an object");
    const Json* version = root.Get("version");
    if (!version || version->kind != Json::Number || version->number != 1.0)
        return fail("not version 1");
    Catalogue result;
    if (const Json* art = root.Get("menu_art"); art && art->kind == Json::Object)
        for (const auto& [key, value] : art->members)
            if (value.kind == Json::String)
                result.menuArt.emplace_back(key, value.text);
    const Json* sections = root.Get("sections");
    if (!sections || sections->kind != Json::Array)
        return fail("no sections");
    for (const Json& s : sections->items) {
        if (s.kind != Json::Object)
            return fail("a section isn't an object");
        Section section;
        section.id = s.Str("id");
        section.tab = s.Str("tab");
        section.title = s.Str("title");
        section.colour = ParseColour(s.Str("colour"), 0xFFC9CBD0);
        if (section.tab.empty())
            section.tab = section.id;
        const Json* levels = s.Get("levels");
        if (!levels || levels->kind != Json::Array)
            return fail("section " + section.id + " has no levels");
        for (const Json& l : levels->items) {
            if (l.kind != Json::Object)
                return fail("a level isn't an object");
            Level level;
            level.name = l.Str("name");
            level.load = l.Str("load");
            level.art = l.Str("art");
            if (const Json* variants = l.Get("art_variants"); variants && variants->kind == Json::Array)
                for (const Json& v : variants->items)
                    if (v.kind == Json::String && !v.text.empty())
                        level.artVariants.push_back(v.text);
            level.flag = l.Str("flag");
            level.seenFlag = l.Str("seen_flag");
            level.gameTag = l.Str("game_tag");
            if (const Json* num = l.Get("level_num")) {
                if (num->kind == Json::Number)
                    level.levelNum = static_cast<int>(num->number);
                else if (num->kind == Json::String)
                    level.levelNumName = num->text;
            }
            const Json* hide = l.Get("hide_when_locked");
            level.hideWhenLocked = hide && hide->kind == Json::Bool && hide->boolean;
            const Json* generic = l.Get("generic_art");
            level.genericArt = generic && generic->kind == Json::Bool && generic->boolean;
            if (level.name.empty() || level.load.empty())
                return fail("a level of " + section.id + " has no name or load script");
            section.levels.push_back(std::move(level));
        }
        result.sections.push_back(std::move(section));
    }
    *out = std::move(result);
    return true;
}

bool Catalogue::CycleArt(const std::string& load, int step)
{
    int s = 0, l = 0;
    if (!Find(load, &s, &l))
        return false;
    Level& level = sections[s].levels[l];
    int count = static_cast<int>(level.artVariants.size());
    if (count < 2)
        return false;
    level.variant = ((level.variant + step) % count + count) % count;
    return true;
}

void Catalogue::ApplyArtChoices(const std::vector<std::pair<std::string, int>>& choices)
{
    for (const auto& [load, variant] : choices) {
        int s = 0, l = 0;
        if (!Find(load, &s, &l))
            continue;
        Level& level = sections[s].levels[l];
        if (variant >= 0 && variant < static_cast<int>(level.artVariants.size()))
            level.variant = variant;
    }
}

std::vector<std::pair<std::string, int>> Catalogue::ArtChoices() const
{
    std::vector<std::pair<std::string, int>> out;
    for (const Section& s : sections)
        for (const Level& l : s.levels)
            if (l.variant > 0)
                out.emplace_back(l.load, l.variant);
    return out;
}

std::vector<std::pair<std::string, int>> ParseArtChoices(const std::string& text)
{
    std::vector<std::pair<std::string, int>> out;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        std::string line = Trim(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? text.size() + 1 : end + 1;
        if (line.empty() || line[0] == '#')
            continue;
        size_t gap = line.find_first_of(" \t");
        if (gap == std::string::npos)
            continue;
        std::string load = line.substr(0, gap), number = Trim(line.substr(gap));
        if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos || number.size() > 3)
            continue;
        out.emplace_back(load, std::atoi(number.c_str()));
    }
    return out;
}

std::string FormatArtChoices(const std::vector<std::pair<std::string, int>>& choices)
{
    std::string text = "# The level select's picture per level: <load script> <picture number, from 0>\n";
    for (const auto& [load, variant] : choices)
        text += load + " " + std::to_string(variant) + "\n";
    return text;
}

// --- the state file ---------------------------------------------------------------------------------------------

SavedState ParseState(const std::string& text)
{
    SavedState state;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        std::string line = Trim(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? text.size() + 1 : end + 1;
        if (line.empty() || line[0] == '#')
            continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        if (key == "last_level")
            state.lastLevel = value;
    }
    return state;
}

std::string FormatState(const SavedState& state)
{
    return "# The new front end's own state (thug2-decomp.ini [features] new_frontend - src/game/front/new_frontend.h)\n"
           "last_level=" + state.lastLevel + "\n";
}

// --- the frame --------------------------------------------------------------------------------------------------

Frame FitFrame(float windowWidth, float windowHeight)
{
    Frame frame;
    if (windowWidth <= 0.0f || windowHeight <= 0.0f)
        return frame;
    frame.scale = std::min(windowWidth / kDesignWidth, windowHeight / kDesignHeight);
    frame.left = std::floor((windowWidth - kDesignWidth * frame.scale) * 0.5f);
    frame.top = std::floor((windowHeight - kDesignHeight * frame.scale) * 0.5f);
    return frame;
}

Uv CoverUv(float imageWidth, float imageHeight, float boxWidth, float boxHeight, float pan, float panRange)
{
    Uv uv = {0.0f, 0.0f, 1.0f, 1.0f};
    if (imageWidth <= 0.0f || imageHeight <= 0.0f || boxWidth <= 0.0f || boxHeight <= 0.0f)
        return uv;
    // The fraction of the picture shown each way: the box's aspect against the picture's, then the zoom for the pan.
    float imageAspect = imageWidth / imageHeight, boxAspect = boxWidth / boxHeight;
    float shownU = 1.0f, shownV = 1.0f;
    if (boxAspect < imageAspect)
        shownU = boxAspect / imageAspect;
    else
        shownV = imageAspect / boxAspect;
    float zoom = 1.0f + std::max(0.0f, panRange);
    shownU /= zoom;
    shownV /= zoom;
    // Centred, then slid along u by the pan across what is left over (the larger slack when there is none along u).
    float slackU = 1.0f - shownU, slackV = 1.0f - shownV;
    float p = std::clamp(pan, 0.0f, 1.0f);
    uv.u0 = slackU * p;
    uv.v0 = slackV * 0.5f;
    uv.u1 = uv.u0 + shownU;
    uv.v1 = uv.v0 + shownV;
    return uv;
}

// --- motion -----------------------------------------------------------------------------------------------------

float EaseOut(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    float r = 1.0f - t;
    return 1.0f - r * r * r;
}

void RowAnim::Tick(bool selected, float dt)
{
    float step = dt / kSelectSeconds;
    value = selected ? std::min(1.0f, value + step) : std::max(0.0f, value - step);
}


// --- the menus ----------------------------------------------------------------------------------------------------

namespace {

constexpr const char* kScreenNames[kScreens] = {"main", "options", "levels", "pause"};

} // namespace

const char* ScreenName(Screen screen)
{
    int i = static_cast<int>(screen);
    return i >= 0 && i < kScreens ? kScreenNames[i] : "";
}

bool ScreenFromName(const std::string& name, Screen* out)
{
    for (int i = 0; i < kScreens; ++i)
        if (_stricmp(name.c_str(), kScreenNames[i]) == 0) {
            if (out)
                *out = static_cast<Screen>(i);
            return true;
        }
    return false;
}

bool IsPauseScreen(Screen screen) { return screen == Screen::Pause; }

std::string DefaultArt(const std::string& id)
{
    // (every main menu item has its own - Quit too: the user's report of 2026-10-25, the art and name must follow
    // the highlighted item; tools/pipeline/menu_level/frontend_levels.ts MENU_ART has the main menu's the same. THUG2's advert
    // slides only where they say what the item is: generic_6 Create-A-Goal, generic_3 Create-A-Graphic.)
    static const std::pair<const char*, const char*> kArt[] = {
        {"career", "loadscrn_boston_story"}, {"thug2story", "loadscrn_barcelona_story"}, {"story", "loadscrn_boston_story"},
        {"classic", "loadscrn_boston_classic"}, {"thugstory", "loadscrn_secret_1"}, {"freeskate", "loadscrn_australia"},
        {"online", "loadscrn_Online"},     {"play", "loadscrn_Online"},          {"identity", "loadscrn_Online2"},
        {"create", "loadscrn_created"},    {"cas", "loadscrn_created"},          {"cat", "loadscrn_trick"},
        {"cag", "loadscrn_generic_6"},     {"cap", "loadscrn_editor"},           {"cagr", "loadscrn_generic_3"},
        {"editor", "loadscrn_editor_play"}, {"options", "loadscrn_generic"},     {"quit", "loadscrn_los_angeles"},
    };
    for (const auto& [key, image] : kArt)
        if (id == key)
            return image;
    return {};
}

void Menu::Reset(const Catalogue* catalogue, std::vector<SectionView> views, const std::string& lastLevel,
                 std::vector<Screen> stack, LevelsFor levels)
{
    m_catalogue = catalogue;
    m_views = std::move(views);
    m_rowInSection.assign(m_views.size(), 0);
    m_lastLevel = lastLevel;
    m_stack = stack.empty() ? std::vector<Screen>{Screen::Main} : std::move(stack);
    m_levelsFor = levels;
    m_section = 0;
    m_scroll = m_scrollTarget = 0.0f;
    m_wipe = m_launch = -1.0f;
    m_switched = false;
    m_launchLoad.clear();
    // The level select opens at the last level played when it's listed.
    int section = 0, level = 0;
    if (m_catalogue && !m_lastLevel.empty() && m_catalogue->Find(m_lastLevel, &section, &level) &&
        section < static_cast<int>(m_views.size())) {
        const std::vector<int>& rows = m_views[section].levels;
        auto it = std::find(rows.begin(), rows.end(), level);
        if (it != rows.end()) {
            m_section = section;
            m_rowInSection[section] = static_cast<int>(it - rows.begin());
        }
    }
    Show(m_stack.back());
}

void Menu::SetViews(std::vector<SectionView> views)
{
    m_views = std::move(views);
    m_rowInSection.resize(m_views.size(), 0);
    if (m_section >= static_cast<int>(m_views.size()))
        m_section = 0;
    if (m_shown == Screen::Levels) {
        ResetRows();
        ClampScroll(true);
    }
}

void Menu::SetOptionGroup(int group)
{
    int count = static_cast<int>(Options::Groups().size());
    if (count > 0)
        m_group = ((group % count) + count) % count;
    m_optionRow = 0;
}

int Menu::Selected() const
{
    if (m_shown == Screen::Levels)
        return LevelRow();
    if (m_shown == Screen::Options)
        return m_optionRow;
    return -1; // (the roots: never shown)
}

bool Menu::OverPause() const { return !m_stack.empty() && m_stack.front() == Screen::Pause; }

const std::vector<int>& Menu::OptionRows() const
{
    static const std::vector<int> none;
    const std::vector<Options::Group>& groups = Options::Groups();
    return m_group >= 0 && m_group < static_cast<int>(groups.size()) ? groups[m_group].rows : none;
}

int Menu::OptionChoice(int option) const
{
    const std::vector<Options::Spec>& specs = Options::Specs();
    if (option < 0 || option >= static_cast<int>(specs.size()))
        return 0;
    int n = static_cast<int>(specs[option].choices.size());
    int value = option < static_cast<int>(m_options.size()) ? m_options[option] : specs[option].fallback;
    return std::clamp(value, 0, std::max(0, n - 1));
}

int Menu::ListCount() const
{
    if (m_shown == Screen::Levels)
        return View() ? static_cast<int>(View()->levels.size()) : 0;
    if (m_shown == Screen::Options)
        return static_cast<int>(OptionRows().size()) + 1; // (and the Game options row)
    return 0;
}

const SectionView* Menu::View() const
{
    return m_section >= 0 && m_section < static_cast<int>(m_views.size()) ? &m_views[m_section] : nullptr;
}

int Menu::LevelRow() const
{
    const SectionView* view = View();
    if (!view || view->levels.empty())
        return -1;
    return std::clamp(m_rowInSection[m_section], 0, static_cast<int>(view->levels.size()) - 1);
}

const Level* Menu::SelectedLevel() const
{
    int row = LevelRow();
    if (row < 0 || !m_catalogue || m_section >= static_cast<int>(m_catalogue->sections.size()))
        return nullptr;
    int index = View()->levels[row];
    const std::vector<Level>& levels = m_catalogue->sections[m_section].levels;
    return index >= 0 && index < static_cast<int>(levels.size()) ? &levels[index] : nullptr;
}

bool Menu::SelectedLocked() const
{
    int row = LevelRow();
    return row >= 0 && row < static_cast<int>(View()->locked.size()) && View()->locked[row];
}

float Menu::RowAmount(int row) const
{
    return row >= 0 && row < static_cast<int>(m_rows.size()) ? m_rows[row].Eased() : 0.0f;
}

float Menu::Pan() const
{
    float phase = std::fmod(m_time, kPanSeconds * 2.0f) / kPanSeconds; // 0..2
    float linear = phase <= 1.0f ? phase : 2.0f - phase;
    return 0.5f - 0.5f * std::cos(linear * 3.14159265f); // (eased at both ends)
}

uint32_t Menu::AccentColour() const
{
    if (m_shown == Screen::Levels && m_catalogue && m_section < static_cast<int>(m_catalogue->sections.size()))
        return m_catalogue->sections[m_section].colour;
    return kHouseYellow;
}

void Menu::ResetRows()
{
    int count = ListCount();
    m_rows.assign(static_cast<size_t>(std::max(0, count)), RowAnim{});
    int selected = Selected();
    if (selected >= 0 && selected < count)
        m_rows[selected].value = 1.0f; // (a new list: its selection drawn at once)
}

void Menu::Show(Screen screen)
{
    m_shown = screen;
    ResetRows();
    if (m_shown == Screen::Levels)
        ClampScroll(true);
}

void Menu::Go(Screen screen)
{
    m_stack.push_back(screen);
    uint32_t colour = kHouseYellow;
    if (screen == Screen::Levels && m_catalogue && m_section < static_cast<int>(m_catalogue->sections.size()))
        colour = m_catalogue->sections[m_section].colour;
    m_wipe = 0.0f;
    m_switched = false;
    m_wipeColour = colour;
}

void Menu::Pop()
{
    if (m_stack.size() > 1)
        m_stack.pop_back();
    m_wipe = 0.0f;
    m_switched = false;
    m_wipeColour = kHouseYellow;
}

void Menu::Open(Screen screen, LevelsFor levels)
{
    if (screen == Screen::Levels)
        m_levelsFor = levels;
    Go(screen);
}

Action Menu::MoveTo(int index)
{
    if (m_shown == Screen::Levels)
        return MoveLevel(index);
    int count = ListCount();
    if (count <= 0)
        return {};
    index = std::clamp(index, 0, count - 1);
    if (m_shown == Screen::Options) {
        if (m_optionRow == index)
            return {};
        m_optionRow = index;
        return {ActionKind::MoveSound};
    }
    return {};
}

Action Menu::MoveBy(int step)
{
    if (m_shown == Screen::Levels)
        return MoveLevel(LevelRow() + step);
    if (m_shown == Screen::Options)
        return MoveTo(m_optionRow + step);
    return {};
}

Action Menu::MoveLevel(int row)
{
    const SectionView* view = View();
    if (!view || view->levels.empty())
        return {};
    row = std::clamp(row, 0, static_cast<int>(view->levels.size()) - 1);
    if (m_rowInSection[m_section] == row)
        return {};
    m_rowInSection[m_section] = row;
    ClampScroll(false);
    return {ActionKind::MoveSound};
}

Action Menu::SetSection(int section)
{
    int count = static_cast<int>(m_views.size());
    if (count == 0)
        return {};
    section = ((section % count) + count) % count;
    if (section == m_section)
        return {};
    m_section = section;
    ResetRows();
    ClampScroll(true);
    return {ActionKind::MoveSound};
}

Action Menu::ChangeOption(int step, bool wrap)
{
    const std::vector<int>& rows = OptionRows();
    if (m_optionRow < 0 || m_optionRow >= static_cast<int>(rows.size()))
        return {};
    int option = rows[m_optionRow];
    int n = static_cast<int>(Options::Specs()[option].choices.size());
    if (n <= 1)
        return {};
    int current = OptionChoice(option);
    int next = wrap ? ((current + step) % n + n) % n : std::clamp(current + step, 0, n - 1);
    if (next == current)
        return {};
    if (m_options.size() < Options::Specs().size()) {
        size_t old = m_options.size();
        m_options.resize(Options::Specs().size());
        for (size_t i = old; i < m_options.size(); ++i)
            m_options[i] = Options::Specs()[i].fallback;
    }
    m_options[option] = next;
    Action a{ActionKind::Option};
    a.option = option;
    a.value = next;
    return a;
}

void Menu::ClampScroll(bool snap)
{
    int row = LevelRow();
    int count = View() ? static_cast<int>(View()->levels.size()) : 0;
    float maxTop = static_cast<float>(std::max(0, count - m_visibleRows));
    if (row >= 0) {
        // (a row of margin kept above and below the selection while the list goes on)
        float top = m_scrollTarget;
        if (row - 1 < top)
            top = static_cast<float>(row - 1);
        if (row + 2 > top + m_visibleRows)
            top = static_cast<float>(row + 2 - m_visibleRows);
        m_scrollTarget = std::clamp(top, 0.0f, maxTop);
    } else {
        m_scrollTarget = 0.0f;
    }
    if (snap)
        m_scroll = m_scrollTarget;
}

Action Menu::StartLaunch(const std::string& load)
{
    if (load.empty())
        return {};
    m_launch = 0.0f;
    m_launchLoad = load;
    m_launchFired = false;
    return {ActionKind::ChooseSound};
}

Action Menu::Confirm()
{
    if (m_shown == Screen::Levels) {
        const Level* level = SelectedLevel();
        if (!level || SelectedLocked())
            return {};
        return StartLaunch(level->load);
    }
    if (m_shown == Screen::Options) {
        if (m_optionRow >= static_cast<int>(OptionRows().size()))
            return {ActionKind::GameOptions};
        return ChangeOption(1, true);
    }
    return {};
}

Action Menu::Back()
{
    if (m_stack.size() <= 1)
        return {};
    Pop();
    return {ActionKind::BackSound};
}

Action Menu::Press(Input input)
{
    if (Busy())
        return {}; // (a wipe or a launch under way: the input waits for neither)
    bool levels = m_shown == Screen::Levels, options = m_shown == Screen::Options;
    switch (input) {
    case Input::Up: return MoveBy(-1);
    case Input::Down: return MoveBy(1);
    case Input::Left:
        return levels ? SetSection(m_section - 1) : options ? ChangeOption(-1, false) : Action{};
    case Input::Right:
        return levels ? SetSection(m_section + 1) : options ? ChangeOption(1, false) : Action{};
    case Input::PrevTab:
        if (options) {
            SetOptionGroup(m_group - 1);
            ResetRows();
            return {ActionKind::MoveSound};
        }
        return levels ? SetSection(m_section - 1) : Action{};
    case Input::NextTab:
        if (options) {
            SetOptionGroup(m_group + 1);
            ResetRows();
            return {ActionKind::MoveSound};
        }
        return levels ? SetSection(m_section + 1) : Action{};
    case Input::Confirm: return Confirm();
    case Input::Back: return Back();
    }
    return {};
}

Action Menu::HoverItem(int index)
{
    if (Busy() || m_shown == Screen::Levels)
        return {};
    return MoveTo(index);
}

Action Menu::ClickItem(int index)
{
    if (Busy() || m_shown == Screen::Levels || index < 0 || index >= ListCount())
        return {};
    if (m_shown == Screen::Options) {
        MoveTo(index);
        return index >= static_cast<int>(OptionRows().size()) ? Confirm() : Action{};
    }
    return {};
}

Action Menu::HoverLevel(int row)
{
    if (Busy() || m_shown != Screen::Levels)
        return {};
    return MoveLevel(row);
}

Action Menu::ClickLevel(int row)
{
    if (Busy() || m_shown != Screen::Levels || !View() || row < 0 || row >= static_cast<int>(View()->levels.size()))
        return {};
    MoveLevel(row);
    return Confirm();
}

Action Menu::ClickTab(int section)
{
    if (Busy())
        return {};
    if (m_shown == Screen::Options) {
        if (section == m_group)
            return {};
        SetOptionGroup(section);
        ResetRows();
        return {ActionKind::MoveSound};
    }
    if (m_shown != Screen::Levels)
        return {};
    return SetSection(section);
}

Action Menu::ClickBack()
{
    if (Busy() || (m_stack.size() <= 1 && m_shown == Screen::Main))
        return {};
    return Press(Input::Back);
}

Action Menu::ClickSkate()
{
    if (Busy() || m_shown != Screen::Levels)
        return {};
    return Confirm();
}

Action Menu::ClickChoice(int row, int choice)
{
    if (Busy() || m_shown != Screen::Options || row < 0 || row >= static_cast<int>(OptionRows().size()))
        return {};
    m_optionRow = row;
    int current = OptionChoice(OptionRows()[row]);
    return choice == current ? Action{} : ChangeOption(choice - current, false);
}

Action Menu::Wheel(int notches)
{
    Action last;
    for (; notches > 0; --notches)
        if (Action a = Press(Input::Up); a.kind != ActionKind::None)
            last = a;
    for (; notches < 0; ++notches)
        if (Action a = Press(Input::Down); a.kind != ActionKind::None)
            last = a;
    return last;
}

bool Menu::SelectId(const std::string& id)
{
    if (Busy())
        return false;
    if (m_shown == Screen::Levels) {
        if (id.rfind("tab:", 0) == 0) {
            for (size_t s = 0; m_catalogue && s < m_catalogue->sections.size() && s < m_views.size(); ++s)
                if (SameText(m_catalogue->sections[s].id, id.substr(4)) || SameText(m_catalogue->sections[s].tab, id.substr(4))) {
                    SetSection(static_cast<int>(s));
                    return true;
                }
            return false;
        }
        for (size_t s = 0; m_catalogue && s < m_catalogue->sections.size() && s < m_views.size(); ++s) {
            // (the section shown first: a name in two games - Airport - picks the one there)
            size_t section = (s + static_cast<size_t>(m_section)) % m_views.size();
            const std::vector<int>& rows = m_views[section].levels;
            for (size_t r = 0; r < rows.size(); ++r) {
                const Level& level = m_catalogue->sections[section].levels[rows[r]];
                if (SameText(level.load, id) || SameText(level.name, id)) {
                    SetSection(static_cast<int>(section));
                    MoveLevel(static_cast<int>(r));
                    return true;
                }
            }
        }
        return false;
    }
    if (m_shown == Screen::Options) {
        const std::vector<Options::Group>& groups = Options::Groups();
        if (id.rfind("group:", 0) == 0) {
            for (size_t g = 0; g < groups.size(); ++g)
                if (SameText(groups[g].id, id.substr(6))) {
                    SetOptionGroup(static_cast<int>(g));
                    ResetRows();
                    return true;
                }
            return false;
        }
        if (SameText(id, "game_options")) {
            m_optionRow = static_cast<int>(OptionRows().size());
            return true;
        }
        size_t dot = id.find('.');
        for (size_t g = 0; g < groups.size(); ++g)
            for (size_t r = 0; r < groups[g].rows.size(); ++r) {
                const Options::Spec& spec = Options::Specs()[groups[g].rows[r]];
                bool match = dot == std::string::npos
                                 ? SameText(spec.key, id)
                                 : SameText(spec.section, id.substr(0, dot)) && SameText(spec.key, id.substr(dot + 1));
                if (match) {
                    if (static_cast<int>(g) != m_group) {
                        SetOptionGroup(static_cast<int>(g));
                        ResetRows();
                    }
                    m_optionRow = static_cast<int>(r);
                    return true;
                }
            }
        return false;
    }
    return false;
}

std::string Menu::SelectedId() const
{
    if (m_shown == Screen::Levels) {
        const Level* level = SelectedLevel();
        return level ? level->load : std::string();
    }
    if (m_shown == Screen::Options) {
        const std::vector<int>& rows = OptionRows();
        if (m_optionRow >= static_cast<int>(rows.size()))
            return "game_options";
        const Options::Spec& spec = Options::Specs()[rows[m_optionRow]];
        return std::string(spec.section) + "." + spec.key;
    }
    return {};
}

Action Menu::Tick(float dt)
{
    dt = std::clamp(dt, 0.0f, 0.1f); // (a long frame - a load, a hitch - doesn't skip the motion)
    m_time += dt;
    if (m_wipe >= 0.0f) {
        m_wipe += dt;
        if (!m_switched && m_wipe >= kWipeSeconds * 0.5f) {
            // Half-way: the screen changes under the wipe; its list's rows start as they are (the selection drawn).
            m_switched = true;
            Show(m_stack.back());
        }
        if (m_wipe >= kWipeSeconds)
            m_wipe = -1.0f;
    }
    int selected = Selected();
    for (size_t i = 0; i < m_rows.size(); ++i)
        m_rows[i].Tick(static_cast<int>(i) == selected, dt);
    // The list's scroll eases toward its target (about a tenth of a second).
    float k = 1.0f - std::exp(-dt * 18.0f);
    m_scroll += (m_scrollTarget - m_scroll) * k;
    if (std::fabs(m_scrollTarget - m_scroll) < 0.001f)
        m_scroll = m_scrollTarget;
    if (m_launch >= 0.0f) {
        m_launch += dt;
        if (m_launch >= kLaunchSeconds) {
            // (the art stays up - Launch() 1 - until the game takes over: the glue resets the menu)
            m_launch = kLaunchSeconds;
            if (!m_launchFired) {
                m_launchFired = true;
                Action a{ActionKind::Skate};
                a.load = m_launchLoad;
                a.levels = m_shown == Screen::Main ? LevelsFor::FreeSkate : m_levelsFor;
                return a;
            }
        }
    }
    return {};
}

// --- covering a hand-off ------------------------------------------------------------------------------------------

void Cover::Start(Over over)
{
    m_over = over;
    m_time = m_still = 0.0f;
    m_screen = 0;
    m_seen = false;
}

bool Cover::Update(const Now& now, float dt, const char** why)
{
    auto end = [this, why](const char* reason) {
        m_over = Over::None;
        if (why)
            *why = reason;
        return false;
    };
    if (m_over == Over::None)
        return false;
    m_time += dt;
    if (now.targetReady)
        return end("the screen it goes to is up");
    if (now.gameplay)
        return end("the game is playing");
    if (!m_seen || now.screen != m_screen) {
        m_seen = true;
        m_screen = now.screen;
        m_still = 0.0f;
    } else {
        m_still += dt;
    }
    if (now.leftOriginal && m_still >= kSettleSeconds)
        return end("an original screen that stays original is up");
    if (m_time >= kMaxSeconds)
        return end("nothing came up in time");
    return true;
}

} // namespace Front::NewFrontend
