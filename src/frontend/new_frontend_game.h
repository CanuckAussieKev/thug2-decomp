#pragma once

#include <cstdint>
#include <string>

namespace frontend {
class UiWidget;
}
namespace modules {
struct FrontEnd;
}

// NOT ORIGINAL - the game's screens as the new front end reads them (ours; see new_frontend.h): which of the game's
// menus is up (for the log and the automation channel's status), the game mode, whether it's a network session, an
// element by id, and a pad event launched at an element as the game's input logic does it. The new front end never
// stands in front of the game's menus and presses their items for the player (the user's 2026-10-26 rule): the game's
// scripts say what the menus hold, the new front end draws them (new_frontend_mirror.h).
namespace frontend::new_frontend::game_screens {

// The game's menus the new front end knows by name.
enum class Anchor {
    kNone,
    kMainMenu,      // the front end's main menu (main_vmenu: the overlay's roundbar menu or the game's wheel)
    kCareerOptions, // STORY MODE / CLASSIC MODE OPTIONS (career_options_vmenu)
    kCreateModes,   // CREATE-A-MODES (its menu_cas item)
    kSkateShop,     // free skate's skate shop menu after the skater select (ss_play_level, single session)
    kFreeze,         // a level's pause menu (pause_vmenu with its CONTINUE: free skate, story, classic)
};
const char* anchor_name(Anchor anchor);

// The menu up now (`frontEnd` for the pause state; any of it may be null).
Anchor at_hand(modules::FrontEnd* front_end);
// The game mode's name ("singlesession", "freeskate", "career", "classic", "net"...; "" before there is one).
std::string active_ruleset_label();
// A network game, online, or a network game mode: the new front end leaves those to the original screens.
bool net_session();

// An element of the screen tree (by id; null if none).
UiWidget* get_find(uint32_t id);

// A pad event (modules::FrontEnd::kPad...) launched at `element` as the game's input logic launches them (device_num, the
// first input handler's device; controller 0).
void launch_pad(modules::FrontEnd* front_end, uint32_t type, UiWidget* element);

} // namespace MenuSystem::NewFrontend::game_screens
