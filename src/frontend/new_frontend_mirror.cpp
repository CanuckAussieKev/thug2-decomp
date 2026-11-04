// NOT ORIGINAL - the new front end's restyled original screens (ours): see new_frontend_mirror.h.

#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_text.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "engine/core/checksums.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/maths/maths.h"

namespace frontend::new_frontend::mirror {

namespace {

constexpr int kDecimalBase = 10;
constexpr std::size_t kMaxViewLines = 12;      // the lines a menu view keeps
constexpr int kDefaultSliderTrack = 5;
constexpr std::size_t kNumberTextSize = 24;
constexpr float kDegreesPerTurn = 360.0F;
constexpr float kMaxHue = 359.0F;
constexpr float kBrightenFactor = 2.0F;

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

// The longest list restyled (ours scrolls it; past this the game's own is kept - nothing the game makes is that long).
constexpr size_t kMaxItems = 400;

ButtonNamer& button_namer()
{
    static ButtonNamer button_namer{nullptr};
    return button_namer;
}

std::string get_upper(std::string text)
{
    for (char& character : text) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return text;
}

// A trailing ':' and spaces off a label ("Songs:" -> "Songs").
std::string caption(const std::string& text)
{
    std::string trimmed = text;
    while (!trimmed.empty() && (trimmed.back() == ':' || trimmed.back() == ' ')) {
        trimmed.pop_back();
    }
    return trimmed;
}

// The first whole number in `text` (-1: none) - "Points Available: 3", "3 Air points available".
int first_number(const std::string& text)
{
    int number = -1;
    for (const char character : text) {
        if (character >= '0' && character <= '9') {
            number = (number < 0 ? 0 : number * kDecimalBase) + (character - '0');
        } else if (number >= 0) {
            break;
        }
    }
    return number;
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
        if (item.stat_index_ < 0) {
            continue;
        }
        Row row;
        for (const std::string& text : item.texts) {
            row.label = caption(clean_text(text));
            if (!row.label.empty()) {
                break;
            }
        }
        row.stat_value_ = item.stat_value_;
        row.stat_cap_ = std::max(item.stat_cap_, item.stat_value_);
        row.stat_column_ = item.stat_column_;
        row.index = item.stat_child_;
        row.focusable = item.focusable;
        row.adjustable = true;
        view.stat_columns_ = std::max(view.stat_columns_, item.stat_column_ + 1);
        if (ints::as_signed(i) == read.stats_selected_) {
            view.selected = static_cast<int>(view.rows.size());
        }
        view.rows.push_back(std::move(row));
    }
    if (view.rows.empty()) {
        View none; // (its rows not made yet)
        none.unsettled = true;
        none.why = "the stats screen without its stats yet";
        return none;
    }
    const std::string title = clean_text(read.title);
    for (const std::string& text : read.extra_texts_) {
        const std::string clean = clean_text(text, true);
        if (clean.empty() || get_upper(clean) == get_upper(title)) {
            continue;
        }
        if (contains_upper(clean, "EDIT STATS")) {
            view.title = clean;
            continue;
        }
        if (contains_upper(clean, "POINTS AVAILABLE")) {
            view.points = std::max(first_number(clean), 0);
            continue;
        }
        // (the original's own "Air:", "3", "/10" beside its stars)
        if (clean.back() == ':' || clean.find_first_not_of("0123456789/ ") == std::string::npos) {
            continue;
        }
        view.lines.push_back(clean);
    }
    if (view.title.empty()) {
        view.title = !title.empty() ? title : std::string(text_tools::kTitleStats);
    }
    if (view.lines.size() > kMaxViewLines) {
        view.lines.resize(kMaxViewLines);
    }
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
    for (const auto& [id, label] : kLabels) {
        if (id == item_id) {
            return label;
        }
    }
    return "";
}

const ItemStyle* menu_item_style(uint32_t item_id)
{
    // (the main menu's items are the overlay's - make_menu_level addNewFrontendMain; the rest the game's own ids)
    static const auto kStyles = std::to_array<std::pair<uint32_t, ItemStyle>>({
        {name_hash("menu_newfe_career"), {.our_id_ = "career", .label = text_tools::kCareer, .blurb = text_tools::kCareerHelp, .tag = nullptr}},
        {name_hash("menu_singlesess"), {.our_id_ = "freeskate", .label = text_tools::kFreeSkate, .blurb = text_tools::kFreeSkateHelp, .tag = nullptr}},
        {name_hash("menu_net"), {.our_id_ = "online", .label = text_tools::kOnline, .blurb = text_tools::kOnlineHelp, .tag = nullptr}},
        {name_hash("menu_createamodes"), {.our_id_ = "create", .label = text_tools::kCreate, .blurb = text_tools::kCreateHelp, .tag = nullptr}},
        {name_hash("menu_newfe_options"), {.our_id_ = "options", .label = text_tools::kOptions, .blurb = text_tools::kOptionsHelp, .tag = nullptr}},
        {name_hash("menu_quit"), {.our_id_ = "quit", .label = text_tools::kQuit, .blurb = text_tools::kQuitHelp, .tag = nullptr}},
        {name_hash("menu_career"), {.our_id_ = "thug2story", .label = text_tools::kStoryThug2, .blurb = text_tools::kStoryThug2Help, .tag = nullptr}},
        {name_hash("menu_classic"), {.our_id_ = "classic", .label = text_tools::kClassic, .blurb = text_tools::kClassicHelp, .tag = nullptr}},
        {name_hash("menu_newfe_thugstory"),
         {.our_id_ = "thugstory", .label = text_tools::kStoryThug, .blurb = text_tools::kStoryThugHelp, .tag = text_tools::kComingSoon}},
        {name_hash("ss_continue_story"), {.our_id_ = "continue", .label = text_tools::kContinue, .blurb = text_tools::kContinueHelp, .tag = nullptr}},
        {name_hash("ss_new_story"), {.our_id_ = "new", .label = nullptr, .blurb = text_tools::kNewHelp, .tag = nullptr}}, // (label: the mode's - Build)
        {name_hash("ss_load_story"), {.our_id_ = "load", .label = text_tools::kLoad, .blurb = text_tools::kLoadHelp, .tag = nullptr}},
        {name_hash("menu_cas"), {.our_id_ = "cas", .label = text_tools::kCas, .blurb = text_tools::kCreateItemHelp, .tag = nullptr}},
        {name_hash("menu_cat"), {.our_id_ = "cat", .label = text_tools::kCat, .blurb = text_tools::kCreateItemHelp, .tag = nullptr}},
        {name_hash("menu_creategoals"), {.our_id_ = "cag", .label = text_tools::kCag, .blurb = text_tools::kCreateItemHelp, .tag = nullptr}},
        {name_hash("menu_parked"), {.our_id_ = "cap", .label = text_tools::kCap, .blurb = text_tools::kCreateItemHelp, .tag = nullptr}},
        {name_hash("menu_cagr"), {.our_id_ = "cagr", .label = text_tools::kCagr, .blurb = text_tools::kCreateItemHelp, .tag = nullptr}},
        {name_hash("menu_newfe_editor"), {.our_id_ = "editor", .label = text_tools::kLevelEditor, .blurb = text_tools::kCreateItemHelp, .tag = nullptr}},
    });
    for (const auto& [id, style] : kStyles) {
        if (id == item_id) {
            return &style;
        }
    }
    return nullptr;
}

bool front_end_menu(uint32_t menu_id)
{
    static const auto kMenus = std::to_array<uint32_t>({name_hash("main_vmenu"), name_hash("newfe_career_vmenu"),
                                      name_hash("career_options_vmenu"), name_hash("main_menu")});
    return std::ranges::any_of(kMenus, [&](const uint32_t id) { return id == menu_id; });
}

bool excluded_title(const std::string& clean_title)
{
    // (screens whose meaning is a picture, not their list: the gamma test card, the screen's position)
    static const auto kTitles = std::to_array<const char*>({"GAMMA", "SCREEN SETUP", "SCREEN"});
    const std::string upper = get_upper(clean_title);
    return std::ranges::any_of(kTitles, [&](const char* title) { return upper == title; });
}

namespace {

// A button escape at `escape_at` in a text (\b, \m and their digit): the key's name as the game spells it, between
// spaces when words touch it.
void append_button_name(std::string& out, const std::string& raw, size_t escape_at)
{
    const char escape = core::at(raw, escape_at + 1);
    const std::string name =
        button_namer() ? button_namer()({.tag = escape, .digit = core::at(raw, escape_at + 2)}) : std::string();
    if (!name.empty()) {
        if (!out.empty() && out.back() != ' ' && out.back() != '/') {
            out += ' ';
        }
        out += name;
        if (escape_at + 3 < raw.size() && core::at(raw, escape_at + 3) != ' ' && core::at(raw, escape_at + 3) != '/' &&
            core::at(raw, escape_at + 3) != '=') {
            out += ' ';
        }
    }
}

// A cleaned text's spaces collapsed (not across line ends) and trimmed.
std::string collapse_spaces(const std::string& out)
{
    std::string tidy;
    for (const char character : out) {
        if (character == ' ' && (tidy.empty() || tidy.back() == ' ' || tidy.back() == '\n')) {
            continue;
        }
        if (character == '\n' && !tidy.empty() && tidy.back() == ' ') {
            tidy.pop_back();
        }
        tidy += character;
    }
    while (!tidy.empty() && (tidy.back() == ' ' || tidy.back() == '\n')) {
        tidy.pop_back();
    }
    return tidy;
}

} // namespace

std::string clean_text(const std::string& raw, bool newlines)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        const char character = core::at(raw, i);
        if (character == '\\' && i + 1 < raw.size()) {
            const char escape = core::at(raw, i + 1);
            if (escape == 'n' || escape == 'N') {
                out += newlines ? '\n' : ' ';
                ++i;
                continue;
            }
            if (escape == '_') {
                out += ' ';
                ++i;
                continue;
            }
            if (escape == '\\') {
                // ("\\" in the file: an escaped backslash - the next character may start an escape of its own, as
                // "\\n" in the scripts' strings reaches the game as \n)
                ++i;
                continue;
            }
            if ((escape == 'b' || escape == 'B' || escape == 'm' || escape == 'M') && i + 2 < raw.size()) {
                // (a button: the key's name as the game spells it, between spaces when words touch it)
                append_button_name(out, raw, i);
                i += 2;
                continue;
            }
            if ((escape == 'c' || escape == 'C') && i + 2 < raw.size()) {
                i += 2; // (a colour: its code character too)
                continue;
            }
            ++i;
            continue;
        }
        if (character == '\n' || character == '\r' || character == '\t') {
            out += newlines && character == '\n' ? '\n' : ' ';
            continue;
        }
        out += character;
    }
    // Spaces collapsed (not across line ends), trimmed.
    return collapse_spaces(out);
}

