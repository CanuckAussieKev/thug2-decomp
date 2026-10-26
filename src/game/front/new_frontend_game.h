#pragma once

#include <cstdint>
#include <string>

namespace Front {
class CScreenElement;
}
namespace Mdl {
struct FrontEnd;
}

// NOT ORIGINAL - the game's screens as the new front end reads them (ours; see new_frontend.h): which of the game's
// menus is up (for the log and the automation channel's status), the game mode, whether it's a network session, an
// element by id, and a pad event launched at an element as the game's input logic does it. The new front end never
// stands in front of the game's menus and presses their items for the player (the user's 2026-10-26 rule): the game's
// scripts say what the menus hold, the new front end draws them (new_frontend_mirror.h).
namespace Front::NewFrontend::GameScreens {

// The game's menus the new front end knows by name.
enum class Anchor {
    None,
    MainMenu,      // the front end's main menu (main_vmenu: the overlay's roundbar menu or the game's wheel)
    CareerOptions, // STORY MODE / CLASSIC MODE OPTIONS (career_options_vmenu)
    CreateModes,   // CREATE-A-MODES (its menu_cas item)
    SkateShop,     // free skate's skate shop menu after the skater select (ss_play_level, single session)
    Pause,         // a level's pause menu (pause_vmenu with its CONTINUE: free skate, story, classic)
};
const char* AnchorName(Anchor anchor);

// The menu up now (`frontEnd` for the pause state; any of it may be null).
Anchor Current(Mdl::FrontEnd* frontEnd);
// The game mode's name ("singlesession", "freeskate", "career", "classic", "net"...; "" before there is one).
std::string GameModeName();
// A network game, online, or a network game mode: the new front end leaves those to the original screens.
bool NetSession();

// An element of the screen tree (by id; null if none).
CScreenElement* Find(uint32_t id);

// A pad event (Mdl::FrontEnd::kPad...) launched at `element` as the game's input logic launches them (device_num, the
// first input handler's device; controller 0).
void LaunchPad(Mdl::FrontEnd* frontEnd, uint32_t type, CScreenElement* element);

} // namespace Front::NewFrontend::GameScreens
