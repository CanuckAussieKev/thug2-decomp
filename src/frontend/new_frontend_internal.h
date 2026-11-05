#pragma once

// NOT ORIGINAL - what new_frontend.cpp's pieces share (the front end's state records, the game queries, the widget and
// ini helpers more than one piece uses). They were the one file's anonymous namespace; here they are in `internal`, and
// each piece that includes this has a using-directive for them (in the piece, not here: SF.7), so it sees them as it
// saw them there.

#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "frontend/new_frontend_draw.h"
#include "frontend/new_frontend_game.h"
#include "frontend/new_frontend_model.h"
#include "game/skate/career.h"
#include "game/skate/skate_module.h"
#include "engine/pad_input/input_event.h"
#include "imgui.h"
#include <gsl/pointers>

namespace frontend::new_frontend::internal {

using game_screens::Anchor;

uint32_t name_hash(const char* name);

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)

uint32_t current_load_state();

objects::RiderStoryProgress* get_career();

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

paint::Highscores records_for(const StageMap& level);

std::vector<SectionView> get_views(bool game_rules = false);

void load_catalogue();

bool cycle_level_art(int step);

void save_state();

void note_level_loaded();

std::string get_skater_name();

void get_sound(const char* script, const char* flag = nullptr);

const StageMap* level_played(const Section** section);

} // namespace frontend::new_frontend::internal
