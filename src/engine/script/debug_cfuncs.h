#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) for debugging and engine odds and ends, 0x004489B0-0x00449960: the heaps' free
// memory display, a dump of the script value layouts, .qb loading, screenshots, metrics, the language tests,
// proximity cleanup, resetting the engine. All are cdecl (params, script) and return whether the script's `if`
// should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_DisplayFreeMem(Scripts::CStruct* params, Scripts::CScript* script); // 0x004489B0
bool __cdecl CFunc_Test(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00449110
bool __cdecl CFunc_LoadQB(Scripts::CStruct* params, Scripts::CScript* script);         // 0x004493B0
bool __cdecl CFunc_UnloadQB(Scripts::CStruct* params, Scripts::CScript* script);       // 0x00449450
bool __cdecl CFunc_ScreenShot(Scripts::CStruct* params, Scripts::CScript* script);     // 0x004494A0
bool __cdecl CFunc_ToggleMetrics(Scripts::CStruct* params, Scripts::CScript* script);  // 0x004494E0
bool __cdecl CFunc_German(Scripts::CStruct* params, Scripts::CScript* script);         // 0x004494F0
bool __cdecl CFunc_French(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00449500
bool __cdecl CFunc_Spanish(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00449510
bool __cdecl CFunc_Italian(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00449520
bool __cdecl CFunc_English(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00449530
bool __cdecl CFunc_ProximCleanup(Scripts::CStruct* params, Scripts::CScript* script);  // 0x004498F0
bool __cdecl CFunc_Inside(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00449950
bool __cdecl CFunc_ResetEngine(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00449960

// The .qb files loaded with LoadQB's LevelSpecific flag (0x00696900, 15 name checksums; 0 = a free slot).
uint32_t* LevelSpecificQbFiles();
constexpr int kLevelSpecificQbFiles = 15;

extern uint32_t g_levelQbFiles[15];

} // namespace ScriptCommands
