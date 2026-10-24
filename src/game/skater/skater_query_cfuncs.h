#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) asking about the skaters, 0x005AC5F0-0x005AD82F: the local skater's speed and scores
// (last trick, total, the score pot), the network game's players (accumulated scores, elimination, who is left,
// places), the career's goal count, the skater camera's mode, skater ids and sex. All are cdecl (params, script) and
// return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_SkaterSpeedGreaterThan(Script::CStruct* params, Script::CScript* script);          // 0x005AC5F0
bool __cdecl CFunc_SkaterSpeedLessThan(Script::CStruct* params, Script::CScript* script);             // 0x005AC660
bool __cdecl CFunc_SkaterLastScoreLandedGreaterThan(Script::CStruct* params, Script::CScript* script); // 0x005AC6D0
bool __cdecl CFunc_SkaterLastScoreLandedLessThan(Script::CStruct* params, Script::CScript* script);    // 0x005AC720
bool __cdecl CFunc_SkaterGetLastScoreLanded(Script::CStruct* params, Script::CScript* script);        // 0x005AC770
bool __cdecl CFunc_AccumulateScores(Script::CStruct* params, Script::CScript* script);                // 0x005AC7B0
bool __cdecl CFunc_EliminateLastPlacePlayer(Script::CStruct* params, Script::CScript* script);        // 0x005AC850
bool __cdecl CFunc_OnlyOnePlayerLeft(Script::CStruct* params, Script::CScript* script);               // 0x005ACAA0
bool __cdecl CFunc_AnySkaterTotalScoreAtLeast(Script::CStruct* params, Script::CScript* script);      // 0x005ACC20
bool __cdecl CFunc_OnlyOneSkaterLeft(Script::CStruct* params, Script::CScript* script);               // 0x005ACD80
bool __cdecl CFunc_SkaterTotalScoreGreaterThan(Script::CStruct* params, Script::CScript* script);     // 0x005ACF50
bool __cdecl CFunc_SkaterTotalScoreLessThan(Script::CStruct* params, Script::CScript* script);        // 0x005ACFA0
bool __cdecl CFunc_SkaterCurrentScorePotGreaterThan(Script::CStruct* params, Script::CScript* script); // 0x005ACFF0
bool __cdecl CFunc_SkaterCurrentScorePotLessThan(Script::CStruct* params, Script::CScript* script);    // 0x005AD070
bool __cdecl CFunc_SkaterGetScoreInfo(Script::CStruct* params, Script::CScript* script);              // 0x005AD0D0
bool __cdecl CFunc_InFirstPlace(Script::CStruct* params, Script::CScript* script);                    // 0x005AD140
bool __cdecl CFunc_InLastPlace(Script::CStruct* params, Script::CScript* script);                     // 0x005AD2D0
bool __cdecl CFunc_InSecondToLastPlace(Script::CStruct* params, Script::CScript* script);             // 0x005AD450
bool __cdecl CFunc_GoalsGreaterThan(Script::CStruct* params, Script::CScript* script);                // 0x005AD5C0
bool __cdecl CFunc_GoalsEqualTo(Script::CStruct* params, Script::CScript* script);                    // 0x005AD610
bool __cdecl CFunc_ToggleSkaterCamMode(Script::CStruct* params, Script::CScript* script);             // 0x005AD660
bool __cdecl CFunc_GetSkaterID(Script::CStruct* params, Script::CScript* script);                     // 0x005AD6C0
bool __cdecl CFunc_SkaterExists(Script::CStruct* params, Script::CScript* script);                    // 0x005AD730
bool __cdecl CFunc_GetSkaterSex(Script::CStruct* params, Script::CScript* script);                    // 0x005AD780
bool __cdecl CFunc_GetCurrentSkaterID(Script::CStruct* params, Script::CScript* script);              // 0x005AD7E0

} // namespace CFuncs
