#pragma once

namespace Mdl {
struct FrontEnd;
}

// NOT ORIGINAL - start in a level (ours, added at the user's request; not the game's and not PartyMod's): straight into
// free skate in a level instead of the main menu - for testing and automation.
//
//   THUG2PM.exe -level <name>                   (or --level <name>, --level=<name>)
//   thug2-decomp.ini [features] start_level=<name>   (the command line wins)
//
// <name>: a level's load script without its Load_ (thps4_zoo, thaw_dt, BO, TR, TestLevel, SkateShopLevel...) or with
// it (Load_BO). Once the main menu has been up a moment, what SKATE NOW does with a level chosen runs: EnableSun,
// SetGameType freeskate, SetCurrentGameType, level_select_change_level level=Load_<name> (as tools/game/boot_level.py
// makes SKATE NOW do). Once per run; an unknown name is logged and the main menu stays.
namespace Front::StartLevel {

// The level asked for ("" if none).
const char* Requested();
// Once a frame from the front end's input logic (MouseMenus::Update).
void Update(Mdl::FrontEnd* frontEnd);

} // namespace Front::StartLevel
