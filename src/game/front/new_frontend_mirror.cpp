// NOT ORIGINAL - the new front end's restyled original screens (ours): see new_frontend_mirror.h.

#include "game/front/new_frontend_mirror.h"
#include "game/front/new_frontend_text.h"

#include <algorithm>
#include <cctype>

#include "game/core/crc.h"

namespace Front::NewFrontend::Mirror {

namespace {

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

// The longest list restyled (a longer one is the original's: a scrolling list ours would have to page).
constexpr size_t kMaxItems = 60;

std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// A trailing ':' and spaces off a label ("Songs:" -> "Songs").
std::string Label(const std::string& text)
{
    std::string s = text;
    while (!s.empty() && (s.back() == ':' || s.back() == ' '))
        s.pop_back();
    return s;
}

} // namespace

const char* KindName(Kind kind)
{
    switch (kind) {
    case Kind::None: return "";
    case Kind::Dialog: return "dialog";
    case Kind::Skaters: return "skaters";
    case Kind::List: return "list";
    case Kind::Pause: return "pause";
    }
    return "";
}

bool OwnMenu(uint32_t menuId)
{
    // main_vmenu (the main menu), career_options_vmenu (STORY / CLASSIC MODE OPTIONS) - the pause menu is restyled
    // (Kind::Pause)
    static const uint32_t kOwn[] = {Checksum("main_vmenu"), Checksum("career_options_vmenu")};
    for (uint32_t id : kOwn)
        if (id == menuId)
            return true;
    return false;
}

const char* PauseLabel(uint32_t itemId)
{
    // (the overlay's own items - menu_newfe_restart, menu_newfe_options - are made with these words already; the rest
    // keep the game's text: TEXT MESSAGES, VIEW GOALS, END CURRENT RUN...)
    static const std::pair<uint32_t, const char*> kLabels[] = {
        {Checksum("menu_continue"), Text::kResume},           {Checksum("menu_retry_goal"), Text::kRetryGoal},
        {Checksum("menu_newfe_restart"), Text::kRestart},     {Checksum("menu_view_goals"), Text::kViewGoals},
        {Checksum("menu_chapters"), Text::kViewGoals},        {Checksum("menu_your_options"), Text::kYourOptions},
        {Checksum("menu_level_select"), Text::kChangeLevel},  {Checksum("menu_level_editor"), Text::kPauseLevelEditor},
        {Checksum("menu_newfe_options"), Text::kPauseOptions}, {Checksum("menu_skateshop"), Text::kQuitToMenu},
    };
    for (const auto& [id, label] : kLabels)
        if (id == itemId)
            return label;
    return "";
}

bool ExcludedTitle(const std::string& cleanTitle)
{
    // (screens that are more than their list: a keyboard's, the create modes' editors when reached another way)
    static const char* const kTitles[] = {
        "CREATE-A-TRICK", "EDIT TRICK", "OVERWRITE TRICK", "PRE-MADE TRICKS", "CREATED GOALS", "SELECT GOAL TYPE",
        "EDIT GOAL", "SELECT GAPS", "COMBO SKATE-TRICKS", "EDIT TRICKTRIS", "EDIT SKATE-TRICKS", "SELECT KEY COMBOS",
        "PRE-MADE PARKS", "GAMMA", "SCREEN SETUP", "SCREEN",
    };
    std::string upper = Upper(cleanTitle);
    for (const char* title : kTitles)
        if (upper == title)
            return true;
    return false;
}

std::string CleanText(const std::string& raw, bool newlines)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            char e = raw[i + 1];
            if (e == 'n' || e == 'N') {
                out += newlines ? '\n' : ' ';
                ++i;
                continue;
            }
            if (e == '_') {
                out += ' ';
                ++i;
                continue;
            }
            if (e == '\\') {
                // ("\\" in the file: an escaped backslash - the next character may start an escape of its own, as
                // "\\n" in the scripts' strings reaches the game as \n)
                ++i;
                continue;
            }
            if ((e == 'c' || e == 'C' || e == 'b' || e == 'B' || e == 'm' || e == 'M') && i + 2 < raw.size()) {
                i += 2; // (a colour or a button glyph: its code character too)
                continue;
            }
            ++i;
            continue;
        }
        if (c == '\n' || c == '\r' || c == '\t') {
            out += newlines && c == '\n' ? '\n' : ' ';
            continue;
        }
        out += c;
    }
    // Spaces collapsed (not across line ends), trimmed.
    std::string tidy;
    for (char c : out) {
        if (c == ' ' && (tidy.empty() || tidy.back() == ' ' || tidy.back() == '\n'))
            continue;
        if (c == '\n' && !tidy.empty() && tidy.back() == ' ')
            tidy.pop_back();
        tidy += c;
    }
    while (!tidy.empty() && (tidy.back() == ' ' || tidy.back() == '\n'))
        tidy.pop_back();
    return tidy;
}

