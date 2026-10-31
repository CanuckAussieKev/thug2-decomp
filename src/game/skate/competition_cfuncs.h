#pragma once

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the game modes' runs, 0x005AF090-0x005AF74F and 0x005AFB40-0x005AFD7F: the split
// screen options, competitions (scores, places, rounds), horse (its runs, players, panel strings), the trick
// objects given away, the game mode's score options, a skater's bonus, whether there's a local skater. All are cdecl
// (params, script) and return whether the script's `if` should be taken (true where nothing is asked).
namespace ScriptCommands {

bool __cdecl CFunc_ApplySplitScreenOptions(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AF090
bool __cdecl CFunc_StartCompetition(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005AF200
bool __cdecl CFunc_StartCompetitionRun(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AF320
bool __cdecl CFunc_EndCompetitionRun(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005AF340
bool __cdecl CFunc_PlaceIs(Scripts::CStruct* params, Scripts::CScript* script);                // 0x005AF3A0
bool __cdecl CFunc_RoundIs(Scripts::CStruct* params, Scripts::CScript* script);                // 0x005AF3D0
bool __cdecl CFunc_EndCompetition(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005AF400
bool __cdecl CFunc_CompetitionEnded(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005AF440
bool __cdecl CFunc_StartHorse(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AF450
bool __cdecl CFunc_HorseEnded(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AF470
bool __cdecl CFunc_HorseStatusEquals(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005AF480
bool __cdecl CFunc_StartHorseRun(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AF4A0
bool __cdecl CFunc_EndHorseRun(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005AF4C0
bool __cdecl CFunc_SwitchHorsePlayers(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005AF4E0
bool __cdecl CFunc_ApplyToHorsePanelString(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AF500
bool __cdecl CFunc_GetHorseString(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005AF660
bool __cdecl CFunc_IsCurrentHorseSkater(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005AF700
bool __cdecl CFunc_TrickOffAllObjects(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005AFB40
bool __cdecl CFunc_SetScoreDegradation(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AFB80
bool __cdecl CFunc_SetTrickScoreTracking(Scripts::CStruct* params, Scripts::CScript* script);  // 0x005AFBC0
bool __cdecl CFunc_SetScoreAccumulation(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005AFC00
bool __cdecl CFunc_AddBonus(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005AFC70
bool __cdecl CFunc_GetBonusValue(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AFCF0
bool __cdecl CFunc_LocalSkaterExists(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005AFD60

} // namespace ScriptCommands
