#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include "frontend/new_frontend_text.h"
#include "engine/core/checked_index.h"

// NOT ORIGINAL - the new front end's pure parts (ours; see new_frontend.h): its data file, its small state file, the
// layout's frame, and the menus' state machine with their animation - no game, no renderer: they run
// offline.
namespace frontend::new_frontend {

// --- the data: <overlay>\frontend\levels.json (tools/pipeline/menu_level/frontend_levels.ts) ----------------------------

struct StageMap {
    std::string name; // "BOSTON"
    std::string load; // its load script ("load_BO")
    std::string art;  // its loading screen (an image name: images\<art>.img.xbx)
    std::vector<std::string>
        art_variants_; // the level select's pictures to choose from (levels.json art_variants; [0] is `art`)
    std::vector<std::string>
        art_labels_; // a label per picture for the indicator (levels.json art_labels, e.g. "70%"; may be empty)
    int variant = 0; // the one shown (Q / E on the level select; saved in thug2-decomp-art.txt)
    // The picture shown: the chosen variant, else `art`.
    [[nodiscard]] const std::string& current_art() const
    {
        return variant >= 0 && std::cmp_less(variant, art_variants_.size()) ? core::at(art_variants_, variant) : art;
    }
    std::string level_num_name_; // level_num as a script name ("LevelNum_BO"), or empty when it was a number
    int level_index_ = -1;       // level_num as a number (-1: a name - resolved by the game)
    std::string flag;            // its unlock flag's script name ("LEVEL_UNLOCKED_BO")
    std::string
        seen_flag_; // its seen flag's ("LEVEL_SEEN_BO"; "" none): outside a career the game's select locks by it
    bool hide_when_locked_ = false;
    bool generic_art_ = false; // (the art is one of THUG2's generic loading screens: the level has none of its own)
    std::string game_tag_;
};

struct Section {
    std::string id;               // "thug2", "thps3", "misc"...
    std::string tab;              // "THUG2", "THPS3", "Misc"
    std::string title;            // "TONY HAWK'S PRO SKATER 3"
    uint32_t colour = 0xFFC9CBD0; // ARGB
    std::vector<StageMap> levels;
};

struct Catalogue {
    std::vector<Section> sections;
    std::vector<std::pair<std::string, std::string>> menu_art_; // a main menu item's id -> its image
    // The image for a menu item ("" if none).
    [[nodiscard]] std::string menu_art(const std::string& id) const;
    // The level whose load script is `load` (any case): its section and index. False if none.
    bool get_find(const std::string& load, int* section, int* level) const;
    // The level's next (`step` 1) or previous (-1) picture, wrapping. False when it has only one.
    bool cycle_art(const std::string& load, int step);
    // The saved picture choices applied / read back (only the levels not on their first picture).
    void apply_art_choices(const std::vector<std::pair<std::string, int>>& choices);
    [[nodiscard]] std::vector<std::pair<std::string, int>> art_choices() const;
};
// thug2-decomp-art.txt: one `<load script> <picture number>` line per level (0-based; '#' comments).
std::vector<std::pair<std::string, int>> parse_art_choices(const std::string& text);
std::string format_art_choices(const std::vector<std::pair<std::string, int>>& choices);

// The file's text into `out`. False (and why in `error`) when it isn't JSON of that shape (version 1); unknown fields
// are ignored.
bool parse_catalogue(const std::string& json, Catalogue* out, std::string* error);
// "#A48BFF" -> 0xFFA48BFF (`fallback` when it isn't #RRGGBB).
uint32_t parse_colour(const std::string& text, uint32_t fallback);

// --- the state file: thug2-decomp-frontend.txt next to the ini ---------------------------------------------------

struct SavedState {
    std::string last_level_; // the load script of the level last started (Continue)
};
// key=value lines (unknown keys ignored, '#' comments).
SavedState parse_state(const std::string& text);
std::string format_state(const SavedState& state);

// --- the frame -----------------------------------------------------------------------------------------------------

// The design is 1280 x 720 (docs/design/frontend/HMain.dc.html, HLevels.dc.html); in a window it keeps 16:9, as large
// as fits, centred: design point (x, y) is window point (left + x * scale, top + y * scale).
constexpr float kDesignWidth = 1280.0f;
constexpr float kDesignHeight = 720.0f;
struct TickCycle {
    float left = 0.0f, top = 0.0f, scale = 1.0f;
    [[nodiscard]] float get_x(float x) const { return left + x * scale; }
    [[nodiscard]] float get_y(float y) const { return top + y * scale; }
    [[nodiscard]] float current_s(float v) const { return v * scale; }
    // Window point -> design point.
    [[nodiscard]] float design_x(float x) const { return (x - left) / scale; }
    [[nodiscard]] float design_y(float y) const { return (y - top) / scale; }
};
TickCycle fit_frame(float window_width, float window_height);

// The art over its box, as CSS's object-fit: cover - the part of the picture (texture coordinates u0 v0 u1 v1) that
// fills a box of `box_width` x `box_height`, centred, and panned: `pan` 0..1 slides the visible part across `pan_range`
// (a fraction of the picture, e.g. 0.03) - the picture is first zoomed by 1 + pan_range so there is room.
struct Uv {
    float u0, v0, u1, v1;
};
Uv cover_uv(float image_width, float image_height, float box_width, float box_height, float pan, float pan_range);

// --- motion --------------------------------------------------------------------------------------------------------

constexpr float kSelectSeconds = 0.12f; // the selection snaps in (ease-out)
constexpr float kWipeSeconds = 0.42f;   // a screen change: one diagonal wipe (the screen changes half-way)
constexpr float kLaunchSeconds = 0.55f; // a level confirmed: the name sliced, the art up through the gap
constexpr float kPanSeconds = 10.0f;    // the art pans across its range and back
float ease_out(float t);                // cubic: 1 - (1 - t)^3, t clamped to 0..1

// A row's selection amount: moves toward 1 (selected) or 0 at 1 / kSelectSeconds a second; drawn eased.
struct RowAnim {
    float value = 0.0f;
    void tick(bool selected, float dt);
    [[nodiscard]] float get_eased() const { return ease_out(value); }
};

// --- the menus -----------------------------------------------------------------------------------------------------

// The screens. The new front end's own screens are only those with no menu of the game's behind them - the settings
// and the level select -, opened by a menu's item (NewFrontend_Open); every menu is the game's, restyled
// (new_frontend_mirror.h). Main and Pause are the roots they open on (never drawn: back at one, ours goes aside).
enum class Screen : uint8_t {
    kMain,    // the root in the front end
    kOptions, // thug2-decomp.ini's settings (new_frontend_options.h) and the game's GAME OPTIONS
    kLevels,  // the level select (Free Skate, the pause menu's Change level, the Level Editor's level)
    kFreeze,  // the root in a level
};
constexpr int kScreens = 4;
const char* get_screen_name(Screen screen); // "main", "options", "levels", "pause" (the automation channel's names)
bool screen_from_name(const std::string& name, Screen* out);
bool is_pause_screen(Screen screen); // Pause - and Options / Levels when opened from it (over_pause)

// What the player did (the keyboard, a controller, the mouse - new_frontend.cpp turns them into these).
enum class Intake : uint8_t { kUp, kDown, kLeft, kRight, kConfirm, kBack, kPrevTab, kNextTab };

// What the menus ask the game for.
enum class ActionKind : uint8_t {
    kNone,
    kMoveSound,   // the selection moved (the menus' up / down sound)
    kChooseSound, // something chosen
    kBackSound,
    kGameOptions, // the game's GAME OPTIONS (in a level: the pause menu's)
    kOption,      // option: an index into Options::get_specs(), value: its choice
    kSkate,       // load; levels: what the level select was opened for
};
enum class LevelsFor : uint8_t { kFreeSkate, kFreeze, kEdit };
struct ActStep {
    ActionKind kind = ActionKind::kNone;
    std::string load;
    std::string id;
    int value = 0;
    int option = -1;
    LevelsFor levels = LevelsFor::kFreeSkate;
};

// The art for a menu item's id (ours - Mirror::menu_item_style) when levels.json's menu_art has none (THUG2's own loading
// screens).
std::string default_art(const std::string& item_id);

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
    void get_reset(const Catalogue* catalogue, std::vector<SectionView> views, const std::string& last_level,
                   std::vector<Screen> stack = {Screen::kMain}, LevelsFor levels = LevelsFor::kFreeSkate);
    void set_last_level(const std::string& load) { last_level_ = load; }
    // The sections' views made again (the locks changed): each section's row kept where it still is.
    void set_views(std::vector<SectionView> views);
    [[nodiscard]] const std::string& get_last_level() const { return last_level_; }
    // The options' choices, one per Options::get_specs() entry.
    void set_option_values(std::vector<int> choices) { options_ = std::move(choices); }
    [[nodiscard]] const std::vector<int>& option_values() const { return options_; }
    void set_option_group(int group);

