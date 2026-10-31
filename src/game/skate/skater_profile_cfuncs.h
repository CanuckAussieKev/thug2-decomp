#pragma once

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the skater profiles and the players' options (0x005AAEF0-0x005B0A40, the profile
// ones): vibration, auto kick and spin taps per player (the skate module's +0x394 table, 8 bytes a player), the
// current pro's display name, selecting the current skater. All are cdecl (params, script) and return whether the
// script's `if` should be taken (true where nothing is asked).
namespace ScriptCommands {

bool __cdecl CFunc_RestoreTemporaryAppearance(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AAEF0
bool __cdecl CFunc_SyncPlayer2Profile(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005AAF90
bool __cdecl CFunc_ResetAllToDefaultStats(Scripts::CStruct* params, Scripts::CScript* script);  // 0x005ABBF0
bool __cdecl CFunc_ResetToDefaultProfile(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005ABE30
bool __cdecl CFunc_ResetAllToDefaultProfile(Scripts::CStruct* params, Scripts::CScript* script); // 0x005ABF00
bool __cdecl CFunc_ForEachSkaterName(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005ABF70
bool __cdecl CFunc_ForEachSkaterProfile(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AC0C0
bool __cdecl CFunc_GetSkaterProfileInfoByName(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AC1F0
bool __cdecl CFunc_SetSkaterProfileInfoByName(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AC280
bool __cdecl CFunc_PrintCurrentAppearance(Scripts::CStruct* params, Scripts::CScript* script);  // 0x005AC320
bool __cdecl CFunc_SetNeversoftSkater(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005AC340
bool __cdecl CFunc_ResetSkaters(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005AC3B0
bool __cdecl CFunc_SetSkaterProfileInfo(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AC410
bool __cdecl CFunc_GetSkaterProfileInfo(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AC470
bool __cdecl CFunc_SetSkaterProfileProperty(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AC4B0
bool __cdecl CFunc_SetClassicStat(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AC520
bool __cdecl CFunc_ToggleAlwaysSpecial(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005AC570
bool __cdecl CFunc_VibrationIsOn(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005AE370
bool __cdecl CFunc_VibrationOn(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AE3A0
bool __cdecl CFunc_VibrationOff(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005AE3D0
bool __cdecl CFunc_AutoKickIsOn(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005AE400
bool __cdecl CFunc_AutoKickOn(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005AE430
bool __cdecl CFunc_AutoKickOff(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AE460
bool __cdecl CFunc_SpinTapsAreOn(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005AE490
bool __cdecl CFunc_SpinTapsOn(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005AE4C0
bool __cdecl CFunc_SpinTapsOff(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AE4F0
bool __cdecl CFunc_GetCurrentProDisplayInfo(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AE520
bool __cdecl CFunc_SelectCurrentSkater(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005AE900
bool __cdecl CFunc_AwardStatPoint(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AF950
bool __cdecl CFunc_AwardSpecialTrickSlot(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005AF9F0
bool __cdecl CFunc_UpdateSkaterStats(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005AFAA0
bool __cdecl CFunc_GetCurrentSkaterProfileIndex(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AFFE0
bool __cdecl CFunc_GetCustomSkaterName(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005B0000
bool __cdecl CFunc_GetCurrentPedProName(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005B0030
bool __cdecl CFunc_GetStatValue(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005B0280
bool __cdecl CFunc_GetNumStatPointsAvailable(Scripts::CStruct* params, Scripts::CScript* script); // 0x005B0360
bool __cdecl CFunc_UnlockSkater(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005B03B0

} // namespace ScriptCommands
