// NOT ORIGINAL - the new front end's restyled original screens (ours): see new_frontend_mirror.h.

#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_text.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <span>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace frontend::new_frontend::mirror {

namespace {

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

// The longest list restyled (ours scrolls it; past this the game's own is kept - nothing the game makes is that long).
constexpr size_t kMaxItems = 400;

ButtonNamer g_button_namer = nullptr;

std::string get_upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// A trailing ':' and spaces off a label ("Songs:" -> "Songs").
std::string caption(const std::string& text)
{
    std::string s = text;
    while (!s.empty() && (s.back() == ':' || s.back() == ' '))
        s.pop_back();
    return s;
}

// The first whole number in `text` (-1: none) - "Points Available: 3", "3 Air points available".
int first_number(const std::string& text)
{
    int n = -1;
    for (const char c : text) {
        if (c >= '0' && c <= '9')
            n = (n < 0 ? 0 : n * 10) + (c - '0');
        else if (n >= 0)
            break;
    }
    return n;
}

bool contains_upper(const std::string& text, const char* upper_word)
{
    return get_upper(text).find(upper_word) != std::string::npos;
}

// Kind::Stats from what was read: the stat rows in the stats' order, the points left, the screen's other words (the
// description, the story's goals; the numbers the original shows beside its own stars dropped - each row has its own).
View stats_view(const MenuRead& read)
{
    View view;
    view.kind = Kind::kStats;
    view.key = read.menu_key_;
    view.over_game_ = read.in_level_;
    view.stats_editing_ = read.stats_editing_;
    for (size_t i = 0; i < read.items.size(); ++i) {
        const ItemRead& item = core::at(read.items, i);
        if (item.stat_index_ < 0)
            continue;
        Row row;
        for (const std::string& t : item.texts) {
            row.label = caption(clean_text(t));
            if (!row.label.empty())
                break;
        }
        row.stat_value_ = item.stat_value_;
        row.stat_cap_ = std::max(item.stat_cap_, item.stat_value_);
        row.stat_column_ = item.stat_column_;
        row.index = item.stat_child_;
        row.focusable = item.focusable;
        row.adjustable = true;
        view.stat_columns_ = std::max(view.stat_columns_, item.stat_column_ + 1);
        if (ints::as_signed(i) == read.stats_selected_)
            view.selected = static_cast<int>(view.rows.size());
        view.rows.push_back(std::move(row));
    }
    if (view.rows.empty()) {
        View none; // (its rows not made yet)
        none.unsettled = true;
        none.why = "the stats screen without its stats yet";
        return none;
    }
    const std::string title = clean_text(read.title);
    for (const std::string& t : read.extra_texts_) {
        const std::string clean = clean_text(t, true);
        if (clean.empty() || get_upper(clean) == get_upper(title))
            continue;
        if (contains_upper(clean, "EDIT STATS")) {
            view.title = clean;
            continue;
        }
        if (contains_upper(clean, "POINTS AVAILABLE")) {
            view.points = std::max(first_number(clean), 0);
            continue;
        }
        // (the original's own "Air:", "3", "/10" beside its stars)
        if (clean.back() == ':' || clean.find_first_not_of("0123456789/ ") == std::string::npos)
            continue;
        view.lines.push_back(clean);
    }
    if (view.title.empty())
        view.title = !title.empty() ? title : std::string(text_tools::kTitleStats);
    if (view.lines.size() > 12)
        view.lines.resize(12);
    view.big_name_ = view.selected >= 0 ? core::at(view.rows, view.selected).label : view.title;
    return view;
}

} // namespace

const char* kind_name(Kind kind)
{
    switch (kind) {
    case Kind::kNone:
        return "";
    case Kind::kDialog:
        return "dialog";
    case Kind::kSkaters:
        return "skaters";
    case Kind::kLinkChain:
        return "list";
    case Kind::kFreeze:
        return "pause";
    case Kind::kMenu:
        return "menu";
    case Kind::kKeyboard:
        return "keyboard";
    case Kind::kStats:
        return "stats";
    }
    return "";
}

bool own_menu(uint32_t menu_id)
{
    // (none any more: the main menu, the career's and Create are the game's menus, restyled - Kind::Menu; the pause
    // menu too - Kind::Freeze. Kept for the policy's shape.)
    (void)menu_id;
    return false;
}