    // A screen opened from outside the menus (with the wipe): the level select for `levels` when it's Levels.
    void unseal(Screen screen, LevelsFor levels = LevelsFor::kFreeSkate);

    // A key / button. The action, if any (a menu action at once; Skate only from Tick, after the launch).
    ActStep get_press(Intake input);
    // The mouse over an option row (its index), a level row, a game tab / option group; a click selects and chooses
    // (as Confirm); on a tab, the tab; on an option's choice, that choice.
    ActStep hover_item(int index);
    ActStep click_item(int index);
    ActStep hover_level(int row);
    ActStep click_level(int row);
    ActStep click_tab(int section);
    ActStep click_back();
    ActStep click_skate();
    ActStep click_choice(int row, int choice);
    // The wheel: notches up (+) or down (-).
    ActStep get_wheel(int notches);

    // Time passes: the animations; the launch's Skate when it's over.
    ActStep tick(float dt);

    // --- the automation channel ---
    // The shown screen's item `id` selected (a level's load script or name; an option's key or "game_options";
    // "tab:<section id>" / "group:<group id>"). False if it isn't there (or disabled).
    bool select_id(const std::string& id);
    // The shown screen's selection as such an id ("" if none).
    [[nodiscard]] std::string chosen_item_id() const;

    // --- what is drawn ---
    [[nodiscard]] Screen get_shown() const { return shown_; } // the screen drawn (changes half-way through a wipe)
    [[nodiscard]] Screen get_target() const { return pile_.empty() ? Screen::kMain : pile_.back(); }
    [[nodiscard]] int deepness() const { return static_cast<int>(pile_.size()); }
    // Whether the screens open are the pause menu's (Options / Levels opened from it are drawn over the game too).
    [[nodiscard]] bool over_pause() const;
    [[nodiscard]] int get_selected() const; // the shown list's selection (-1: none)
    [[nodiscard]] LevelsFor levels_purpose() const { return levels_for_; }
    [[nodiscard]] int get_section_index() const { return section_; }
    [[nodiscard]] int level_row() const;                  // the selected row in the section's list (-1: an empty list)
    [[nodiscard]] const SectionView* get_view() const;    // the section's view (null: no sections)
    [[nodiscard]] const StageMap* selected_level() const; // (null: none)
    [[nodiscard]] bool selected_locked() const;
    [[nodiscard]] int section_count() const { return static_cast<int>(views_.size()); }
    [[nodiscard]] const Catalogue* payload() const { return catalogue_; }
    // Options: the group shown, its rows (indices into Options::get_specs()) and the selected row (rows.size(): the
    // Game options row).
    [[nodiscard]] int option_group() const { return bundle_; }
    [[nodiscard]] const std::vector<int>& option_rows() const;
    [[nodiscard]] int option_row() const { return option_row_; }
    [[nodiscard]] int option_choice(int option) const;
    // The list's first row shown (animated toward keeping the selection in view): rows `visible_rows` tall.
    [[nodiscard]] float list_scroll() const { return scroll_; }
    void set_visible_rows(int rows) { visible_rows_ = rows > 0 ? rows : 1; }
    [[nodiscard]] int get_visible_rows() const { return visible_rows_; }
    // The rows' selection amounts (the current list).
    [[nodiscard]] float row_amount(int row) const;
    // The wipe under way: its 0..1 progress (-1: none) and its colour.
    [[nodiscard]] float wipe() const { return wipe_ < 0.0f ? -1.0f : wipe_ / kWipeSeconds; }
    [[nodiscard]] uint32_t wipe_colour() const { return wipe_colour_; }
    // The launch under way: its 0..1 progress (-1: none; it stays at 1 once Skate was given, until the next Reset),
    // and the level going.
    [[nodiscard]] float get_launch() const { return launch_ < 0.0f ? -1.0f : launch_ / kLaunchSeconds; }
    [[nodiscard]] const std::string& launch_load() const { return launch_load_; }
    // The game couldn't start the level: the launch undone (the menus take input again).
    void cancel_launch()
    {
        launch_ = -1.0f;
        launch_load_.clear();
    }
    [[nodiscard]] bool occupied() const { return wipe_ >= 0.0f || launch_ >= 0.0f; }
    // The art's pan, 0..1..0 over kPanSeconds * 2.
    [[nodiscard]] float get_pan() const;
    // The colour of the section shown (the house yellow outside the level select).
    [[nodiscard]] uint32_t accent_colour() const;

