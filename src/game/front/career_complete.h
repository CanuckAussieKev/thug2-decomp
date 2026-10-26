#pragma once

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// NOT ORIGINAL - GAME OPTIONS' "CREATE 100% CAS" (ours, added at the user's request; not the game's and not
// PartyMod's): a Story Mode and Classic Mode career as a player who finished both completely (every goal, every gap)
// would have them, and the created skater's stats maxed. The option is script (tools/pipeline/menu_level/career_complete.ts writes it into the data overlay's
// levels\mainmenu\mainmenu_options.qb and does all it can with the game's own scripts and commands); this file is
// the two script commands it needs that the game doesn't have:
//
//   Career100_Available - true unless thug2-decomp.ini [features] create_100_cas=0 (the menu item shows while it
//     is true).
//   Career100_Apply levels=[{level=<level number> [points=<story points> total=<points potential>]
//                            [goals=[<story goal name> ...]] [classic_goals=[<classic goal name> ...]]
//                            [gaps=[{name="<gap>" score=<score>} ...]]} ...]
//     For each level (each part only when given):
//     - points / total: the career's (Mdl::Skate +0x20) points of that level (the cash a story goal's win adds -
//       0x005845C0) and its potential (the amount SetLevelTotalPoints sets - 0x005845F0) set, their totals changed
//       by the differences as those two do;
//     - goals / classic_goals: each goal as the goal manager (Mdl::Skate +0x38C) keeps a goal that isn't loaded
//       (its +0x14 structure, one structure per goal name - written by a goal's save at 0x00554770, read back into
//       the level's goals by GoalManager_LevelLoad 0x00558A00): levelNum, unlocked (0xD3E93882) 1, 0x2206B1E7 0,
//       hasBeaten 1, its record (0xC22A2B72: one already stored is kept, else 0), and the bare name `career` or,
//       for a classic goal, `classic` (as the save tags it). Classic goals also set the level's classic goal counter
//       (the career's +0x5DC + level: what a classic goal's win adds - IncrementCounter 0x00584620 from the goal win
//       0x005576D0) to their number; the counters' total (+0x5EE) is then summed;
//     - gaps: the level's gap checklist (the career's +0x63C + level * 4 - the one the level builds when it loads,
//       0x00578AC0, and the save keeps) gets each gap landed once (CGapChecklist::AddGap; gaps already there keep
//       their count, any left with none gets one), and the level is marked visited (+0x690 + level), so that
//       CSkaterCareer::AllGapsGot (0x00584BC0) holds once every level of levels_with_gaps is done.
//     Then the local skater's story stat challenges (its stats manager, component 0x4FF8DAC6 - stats_goals) all
//     marked done, so the stats menu allows the maxed stats (3 + the done ones per stat). False (nothing changed)
//     when switched off or without the skate module.
namespace Front::CareerComplete {

bool& Enabled();

bool __cdecl CFunc_Available(Script::CStruct* params, Script::CScript* script); // Career100_Available
bool __cdecl CFunc_Apply(Script::CStruct* params, Script::CScript* script);     // Career100_Apply

// Our script commands, registered after the game's own table (Sys::Main, after 0x005A5E70).
void RegisterScriptFunctions();

} // namespace Front::CareerComplete
