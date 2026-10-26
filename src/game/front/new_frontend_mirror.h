#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/new_frontend_model.h"

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
namespace Front::NewFrontend::Mirror {

enum class Kind {
    None,    // nothing to restyle (the original as it is, or ours)
    Dialog,  // a dialog box: its title, text and buttons
    Skaters, // the skater select: the skaters, the selected one's name and stats (the 3D skater left in view)
    List,    // a plain vertical menu: its title, its items (label, value), the other words on its screen
    Pause,   // a level's pause menu (pause_vmenu): its items, labelled as ours (PauseLabel), over the game
};
const char* KindName(Kind kind); // "dialog", "skaters", "list", "pause" ("" for None)

// One child of the menu as read.
struct ItemRead {
    uint32_t id = 0;
    bool focusable = true;              // (not tagged not_focusable, no block_events)
    std::vector<std::string> texts;     // its text elements' strings, in tree order (raw: the game's escapes)
    bool graphicValue = false;          // a visible sprite in it that isn't a highlight (a slider, a checkbox...)
    std::string display;                // (the skater select: the profile's display name)
};

// What the glue read: the menu that takes the pad events now (the deepest focused one) and what is around it.
struct MenuRead {
    bool found = false;
    uint32_t menuId = 0;
    uintptr_t menuKey = 0;              // (its address: a new menu is a new screen - the animations start again)
    bool vertical = true;
    int selectedIndex = -1;             // the menu's +0x178 (its children's index)
    std::vector<ItemRead> items;
    // A dialog box: the menu is in dialog_box_container; its title (the menu's first text), its text block's lines.
    bool inDialog = false;
    std::string dialogTitle;
    std::vector<std::string> dialogLines;
    // The screen's title (title_text, the themed menus') and the other words on the menu's screen (texts outside the
    // menu, its title and the helper text).
    std::string title;
    std::vector<std::string> extraTexts;
    // The skater select: the selected skater's name as the original shows it (select_skater_name), the stats.
    std::string skaterName;
    std::vector<std::pair<std::string, int>> stats; // (label, value 0-10)
    bool inLevel = false;               // (drawn over the game rather than over the front end)
};

// What decides whether a list is restyled (the glue fills it from the game).
struct Policy {
    bool lists = true;                  // [frontend] restyle_menus (1)
    bool dialogs = true;                // [frontend] restyle_dialogs (1)
    bool skaters = true;                // [frontend] restyle_skater_select (1)
    bool editor = false;                // a Create-a-mode's editor is up (CAS, CAT, CAG, park editor, graphics) - left alone
};

// The menus ours stands for (never restyled: ours is drawn over them, or the player asked for the original).
bool OwnMenu(uint32_t menuId);
// A pause menu item's label in our words, by its id ("" for the item's own text): CONTINUE is Resume, QUIT Quit to menu.
const char* PauseLabel(uint32_t itemId);
// Titles of lists left original (screens whose meaning is in what isn't text).
bool ExcludedTitle(const std::string& cleanTitle);

struct Row {
    std::string label;
    std::string value;
    bool focusable = true;
    int index = 0;                      // the menu's child index
};

struct View {
    Kind kind = Kind::None;
    uintptr_t key = 0;                  // (the menu read: a change restarts the rows' animations)
    std::string title;
    std::vector<std::string> lines;     // a dialog's text (wrapped by the drawing); a list's other words
    std::vector<Row> rows;
    int selected = -1;                  // an index into rows (-1: none)
    bool horizontal = false;            // (the skater select: Left / Right move)
    std::string bigName;                // the skater select's name; a list's selected label
    std::vector<std::pair<std::string, int>> stats;
    bool overGame = false;
    std::string why; // (kind None: why the menu read stays original - for the log)
};

// The game's text escapes made plain: \n (a line end: a space, or kept with `newlines`), \c<x> (a colour) and \b<x> /
// \m<x> (a button glyph) dropped, \_ a space, \\ a backslash; spaces collapsed and trimmed.
std::string CleanText(const std::string& raw, bool newlines = false);

// The view for what was read (kind None when nothing is restyled).
View Build(const MenuRead& read, const Policy& policy);

// The view held over a few frames: a screen comes up once it has been there kFramesToShow frames in a row (no flicker
// while the original builds its screens one element at a time) and goes once it has been gone kFramesToHide in a row;
// the rows' selection animations.
constexpr int kFramesToShow = 2;
constexpr int kFramesToHide = 3;
class Tracker {
public:
    // A frame: what Build said now. Returns whether a view is up.
    bool Update(const View& now, float dt);
    bool Up() const { return m_up; }
    const View& Shown() const { return m_view; }
    float RowAmount(int row) const;
    void Reset();

private:
    View m_view;
    bool m_up = false;
    int m_seen = 0, m_gone = 0;
    Kind m_pendingKind = Kind::None;
    std::vector<RowAnim> m_rows;
};

} // namespace Front::NewFrontend::Mirror
