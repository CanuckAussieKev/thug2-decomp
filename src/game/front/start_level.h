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

} // namespace Front::StartLevel
