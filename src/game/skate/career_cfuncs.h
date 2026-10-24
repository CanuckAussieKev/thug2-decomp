#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the career and the run, 0x005AE990-0x005AF08F: the career's level, scores,
// classic goal counts and chapter, its goals, level flags and global flags, ProfileEquals, IsCareerMode, ending the
// run, the first trick, the final scores. All are cdecl (params, script) and return whether the script's `if` should
// be taken (true where nothing is asked).
//
// Where a command reads a parameter it asserts on (a compiled-out assert) and the parameter is missing, the original
// goes on with an uninitialised local (what the stack held): here 0.
namespace CFuncs {

bool __cdecl CFunc_CareerStartLevel(Script::CStruct* params, Script::CScript* script);        // 0x005AE990
bool __cdecl CFunc_CareerLevelIs(Script::CStruct* params, Script::CScript* script);           // 0x005AE9C0
bool __cdecl CFunc_GetCareerLevelScore(Script::CStruct* params, Script::CScript* script);     // 0x005AE9F0
bool __cdecl CFunc_SetLevelTotalPoints(Script::CStruct* params, Script::CScript* script);     // 0x005AEAA0
bool __cdecl CFunc_GetClassicLevelGoalCount(Script::CStruct* params, Script::CScript* script); // 0x005AEAE0
bool __cdecl CFunc_SetClassicTotalGoals(Script::CStruct* params, Script::CScript* script);    // 0x005AEB80
bool __cdecl CFunc_GetCurrentChapter(Script::CStruct* params, Script::CScript* script);       // 0x005AEC20
bool __cdecl CFunc_SetCurrentChapter(Script::CStruct* params, Script::CScript* script);       // 0x005AEC50
bool __cdecl CFunc_CareerReset(Script::CStruct* params, Script::CScript* script);             // 0x005AEC80
bool __cdecl CFunc_SetGoal(Script::CStruct* params, Script::CScript* script);                 // 0x005AECC0
bool __cdecl CFunc_UnSetGoal(Script::CStruct* params, Script::CScript* script);               // 0x005AED10
bool __cdecl CFunc_GetGoal(Script::CStruct* params, Script::CScript* script);                 // 0x005AED40
bool __cdecl CFunc_JustGotGoal(Script::CStruct* params, Script::CScript* script);             // 0x005AED70
bool __cdecl CFunc_SetFlag(Script::CStruct* params, Script::CScript* script);                 // 0x005AEDA0
bool __cdecl CFunc_UnSetFlag(Script::CStruct* params, Script::CScript* script);               // 0x005AEDD0
bool __cdecl CFunc_GetFlag(Script::CStruct* params, Script::CScript* script);                 // 0x005AEE30
bool __cdecl CFunc_JustGotFlag(Script::CStruct* params, Script::CScript* script);             // 0x005AEEA0
bool __cdecl CFunc_SetGlobalFlag(Script::CStruct* params, Script::CScript* script);           // 0x005AEED0
bool __cdecl CFunc_UnSetGlobalFlag(Script::CStruct* params, Script::CScript* script);         // 0x005AEF00
bool __cdecl CFunc_GetGlobalFlag(Script::CStruct* params, Script::CScript* script);           // 0x005AEF30
bool __cdecl CFunc_ProfileEquals(Script::CStruct* params, Script::CScript* script);           // 0x005AEF60
bool __cdecl CFunc_IsCareerMode(Script::CStruct* params, Script::CScript* script);            // 0x005AEFC0
bool __cdecl CFunc_EndRun(Script::CStruct* params, Script::CScript* script);                  // 0x005AEFE0
bool __cdecl CFunc_ShouldEndRun(Script::CStruct* params, Script::CScript* script);            // 0x005AEFF0
bool __cdecl CFunc_EndRunSelected(Script::CStruct* params, Script::CScript* script);          // 0x005AF020
bool __cdecl CFunc_AllSkatersAreIdle(Script::CStruct* params, Script::CScript* script);       // 0x005AF030
bool __cdecl CFunc_FirstTrickStarted(Script::CStruct* params, Script::CScript* script);       // 0x005AF040
bool __cdecl CFunc_FirstTrickCompleted(Script::CStruct* params, Script::CScript* script);     // 0x005AF050
bool __cdecl CFunc_CalculateFinalScores(Script::CStruct* params, Script::CScript* script);    // 0x005AF060

} // namespace CFuncs
