#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) for debugging and engine odds and ends, 0x004489B0-0x00449960: the heaps' free
// memory display, a dump of the script value layouts, .qb loading, screenshots, metrics, the language tests,
// proximity cleanup, resetting the engine. All are cdecl (params, script) and return whether the script's `if`
// should be taken.
namespace CFuncs {

bool __cdecl CFunc_DisplayFreeMem(Script::CStruct* params, Script::CScript* script); // 0x004489B0
bool __cdecl CFunc_Test(Script::CStruct* params, Script::CScript* script);           // 0x00449110
bool __cdecl CFunc_LoadQB(Script::CStruct* params, Script::CScript* script);         // 0x004493B0
bool __cdecl CFunc_UnloadQB(Script::CStruct* params, Script::CScript* script);       // 0x00449450
bool __cdecl CFunc_ScreenShot(Script::CStruct* params, Script::CScript* script);     // 0x004494A0
bool __cdecl CFunc_ToggleMetrics(Script::CStruct* params, Script::CScript* script);  // 0x004494E0
bool __cdecl CFunc_German(Script::CStruct* params, Script::CScript* script);         // 0x004494F0
bool __cdecl CFunc_French(Script::CStruct* params, Script::CScript* script);         // 0x00449500
bool __cdecl CFunc_Spanish(Script::CStruct* params, Script::CScript* script);        // 0x00449510
bool __cdecl CFunc_Italian(Script::CStruct* params, Script::CScript* script);        // 0x00449520
bool __cdecl CFunc_English(Script::CStruct* params, Script::CScript* script);        // 0x00449530
bool __cdecl CFunc_ProximCleanup(Script::CStruct* params, Script::CScript* script);  // 0x004498F0
bool __cdecl CFunc_Inside(Script::CStruct* params, Script::CScript* script);         // 0x00449950
bool __cdecl CFunc_ResetEngine(Script::CStruct* params, Script::CScript* script);    // 0x00449960

// The .qb files loaded with LoadQB's LevelSpecific flag (0x00696900, 15 name checksums; 0 = a free slot).
uint32_t* LevelSpecificQbFiles();
constexpr int kLevelSpecificQbFiles = 15;

} // namespace CFuncs
