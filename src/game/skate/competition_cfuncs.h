#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the game modes' runs, 0x005AF090-0x005AF74F and 0x005AFB40-0x005AFD7F: the split
// screen options, competitions (scores, places, rounds), horse (its runs, players, panel strings), the trick
// objects given away, the game mode's score options, a skater's bonus, whether there's a local skater. All are cdecl
// (params, script) and return whether the script's `if` should be taken (true where nothing is asked).
namespace CFuncs {

bool __cdecl CFunc_ApplySplitScreenOptions(Script::CStruct* params, Script::CScript* script); // 0x005AF090
bool __cdecl CFunc_StartCompetition(Script::CStruct* params, Script::CScript* script);       // 0x005AF200
bool __cdecl CFunc_StartCompetitionRun(Script::CStruct* params, Script::CScript* script);    // 0x005AF320
bool __cdecl CFunc_EndCompetitionRun(Script::CStruct* params, Script::CScript* script);      // 0x005AF340
bool __cdecl CFunc_PlaceIs(Script::CStruct* params, Script::CScript* script);                // 0x005AF3A0
bool __cdecl CFunc_RoundIs(Script::CStruct* params, Script::CScript* script);                // 0x005AF3D0
bool __cdecl CFunc_EndCompetition(Script::CStruct* params, Script::CScript* script);         // 0x005AF400
bool __cdecl CFunc_CompetitionEnded(Script::CStruct* params, Script::CScript* script);       // 0x005AF440
bool __cdecl CFunc_StartHorse(Script::CStruct* params, Script::CScript* script);             // 0x005AF450
bool __cdecl CFunc_HorseEnded(Script::CStruct* params, Script::CScript* script);             // 0x005AF470
bool __cdecl CFunc_HorseStatusEquals(Script::CStruct* params, Script::CScript* script);      // 0x005AF480
bool __cdecl CFunc_StartHorseRun(Script::CStruct* params, Script::CScript* script);          // 0x005AF4A0
bool __cdecl CFunc_EndHorseRun(Script::CStruct* params, Script::CScript* script);            // 0x005AF4C0
bool __cdecl CFunc_SwitchHorsePlayers(Script::CStruct* params, Script::CScript* script);     // 0x005AF4E0
bool __cdecl CFunc_ApplyToHorsePanelString(Script::CStruct* params, Script::CScript* script); // 0x005AF500
bool __cdecl CFunc_GetHorseString(Script::CStruct* params, Script::CScript* script);         // 0x005AF660
bool __cdecl CFunc_IsCurrentHorseSkater(Script::CStruct* params, Script::CScript* script);   // 0x005AF700
bool __cdecl CFunc_TrickOffAllObjects(Script::CStruct* params, Script::CScript* script);     // 0x005AFB40
bool __cdecl CFunc_SetScoreDegradation(Script::CStruct* params, Script::CScript* script);    // 0x005AFB80
bool __cdecl CFunc_SetTrickScoreTracking(Script::CStruct* params, Script::CScript* script);  // 0x005AFBC0
bool __cdecl CFunc_SetScoreAccumulation(Script::CStruct* params, Script::CScript* script);   // 0x005AFC00
bool __cdecl CFunc_AddBonus(Script::CStruct* params, Script::CScript* script);               // 0x005AFC70
bool __cdecl CFunc_GetBonusValue(Script::CStruct* params, Script::CScript* script);          // 0x005AFCF0
bool __cdecl CFunc_LocalSkaterExists(Script::CStruct* params, Script::CScript* script);      // 0x005AFD60

} // namespace CFuncs