const char* pause_label(uint32_t item_id)
{
    // (the overlay's own items - menu_newfe_restart, menu_newfe_options - are made with these words already; the rest
    // keep the game's text: TEXT MESSAGES, VIEW GOALS, END CURRENT RUN...)
    static const auto kLabels = std::to_array<std::pair<uint32_t, const char*>>({
        {name_hash("menu_continue"), text_tools::kResume},
        {name_hash("menu_retry_goal"), text_tools::kRetryGoal},
        {name_hash("menu_newfe_restart"), text_tools::kRestart},
        {name_hash("menu_view_goals"), text_tools::kViewGoals},
        {name_hash("menu_chapters"), text_tools::kViewGoals},
        {name_hash("menu_your_options"), text_tools::kYourOptions},
        {name_hash("menu_level_select"), text_tools::kChangeLevel},
        {name_hash("menu_level_editor"), text_tools::kPauseLevelEditor},
        {name_hash("menu_newfe_options"), text_tools::kPauseOptions},
        {name_hash("menu_skateshop"), text_tools::kQuitToMenu},
    });
    for (const auto& [id, label] : kLabels)
        if (id == item_id)
            return label;
    return "";
}

const ItemStyle* menu_item_style(uint32_t item_id)
{
    // (the main menu's items are the overlay's - make_menu_level addNewFrontendMain; the rest the game's own ids)
    static const auto kStyles = std::to_array<std::pair<uint32_t, ItemStyle>>({
        {name_hash("menu_newfe_career"), {"career", text_tools::kCareer, text_tools::kCareerHelp, nullptr}},
        {name_hash("menu_singlesess"), {"freeskate", text_tools::kFreeSkate, text_tools::kFreeSkateHelp, nullptr}},
        {name_hash("menu_net"), {"online", text_tools::kOnline, text_tools::kOnlineHelp, nullptr}},
        {name_hash("menu_createamodes"), {"create", text_tools::kCreate, text_tools::kCreateHelp, nullptr}},
        {name_hash("menu_newfe_options"), {"options", text_tools::kOptions, text_tools::kOptionsHelp, nullptr}},
        {name_hash("menu_quit"), {"quit", text_tools::kQuit, text_tools::kQuitHelp, nullptr}},
        {name_hash("menu_career"), {"thug2story", text_tools::kStoryThug2, text_tools::kStoryThug2Help, nullptr}},
        {name_hash("menu_classic"), {"classic", text_tools::kClassic, text_tools::kClassicHelp, nullptr}},
        {name_hash("menu_newfe_thugstory"),
         {"thugstory", text_tools::kStoryThug, text_tools::kStoryThugHelp, text_tools::kComingSoon}},
        {name_hash("ss_continue_story"), {"continue", text_tools::kContinue, text_tools::kContinueHelp, nullptr}},
        {name_hash("ss_new_story"), {"new", nullptr, text_tools::kNewHelp, nullptr}}, // (label: the mode's - Build)
        {name_hash("ss_load_story"), {"load", text_tools::kLoad, text_tools::kLoadHelp, nullptr}},
        {name_hash("menu_cas"), {"cas", text_tools::kCas, text_tools::kCreateItemHelp, nullptr}},
        {name_hash("menu_cat"), {"cat", text_tools::kCat, text_tools::kCreateItemHelp, nullptr}},
        {name_hash("menu_creategoals"), {"cag", text_tools::kCag, text_tools::kCreateItemHelp, nullptr}},
        {name_hash("menu_parked"), {"cap", text_tools::kCap, text_tools::kCreateItemHelp, nullptr}},
        {name_hash("menu_cagr"), {"cagr", text_tools::kCagr, text_tools::kCreateItemHelp, nullptr}},
        {name_hash("menu_newfe_editor"), {"editor", text_tools::kLevelEditor, text_tools::kCreateItemHelp, nullptr}},
    });
    for (const auto& [id, style] : kStyles)
        if (id == item_id)
            return &style;
    return nullptr;
}

bool front_end_menu(uint32_t menu_id)
{
    static const auto kMenus = std::to_array<uint32_t>({name_hash("main_vmenu"), name_hash("newfe_career_vmenu"),
                                      name_hash("career_options_vmenu"), name_hash("main_menu")});
    for (const uint32_t id : kMenus)
        if (id == menu_id)
            return true;
    return false;
}