    static constexpr uint32_t kHouseYellow = 0xFFF2E205;

private:
    void get_go(Screen screen);
    void stack_off();
    void get_show(Screen screen); // (the wipe's half-way)
    [[nodiscard]] int list_count() const;
    ActStep move_to(int index);
    ActStep move_by(int step);
    ActStep move_level(int row);
    ActStep set_section(int section);
    ActStep change_option(int step, bool wrap);
    ActStep get_confirm();
    ActStep get_back();
    ActStep start_launch(const std::string& load);
    void clamp_scroll(bool snap);
    void reset_rows();

    const Catalogue* catalogue_ = nullptr;
    std::vector<SectionView> views_;
    std::vector<int> row_in_section_; // the row last selected in each section
    std::string last_level_;
    std::vector<Screen> pile_ = {Screen::kMain};
    Screen shown_ = Screen::kMain;
    LevelsFor levels_for_ = LevelsFor::kFreeSkate;
    int section_ = 0;
    std::vector<int> options_;
    int bundle_ = 0;
    int option_row_ = 0;
    float scroll_ = 0.0f, scroll_target_ = 0.0f;
    int visible_rows_ = 13;
    std::vector<RowAnim> rows_;
    float wipe_ = -1.0f;
    bool switched_ = false;
    uint32_t wipe_colour_ = kHouseYellow;
    float launch_ = -1.0f;
    std::string launch_load_;
    bool launch_fired_ = false;
    float clock_ = 0.0f;
};

