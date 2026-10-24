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
bool __cdecl CFunc_SetViewerModel(Script::CStruct* params, Script::CScript* script);           // 0x005A1C80
bool __cdecl CFunc_GetViewerObjectID(Script::CStruct* params, Script::CScript* script);        // 0x005A1D40
bool __cdecl CFunc_ReloadAnim(Script::CStruct* params, Script::CScript* script);               // 0x005A1D80
bool __cdecl CFunc_Debounce(Script::CStruct* params, Script::CScript* script);                 // 0x005A1E70
bool __cdecl CFunc_StartKeyboardHandler(Script::CStruct* params, Script::CScript* script);     // 0x005A2050
bool __cdecl CFunc_StopKeyboardHandler(Script::CStruct* params, Script::CScript* script);      // 0x005A2080
bool __cdecl CFunc_SpawnSecondControllerCheck(Script::CStruct* params, Script::CScript* script); // 0x005A2100
bool __cdecl CFunc_MoveNode(Script::CStruct* params, Script::CScript* script);                 // 0x005A21C0
bool __cdecl CFunc_SetActiveCamera(Script::CStruct* params, Script::CScript* script);          // 0x005A2370
bool __cdecl CFunc_ShowTracking(Script::CStruct* params, Script::CScript* script);             // 0x005A23E0
bool __cdecl CFunc_ChooseFaceForFacemap(Script::CStruct* params, Script::CScript* script);     // 0x005A2790
bool __cdecl CFunc_SkaterIsCustom(Script::CStruct* params, Script::CScript* script);           // 0x005A29A0
bool __cdecl CFunc_SetSceneColor(Script::CStruct* params, Script::CScript* script);            // 0x005A29E0

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
bool __cdecl CFunc_DumpNetMessageStats(Script::CStruct* params, Script::CScript* script);      // 0x005A0F00
bool __cdecl CFunc_SetServerMode(Script::CStruct* params, Script::CScript* script);            // 0x005A1020
bool __cdecl CFunc_NotifyBailDone(Script::CStruct* params, Script::CScript* script);           // 0x005A1050
bool __cdecl CFunc_EnterObserverMode(Script::CStruct* params, Script::CScript* script);        // 0x005A10D0
bool __cdecl CFunc_ObserveNextSkater(Script::CStruct* params, Script::CScript* script);        // 0x005A10E0
bool __cdecl CFunc_RefreshServerList(Script::CStruct* params, Script::CScript* script);        // 0x005A10F0
bool __cdecl CFunc_StartServerList(Script::CStruct* params, Script::CScript* script);          // 0x005A1120
bool __cdecl CFunc_StartLobbyList(Script::CStruct* params, Script::CScript* script);           // 0x005A1130
bool __cdecl CFunc_StopServerList(Script::CStruct* params, Script::CScript* script);           // 0x005A1160
bool __cdecl CFunc_FreeServerList(Script::CStruct* params, Script::CScript* script);           // 0x005A1170
// (these two return 1 or 0 in all of EAX: some of their callers read it)
int __cdecl CFunc_InSplitScreenGame(Script::CStruct* params, Script::CScript* script);         // 0x005A1180
int __cdecl CFunc_InMultiplayerGame(Script::CStruct* params, Script::CScript* script);         // 0x005A11B0
bool __cdecl CFunc_OnServer(Script::CStruct* params, Script::CScript* script);                 // 0x005A1220
bool __cdecl CFunc_ToggleScores(Script::CStruct* params, Script::CScript* script);             // 0x005A12D0

// --- game modes and the level (game_flow_cfuncs.cpp) ------------------------------------------------------------

