#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the skater profiles and the players' options (0x005AAEF0-0x005B0A40, the profile
// ones): vibration, auto kick and spin taps per player (the skate module's +0x394 table, 8 bytes a player), the
// current pro's display name, selecting the current skater. All are cdecl (params, script) and return whether the
// script's `if` should be taken (true where nothing is asked).
namespace CFuncs {

bool __cdecl CFunc_VibrationIsOn(Script::CStruct* params, Script::CScript* script);           // 0x005AE370
bool __cdecl CFunc_VibrationOn(Script::CStruct* params, Script::CScript* script);             // 0x005AE3A0
bool __cdecl CFunc_VibrationOff(Script::CStruct* params, Script::CScript* script);            // 0x005AE3D0
bool __cdecl CFunc_AutoKickIsOn(Script::CStruct* params, Script::CScript* script);            // 0x005AE400
bool __cdecl CFunc_AutoKickOn(Script::CStruct* params, Script::CScript* script);              // 0x005AE430
bool __cdecl CFunc_AutoKickOff(Script::CStruct* params, Script::CScript* script);             // 0x005AE460
bool __cdecl CFunc_SpinTapsAreOn(Script::CStruct* params, Script::CScript* script);           // 0x005AE490
bool __cdecl CFunc_SpinTapsOn(Script::CStruct* params, Script::CScript* script);              // 0x005AE4C0
bool __cdecl CFunc_SpinTapsOff(Script::CStruct* params, Script::CScript* script);             // 0x005AE4F0
bool __cdecl CFunc_GetCurrentProDisplayInfo(Script::CStruct* params, Script::CScript* script); // 0x005AE520
bool __cdecl CFunc_SelectCurrentSkater(Script::CStruct* params, Script::CScript* script);     // 0x005AE900

} // namespace CFuncs