View Build(const MenuRead& read, const Policy& policy)
{
    View view;
    if (!read.found)
        return view;
    view.key = read.menuKey;
    view.overGame = read.inLevel;

    static const uint32_t kSkaterMenu = Checksum("select_skater_hmenu");
    static const uint32_t kCasItem = Checksum("menu_cas"), kShopItem = Checksum("ss_play_level");

    // The rows: every child with words (a dialog's title and text are the menu's first children: not rows).
    auto rowsFrom = [&](bool dialog) {
        bool skippedTitle = false;
        for (size_t i = 0; i < read.items.size(); ++i) {
            const ItemRead& item = read.items[i];
            if (dialog && !item.focusable && !skippedTitle) {
                skippedTitle = true; // (the title text)
                continue;
            }
            if (dialog && !item.focusable)
                continue; // (the text block, the spacer, a logo)
            std::vector<std::string> texts;
            for (const std::string& t : item.texts) {
                std::string clean = CleanText(t);
                if (!clean.empty())
                    texts.push_back(clean);
            }
            if (texts.empty() && item.display.empty())
                continue;
            Row row;
            // The label: the first text with a letter in it (a gap list's row is score, name, times); the rest its
            // value.
            size_t labelAt = texts.empty() ? 0 : texts.size();
            for (size_t k = 0; k < texts.size() && labelAt == texts.size(); ++k)
                for (char ch : texts[k])
                    if (std::isalpha(static_cast<unsigned char>(ch))) {
                        labelAt = k;
                        break;
                    }
            if (labelAt >= texts.size())
                labelAt = 0;
            row.label = !item.display.empty() ? item.display : Label(texts[labelAt]);
            for (size_t k = 0; k < texts.size(); ++k)
                if (!item.display.empty() || k != labelAt)
                    row.value += (row.value.empty() ? "" : " \xC2\xB7 ") + texts[k];
            row.focusable = item.focusable;
            row.index = static_cast<int>(i);
            if (static_cast<int>(i) == read.selectedIndex)
                view.selected = static_cast<int>(view.rows.size());
            view.rows.push_back(std::move(row));
        }
    };

    if (read.inDialog) {
        if (!policy.dialogs)
            return view;
        view.kind = Kind::Dialog;
        view.title = CleanText(read.dialogTitle);
        for (const std::string& line : read.dialogLines) {
            std::string clean = CleanText(line, true);
            if (!clean.empty())
                view.lines.push_back(clean);
        }
        rowsFrom(true);
        return view;
    }
    static const uint32_t kPauseMenu = Checksum("pause_vmenu");
    if (read.menuId == kPauseMenu) {
        // The pause menu: every item with words, in the game's order (roundbar items carry their bars and a shadow copy
        // of the text - the first text is the label), not-focusable ones dimmed.
        view.kind = Kind::Pause;
        view.title = Text::kTitlePaused;
        for (size_t i = 0; i < read.items.size(); ++i) {
            const ItemRead& item = read.items[i];
            const char* ours = PauseLabel(item.id);
            std::string label = *ours ? std::string(ours) : std::string();
            for (size_t k = 0; label.empty() && k < item.texts.size(); ++k)
                label = Label(CleanText(item.texts[k]));
            if (label.empty())
                continue;
            Row row;
            row.label = label;
            row.focusable = item.focusable;
            row.index = static_cast<int>(i);
            if (static_cast<int>(i) == read.selectedIndex)
                view.selected = static_cast<int>(view.rows.size());
            view.rows.push_back(std::move(row));
        }
        if (view.rows.empty())
            view.kind = Kind::None;
        view.bigName = view.selected >= 0 ? view.rows[view.selected].label : view.title;
        return view;
    }
    if (read.menuId == kSkaterMenu) {
        if (!policy.skaters)
            return view;
        view.kind = Kind::Skaters;
        view.horizontal = true;
        for (size_t i = 0; i < read.items.size(); ++i) {
            const ItemRead& item = read.items[i];
            Row row;
            row.label = item.display.empty() ? std::string("?") : CleanText(item.display);
            row.focusable = item.focusable;
            row.index = static_cast<int>(i);
            if (static_cast<int>(i) == read.selectedIndex)
                view.selected = static_cast<int>(view.rows.size());
            view.rows.push_back(std::move(row));
        }
        std::string name = CleanText(read.skaterName);
        view.bigName = !name.empty() ? name : view.selected >= 0 ? view.rows[view.selected].label : std::string();
        view.stats = read.stats;
        view.title = Text::kTitleSkaters;
        return view;
    }

    // A plain list: vertical, ours not over it, no editor, words in every item it can focus, no slider / checkbox.
    auto original = [&view](const char* why) {
        view.why = why;
        return view;
    };
    if (!policy.lists)
        return original("restyle_menus=0");
    if (policy.editor)
        return original("an editor is up");
    if (!read.vertical)
        return original("not a vertical menu");
    if (OwnMenu(read.menuId))
        return original("the new front end's own");
    if (read.items.empty() || read.items.size() > kMaxItems)
        return original("no items, or too many");
    bool anyFocusable = false;
    for (const ItemRead& item : read.items) {
        if (item.id == kCasItem || item.id == kShopItem)
            return original("the new front end's own"); // (CREATE-A-MODES, the skate shop's menu: ours stands for them)
        if (!item.focusable)
            continue;
        anyFocusable = true;
        bool words = false;
        for (const std::string& t : item.texts)
            words = words || !CleanText(t).empty();
        if (!words)
            return original("an item without words");
        if (item.graphicValue)
            return original("an item with a picture (a slider, a checkbox)");
    }
    if (!anyFocusable)
        return original("nothing to choose");
    std::string title = CleanText(read.title);
    if (ExcludedTitle(title))
        return original("a screen left original by its title");
    view.kind = Kind::List;
    view.title = title;
    rowsFrom(false);
    if (view.rows.empty()) {
        view.kind = Kind::None;
        return view;
    }
    for (const std::string& t : read.extraTexts) {
        std::string clean = CleanText(t, true);
        if (!clean.empty() && Upper(clean) != Upper(title))
            view.lines.push_back(clean);
    }
    if (view.lines.size() > 12)
        view.lines.resize(12);
    view.bigName = view.selected >= 0 ? view.rows[view.selected].label : view.title;
    return view;
}