bool excluded_title(const std::string& clean_title)
{
    // (screens whose meaning is a picture, not their list: the gamma test card, the screen's position)
    static const auto kTitles = std::to_array<const char*>({"GAMMA", "SCREEN SETUP", "SCREEN"});
    const std::string upper = get_upper(clean_title);
    for (const char* title : kTitles)
        if (upper == title)
            return true;
    return false;
}

std::string clean_text(const std::string& raw, bool newlines)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        const char c = core::at(raw, i);
        if (c == '\\' && i + 1 < raw.size()) {
            const char e = core::at(raw, i + 1);
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
            if ((e == 'b' || e == 'B' || e == 'm' || e == 'M') && i + 2 < raw.size()) {
                // (a button: the key's name as the game spells it, between spaces when words touch it)
                const std::string name = g_button_namer ? g_button_namer(e, core::at(raw, i + 2)) : std::string();
                if (!name.empty()) {
                    if (!out.empty() && out.back() != ' ' && out.back() != '/')
                        out += ' ';
                    out += name;
                    if (i + 3 < raw.size() && core::at(raw, i + 3) != ' ' && core::at(raw, i + 3) != '/' && core::at(raw, i + 3) != '=')
                        out += ' ';
                }
                i += 2;
                continue;
            }
            if ((e == 'c' || e == 'C') && i + 2 < raw.size()) {
                i += 2; // (a colour: its code character too)
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
    for (const char c : out) {
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

void set_button_namer(ButtonNamer namer) { g_button_namer = namer; }

// The colour menus' ranges (colormenu.qb: colormenu_max_saturation 90, colormenu_min_value 12, colormenu_max_value 60;
// Create-A-Graphic's value starts at 0).
constexpr float kSatMax = 90.0f, kValMinCas = 12.0f, kValMax = 60.0f;

View get_build(const MenuRead& read, const Policy& policy)
{
    View view;
    if (!read.found)
        return view;
    view.key = read.menu_key_;
    view.over_game_ = read.in_level_;

    static const uint32_t kSkaterMenu = name_hash("select_skater_hmenu");

    // The rows: every child with words (a dialog's title and text are the menu's first children: not rows).
    auto rows_from = [&](bool dialog) {
        bool skipped_title = false;
        for (size_t i = 0; i < read.items.size(); ++i) {
            const ItemRead& item = core::at(read.items, i);
            if (dialog && !item.focusable && !skipped_title) {
                skipped_title = true; // (the title text)
                continue;
            }
            if (dialog && !item.focusable)
                continue; // (the text block, the spacer, a logo)
            std::vector<std::string> texts;
            for (const std::string& t : item.texts) {
                const std::string clean = clean_text(t);
                if (!clean.empty())
                    texts.push_back(clean);
            }
            if (texts.empty() && item.display.empty() && item.checkbox < 0)
                continue;
            Row row;
            // The label: the first text with a letter in it (a gap list's row is score, name, times); the rest its
            // value.
            size_t label_at = texts.empty() ? 0 : texts.size();
            for (size_t k = 0; k < texts.size() && label_at == texts.size(); ++k)
                for (const char ch : core::at(texts, k))
                    if (std::isalpha(static_cast<unsigned char>(ch))) {
                        label_at = k;
                        break;
                    }
            if (label_at >= texts.size())
                label_at = 0;
            row.label = !item.display.empty() ? item.display : texts.empty() ? std::string() : caption(core::at(texts, label_at));
            for (size_t k = 0; k < texts.size(); ++k)
                if (!item.display.empty() || k != label_at)
                    row.value += (row.value.empty() ? "" : " \xC2\xB7 ") + core::at(texts, k);
            if (item.checkbox >= 0)
                row.value += (row.value.empty() ? "" : " \xC2\xB7 ") +
                             std::string(item.checkbox ? text_tools::kOn : text_tools::kOff);
            row.adjustable = item.adjustable;
            // A slider row: its track is drawn (Paint) from `track` / `track_at_`; its value shown as a number. The colour
            // menus' markers are all moved by any change (colormenu_refresh_arrows), so the numbers are true on every row
            // (Create-A-Graphic moves only the focused slider's: the others' are as of when they were last focused); the
            // scaling bars show their place on the bar, the selected one's only.
            if (item.slider_at_ >= 0.0f) {
                row.track = item.slider_kind_ == 0 ? 5 : item.slider_kind_;
                row.track_at_ = item.slider_at_;
                std::array<char, 24> number{};
                bool show = true;
                if (item.slider_kind_ == 1)
                    // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                    text::format_to(number, "%d", static_cast<int>(item.slider_at_ * 360.0f + 0.5f));
                else if (item.slider_kind_ == 2)
                    // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                    text::format_to(number, "%d%%", static_cast<int>(item.slider_at_ * kSatMax + 0.5f));
                else if (item.slider_kind_ == 3) {
                    const float low = read.graphic_editor_ ? 0.0f : kValMinCas;
                    text::format_to(
                        number, "%d%%",
                        // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                        static_cast<int>(low + item.slider_at_ * (kValMax - low) + 0.5f));
                } else {
                    show = ints::as_signed(i) == read.selected_index_;
                    // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                    text::format_to(number, "%d%%", static_cast<int>(item.slider_at_ * 100.0f + 0.5f));
                }
                if (show)
                    row.value += (row.value.empty() ? "" : "  ") + std::string(number.data());
            }
            row.focusable = item.focusable;
            row.index = static_cast<int>(i);
            if (ints::as_signed(i) == read.selected_index_)
                view.selected = static_cast<int>(view.rows.size());
            view.rows.push_back(std::move(row));
        }
    };

    if (read.in_dialog_) {
        if (!policy.dialogs)
            return view;
        view.kind = Kind::kDialog;
        view.title = clean_text(read.dialog_title_);
        for (const std::string& line : read.dialog_lines_) {
            const std::string clean = clean_text(line, true);
            if (!clean.empty())
                view.lines.push_back(clean);
        }
        rows_from(true);
        return view;
    }
    if (read.stats_screen_)
        return policy.lists ? stats_view(read) : view;
    static const uint32_t kPauseMenu = name_hash("pause_vmenu");
    if (read.menu_id_ == kPauseMenu) {
        // The pause menu: every item with words, in the game's order (roundbar items carry their bars and a shadow copy
        // of the text - the first text is the label), not-focusable ones dimmed.
        view.kind = Kind::kFreeze;
        view.title = text_tools::kTitlePaused;
        for (size_t i = 0; i < read.items.size(); ++i) {
            const ItemRead& item = core::at(read.items, i);
            const char* ours = pause_label(item.id);
            std::string label = *ours ? std::string(ours) : std::string();
            for (size_t k = 0; label.empty() && k < item.texts.size(); ++k)
                label = caption(clean_text(core::at(item.texts, k)));
            if (label.empty())
                continue;
            Row row;
            row.label = label;
            row.focusable = item.focusable;
            row.index = static_cast<int>(i);
            if (ints::as_signed(i) == read.selected_index_)
                view.selected = static_cast<int>(view.rows.size());
            view.rows.push_back(std::move(row));
        }
        if (view.rows.empty()) {
            view.kind = Kind::kNone;
            view.unsettled = true;
        }
        view.big_name_ = view.selected >= 0 ? core::at(view.rows, view.selected).label : view.title;
        return view;
    }
    if (front_end_menu(read.menu_id_) && !(policy.editor && read.menu_id_ == name_hash("main_menu"))) {
        // A front end menu: every item with words in the game's order, in our words where the style table has the
        // item (its art, the line under the list, a note); the menu's title ours for the ones we know.
        static const uint32_t kMainMenu = name_hash("main_vmenu"), kCareerMenu = name_hash("newfe_career_vmenu");
        const std::string title = clean_text(read.title);
        const std::string upper = get_upper(title);
        const bool classic = upper.find("CLASSIC") != std::string::npos;
        view.kind = Kind::kMenu;
        view.main_menu_ = read.menu_id_ == kMainMenu;
        if (view.main_menu_) {
            view.title = text_tools::kTitleMain;
            view.screen_art_ = "career";
        } else if (read.menu_id_ == kCareerMenu) {
            view.title = text_tools::kTitleCareer;
            view.screen_art_ = "career";
        } else if (read.menu_id_ == name_hash("career_options_vmenu")) {
            view.title = classic ? text_tools::kTitleClassic : text_tools::kTitleStory;
            view.screen_art_ = classic ? "classic" : "story";
        } else if (upper == "CREATE-A-MODES") {
            view.title = text_tools::kTitleCreate;
            view.screen_art_ = "create";
        } else if (upper == "SINGLE SESSION" || upper == "FREE SKATE") {
            view.title = text_tools::kTitleFreeSkate; // (the skate shop's menu after the skater select)
            view.screen_art_ = "freeskate";
        } else {
            view.title = title.empty() ? std::string(text_tools::kTitleMenu) : title;
            view.screen_art_ = "freeskate";
        }
        for (size_t i = 0; i < read.items.size(); ++i) {
            const ItemRead& item = core::at(read.items, i);
            const ItemStyle* style = menu_item_style(item.id);
            std::string label;
            if (style && style->label)
                label = style->label;
            else if (style && item.id == name_hash("ss_new_story"))
                label = classic ? text_tools::kNewClassic : text_tools::kNewStory;
            for (size_t k = 0; label.empty() && k < item.texts.size(); ++k)
                label = caption(clean_text(core::at(item.texts, k)));
            if (label.empty())
                continue;
            Row row;
            row.label = label;
            row.focusable = item.focusable;
            row.index = static_cast<int>(i);
            if (style) {
                row.art = style->our_id_ ? style->our_id_ : "";
                row.blurb = style->blurb ? style->blurb : "";
                row.tag = style->tag ? style->tag : "";
            }
            if (ints::as_signed(i) == read.selected_index_)
                view.selected = static_cast<int>(view.rows.size());
            view.rows.push_back(std::move(row));
        }
        if (view.rows.empty()) {
            view.kind = Kind::kNone;
            view.unsettled = true;
            return view;
        }
        if (view.main_menu_ && view.selected >= 0)
            view.kicker = std::string(text_tools::kMainKicker) + " \xE2\x80\x94 " +
                          (view.selected + 1 < 10 ? "0" : "") + std::to_string(view.selected + 1);
        else
            view.kicker = view.title;
        view.big_name_ = view.selected >= 0 ? core::at(view.rows, view.selected).label : view.title;
        return view;
    }
    if (read.keyboard) {
        if (!policy.lists)
            return view;
        view.kind = Kind::kKeyboard;
        view.title = clean_text(read.title);
        if (view.title.empty())
            view.title = text_tools::kTitleKeyboard;
        view.entry = clean_text(read.entry);
        for (const std::vector<std::string>& row : read.keys) {
            std::vector<std::string> keys;
            for (const std::string& key : row) {
                const std::string clean = clean_text(key);
                keys.push_back(clean.empty() && !key.empty() ? std::string(" ") : clean);
            }
            view.keys.push_back(std::move(keys));
        }
        view.key_row_ = read.key_row_;
        view.key_col_ = read.key_col_;
        view.key = read.menu_key_;
        view.over_game_ = read.in_level_;
        return view;
    }
    if (read.menu_id_ == kSkaterMenu) {
        if (!policy.skaters)
            return view;
        view.kind = Kind::kSkaters;
        view.horizontal = true;
        for (size_t i = 0; i < read.items.size(); ++i) {
            const ItemRead& item = core::at(read.items, i);
            Row row;
            row.label = item.display.empty() ? std::string("?") : clean_text(item.display);
            row.focusable = item.focusable;
            row.index = static_cast<int>(i);
            if (ints::as_signed(i) == read.selected_index_)
                view.selected = static_cast<int>(view.rows.size());
            view.rows.push_back(std::move(row));
        }
        const std::string name = clean_text(read.skater_name_);
        view.big_name_ = !name.empty() ? name : view.selected >= 0 ? core::at(view.rows, view.selected).label : std::string();
        view.stats = read.stats;
        view.title = text_tools::kTitleSkaters;
        return view;
    }

    // A plain list: vertical, ours not over it, no editor, words in every item it can focus, no slider / checkbox.
    auto original = [&view](const char* why) {
        view.why = why;
        return view;
    };
    if (!policy.lists)
        return original("restyle_menus=0");
    if (own_menu(read.menu_id_))
        return original("the new front end's own");
    if (read.items.empty()) {
        view.unsettled = true;
        return original("no items");
    }
    if (read.items.size() > kMaxItems)
        return original("too many items");
    bool any_focusable = false;
    for (const ItemRead& item : read.items) {
        if (!item.focusable)
            continue;
        any_focusable = true;
        bool words = false;
        for (const std::string& t : item.texts)
            words = words || !clean_text(t).empty();
        if (!words && item.checkbox < 0)
            return original("an item without words");
    }
    if (!any_focusable) {
        view.unsettled = true;
        return original("nothing to choose");
    }
    const std::string title = clean_text(read.title);
    if (excluded_title(title))
        return original("a screen left original by its title");
    view.kind = Kind::kLinkChain;
    view.title = title;
    view.horizontal = !read.vertical;
    view.editor = policy.editor;
    rows_from(false);
    if (view.rows.empty()) {
        view.kind = Kind::kNone;
        view.unsettled = true;
        return view;
    }
    for (const std::string& t : read.extra_texts_) {
        const std::string clean = clean_text(t, true);
        if (!clean.empty() && get_upper(clean) != get_upper(title))
            view.lines.push_back(clean);
    }
    if (view.lines.size() > 12)
        view.lines.resize(12);
    view.big_name_ = view.selected >= 0 ? core::at(view.rows, view.selected).label : view.title;
    // A colour menu: hue, saturation and value all there - the numbers back from the markers' places.
    {
        std::array<float, 4> at = {{-1.0f, -1.0f, -1.0f, -1.0f}};
        for (const Row& r : view.rows)
            if (r.track >= 1 && r.track <= 3)
                core::at(at, r.track) = r.track_at_;
        if (core::at(at, 1) >= 0.0f && core::at(at, 2) >= 0.0f && core::at(at, 3) >= 0.0f) {
            const float low = read.graphic_editor_ ? 0.0f : kValMinCas;
            view.has_colour_ = true;
            view.hue = std::min(core::at(at, 1) * 360.0f, 359.0f);
            view.sat = core::at(at, 2) * kSatMax;
            view.val = low + core::at(at, 3) * (kValMax - low);
        }
    }
    return view;
}

void skater_colour(float hue, float sat_percent, float val_percent, std::span<float, 3> rgb)
{
    const float s = sat_percent * 0.01f;
    const float v = val_percent * 0.01f;
    float r = v, g = v, b = v;
    if (s != 0.0f) {
        const float sector = hue * (1.0f / 60.0f);
        const int i = static_cast<int>(sector);
        const float f = sector - static_cast<float>(i);
        const float p = (1.0f - s) * v;
        const float q = (1.0f - s * f) * v;
        const float t = (1.0f - (1.0f - f) * s) * v;
        switch (static_cast<unsigned>(i)) {
        case 0:
            r = v, g = t, b = p;
            break;
        case 1:
            r = q, g = v, b = p;
            break;
        case 2:
            r = p, g = v, b = t;
            break;
        case 3:
            r = p, g = q, b = v;
            break;
        case 4:
            r = t, g = p, b = v;
            break;
        default:
            r = v, g = p, b = q;
            break;
        }
    }
    core::at(rgb, 0) = std::min(1.0f, r * 2.0f);
    core::at(rgb, 1) = std::min(1.0f, g * 2.0f);
    core::at(rgb, 2) = std::min(1.0f, b * 2.0f);
}

bool Tracker::tick_frame(const View& now, float dt)
{
    if (now.kind == Kind::kNone) {
        seen_ = 0;
        pending_kind_ = Kind::kNone;
        if (up_dir_ && ++gone_ >= kFramesToHide) {
            up_dir_ = false;
            camera_view_ = View{};
            rows_.clear();
        }
    } else {
        gone_ = 0;
        if (!up_dir_) {
            seen_ = now.kind == pending_kind_ ? seen_ + 1 : 1;
            pending_kind_ = now.kind;
            // (the pause menu is made in one go - create_pause_menu adds every item before the frame is drawn: up at
            // once, so its first frame isn't one with neither the original nor ours)
            if (seen_ >= (now.kind == Kind::kFreeze ? 1 : kFramesToShow)) {
                up_dir_ = true;
                rows_.clear();
            }
        }
        if (up_dir_) {
            if (now.key != camera_view_.key || now.kind != camera_view_.kind)
                rows_.clear();
            camera_view_ = now;
        }
    }
    if (up_dir_) {
        rows_.resize(camera_view_.rows.size());
        for (size_t i = 0; i < rows_.size(); ++i)
            core::at(rows_, i).tick(ints::as_signed(i) == camera_view_.selected, dt);
    }
    return up_dir_;
}

float Tracker::row_amount(int row) const
{
    return row >= 0 && std::cmp_less(row, rows_.size()) ? core::at(rows_, row).get_eased() : 0.0f;
}

void Tracker::get_reset()
{
    camera_view_ = View{};
    up_dir_ = false;
    seen_ = gone_ = 0;
    pending_kind_ = Kind::kNone;
    rows_.clear();
}

} // namespace frontend::new_frontend::mirror
