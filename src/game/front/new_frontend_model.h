#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/new_frontend_text.h"

// NOT ORIGINAL - the new front end's pure parts (ours; see new_frontend.h): its data file, its small state file, the
// layout's frame, and the menus' state machine with their animation - no game, no renderer, so the self-tests run them
// offline (src/hook/selftests_frontend.cpp).
namespace Front::NewFrontend {

// --- the data: <overlay>\frontend\levels.json (tools/pipeline/menu_level/frontend_levels.ts) ----------------------------

struct Level {
    std::string name;  // "BOSTON"
    std::string load;  // its load script ("load_BO")
    std::string art;   // its loading screen (an image name: images\<art>.img.xbx)
    std::vector<std::string> artVariants; // the level select's pictures to choose from (levels.json art_variants; [0] is `art`)
    int variant = 0;                      // the one shown (Q / E on the level select; saved in thug2-decomp-art.txt)
    // The picture shown: the chosen variant, else `art`.
    const std::string& CurrentArt() const
    {
        return variant > 0 && variant < static_cast<int>(artVariants.size()) ? artVariants[variant] : art;
    }
    std::string levelNumName; // level_num as a script name ("LevelNum_BO"), or empty when it was a number
    int levelNum = -1;        // level_num as a number (-1: a name - resolved by the game)
    std::string flag;         // its unlock flag's script name ("LEVEL_UNLOCKED_BO")
    std::string seenFlag;     // its seen flag's ("LEVEL_SEEN_BO"; "" none): outside a career the game's select locks by it
    bool hideWhenLocked = false;
    bool genericArt = false; // (the art is one of THUG2's generic loading screens: the level has none of its own)
    std::string gameTag;
};

struct Section {
    std::string id;    // "thug2", "thps3", "misc"...
    std::string tab;   // "THUG2", "THPS3", "Misc"
    std::string title; // "TONY HAWK'S PRO SKATER 3"
    uint32_t colour = 0xFFC9CBD0; // ARGB
    std::vector<Level> levels;
};

struct Catalogue {
    std::vector<Section> sections;
    std::vector<std::pair<std::string, std::string>> menuArt; // a main menu item's id -> its image
    // The image for a menu item ("" if none).
    std::string MenuArt(const std::string& id) const;
    // The level whose load script is `load` (any case): its section and index. False if none.
    bool Find(const std::string& load, int* section, int* level) const;
    // The level's next (`step` 1) or previous (-1) picture, wrapping. False when it has only one.
    bool CycleArt(const std::string& load, int step);
    // The saved picture choices applied / read back (only the levels not on their first picture).
    void ApplyArtChoices(const std::vector<std::pair<std::string, int>>& choices);
    std::vector<std::pair<std::string, int>> ArtChoices() const;
};
// thug2-decomp-art.txt: one `<load script> <picture number>` line per level (0-based; '#' comments).
std::vector<std::pair<std::string, int>> ParseArtChoices(const std::string& text);
std::string FormatArtChoices(const std::vector<std::pair<std::string, int>>& choices);

// The file's text into `out`. False (and why in `error`) when it isn't JSON of that shape (version 1); unknown fields
// are ignored.
bool ParseCatalogue(const std::string& json, Catalogue* out, std::string* error);
// "#A48BFF" -> 0xFFA48BFF (`fallback` when it isn't #RRGGBB).
uint32_t ParseColour(const std::string& text, uint32_t fallback);

// --- the state file: thug2-decomp-frontend.txt next to the ini ---------------------------------------------------

struct SavedState {
    std::string lastLevel; // the load script of the level last started (Continue)
};
// key=value lines (unknown keys ignored, '#' comments).
SavedState ParseState(const std::string& text);
std::string FormatState(const SavedState& state);

// --- the frame -----------------------------------------------------------------------------------------------------

// The design is 1280 x 720 (docs/design/frontend/HMain.dc.html, HLevels.dc.html); in a window it keeps 16:9, as large
// as fits, centred: design point (x, y) is window point (left + x * scale, top + y * scale).
constexpr float kDesignWidth = 1280.0f;
constexpr float kDesignHeight = 720.0f;
struct Frame {
    float left = 0.0f, top = 0.0f, scale = 1.0f;
    float X(float x) const { return left + x * scale; }
    float Y(float y) const { return top + y * scale; }
    float S(float v) const { return v * scale; }
    // Window point -> design point.
    float DesignX(float x) const { return (x - left) / scale; }
    float DesignY(float y) const { return (y - top) / scale; }
};
Frame FitFrame(float windowWidth, float windowHeight);

// The art over its box, as CSS's object-fit: cover - the part of the picture (texture coordinates u0 v0 u1 v1) that
// fills a box of `boxWidth` x `boxHeight`, centred, and panned: `pan` 0..1 slides the visible part across `panRange`
// (a fraction of the picture, e.g. 0.03) - the picture is first zoomed by 1 + panRange so there is room.
struct Uv {
    float u0, v0, u1, v1;
};
Uv CoverUv(float imageWidth, float imageHeight, float boxWidth, float boxHeight, float pan, float panRange);

// --- motion --------------------------------------------------------------------------------------------------------

constexpr float kSelectSeconds = 0.12f; // the selection snaps in (ease-out)
constexpr float kWipeSeconds = 0.42f;   // a screen change: one diagonal wipe (the screen changes half-way)
constexpr float kLaunchSeconds = 0.55f; // a level confirmed: the name sliced, the art up through the gap
constexpr float kPanSeconds = 10.0f;    // the art pans across its range and back
float EaseOut(float t);                 // cubic: 1 - (1 - t)^3, t clamped to 0..1

// A row's selection amount: moves toward 1 (selected) or 0 at 1 / kSelectSeconds a second; drawn eased.
struct RowAnim {
    float value = 0.0f;
    void Tick(bool selected, float dt);
    float Eased() const { return EaseOut(value); }
};


// --- the menus -----------------------------------------------------------------------------------------------------

// The screens. The new front end's own screens are only those with no menu of the game's behind them - the settings
// and the level select -, opened by a menu's item (NewFrontend_Open); every menu is the game's, restyled
// (new_frontend_mirror.h). Main and Pause are the roots they open on (never drawn: back at one, ours goes aside).
enum class Screen {
    Main,    // the root in the front end
    Options, // thug2-decomp.ini's settings (new_frontend_options.h) and the game's GAME OPTIONS
    Levels,  // the level select (Free Skate, the pause menu's Change level, the Level Editor's level)
    Pause,   // the root in a level
};
constexpr int kScreens = 4;
const char* ScreenName(Screen screen); // "main", "options", "levels", "pause" (the automation channel's names)
bool ScreenFromName(const std::string& name, Screen* out);
bool IsPauseScreen(Screen screen); // Pause - and Options / Levels when opened from it (OverPause)

// What the player did (the keyboard, a controller, the mouse - new_frontend.cpp turns them into these).
enum class Input { Up, Down, Left, Right, Confirm, Back, PrevTab, NextTab };

// What the menus ask the game for.
enum class ActionKind {
    None,
    MoveSound,      // the selection moved (the menus' up / down sound)
    ChooseSound,    // something chosen
    BackSound,
    GameOptions,    // the game's GAME OPTIONS (in a level: the pause menu's)
    Option,         // option: an index into Options::Specs(), value: its choice
    Skate,          // load; levels: what the level select was opened for
};
enum class LevelsFor { FreeSkate, Pause, Edit };
struct Action {
    ActionKind kind = ActionKind::None;
    std::string load;
    std::string id;
    int value = 0;
    int option = -1;
    LevelsFor levels = LevelsFor::FreeSkate;
};

// The art for a menu item's id (ours - Mirror::MenuItemStyle) when levels.json's menu_art has none (THUG2's own loading
// screens).
std::string DefaultArt(const std::string& itemId);

// One section as the level select shows it: the levels to list (indices into Section::levels: a locked
// hide_when_locked level is left out) and whether each is locked (listed, but not skated).
struct SectionView {
    std::vector<int> levels;
    std::vector<bool> locked;
};

class Menu {
public:
    // The data (kept by the caller), each section's view, and the screens open (`stack`, the last shown): Back pops
    // them (back at the root: nothing - the caller steps ours aside). `levels`: what an open level select is for.
    void Reset(const Catalogue* catalogue, std::vector<SectionView> views, const std::string& lastLevel,
               std::vector<Screen> stack = {Screen::Main}, LevelsFor levels = LevelsFor::FreeSkate);
    void SetLastLevel(const std::string& load) { m_lastLevel = load; }
    // The sections' views made again (the locks changed): each section's row kept where it still is.
    void SetViews(std::vector<SectionView> views);
    const std::string& LastLevel() const { return m_lastLevel; }
    // The options' choices, one per Options::Specs() entry.
    void SetOptionValues(std::vector<int> choices) { m_options = std::move(choices); }
    const std::vector<int>& OptionValues() const { return m_options; }
    void SetOptionGroup(int group);

