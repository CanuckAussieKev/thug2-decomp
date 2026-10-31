#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the skate game, 0x0059EDF0-0x0059F610: controller debounce, fake lights,
// nodes, the viewer, spawning and killing scripts, the loading screen, Omnigon, the level name, DumpScripts.
// All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace ScriptCommands {

// The controller's input state (the object at the controller's +0x18): one byte per button, non-zero = held.
struct PadInputSample {
    uint8_t unknown0[4];
    uint8_t right; // +0x04
    uint8_t left;  // +0x05
    uint8_t up;    // +0x06
    uint8_t down;  // +0x07
    uint8_t triangle; // +0x08
    uint8_t circle;   // +0x09
    uint8_t x;        // +0x0A
    uint8_t square;   // +0x0B
    uint8_t l1;       // +0x0C
    uint8_t r1;       // +0x0D
    uint8_t l2;       // +0x0E
    uint8_t r2;       // +0x0F
    uint8_t unknown10[6];
    uint8_t black; // +0x16
    uint8_t white; // +0x17
    uint8_t z;     // +0x18
};

// The per-button debounce times (0x007CE758, 22 entries, indexed like the button table 0x0067D6D8): a button
// doesn't count as pressed until the game clock (ms) reaches its time.
uint32_t* DebounceTimes();

// 0x0059EE80: whether `button` (a checksum: up, down, left, right, the diagonals, x, triangle, square,
// circle, l1, l2, r1, r2, black, white, z) is held on `controller` (its input state at +0x18) and isn't being
// debounced. A diagonal is held when both its directions are. Unknown buttons: false.
bool __cdecl ControllerPressed(const void* controller, uint32_t button);

bool __cdecl CFunc_ControllerDebounce(Scripts::CStruct* params, Scripts::CScript* script);   // 0x0059EDF0
bool __cdecl CFunc_FakeLights(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0059F0F0
bool __cdecl CFunc_UpdateFxLighting(Scripts::CStruct* params, Scripts::CScript* script);     // 0x0059F1B0
bool __cdecl CFunc_NodeExists(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0059F1C0
bool __cdecl CFunc_LoadNodeArray(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0059F1F0
bool __cdecl CFunc_ResetCamera(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0059F260
bool __cdecl CFunc_SetMovementVelocity(Scripts::CStruct* params, Scripts::CScript* script);  // 0x0059F290
bool __cdecl CFunc_SetRotateVelocity(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0059F2C0
bool __cdecl CFunc_ClearViewerObject(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0059F2F0
bool __cdecl CFunc_CasArtist(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0059F310
bool __cdecl CFunc_SpawnScript(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0059F330
bool __cdecl CFunc_UnBlockInterruptedScript(Scripts::CStruct* params, Scripts::CScript* script); // 0x0059F440
bool __cdecl CFunc_KillSpawnedScript(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0059F450
bool __cdecl CFunc_PauseSpawnedScripts(Scripts::CStruct* params, Scripts::CScript* script);  // 0x0059F4C0
bool __cdecl CFunc_UnPauseSpawnedScripts(Scripts::CStruct* params, Scripts::CScript* script); // 0x0059F4D0
bool __cdecl CFunc_DisplayLoadingScreen(Scripts::CStruct* params, Scripts::CScript* script); // 0x0059F4E0
bool __cdecl CFunc_HideLoadingScreen(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0059F550
bool __cdecl CFunc_LastBroadcastedOmnigonWas(Scripts::CStruct* params, Scripts::CScript* script); // 0x0059F560
bool __cdecl CFunc_OmnigonAllowed(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0059F590
bool __cdecl CFunc_SetLevelName(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0059F5B0
bool __cdecl CFunc_DumpScripts(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0059F5E0

extern char g_nodeListName[128];

} // namespace ScriptCommands
