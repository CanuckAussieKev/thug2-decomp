#pragma once

namespace Scripts {
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
namespace ScriptCommands {

bool __cdecl CFunc_CareerStartLevel(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005AE990
bool __cdecl CFunc_CareerLevelIs(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005AE9C0
bool __cdecl CFunc_GetCareerLevelScore(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005AE9F0
bool __cdecl CFunc_SetLevelTotalPoints(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005AEAA0
bool __cdecl CFunc_GetClassicLevelGoalCount(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AEAE0
bool __cdecl CFunc_SetClassicTotalGoals(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AEB80
bool __cdecl CFunc_GetCurrentChapter(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005AEC20
bool __cdecl CFunc_SetCurrentChapter(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005AEC50
bool __cdecl CFunc_CareerReset(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AEC80
bool __cdecl CFunc_SetGoal(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005AECC0
bool __cdecl CFunc_UnSetGoal(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005AED10
bool __cdecl CFunc_GetGoal(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005AED40
bool __cdecl CFunc_JustGotGoal(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AED70
bool __cdecl CFunc_SetFlag(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005AEDA0
bool __cdecl CFunc_UnSetFlag(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005AEDD0
bool __cdecl CFunc_GetFlag(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005AEE30
bool __cdecl CFunc_JustGotFlag(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AEEA0
bool __cdecl CFunc_SetGlobalFlag(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005AEED0
bool __cdecl CFunc_UnSetGlobalFlag(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005AEF00
bool __cdecl CFunc_GetGlobalFlag(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005AEF30
bool __cdecl CFunc_ProfileEquals(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005AEF60
bool __cdecl CFunc_IsCareerMode(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005AEFC0
bool __cdecl CFunc_EndRun(Scripts::CStruct* params, Scripts::CScript* script);                  // 0x005AEFE0
bool __cdecl CFunc_ShouldEndRun(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005AEFF0
bool __cdecl CFunc_EndRunSelected(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AF020
bool __cdecl CFunc_AllSkatersAreIdle(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005AF030
bool __cdecl CFunc_FirstTrickStarted(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005AF040
bool __cdecl CFunc_FirstTrickCompleted(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005AF050
bool __cdecl CFunc_CalculateFinalScores(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AF060

} // namespace ScriptCommands