    // A screen opened from outside the menus (with the wipe): the level select for `levels` when it's Levels.
    void Open(Screen screen, LevelsFor levels = LevelsFor::FreeSkate);

    // A key / button. The action, if any (a menu action at once; Skate only from Tick, after the launch).
    Action Press(Input input);
    // The mouse over an option row (its index), a level row, a game tab / option group; a click selects and chooses
    // (as Confirm); on a tab, the tab; on an option's choice, that choice.
    Action HoverItem(int index);
    Action ClickItem(int index);
    Action HoverLevel(int row);
    Action ClickLevel(int row);
    Action ClickTab(int section);
    Action ClickBack();
    Action ClickSkate();
    Action ClickChoice(int row, int choice);
    // The wheel: notches up (+) or down (-).
    Action Wheel(int notches);

    // Time passes: the animations; the launch's Skate when it's over.
    Action Tick(float dt);

    // --- the automation channel ---
    // The shown screen's item `id` selected (a level's load script or name; an option's key or "game_options";
    // "tab:<section id>" / "group:<group id>"). False if it isn't there (or disabled).
    bool SelectId(const std::string& id);
    // The shown screen's selection as such an id ("" if none).
    std::string SelectedId() const;

    // --- what is drawn ---
    Screen Shown() const { return m_shown; } // the screen drawn (changes half-way through a wipe)
    Screen Target() const { return m_stack.empty() ? Screen::Main : m_stack.back(); }
    int Depth() const { return static_cast<int>(m_stack.size()); }
    // Whether the screens open are the pause menu's (Options / Levels opened from it are drawn over the game too).
    bool OverPause() const;
    int Selected() const;                                       // the shown list's selection (-1: none)
    LevelsFor LevelsPurpose() const { return m_levelsFor; }
    int SectionIndex() const { return m_section; }
    int LevelRow() const;                      // the selected row in the section's list (-1: an empty list)
    const SectionView* View() const;           // the section's view (null: no sections)
    const Level* SelectedLevel() const;        // (null: none)
    bool SelectedLocked() const;
    int SectionCount() const { return static_cast<int>(m_views.size()); }
    const Catalogue* Data() const { return m_catalogue; }
    // Options: the group shown, its rows (indices into Options::Specs()) and the selected row (rows.size(): the
    // Game options row).
    int OptionGroup() const { return m_group; }
    const std::vector<int>& OptionRows() const;
    int OptionRow() const { return m_optionRow; }
    int OptionChoice(int option) const;
    // The list's first row shown (animated toward keeping the selection in view): rows `visibleRows` tall.
    float ListScroll() const { return m_scroll; }
    void SetVisibleRows(int rows) { m_visibleRows = rows > 0 ? rows : 1; }
    int VisibleRows() const { return m_visibleRows; }
    // The rows' selection amounts (the current list).
    float RowAmount(int row) const;
    // The wipe under way: its 0..1 progress (-1: none) and its colour.
    float Wipe() const { return m_wipe < 0.0f ? -1.0f : m_wipe / kWipeSeconds; }
    uint32_t WipeColour() const { return m_wipeColour; }
    // The launch under way: its 0..1 progress (-1: none; it stays at 1 once Skate was given, until the next Reset),
    // and the level going.
    float Launch() const { return m_launch < 0.0f ? -1.0f : m_launch / kLaunchSeconds; }
    const std::string& LaunchLoad() const { return m_launchLoad; }
    // The game couldn't start the level: the launch undone (the menus take input again).
    void CancelLaunch()
    {
        m_launch = -1.0f;
        m_launchLoad.clear();
    }
    bool Busy() const { return m_wipe >= 0.0f || m_launch >= 0.0f; }
    // The art's pan, 0..1..0 over kPanSeconds * 2.
    float Pan() const;
    // The colour of the section shown (the house yellow outside the level select).
    uint32_t AccentColour() const;

