#pragma once

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// NOT ORIGINAL - GAME OPTIONS' "CREATE 100% CAS" (ours, added at the user's request; not the game's and not
// PartyMod's): a Story Mode career as a player who finished it completely would have it, and the created skater's
// stats maxed. The option is script (tools/menu_level/career_complete.py writes it into the data overlay's
// levels\mainmenu\mainmenu_options.qb and does all it can with the game's own scripts and commands); this file is
// the two script commands it needs that the game doesn't have:
//
//   Career100_Available - true unless thug2-decomp.ini [features] create_100_cas=0 (the menu item shows while it
//     is true).
//   Career100_Apply levels=[{level=<level number> points=<story points> total=<points potential>
//                            goals=[<goal name> ...]} ...]
//     For each level: the career's (Mdl::Skate +0x20) points of that level (the cash a goal's win adds -
//     0x005845C0) and its potential (the amount SetLevelTotalPoints sets - 0x005845F0) set, their totals changed
//     by the differences as those two do; and each goal, as the goal manager (Mdl::Skate +0x38C) keeps a goal that
//     isn't loaded (its +0x14 structure, one structure per goal name - written by a goal's save at 0x00554770,
//     read back into the level's goals by GoalManager_LevelLoad 0x00558A00): levelNum, unlocked (0xD3E93882) 1,
//     0x2206B1E7 0, hasBeaten 1, its record (0xC22A2B72) 0, and the bare name `career`. Then the local skater's
//     story stat challenges (its stats manager, component 0x4FF8DAC6 - stats_goals) all marked done, so the stats
//     menu allows the maxed stats (3 + the done ones per stat). False (nothing changed) when switched off or
//     without the skate module.
namespace Front::CareerComplete {

bool& Enabled();

bool __cdecl CFunc_Available(Script::CStruct* params, Script::CScript* script); // Career100_Available
bool __cdecl CFunc_Apply(Script::CStruct* params, Script::CScript* script);     // Career100_Apply

// Our script commands, registered after the game's own table (Sys::Main, after 0x005A5E70).
void RegisterScriptFunctions();

} // namespace Front::CareerComplete