bool __cdecl CFunc_RoundIncrement(Script::CStruct* params, Script::CScript* script);           // 0x005A11C0
bool __cdecl CFunc_GameModeEquals(Script::CStruct* params, Script::CScript* script);           // 0x005A11D0
bool __cdecl CFunc_GetCurrentLevel(Script::CStruct* params, Script::CScript* script);          // 0x005A1230
bool __cdecl CFunc_SetLevelStructureName(Script::CStruct* params, Script::CScript* script);    // 0x005A1270
bool __cdecl CFunc_GetLevelStructureName(Script::CStruct* params, Script::CScript* script);    // 0x005A12A0
bool __cdecl CFunc_RestartLevel(Script::CStruct* params, Script::CScript* script);             // 0x005A12C0
bool __cdecl CFunc_GameFlow(Script::CStruct* params, Script::CScript* script);                 // 0x005A12E0
bool __cdecl CFunc_ClearOmnigons(Script::CStruct* params, Script::CScript* script);            // 0x005A1320
bool __cdecl CFunc_BroadcastOmnigon(Script::CStruct* params, Script::CScript* script);         // 0x005A1540
bool __cdecl CFunc_SetScreenMode(Script::CStruct* params, Script::CScript* script);            // 0x005A1720
bool __cdecl CFunc_SetScreenModeFromGameMode(Script::CStruct* params, Script::CScript* script); // 0x005A1770
bool __cdecl CFunc_TryCheatString(Script::CStruct* params, Script::CScript* script);           // 0x005A1840
bool __cdecl CFunc_LevelIs(Script::CStruct* params, Script::CScript* script);                  // 0x005A1A90
bool __cdecl CFunc_SetStatOverride(Script::CStruct* params, Script::CScript* script);          // 0x005A1F80
bool __cdecl CFunc_ToggleRails(Script::CStruct* params, Script::CScript* script);              // 0x005A1FB0
bool __cdecl CFunc_GetTimeLimit(Script::CStruct* params, Script::CScript* script);             // 0x005A1FD0
bool __cdecl CFunc_GetGoalsMode(Script::CStruct* params, Script::CScript* script);             // 0x005A1FF0
bool __cdecl CFunc_GetTeamGraffitiMode(Script::CStruct* params, Script::CScript* script);      // 0x005A2010
bool __cdecl CFunc_GetGameMode(Script::CStruct* params, Script::CScript* script);              // 0x005A2030
bool __cdecl CFunc_SetTesterScript(Script::CStruct* params, Script::CScript* script);          // 0x005A2090
bool __cdecl CFunc_KillTesterScript(Script::CStruct* params, Script::CScript* script);         // 0x005A20F0
bool __cdecl CFunc_AutoRail(Script::CStruct* params, Script::CScript* script);                 // 0x005A21A0

// --- the lobby and network options (net_state_cfuncs.cpp) -------------------------------------------------------

bool __cdecl CFunc_JoinWithPassword(Script::CStruct* params, Script::CScript* script);         // 0x005A1680
bool __cdecl CFunc_LoadPendingPlayers(Script::CStruct* params, Script::CScript* script);       // 0x005A1820
bool __cdecl CFunc_LaunchQueuedScripts(Script::CStruct* params, Script::CScript* script);      // 0x005A1830
bool __cdecl CFunc_StartNetworkLobby(Script::CStruct* params, Script::CScript* script);        // 0x005A1AC0
bool __cdecl CFunc_ObserversAllowed(Script::CStruct* params, Script::CScript* script);         // 0x005A1AD0
bool __cdecl CFunc_NumPlayersAllowed(Script::CStruct* params, Script::CScript* script);        // 0x005A1AF0
bool __cdecl CFunc_AutoDNS(Script::CStruct* params, Script::CScript* script);                  // 0x005A1B30
bool __cdecl CFunc_UsingDefaultMasterServers(Script::CStruct* params, Script::CScript* script); // 0x005A1B70
bool __cdecl CFunc_UsingDHCP(Script::CStruct* params, Script::CScript* script);                // 0x005A1BB0
bool __cdecl CFunc_InInternetMode(Script::CStruct* params, Script::CScript* script);           // 0x005A1BF0
bool __cdecl CFunc_EnteringNetGame(Script::CStruct* params, Script::CScript* script);          // 0x005A1C00
bool __cdecl CFunc_DeviceChosen(Script::CStruct* params, Script::CScript* script);             // 0x005A1C30
bool __cdecl CFunc_GameIsOver(Script::CStruct* params, Script::CScript* script);               // 0x005A1C70
bool __cdecl CFunc_TryJoinInternetGame(Script::CStruct* params, Script::CScript* script);      // 0x005A28A0
bool __cdecl CFunc_TryJoinServerIP(Script::CStruct* params, Script::CScript* script);          // 0x005A28D0
bool __cdecl CFunc_TryJoinServerIPCancel(Script::CStruct* params, Script::CScript* script);    // 0x005A2960
bool __cdecl CFunc_ForceServerListRefresh(Script::CStruct* params, Script::CScript* script);   // 0x005A2990

// --- the viewer camera on the scenes, node objects (node_object_cfuncs.cpp) --------------------------------------

bool __cdecl CFunc_CenterCamera(Script::CStruct* params, Script::CScript* script);             // 0x005A2D60
bool __cdecl CFunc_CreateFromStructure(Script::CStruct* params, Script::CScript* script);      // 0x005A34D0
bool __cdecl CFunc_ParseNodeArray(Script::CStruct* params, Script::CScript* script);          // 0x005A34E0
bool __cdecl CFunc_ReLoadNodeArray(Script::CStruct* params, Script::CScript* script);          // 0x005A39F0
// 0x005A2A00: the viewer's camera object placed to look at a scene's sectors (all, or those named `only`).
void __cdecl FitViewerCamera(uint8_t* scene, float scale, float angleX, float angleY, float angleZ, uint32_t only);
// 0x005A2E50 / 0x005A32A0: the object of a node (its structure / its name) made by its class.
bool __cdecl CreateFromNode(Script::CStruct* node);
bool __cdecl CreateFromNodeName(uint32_t name);
// 0x005A32C0: the object of the node `name` killed (switched off) by its class.
bool __cdecl KillFromNodeName(uint32_t name, Script::CScript* script);

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