    static constexpr uint32_t kHouseYellow = 0xFFF2E205;

private:
    void Go(Screen screen);
    void Pop();
    void Show(Screen screen); // (the wipe's half-way)
    int ListCount() const;
    Action MoveTo(int index);
    Action MoveBy(int step);
    Action MoveLevel(int row);
    Action SetSection(int section);
    Action ChangeOption(int step, bool wrap);
    Action Confirm();
    Action Back();
    Action StartLaunch(const std::string& load);
    void ClampScroll(bool snap);
    void ResetRows();

    const Catalogue* m_catalogue = nullptr;
    std::vector<SectionView> m_views;
    std::vector<int> m_rowInSection; // the row last selected in each section
    std::string m_lastLevel;
    std::vector<Screen> m_stack = {Screen::Main};
    Screen m_shown = Screen::Main;
    LevelsFor m_levelsFor = LevelsFor::FreeSkate;
    int m_section = 0;
    std::vector<int> m_options;
    int m_group = 0;
    int m_optionRow = 0;
    float m_scroll = 0.0f, m_scrollTarget = 0.0f;
    int m_visibleRows = 13;
    std::vector<RowAnim> m_rows;
    float m_wipe = -1.0f;
    bool m_switched = false;
    uint32_t m_wipeColour = kHouseYellow;
    float m_launch = -1.0f;
    std::string m_launchLoad;
    bool m_launchFired = false;
    float m_time = 0.0f;
};

// --- covering a change of screen -----------------------------------------------------------------------------------

// No original screen may show between ours and the next screen, nor on the way back (the user's report of 2026-10-25:
// GAME OPTIONS flashed the original THUG2 menu for a moment). While a cover is on, the last screen drawn
// (ours, or the restyled one) stays on screen with the original's elements hidden, until the target is ready: ours or a
// restyled screen up, the game playing (or a tool's own UI, a loading screen), or an original screen that stays
// original has stood still for kSettleSeconds; kMaxSeconds at most.
class Cover {
public:
    enum class Over { None, Ours, Restyled }; // what is drawn meanwhile
    static constexpr float kSettleSeconds = 0.25f;
    static constexpr float kMaxSeconds = 3.0f;
    struct Now {
        bool targetReady = false;  // ours shown, or a restyled screen up
        bool gameplay = false;     // in a level and playing, a tool's UI (the level editor), a loading screen, no level
        bool leftOriginal = false; // the screen up stays the original's (not restyled: the decision says so)
        uintptr_t screen = 0;      // the menu taking the pad events (0: none) - settled when it stays the same
    };
    void Start(Over over);
    void Stop() { m_over = Over::None; }
    // A frame. Returns whether the cover is still on; `why` says why it ended (when it just did).
    bool Update(const Now& now, float dt, const char** why = nullptr);
    bool On() const { return m_over != Over::None; }
    Over Drawn() const { return m_over; }
    float Time() const { return m_time; }

private:
    Over m_over = Over::None;
    float m_time = 0.0f, m_still = 0.0f;
    uintptr_t m_screen = 0;
    bool m_seen = false;
};

} // namespace Front::NewFrontend
