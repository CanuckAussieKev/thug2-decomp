#pragma once

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) asking about the skaters, 0x005AC5F0-0x005AD82F: the local skater's speed and scores
// (last trick, total, the score pot), the network game's players (accumulated scores, elimination, who is left,
// places), the career's goal count, the skater camera's mode, skater ids and sex. All are cdecl (params, script) and
// return whether the script's `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_SkaterSpeedGreaterThan(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AC5F0
bool __cdecl CFunc_SkaterSpeedLessThan(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AC660
bool __cdecl CFunc_SkaterLastScoreLandedGreaterThan(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AC6D0
bool __cdecl CFunc_SkaterLastScoreLandedLessThan(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AC720
bool __cdecl CFunc_SkaterGetLastScoreLanded(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005AC770
bool __cdecl CFunc_AccumulateScores(Scripts::CStruct* params, Scripts::CScript* script);                // 0x005AC7B0
bool __cdecl CFunc_EliminateLastPlacePlayer(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005AC850
bool __cdecl CFunc_OnlyOnePlayerLeft(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005ACAA0
bool __cdecl CFunc_AnySkaterTotalScoreAtLeast(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005ACC20
bool __cdecl CFunc_OnlyOneSkaterLeft(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005ACD80
bool __cdecl CFunc_SkaterTotalScoreGreaterThan(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005ACF50
bool __cdecl CFunc_SkaterTotalScoreLessThan(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005ACFA0
bool __cdecl CFunc_SkaterCurrentScorePotGreaterThan(Scripts::CStruct* params, Scripts::CScript* script); // 0x005ACFF0
bool __cdecl CFunc_SkaterCurrentScorePotLessThan(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AD070
bool __cdecl CFunc_SkaterGetScoreInfo(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005AD0D0
bool __cdecl CFunc_InFirstPlace(Scripts::CStruct* params, Scripts::CScript* script);                    // 0x005AD140
bool __cdecl CFunc_InLastPlace(Scripts::CStruct* params, Scripts::CScript* script);                     // 0x005AD2D0
bool __cdecl CFunc_InSecondToLastPlace(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AD450
bool __cdecl CFunc_GoalsGreaterThan(Scripts::CStruct* params, Scripts::CScript* script);                // 0x005AD5C0
bool __cdecl CFunc_GoalsEqualTo(Scripts::CStruct* params, Scripts::CScript* script);                    // 0x005AD610
bool __cdecl CFunc_ToggleSkaterCamMode(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AD660
bool __cdecl CFunc_GetSkaterID(Scripts::CStruct* params, Scripts::CScript* script);                     // 0x005AD6C0
bool __cdecl CFunc_SkaterExists(Scripts::CStruct* params, Scripts::CScript* script);                    // 0x005AD730
bool __cdecl CFunc_GetSkaterSex(Scripts::CStruct* params, Scripts::CScript* script);                    // 0x005AD780
bool __cdecl CFunc_GetCurrentSkaterID(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005AD7E0
bool __cdecl CFunc_GetSkaterPosition(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005B0070
bool __cdecl CFunc_SetSkaterVelocity(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005B00F0

} // namespace ScriptCommands
