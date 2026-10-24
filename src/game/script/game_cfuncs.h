#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script
namespace Obj {
class CObject;
}

// Script commands (CFunctions) of the skate game, 0x0059F610-0x005A5360 (after skate_cfuncs.h's): the viewer,
// controllers, preferences, the asynchronous loads, pausing, the game flow, the network state (hosting, joining,
// the server list, observers), the game modes, debouncing, objects made from structures. All are cdecl
// (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

// --- the viewer, controllers, node arrays, preferences (game_cfuncs.cpp) --------------------------------------

bool __cdecl CFunc_SetViewerAnim(Script::CStruct* params, Script::CScript* script);            // 0x0059F610
bool __cdecl CFunc_SetViewerLODDist(Script::CStruct* params, Script::CScript* script);         // 0x0059F650
bool __cdecl CFunc_ReloadViewerAnim(Script::CStruct* params, Script::CScript* script);         // 0x0059F6C0
bool __cdecl CFunc_CheckForHoles(Script::CStruct* params, Script::CScript* script);            // 0x0059F730
bool __cdecl CFunc_StopSecondControllerCheck(Script::CStruct* params, Script::CScript* script); // 0x0059F740
bool __cdecl CFunc_QuitGame(Script::CStruct* params, Script::CScript* script);                 // 0x0059F760
bool __cdecl CFunc_IsInternetGameHost(Script::CStruct* params, Script::CScript* script);       // 0x0059F770
bool __cdecl CFunc_IsJoiningInternetGame(Script::CStruct* params, Script::CScript* script);    // 0x0059F780
bool __cdecl CFunc_TryingToJoinGame(Script::CStruct* params, Script::CScript* script);         // 0x0059F7A0
bool __cdecl CFunc_TryJoinServerIPSuccess(Script::CStruct* params, Script::CScript* script);   // 0x0059F7B0
bool __cdecl CFunc_GSEnableNet(Script::CStruct* params, Script::CScript* script);              // 0x0059F7E0
bool __cdecl CFunc_GSDisableNet(Script::CStruct* params, Script::CScript* script);             // 0x0059F7F0
bool __cdecl CFunc_WaitForAsyncLoadToFinish(Script::CStruct* params, Script::CScript* script); // 0x0059F800
bool __cdecl CFunc_GetCustomSkaterFilename(Script::CStruct* params, Script::CScript* script);  // 0x0059F930
bool __cdecl CFunc_SetCustomSkaterFilename(Script::CStruct* params, Script::CScript* script);  // 0x0059F960
bool __cdecl CFunc_ControllerPressed(Script::CStruct* params, Script::CScript* script);        // 0x0059F9A0
bool __cdecl CFunc_GetAnalogueInfo(Script::CStruct* params, Script::CScript* script);          // 0x0059FA40
bool __cdecl CFunc_NodeArrayManLoad(Script::CStruct* params, Script::CScript* script);         // 0x0059FB30
bool __cdecl CFunc_NodeArrayManUnload(Script::CStruct* params, Script::CScript* script);       // 0x0059FB90
bool __cdecl CFunc_GetPreferenceString(Script::CStruct* params, Script::CScript* script);      // 0x0059FBC0
bool __cdecl CFunc_GetPreferencePassword(Script::CStruct* params, Script::CScript* script);    // 0x0059FC40
bool __cdecl CFunc_GetPreferenceChecksum(Script::CStruct* params, Script::CScript* script);    // 0x0059FD00
bool __cdecl CFunc_ToggleNetMetrics(Script::CStruct* params, Script::CScript* script);         // 0x0059FD80
bool __cdecl CFunc_SetViewMode(Script::CStruct* params, Script::CScript* script);              // 0x0059FD90

// 0x0059F820: the second controller check's task (SpawnSecondControllerCheck): runs the script 0x07C28B45 when
// both controllers are plugged in, else 0x9C9DFCF8 - every frame while the task is on.
void __cdecl SecondControllerCheckTask(void* task);
// 0x0059F890: make the camera of the object `id` (its 0xC4E311FA component) the camera of `viewport`; with
// `takeCurrentView`, the object first moves to where the viewport's camera is now (and the component is told).
// False if there's no such object.
bool __cdecl MakeCameraActive(uint32_t id, int viewport, bool takeCurrentView);
// 0x0059FB10: a manager walk's callback: the object deletes itself (unless locked) if its id is `*id`.
void __cdecl DestroyObjectWithId(Obj::CObject* object, void* id);

} // namespace CFuncs
