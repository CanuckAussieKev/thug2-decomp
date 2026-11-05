#pragma once

// NOT ORIGINAL - what new_frontend.cpp's pieces share (the front end's state records, the game queries, the widget and
// ini helpers more than one piece uses). They were the one file's anonymous namespace; here they are in `internal`, and
// each piece that includes this has a using-directive for them (in the piece, not here: SF.7), so it sees them as it
// saw them there.

#include "frontend/new_frontend.h"
#include "engine/pad_input/input_source.h"
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <SDL2/SDL.h>
#include "game/front_end/front_end.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend_draw.h"
#include "frontend/new_frontend_game.h"
#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_model.h"
#include "engine/ui/screen_element.h"
#include "game/skate/career.h"
#include "game/skate/skate_module.h"
#include "game/rider/thaw_bmx.h"
#include "engine/pad_input/input_event.h"
#include "imgui.h"
#include <gsl/pointers>

namespace frontend::new_frontend::internal {

constexpr float kInvisibleThreshold = 0.001F;   // an element scaled or faded below this isn't there

constexpr int kMaxParentDepth = 64;             // the most containers walked up

void continue_last_level(modules::FrontEnd* front_end);

using game_screens::Anchor;

uint32_t name_hash(const char* name);

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)

uint32_t current_load_state();

bool in_front_end();

objects::RiderStoryProgress* get_career();

modules::FrontEnd* the_front_end();

bool is_script(uint32_t name);

int script_integer(const std::string& name);

std::string ini_path();

std::string ini_string(const char* key, const char* fallback);

paint::FontFiles resolve_fonts();

std::string read_text(const std::string& path);

bool write_atomically(const std::string& path, std::string_view text);

// --- the state ---

enum class State : uint8_t {
    kIdle,      // not up (a level being played, the game's screens restyled or as they are)
    kShown,     // our screen a menu's item asked for: drawn, with the input
    kAway,      // the game's screens up (restyled or as they are)
    kLaunching, // a level started: the art up until it loads
};

const char* get_state_name(State state);

// NOT ORIGINAL grouping - new_frontend.cpp's state (were globals). Made on first use, never destroyed
// (new_frontend_state()).
struct NewFrontendState {
    State state_ = State::kIdle;
    float state_time_ = 0.0F;
    Anchor anchor_ = Anchor::kNone; // the game's menu up when ours came up (the log, the status)
    // Our screen a menu's item asked for (NewFrontend_Open: the overlay's CHANGE LEVEL / OPTIONS / LEVEL EDITOR items, the
    // skate shop's Play Level) - shown once, over whatever is up, on our root (Main in the front end, Pause in a level);
    // back at the root, ours goes aside again (the restyled menu it came from under it, or `new_frontend_state().open_back_` spawned when that
    // menu went - the skate shop's).
    Screen open_screen_ = Screen::kMain; // (Main: none asked for)
    LevelsFor open_levels_ = LevelsFor::kFreeSkate;
    uint32_t open_back_ = 0; // (a script spawned when it is closed with Back; 0: none)
    uint32_t shown_back_ = 0; // (the one of the screen shown now)
    Screen open_root_ = Screen::kMain;
    bool root_hidden_ = false; // the original's screen elements hidden under our pause menu
    uint32_t launch_level_ = 0; // the level up when a launch started (in a level: done when it changes)
    Catalogue catalogue_{};
    bool catalogue_tried_ = false;
    bool catalogue_ok_ = false;
    SavedState saved_{};
    uint32_t last_load_ = 0;
    std::vector<input::Event> events_;
    std::vector<paint::Hit> hits_;
    bool guard_ = false; // (the game's input held back until the keys that chose are let go)
    float guard_time_ = 0.0F;
    int buttons_down_ = 0;
    std::string skater_name_;
};

NewFrontendState& new_frontend_state();

Menu& menu_state();

// Controller directions held (the d-pad, the left stick): repeated as the keyboard repeats.
struct Repeat {
    int direction = -1; // an Input (Up / Down / Left / Right), -1 none
    float wait = 0.0F;
};

// NOT ORIGINAL grouping - the menu stick's repeat and its last position (were globals). Made on first use, never
// destroyed (stick_repeat()).
struct StickRepeat {
    Repeat repeat_{};
    int stick_x_ = 0;
    int stick_y_ = 0;
};

StickRepeat& stick_repeat();

constexpr float kRepeatDelay = 0.35F, kRepeatEvery = 0.09F;

paint::Highscores records_for(const StageMap& level);

std::vector<SectionView> get_views(bool game_rules = false);

void load_catalogue();

bool cycle_level_art(int step);

void save_state();

void note_level_loaded();

std::string get_skater_name();

void get_sound(const char* script, const char* flag = nullptr);

const StageMap* level_played(const Section** section);

uint32_t id_of_element(const UiWidget* element);

UiWidget* first_child(const UiWidget* element);

UiWidget* get_next(const UiWidget* element);

UiWidget* active_menu(bool* in_dialog = nullptr, bool coming = false);

mirror::MenuRead read_menu(UiWidget* menu, bool in_dialog);

bool stats_screen_up(const UiWidget* menu);

mirror::MenuRead read_stats(UiWidget* active);

void dump_left_menu(UiWidget* menu, const std::string& why);

UiWidget* keyboard_of(UiWidget* menu);

mirror::MenuRead read_keyboard(UiWidget* active);

std::string pad_button_label_for(mirror::ButtonTag button_tag);

// NOT ORIGINAL grouping - the mirror of the game's menu: its hits, events, scroll and what was logged (the original's
// globals). Made on first use, never destroyed (menu_mirror()).
struct MenuMirror {
    mirror::Tracker mirror_{};
    std::vector<paint::Hit> mirror_hits_;
    std::vector<input::Event> mirror_events_;
    float mirror_scroll_ = 0.0F;
    // Set while the original's screens are those of a hand-off left to them entirely: the create modes' editors (CAS,
    // CAT, CAG, the park editor, graphics) and online - nothing restyled until ours is up again.
    const char* left_original_ = nullptr;
    std::string mirror_logged_;
};

MenuMirror& menu_mirror();

void mirror_mouse(modules::FrontEnd* front_end);

void run_action(modules::FrontEnd* front_end, const ActStep& action);

bool get_key_down(input::KeyState keys, SDL_Scancode key);

bool left_button_down();

void press_direction(modules::FrontEnd* front_end, Intake input);

void handle_queued(modules::FrontEnd* front_end, const input::Event& widget);

void handle_event(const input::Event& event);

// --- the automation channel's commands ---

struct Awaiting {
    bool active = false;
    bool confirm = false;
    Screen screen = Screen::kMain;
    std::string item;
    std::string result; // (set once done)
};

Awaiting& stored_pending();

bool shown_and_idle(Screen screen);

bool get_refused(Screen screen, const std::string& item);

} // namespace frontend::new_frontend::internal