// --- covering a change of screen -----------------------------------------------------------------------------------

// No original screen may show between ours and the next screen, nor on the way back (the user's report of 2026-10-25:
// GAME OPTIONS flashed the original THUG2 menu for a moment). While a cover is on, the last screen drawn
// (ours, or the restyled one) stays on screen with the original's elements hidden, until the target is ready: ours or a
// restyled screen up, the game playing (or a tool's own UI, a loading screen), or an original screen that stays
// original has stood still for kSettleSeconds; kMaxSeconds at most.
class Cover {
public:
    enum class Over : uint8_t { kNone, kOurs, kRestyled }; // what is drawn meanwhile
    static constexpr float kSettleSeconds = 0.25f;
    static constexpr float kMaxSeconds = 3.0f;
    struct Now {
        bool target_ready_ = false; // ours shown, or a restyled screen up
        bool gameplay = false; // in a level and playing, a tool's UI (the level editor), a loading screen, no level
        bool left_original_ = false; // the screen up stays the original's (not restyled: the decision says so)
        const void* screen = nullptr;      // the menu taking the pad events (0: none) - settled when it stays the same
    };
    void kickoff(Over over);
    void halt() { over_ = Over::kNone; }
    // A frame. Returns whether the cover is still on; `why` says why it ended (when it just did).
    bool tick_frame(const Now& now, float dt, const char** why = nullptr);
    [[nodiscard]] bool get_on() const { return over_ != Over::kNone; }
    [[nodiscard]] Over get_drawn() const { return over_; }
    [[nodiscard]] float get_instant() const { return clock_; }

private:
    Over over_ = Over::kNone;
    float clock_ = 0.0f, still_ = 0.0f;
    const void* screen_ = nullptr;
    bool seen_ = false;
};

} // namespace frontend::new_frontend
