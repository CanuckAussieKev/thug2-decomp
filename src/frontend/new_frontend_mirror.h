#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "frontend/new_frontend_model.h"

// NOT ORIGINAL - the new front end's restyled original screens (ours; see new_frontend.h, phase 3): the original
// screens the new front end doesn't replace - a level's pause menu (pause_vmenu: its items as the game made them for
// the mode, the overlay's among them, in the pause design - never a menu of ours handing presses on), the game's dialog boxes (create_snazzy_dialog_box: every "QUIT? Are you
// sure?", warning, error and info box), the skater select (select_skater_hmenu) and plain menu lists (the game options'
// sub-menus, YOUR OPTIONS, high scores, cutscenes...) - drawn in the B+C look over the original, whose screen elements
// are hidden (root_window scaled to 0, as the pause menu's). The original stays in charge: the keyboard and the
// controllers go on reaching its menu (its own scripts move, choose and go back), and what is drawn is read from it each
// frame - its title, its items' text, its menu's selected index. The mouse: our rows step the original menu's
// selection with its own pad events and choose with pad_choose.
//
// This part is pure (the self-tests run it offline): what was read from the screen tree in, what to draw out.
namespace frontend::new_frontend::mirror {

enum class Kind : uint8_t {
    kNone,      // nothing to restyle (the original as it is, or ours)
    kDialog,    // a dialog box: its title, text and buttons
    kSkaters,   // the skater select: the skaters, the selected one's name and stats (the 3D skater left in view)
    kLinkChain, // a plain vertical menu: its title, its items (label, value), the other words on its screen
    kFreeze,    // a level's pause menu (pause_vmenu): its items, labelled as ours (pause_label), over the game
    kMenu, // a front end roundbar menu (the main menu, Career, STORY / CLASSIC MODE OPTIONS, Create, the skate shop's
           // menu...): its items, in our words, art and notes where the style table has them (menu_item_style)
    kKeyboard, // the on-screen keyboard (a vertical menu of horizontal rows of keys): the text typed, the keys
};
const char* kind_name(Kind kind); // "dialog", "skaters", "list", "pause", "menu", "keyboard" ("" for None)

// One child of the menu as read.
struct ItemRead {
    uint32_t id = 0;
    bool focusable = true;          // (not tagged not_focusable, no block_events)
    std::vector<std::string> texts; // its text elements' strings, in tree order (raw: the game's escapes)
    bool graphic_value_ = false;    // a visible sprite in it that isn't a highlight (decoration: bars, icons -
                                    // not a reason to leave the screen as it is)
    int checkbox = -1;              // a checkbox in it (the "checkbox" texture): 1 ticked ("checkmark"), 0 not, -1 none
    bool adjustable = false;        // its value changes with Left / Right (the left_arrow / right_arrow sprites, or a
                                    // slider's up_arrow / down_arrow markers)
    // A slider item (the colour menus' HUE / SAT. / VAL., the scaling menus' bars - sliderbar_add_item): the marker's
    // place on the bar, 0 (left end) .. 1 (right end); -1 not a slider. `slider_hue_`: the bar is the hue's (degrees).
    float slider_at_ = -1.0f;
    bool slider_hue_ = false;
    int slider_kind_ = 0; // which slider: 1 hue, 2 saturation, 3 value, 4 alpha (CAGR), 0 any other bar
    std::string display;  // (the skater select: the profile's display name)
};

// What the glue read: the menu that takes the pad events now (the deepest focused one) and what is around it.
struct MenuRead {
    bool found = false;
    uint32_t menu_id_ = 0;
    const void* menu_key_ = nullptr; // (its address: a new menu is a new screen - the animations start again)
    bool vertical = true;
    int selected_index_ = -1; // the menu's +0x178 (its children's index)
    std::vector<ItemRead> items;
    // A dialog box: the menu is in dialog_box_container; its title (the menu's first text), its text block's lines.
    bool in_dialog_ = false;
    std::string dialog_title_;
    std::vector<std::string> dialog_lines_;
    // The screen's title (title_text, the themed menus') and the other words on the menu's screen (texts outside the
    // menu, its title and the helper text).
    std::string title;
    std::vector<std::string> extra_texts_;
    // The skater select: the selected skater's name as the original shows it (select_skater_name), the stats.
    std::string skater_name_;
    std::vector<std::pair<std::string, int>> stats; // (label, value 0-10)
    bool in_level_ = false;                         // (drawn over the game rather than over the front end)
    bool graphic_editor_ = false; // Create-A-Graphic is up (its value slider starts at 0, CAS's at 12)
    // The on-screen keyboard: its rows of keys (each key's words; the row's own label for a wide key), the key
    // selected (row, key; -1 none), the text typed so far.
    bool keyboard = false;
    std::vector<std::vector<std::string>> keys;
    int key_row_ = -1, key_col_ = -1;
    std::string entry;
};

// What decides whether a list is restyled (the glue fills it from the game).
struct Policy {
    bool lists = true;   // [frontend] restyle_menus (1)
    bool dialogs = true; // [frontend] restyle_dialogs (1)
    bool skaters = true; // [frontend] restyle_skater_select (1)
    bool editor = false; // a Create-a-mode's editor is up (CAS, CAT, CAG, park editor, graphics) - its
                         // menus restyled lighter over it (the editor's view shows through)
    bool online = true;  // [frontend] restyle_online (1): the online screens too
};

// The menus ours stands for (never restyled: ours is drawn over them, or the player asked for the original).
bool own_menu(uint32_t menu_id);
// A pause menu item's label in our words, by its id ("" for the item's own text): CONTINUE is Resume, QUIT Quit to menu.
const char* pause_label(uint32_t item_id);

// How a front end menu's item is shown, by the game's item id (the overlay's and the game's): our id (its art -
// levels.json's menu_art / default_art), our label, the line under the list, a note after the label. Null: none - the
// item's own text, the menu's art.
struct ItemStyle {
    const char* our_id_;
    const char* label;
    const char* blurb;
    const char* tag;
};
const ItemStyle* menu_item_style(uint32_t item_id);
// Whether a menu is one of the front end's roundbar menus restyled as Kind::Menu (main_vmenu, newfe_career_vmenu,
// career_options_vmenu, and main_menu - make_new_roundbar_menu's own id: CREATE-A-MODES, the skate shop's menu...).
bool front_end_menu(uint32_t menu_id);
// Titles of lists left original (screens whose meaning is in what isn't text).
bool excluded_title(const std::string& clean_title);

struct Row {
    std::string label;
    std::string value;
    bool focusable = true;
    int index = 0;           // the menu's child index
    bool adjustable = false; // Left / Right change its value (the mouse clicks its sides)
    // A slider row's track: which (ItemRead::slider_kind_, 0 none) and where the marker stands on it, 0 .. 1.
    int track = 0;
    float track_at_ = 0.0f;
    // (Kind::Menu: the style table's - our id for the art, the line under the list, a note after the label)
    std::string art, blurb, tag;
};

struct View {
    Kind kind = Kind::kNone;
    const void* key = nullptr; // (the menu read: a change restarts the rows' animations)
    std::string title;
    std::vector<std::string> lines; // a dialog's text (wrapped by the drawing); a list's other words
    std::vector<Row> rows;
    int selected = -1;       // an index into rows (-1: none)
    bool horizontal = false; // (the skater select: Left / Right move)
    std::string big_name_;   // the skater select's name; a list's selected label
    std::vector<std::pair<std::string, int>> stats;
    bool over_game_ = false;
    // A colour menu (HUE / SAT. / VAL. all there): the colour's values as the game holds them - hue in degrees,
    // saturation and value in percent (the markers' places turned back into the numbers).
    bool has_colour_ = false;
    float hue = 0.0f, sat = 0.0f, val = 0.0f;
    bool editor = false; // (an editor's: drawn lighter over its view)
    // (Kind::Keyboard: the keys, the one selected, the text typed)
    std::vector<std::vector<std::string>> keys;
    int key_row_ = -1, key_col_ = -1;
    std::string entry;
    // (Kind::Menu: the menu's own art id - the item's when it has none -, its kicker, the main menu - numbered kicker,
    // the Continue hint)
    std::string screen_art_, kicker;
    bool main_menu_ = false;
    std::string why; // (kind None: why the menu read stays original - for the log)
    // (kind None: a menu with nothing to show yet - no items, none with words or none to choose: the original may still
    // be building it, so its elements stay hidden a few frames before it is left as it is - kFramesUnsettled)
    bool unsettled = false;
};

// The colour a HSV the game stores (hue degrees, saturation / value percent) shows on the skater: the game's HSV -> RGB
// (the same sector formula as the texture code's), as the part's vertex colour - which multiplies the texture with 0.5
// neutral, so shown doubled (clamped) as a colour on a white texture. rgb 0 .. 1.
void skater_colour(float hue, float sat_percent, float val_percent, std::span<float, 3> rgb);

// The game's text escapes made plain: \n (a line end: a space, or kept with `newlines`), \c<x> (a colour) dropped,
// \b<x> / \m<x> (a button) the key's name as the game spells it (ButtonNamer; dropped without one), \_ a space, \\ a
// backslash; spaces collapsed and trimmed.
std::string clean_text(const std::string& raw, bool newlines = false);
// How a button tag is spelled (the tag letter b / B / m / M and its digit): the glue sets it to the game's own (the key
// a button is on, Up / Down / Left / Right for the arrow glyphs); null: buttons dropped (the self-tests).
using ButtonNamer = std::string (*)(char tag, char digit);
void set_button_namer(ButtonNamer namer);

// The view for what was read (kind None when nothing is restyled).
View get_build(const MenuRead& read, const Policy& policy);

// The view held over a few frames: a screen comes up once it has been there kFramesToShow frames in a row (no flicker
// while the original builds its screens one element at a time; the pause menu, made in one go, at once) and goes once
// it has been gone kFramesToHide in a row;
// the rows' selection animations.
constexpr int kFramesToShow = 2;
constexpr int kFramesToHide = 3;
// How long a menu kept unsettled (View::unsettled) stays hidden before it is shown as the game made it.
constexpr int kFramesUnsettled = 10;
class Tracker {
public:
    // A frame: what Build said now. Returns whether a view is up.
    bool tick_frame(const View& now, float dt);
    [[nodiscard]] bool get_up() const { return up_dir_; }
    [[nodiscard]] const View& get_shown() const { return camera_view_; }
    [[nodiscard]] float row_amount(int row) const;
    void get_reset();

private:
    View camera_view_;
    bool up_dir_ = false;
    int seen_ = 0, gone_ = 0;
    Kind pending_kind_ = Kind::kNone;
    std::vector<RowAnim> rows_;
};

} // namespace frontend::new_frontend::mirror