void set_button_namer(ButtonNamer namer) { button_namer() = namer; }

// The colour menus' ranges (colormenu.qb: colormenu_max_saturation 90, colormenu_min_value 12, colormenu_max_value 60;
// Create-A-Graphic's value starts at 0).
constexpr float kSatMax = 90.0F, kValMinCas = 12.0F, kValMax = 60.0F;

namespace {

// The label of a row's texts: the first text with a letter in it (a gap list's row is score, name, times); the rest
// its value.
size_t label_index(const std::vector<std::string>& texts)
{
    size_t label_at = texts.empty() ? 0 : texts.size();
    for (size_t k = 0; k < texts.size() && label_at == texts.size(); ++k) {
        for (const char ch : core::at(texts, k)) {
            if (std::isalpha(static_cast<unsigned char>(ch)) != 0) {
                label_at = k;
                break;
            }
        }
    }
    if (label_at >= texts.size()) {
        label_at = 0;
    }
    return label_at;
}

// A slider row: its track is drawn (Paint) from `track` / `track_at_`; its value shown as a number. The colour
// menus' markers are all moved by any change (colormenu_refresh_arrows), so the numbers are true on every row
// (Create-A-Graphic moves only the focused slider's: the others' are as of when they were last focused); the
// scaling bars show their place on the bar, the selected one's only.
void add_slider(const MenuRead& read, const ItemRead& item, size_t item_index, Row& row)
{
    row.track = item.slider_kind_ == 0 ? kDefaultSliderTrack : item.slider_kind_;
    row.track_at_ = item.slider_at_;
    std::array<char, kNumberTextSize> number{};
    bool show = true;
    if (item.slider_kind_ == 1) {
        text::format_to(number, "%d", maths::round_non_negative<int>(item.slider_at_ * kDegreesPerTurn));
    } else if (item.slider_kind_ == 2) {
        text::format_to(number, "%d%%", maths::round_non_negative<int>(item.slider_at_ * kSatMax));
    } else if (item.slider_kind_ == 3) {
        const float low = read.graphic_editor_ ? 0.0F : kValMinCas;
        text::format_to(number, "%d%%", maths::round_non_negative<int>(low + (item.slider_at_ * (kValMax - low))));
    } else {
        show = ints::as_signed(item_index) == read.selected_index_;
        text::format_to(number, "%d%%", maths::round_non_negative<int>(item.slider_at_ * 100.0F));
    }
    if (show) {
        row.value += (row.value.empty() ? "" : "  ") + std::string(number.data());
    }
}

// The item's texts with words in them, cleaned.
std::vector<std::string> worded_texts(const ItemRead& item)
{
    std::vector<std::string> texts;
    for (const std::string& text : item.texts) {
        const std::string clean = clean_text(text);
        if (!clean.empty()) {
            texts.push_back(clean);
        }
    }
    return texts;
}

// One item's row: its label, its value (the other texts, the checkbox, the slider's number).
Row item_row(const MenuRead& read, size_t item_index, const std::vector<std::string>& texts)
{
    const ItemRead& item = core::at(read.items, item_index);
    Row row;
    const size_t label_at = label_index(texts);
    if (!item.display.empty()) {
        row.label = item.display;
    } else if (texts.empty()) {
        row.label = std::string();
    } else {
        row.label = caption(core::at(texts, label_at));
    }
    for (size_t k = 0; k < texts.size(); ++k) {
        if (!item.display.empty() || k != label_at) {
            row.value += (row.value.empty() ? "" : " \xC2\xB7 ") + core::at(texts, k);
        }
    }
    if (item.checkbox >= 0) {
        row.value += (row.value.empty() ? "" : " \xC2\xB7 ") +
                     std::string(item.checkbox != 0 ? text_tools::kOn : text_tools::kOff);
    }
    row.adjustable = item.adjustable;
    if (item.slider_at_ >= 0.0F) {
        add_slider(read, item, item_index, row);
    }
    row.focusable = item.focusable;
    row.index = static_cast<int>(item_index);
    return row;
}

// The rows: every child with words (a dialog's title and text are the menu's first children: not rows).
void add_item_rows(const MenuRead& read, View& view, bool dialog)
{
    bool skipped_title = false;
    for (size_t i = 0; i < read.items.size(); ++i) {
        const ItemRead& item = core::at(read.items, i);
        if (dialog && !item.focusable && !skipped_title) {
            skipped_title = true; // (the title text)
            continue;
        }
        if (dialog && !item.focusable) {
            continue; // (the text block, the spacer, a logo)
        }
        const std::vector<std::string> texts = worded_texts(item);
        if (texts.empty() && item.display.empty() && item.checkbox < 0) {
            continue;
        }
        Row row = item_row(read, i, texts);
        if (ints::as_signed(i) == read.selected_index_) {
            view.selected = static_cast<int>(view.rows.size());
        }
        view.rows.push_back(std::move(row));
    }
}

// A row added (the selected one noted).
void add_row(const MenuRead& read, size_t item_index, Row&& row, View& view)
{
    if (ints::as_signed(item_index) == read.selected_index_) {
        view.selected = static_cast<int>(view.rows.size());
    }
    view.rows.push_back(std::move(row));
}

void build_dialog(const MenuRead& read, View& view)
{
    view.kind = Kind::kDialog;
    view.title = clean_text(read.dialog_title_);
    for (const std::string& line : read.dialog_lines_) {
        const std::string clean = clean_text(line, true);
        if (!clean.empty()) {
            view.lines.push_back(clean);
        }
    }
    add_item_rows(read, view, true);
}

// The pause menu: every item with words, in the game's order (roundbar items carry their bars and a shadow copy of
// the text - the first text is the label), not-focusable ones dimmed.
void build_pause(const MenuRead& read, View& view)
{
    view.kind = Kind::kFreeze;
    view.title = text_tools::kTitlePaused;
    for (size_t i = 0; i < read.items.size(); ++i) {
        const ItemRead& item = core::at(read.items, i);
        const char* ours = pause_label(item.id);
        std::string label = (*ours != '\0') ? std::string(ours) : std::string();
        for (size_t k = 0; label.empty() && k < item.texts.size(); ++k) {
            label = caption(clean_text(core::at(item.texts, k)));
        }
        if (label.empty()) {
            continue;
        }
        Row row;
        row.label = label;
        row.focusable = item.focusable;
        row.index = static_cast<int>(i);
        add_row(read, i, std::move(row), view);
    }
    if (view.rows.empty()) {
        view.kind = Kind::kNone;
        view.unsettled = true;
    }
    view.big_name_ = view.selected >= 0 ? core::at(view.rows, view.selected).label : view.title;
}

// A front end menu's title and art: ours for the ones we know. Whether it is a Classic menu.
bool set_front_end_title(const MenuRead& read, const std::string& title, View& view)
{
    static const uint32_t kMainMenu = name_hash("main_vmenu");
    static const uint32_t kCareerMenu = name_hash("newfe_career_vmenu");
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
    return classic;
}

// A front end menu's item row, in our words where the style table has the item (its art, the line under the list,
// a note); none for an item without words.
bool front_end_row(const ItemRead& item, size_t item_index, bool classic, Row& row)
{
    const ItemStyle* style = menu_item_style(item.id);
    std::string label;
    if (style && style->label) {
        label = style->label;
    } else if (style && item.id == name_hash("ss_new_story")) {
        label = classic ? text_tools::kNewClassic : text_tools::kNewStory;
    }
    for (size_t k = 0; label.empty() && k < item.texts.size(); ++k) {
        label = caption(clean_text(core::at(item.texts, k)));
    }
    if (label.empty()) {
        return false;
    }
    row.label = label;
    row.focusable = item.focusable;
    row.index = static_cast<int>(item_index);
    if (style) {
        row.art = style->our_id_ ? style->our_id_ : "";
        row.blurb = style->blurb ? style->blurb : "";
        row.tag = style->tag ? style->tag : "";
    }
    return true;
}

// A front end menu: every item with words in the game's order, in our words where the style table has the item; the
// menu's title ours for the ones we know.
void build_front_end(const MenuRead& read, View& view)
{
    const bool classic = set_front_end_title(read, clean_text(read.title), view);
    for (size_t i = 0; i < read.items.size(); ++i) {
        Row row;
        if (front_end_row(core::at(read.items, i), i, classic, row)) {
            add_row(read, i, std::move(row), view);
        }
    }
    if (view.rows.empty()) {
        view.kind = Kind::kNone;
        view.unsettled = true;
        return;
    }
    if (view.main_menu_ && view.selected >= 0) {
        view.kicker = std::string(text_tools::kMainKicker) + " \xE2\x80\x94 " +
                      (view.selected + 1 < kDecimalBase ? "0" : "") + std::to_string(view.selected + 1);
    } else {
        view.kicker = view.title;
    }
    view.big_name_ = view.selected >= 0 ? core::at(view.rows, view.selected).label : view.title;
}

void build_keyboard(const MenuRead& read, View& view)
{
    view.kind = Kind::kKeyboard;
    view.title = clean_text(read.title);
    if (view.title.empty()) {
        view.title = text_tools::kTitleKeyboard;
    }
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
}

void build_skaters(const MenuRead& read, View& view)
{
    view.kind = Kind::kSkaters;
    view.horizontal = true;
    for (size_t i = 0; i < read.items.size(); ++i) {
        const ItemRead& item = core::at(read.items, i);
        Row row;
        row.label = item.display.empty() ? std::string("?") : clean_text(item.display);
        row.focusable = item.focusable;
        row.index = static_cast<int>(i);
        add_row(read, i, std::move(row), view);
    }
    const std::string name = clean_text(read.skater_name_);
    if (!name.empty()) {
        view.big_name_ = name;
    } else if (view.selected >= 0) {
        view.big_name_ = core::at(view.rows, view.selected).label;
    } else {
        view.big_name_ = std::string();
    }
    view.stats = read.stats;
    view.title = text_tools::kTitleSkaters;
}

// Why a plain list's items leave it original (nullptr: none - every focusable item has words or a checkbox and
// there is one); `unsettled` set when nothing can be chosen.
const char* unworded_items(const MenuRead& read, View& view)
{
    bool any_focusable = false;
    for (const ItemRead& item : read.items) {
        if (!item.focusable) {
            continue;
        }
        any_focusable = true;
        bool words = false;
        for (const std::string& text : item.texts) {
            words = words || !clean_text(text).empty();
        }
        if (!words && item.checkbox < 0) {
            return "an item without words";
        }
    }
    if (!any_focusable) {
        view.unsettled = true;
        return "nothing to choose";
    }
    return nullptr;
}

// A colour menu: hue, saturation and value all there - the numbers back from the markers' places.
void read_colour(const MenuRead& read, View& view)
{
    std::array<float, 4> at = {{-1.0F, -1.0F, -1.0F, -1.0F}};
    for (const Row& row : view.rows) {
        if (row.track >= 1 && row.track <= 3) {
            core::at(at, row.track) = row.track_at_;
        }
    }
    if (core::at(at, 1) >= 0.0F && core::at(at, 2) >= 0.0F && core::at(at, 3) >= 0.0F) {
        const float low = read.graphic_editor_ ? 0.0F : kValMinCas;
        view.has_colour_ = true;
        view.hue = std::min(core::at(at, 1) * kDegreesPerTurn, kMaxHue);
        view.sat = core::at(at, 2) * kSatMax;
        view.val = low + (core::at(at, 3) * (kValMax - low));
    }
}

// A plain list: vertical, ours not over it, no editor, words in every item it can focus, no slider / checkbox.
View build_list(const MenuRead& read, const Policy& policy, View& view)
{
    auto original = [&view](const char* why) {
        view.why = why;
        return view;
    };
    if (!policy.lists) {
        return original("restyle_menus=0");
    }
    if (own_menu(read.menu_id_)) {
        return original("the new front end's own");
    }
    if (read.items.empty()) {
        view.unsettled = true;
        return original("no items");
    }
    if (read.items.size() > kMaxItems) {
        return original("too many items");
    }
    if (const char* why = unworded_items(read, view)) {
        return original(why);
    }
    const std::string title = clean_text(read.title);
    if (excluded_title(title)) {
        return original("a screen left original by its title");
    }
    view.kind = Kind::kLinkChain;
    view.title = title;
    view.horizontal = !read.vertical;
    view.editor = policy.editor;
    add_item_rows(read, view, false);
    if (view.rows.empty()) {
        view.kind = Kind::kNone;
        view.unsettled = true;
        return view;
    }
    for (const std::string& text : read.extra_texts_) {
        const std::string clean = clean_text(text, true);
        if (!clean.empty() && get_upper(clean) != get_upper(title)) {
            view.lines.push_back(clean);
        }
    }
    if (view.lines.size() > kMaxViewLines) {
        view.lines.resize(kMaxViewLines);
    }
    view.big_name_ = view.selected >= 0 ? core::at(view.rows, view.selected).label : view.title;
    read_colour(read, view);
    return view;
}

} // namespace

