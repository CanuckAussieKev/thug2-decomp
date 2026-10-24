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

// --- cleaning up, object flags, skater scripts, pausing, the flash (game_flow_cfuncs.cpp) --------------------------

bool __cdecl CFunc_Cleanup(Script::CStruct* params, Script::CScript* script);         // 0x0059FE50
bool __cdecl CFunc_MakeSkaterGoto(Script::CStruct* params, Script::CScript* script);  // 0x005A0300
bool __cdecl CFunc_MakeSkaterGosub(Script::CStruct* params, Script::CScript* script); // 0x005A03C0
bool __cdecl CFunc_SpawnSound(Script::CStruct* params, Script::CScript* script);      // 0x005A0460
bool __cdecl CFunc_PauseSkater(Script::CStruct* params, Script::CScript* script);     // 0x005A0520
bool __cdecl CFunc_UnPauseSkater(Script::CStruct* params, Script::CScript* script);   // 0x005A0580
bool __cdecl CFunc_PauseGame(Script::CStruct* params, Script::CScript* script);       // 0x005A05C0
bool __cdecl CFunc_UnPauseGame(Script::CStruct* params, Script::CScript* script);     // 0x005A05E0
bool __cdecl CFunc_GameIsPaused(Script::CStruct* params, Script::CScript* script);    // 0x005A05F0
bool __cdecl CFunc_PauseObjects(Script::CStruct* params, Script::CScript* script);    // 0x005A0620
bool __cdecl CFunc_UnPauseObjects(Script::CStruct* params, Script::CScript* script);  // 0x005A0630
bool __cdecl CFunc_ResetClock(Script::CStruct* params, Script::CScript* script);      // 0x005A0640
bool __cdecl CFunc_PadsPluggedIn(Script::CStruct* params, Script::CScript* script);   // 0x005A0670
bool __cdecl CFunc_DoFlash(Script::CStruct* params, Script::CScript* script);         // 0x005A0680
bool __cdecl CFunc_ClearFlag(Script::CStruct* params, Script::CScript* script);       // 0x005A3B00
bool __cdecl CFunc_SendFlag(Script::CStruct* params, Script::CScript* script);        // 0x005A3B20
bool __cdecl CFunc_QueryFlag(Script::CStruct* params, Script::CScript* script);       // 0x005A3B40
bool __cdecl CFunc_SendException(Script::CStruct* params, Script::CScript* script);   // 0x005A3B60

// --- servers, the network mode and setup, going online, observing (net_state_cfuncs.cpp) ------------------------

bool __cdecl CFunc_StartServer(Script::CStruct* params, Script::CScript* script);              // 0x005A0880
bool __cdecl CFunc_LeaveServer(Script::CStruct* params, Script::CScript* script);              // 0x005A08D0
bool __cdecl CFunc_JoinServer(Script::CStruct* params, Script::CScript* script);               // 0x005A08E0
bool __cdecl CFunc_SetNetworkMode(Script::CStruct* params, Script::CScript* script);           // 0x005A0A00
bool __cdecl CFunc_ProfileTasks(Script::CStruct* params, Script::CScript* script);             // 0x005A0A60
bool __cdecl CFunc_UseNetworkPreferences(Script::CStruct* params, Script::CScript* script);    // 0x005A0AB0
bool __cdecl CFunc_TestNetSetup(Script::CStruct* params, Script::CScript* script);             // 0x005A0AC0
bool __cdecl CFunc_NeedToTestNetSetup(Script::CStruct* params, Script::CScript* script);       // 0x005A0BD0
bool __cdecl CFunc_CanChangeDevices(Script::CStruct* params, Script::CScript* script);         // 0x005A0BE0
bool __cdecl CFunc_LoadNetworkDrivers(Script::CStruct* params, Script::CScript* script);       // 0x005A0BF0
bool __cdecl CFunc_ConnectToInternet(Script::CStruct* params, Script::CScript* script);        // 0x005A0C60
bool __cdecl CFunc_CancelConnectToInternet(Script::CStruct* params, Script::CScript* script);  // 0x005A0CC0
bool __cdecl CFunc_IsOnline(Script::CStruct* params, Script::CScript* script);                 // 0x005A0CD0
bool __cdecl CFunc_DisconnectFromInternet(Script::CStruct* params, Script::CScript* script);   // 0x005A0CE0
bool __cdecl CFunc_EnableActuators(Script::CStruct* params, Script::CScript* script);          // 0x005A0D20
bool __cdecl CFunc_InNetGame(Script::CStruct* params, Script::CScript* script);                // 0x005A0D80
bool __cdecl CFunc_IsObserving(Script::CStruct* params, Script::CScript* script);              // 0x005A0D90
bool __cdecl CFunc_SetSlomo(Script::CStruct* params, Script::CScript* script);                 // 0x005A0E50
bool __cdecl CFunc_TogglePlayerNames(Script::CStruct* params, Script::CScript* script);        // 0x005A0EB0
bool __cdecl CFunc_SetCurrentGameType(Script::CStruct* params, Script::CScript* script);       // 0x005A0EF0

// What FlagCommandByName does to the objects: set / clear / test a script flag (of 32), set / clear object flag
// bits, send an exception.
enum : int {
    kFlagActionSet = 0,
    kFlagActionClear = 1,
    kFlagActionTest = 2,
    kFlagActionSetObjectFlags = 3,
    kFlagActionClearObjectFlags = 4,
    kFlagActionException = 5,
};
// 0x005A01D0: the flag (a number) or exception (a checksum) of `params`, done to the objects with the id name= or
// those of the nodes prefix=; false without either. Whether it was done (for a test: whether one has the flag).
bool __cdecl FlagCommandByName(Script::CStruct* params, Script::CScript* script, int action);
// 0x005A01B0: its manager walk's callback (`data`: {id, value, action, result, found}): the command done if the
// object has the id.
void __cdecl ApplyFlagCommandToObject(Obj::CObject* object, void* data);
// 0x005A0120: the command done to an object (`data` from the value on, in ESI; the object in EAX): the naked
// adapter, and our code behind it.
void ApplyFlagCommandStub();
void __cdecl ApplyFlagCommandRegisters(void* data, Obj::CObject* object);

} // namespace CFuncs