bool Tracker::Update(const View& now, float dt)
{
    if (now.kind == Kind::None) {
        m_seen = 0;
        m_pendingKind = Kind::None;
        if (m_up && ++m_gone >= kFramesToHide) {
            m_up = false;
            m_view = View{};
            m_rows.clear();
        }
    } else {
        m_gone = 0;
        if (!m_up) {
            m_seen = now.kind == m_pendingKind ? m_seen + 1 : 1;
            m_pendingKind = now.kind;
            if (m_seen >= kFramesToShow) {
                m_up = true;
                m_rows.clear();
            }
        }
        if (m_up) {
            if (now.key != m_view.key || now.kind != m_view.kind)
                m_rows.clear();
            m_view = now;
        }
    }
    if (m_up) {
        m_rows.resize(m_view.rows.size());
        for (size_t i = 0; i < m_rows.size(); ++i)
            m_rows[i].Tick(static_cast<int>(i) == m_view.selected, dt);
    }
    return m_up;
}

float Tracker::RowAmount(int row) const
{
    return row >= 0 && row < static_cast<int>(m_rows.size()) ? m_rows[row].Eased() : 0.0f;
}

void Tracker::Reset()
{
    m_view = View{};
    m_up = false;
    m_seen = m_gone = 0;
    m_pendingKind = Kind::None;
    m_rows.clear();
}

} // namespace Front::NewFrontend::Mirror