View get_build(const MenuRead& read, const Policy& policy)
{
    View view;
    if (!read.found) {
        return view;
    }
    view.key = read.menu_key_;
    view.over_game_ = read.in_level_;

    static const uint32_t kSkaterMenu = name_hash("select_skater_hmenu");

    if (read.in_dialog_) {
        if (!policy.dialogs) {
            return view;
        }
        build_dialog(read, view);
        return view;
    }
    if (read.stats_screen_) {
        return policy.lists ? stats_view(read) : view;
    }
    static const uint32_t kPauseMenu = name_hash("pause_vmenu");
    if (read.menu_id_ == kPauseMenu) {
        build_pause(read, view);
        return view;
    }
    if (front_end_menu(read.menu_id_) && !(policy.editor && read.menu_id_ == name_hash("main_menu"))) {
        build_front_end(read, view);
        return view;
    }
    if (read.keyboard) {
        if (!policy.lists) {
            return view;
        }
        build_keyboard(read, view);
        return view;
    }
    if (read.menu_id_ == kSkaterMenu) {
        if (!policy.skaters) {
            return view;
        }
        build_skaters(read, view);
        return view;
    }
    return build_list(read, policy, view);
}

void skater_colour(const Hsv& colour, std::span<float, 3> rgb)
{
    const float hue = colour.hue;
    const float saturation = colour.sat * 0.01F;
    const float v = colour.val * 0.01F;
    float red = v;
    float green = v;
    float blue = v;
    if (saturation != 0.0F) {
        const float sector = hue * (1.0F / 60.0F);
        const int sector_index = static_cast<int>(sector);
        const float fraction = sector - static_cast<float>(sector_index);
        const float low = (1.0F - saturation) * v;
        const float falling = (1.0F - (saturation * fraction)) * v;
        const float rising = (1.0F - ((1.0F - fraction) * saturation)) * v;
        switch (static_cast<unsigned>(sector_index)) {
        case 0:
            red = v, green = rising, blue = low;
            break;
        case 1:
            red = falling, green = v, blue = low;
            break;
        case 2:
            red = low, green = v, blue = rising;
            break;
        case 3:
            red = low, green = falling, blue = v;
            break;
        case 4:
            red = rising, green = low, blue = v;
            break;
        default:
            red = v, green = low, blue = falling;
            break;
        }
    }
    core::at(rgb, 0) = std::min(1.0F, red * kBrightenFactor);
    core::at(rgb, 1) = std::min(1.0F, green * kBrightenFactor);
    core::at(rgb, 2) = std::min(1.0F, blue * kBrightenFactor);
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
            if (now.key != camera_view_.key || now.kind != camera_view_.kind) {
                rows_.clear();
            }
            camera_view_ = now;
        }
    }
    if (up_dir_) {
        rows_.resize(camera_view_.rows.size());
        for (size_t i = 0; i < rows_.size(); ++i) {
            core::at(rows_, i).tick(ints::as_signed(i) == camera_view_.selected, dt);
        }
    }
    return up_dir_;
}

float Tracker::row_amount(int row) const
{
    return row >= 0 && std::cmp_less(row, rows_.size()) ? core::at(rows_, row).get_eased() : 0.0F;
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
