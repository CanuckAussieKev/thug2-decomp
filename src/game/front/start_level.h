#pragma once

#include <string>

namespace Mdl {
struct FrontEnd;
}

// NOT ORIGINAL - start in a level (ours, added at the user's request; not the game's and not PartyMod's): straight into
// free skate in a level instead of the main menu - for testing and automation.
//
//   THUG2PM.exe -level <name>                   (or --level <name>, --level=<name>)
//   thug2-decomp.ini [features] start_level=<name>   (the command line wins)
//
// <name>: a level's load script without its Load_ (zoo, z_dt, BO, TR, TestLevel, SkateShopLevel...) or with
// it (Load_BO). Once the main menu has been up a moment, what SKATE NOW does with a level chosen runs: EnableSun,
// SetGameType freeskate, SetCurrentGameType, level_select_change_level level=Load_<name> (as tools/game/boot_level.ts
// makes SKATE NOW do). Once per run; an unknown name, or a level whose scene file isn't installed (Default: the PC
// release ships only its scripts and sky), is logged and the main menu stays.
//
// Straight into the level editor (ours too, at the user's request - tools/game/edit_level.ts starts it):
//
//   THUG2.exe -edit <name>                      (or --edit <name>, --edit=<name>)
//   thug2-decomp.ini [features] edit_level=<name>    (the command line wins; -edit before -level, edit_level before
//                                                     start_level)
//
// As -level, and the intro movies are skipped for that run (Nx::SkipIntroMovies). Once the level is up - the skater
// there, not paused, a second and a half settled - Start is pressed for the player (the pad_start event the menus
// get: handle_start_pressed pauses the game and makes the pause menu), and once the pause menu is up, level_editor_enter
// runs - what the pause menu's LEVEL EDITOR item runs (game/front/level_editor.h). Leaving the editor is as usual (the
// pause menu, then free skate). With [features] level_editor=0, or without the overlay's level_editor_enter, that is
// logged and it stays -level (free skate); an unknown level stays the main menu, as with -level. Never in network
// games (free skate only).
//
// At a spot, standing (ours too, at the user's request: "launch the game in the exact spot, standing so I don't move,
// and I can try it" - tools/game/try_spot.ts starts it):
//
//   THUG2.exe -level <name> -at x,y,z[,heading]  (or --at x,y,z / --at=x,y,z; commas and / or spaces, quoted if spaces)
//   thug2-decomp.ini [features] start_at=x,y,z[,heading]   (the command line wins)
//
// With -level / -edit (or start_level / edit_level): once the level is up - the skater there, not paused, a second
// settled - the skater is put at (x, y, z) STANDING, as the test views' teleport does it (game/front/test_view.h: on
// the ground below the point, walking, no momentum, the camera behind it), facing `heading` (degrees, 0 = +z, 90 =
// +x - as the screenshot sidecar gives it) or the way it faced. Once per run, before -edit's editor opens. Without a
// level it is logged and ignored; never in network games (the level is loaded in free skate). A malformed spot is
// logged and ignored.
namespace Front::StartLevel {

// The level asked for ("" if none), and whether it came from -edit / edit_level (the editor opens there).
const char* Requested();
bool EditRequested();
// Once a frame from the front end's input logic (MouseMenus::Update).
void Update(Mdl::FrontEnd* frontEnd);
// What Update does once the menu is up: the level `name` (with or without Load_) in free skate. False (logged, and
// why in `why` if given) for an unknown level or one whose scene isn't installed. (Also the automation channel's
// `level` command - game/front/automation.h.)
bool Load(const char* name, std::string* why);

// The command line's option (pure, self-tested): the word after `-<option> `, `--<option> ` or `--<option>=` in
// `line` ("" if none; a quoted value keeps what is inside the quotes).
std::string OptionValue(const std::string& line, const char* option);

// -at / start_at (pure, self-tested): three or four numbers separated by commas and / or spaces (optionally in
// parentheses, as the screenshot sidecar writes a position). False for anything else: fewer than three or more than
// four, a word, an empty field ("1,,2,3"), a number that isn't finite.
struct Spot {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool hasHeading = false;
    float heading = 0.0f;
};
bool ParseSpot(const std::string& text, Spot* out);

} // namespace Front::StartLevel
