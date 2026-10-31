#pragma once

#include <cstdint>

namespace Gfx2D {
class CTODManager;
}

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script
namespace GameObjects {
class CEntity;
}

// Script commands (CFunctions) of the skate game, 0x0059F610-0x005A5360 (after skate_cfuncs.h's): the viewer,
// controllers, preferences, the asynchronous loads, pausing, the game flow, the network state (hosting, joining,
// the server list, observers), the game modes, debouncing, objects made from structures. All are cdecl
// (params, script) and return whether the script's `if` should be taken.
namespace ScriptCommands {

// --- the viewer, controllers, node arrays, preferences (game_cfuncs.cpp) --------------------------------------

bool __cdecl CFunc_SetViewerAnim(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0059F610
bool __cdecl CFunc_SetViewerLODDist(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0059F650
bool __cdecl CFunc_ReloadViewerAnim(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0059F6C0
bool __cdecl CFunc_CheckForHoles(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0059F730
bool __cdecl CFunc_StopSecondControllerCheck(Scripts::CStruct* params, Scripts::CScript* script); // 0x0059F740
bool __cdecl CFunc_QuitGame(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x0059F760
bool __cdecl CFunc_IsInternetGameHost(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0059F770
bool __cdecl CFunc_IsJoiningInternetGame(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0059F780
bool __cdecl CFunc_TryingToJoinGame(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0059F7A0
bool __cdecl CFunc_TryJoinServerIPSuccess(Scripts::CStruct* params, Scripts::CScript* script);   // 0x0059F7B0
bool __cdecl CFunc_GSEnableNet(Scripts::CStruct* params, Scripts::CScript* script);              // 0x0059F7E0
bool __cdecl CFunc_GSDisableNet(Scripts::CStruct* params, Scripts::CScript* script);             // 0x0059F7F0
bool __cdecl CFunc_WaitForAsyncLoadToFinish(Scripts::CStruct* params, Scripts::CScript* script); // 0x0059F800
bool __cdecl CFunc_GetCustomSkaterFilename(Scripts::CStruct* params, Scripts::CScript* script);  // 0x0059F930
bool __cdecl CFunc_SetCustomSkaterFilename(Scripts::CStruct* params, Scripts::CScript* script);  // 0x0059F960
bool __cdecl CFunc_ControllerPressed(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0059F9A0
bool __cdecl CFunc_GetAnalogueInfo(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0059FA40
bool __cdecl CFunc_NodeArrayManLoad(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0059FB30
bool __cdecl CFunc_NodeArrayManUnload(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0059FB90
bool __cdecl CFunc_GetPreferenceString(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0059FBC0
bool __cdecl CFunc_GetPreferencePassword(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0059FC40
bool __cdecl CFunc_GetPreferenceChecksum(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0059FD00
bool __cdecl CFunc_ToggleNetMetrics(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0059FD80
bool __cdecl CFunc_SetViewMode(Scripts::CStruct* params, Scripts::CScript* script);              // 0x0059FD90
bool __cdecl CFunc_SetViewerModel(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A1C80
bool __cdecl CFunc_GetViewerObjectID(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A1D40
bool __cdecl CFunc_ReloadAnim(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005A1D80
bool __cdecl CFunc_Debounce(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005A1E70
bool __cdecl CFunc_StartKeyboardHandler(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005A2050
bool __cdecl CFunc_StopKeyboardHandler(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005A2080
bool __cdecl CFunc_SpawnSecondControllerCheck(Scripts::CStruct* params, Scripts::CScript* script); // 0x005A2100
bool __cdecl CFunc_MoveNode(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005A21C0
bool __cdecl CFunc_SetActiveCamera(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A2370
bool __cdecl CFunc_ShowTracking(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005A23E0
bool __cdecl CFunc_ChooseFaceForFacemap(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005A2790
bool __cdecl CFunc_SkaterIsCustom(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A29A0
bool __cdecl CFunc_SetSceneColor(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005A29E0
bool __cdecl CFunc_AddRestartsToMenu(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A4260
bool __cdecl CFunc_AddWarpPointsToMenu(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005A4560
bool __cdecl CFunc_AddFacesToMenu(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A4730

// 0x0059F820: the second controller check's task (SpawnSecondControllerCheck): runs the script 0x07C28B45 when
// both controllers are plugged in, else 0x9C9DFCF8 - every frame while the task is on.
void __cdecl SecondPadWatchTick(void* task);
// 0x0059F890: make the camera of the object `id` (its 0xC4E311FA component) the camera of `viewport`; with
// `takeCurrentView`, the object first moves to where the viewport's camera is now (and the component is told).
// False if there's no such object.
bool __cdecl MakeCameraActive(uint32_t id, int viewport, bool takeCurrentView);
// 0x0059FB10: a manager walk's callback: the object deletes itself (unless locked) if its id is `*id`.
void __cdecl DestroyObjectWithId(GameObjects::CEntity* object, void* id);

// --- cleaning up, object flags, skater scripts, pausing, the flash (game_flow_cfuncs.cpp) --------------------------

bool __cdecl CFunc_Cleanup(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0059FE50
bool __cdecl CFunc_MakeSkaterGoto(Scripts::CStruct* params, Scripts::CScript* script);  // 0x005A0300
bool __cdecl CFunc_MakeSkaterGosub(Scripts::CStruct* params, Scripts::CScript* script); // 0x005A03C0
bool __cdecl CFunc_SpawnSound(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005A0460
bool __cdecl CFunc_PauseSkater(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005A0520
bool __cdecl CFunc_UnPauseSkater(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A0580
bool __cdecl CFunc_PauseGame(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A05C0
bool __cdecl CFunc_UnPauseGame(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005A05E0
bool __cdecl CFunc_GameIsPaused(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005A05F0
bool __cdecl CFunc_PauseObjects(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005A0620
bool __cdecl CFunc_UnPauseObjects(Scripts::CStruct* params, Scripts::CScript* script);  // 0x005A0630
bool __cdecl CFunc_ResetClock(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005A0640
bool __cdecl CFunc_PadsPluggedIn(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A0670
bool __cdecl CFunc_DoFlash(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005A0680
bool __cdecl CFunc_ClearFlag(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A3B00
bool __cdecl CFunc_SendFlag(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A3B20
bool __cdecl CFunc_QueryFlag(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A3B40
bool __cdecl CFunc_SendException(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A3B60

// --- servers, the network mode and setup, going online, observing (net_state_cfuncs.cpp) ------------------------

bool __cdecl CFunc_StartServer(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005A0880
bool __cdecl CFunc_LeaveServer(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005A08D0
bool __cdecl CFunc_JoinServer(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005A08E0
bool __cdecl CFunc_SetNetworkMode(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A0A00
bool __cdecl CFunc_ProfileTasks(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005A0A60
bool __cdecl CFunc_UseNetworkPreferences(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005A0AB0
bool __cdecl CFunc_TestNetSetup(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005A0AC0
bool __cdecl CFunc_NeedToTestNetSetup(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A0BD0
bool __cdecl CFunc_CanChangeDevices(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005A0BE0
bool __cdecl CFunc_LoadNetworkDrivers(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A0BF0
bool __cdecl CFunc_ConnectToInternet(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A0C60
bool __cdecl CFunc_CancelConnectToInternet(Scripts::CStruct* params, Scripts::CScript* script);  // 0x005A0CC0
bool __cdecl CFunc_IsOnline(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005A0CD0
bool __cdecl CFunc_DisconnectFromInternet(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A0CE0
bool __cdecl CFunc_EnableActuators(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A0D20
bool __cdecl CFunc_InNetGame(Scripts::CStruct* params, Scripts::CScript* script);                // 0x005A0D80
bool __cdecl CFunc_IsObserving(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005A0D90
bool __cdecl CFunc_SetSlomo(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005A0E50
bool __cdecl CFunc_TogglePlayerNames(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A0EB0
bool __cdecl CFunc_SetCurrentGameType(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A0EF0
bool __cdecl CFunc_DumpNetMessageStats(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005A0F00
bool __cdecl CFunc_SetServerMode(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005A1020
bool __cdecl CFunc_NotifyBailDone(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A1050
bool __cdecl CFunc_EnterObserverMode(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A10D0
bool __cdecl CFunc_ObserveNextSkater(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A10E0
bool __cdecl CFunc_RefreshServerList(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A10F0
bool __cdecl CFunc_StartServerList(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A1120
bool __cdecl CFunc_StartLobbyList(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A1130
bool __cdecl CFunc_StopServerList(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A1160
bool __cdecl CFunc_FreeServerList(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A1170
// (these two return 1 or 0 in all of EAX: some of their callers read it)
int __cdecl CFunc_InSplitScreenGame(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005A1180
int __cdecl CFunc_InMultiplayerGame(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005A11B0
bool __cdecl CFunc_OnServer(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005A1220
bool __cdecl CFunc_ToggleScores(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005A12D0

// --- game modes and the level (game_flow_cfuncs.cpp) ------------------------------------------------------------

bool __cdecl CFunc_RoundIncrement(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A11C0
bool __cdecl CFunc_GameModeEquals(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A11D0
bool __cdecl CFunc_GetCurrentLevel(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A1230
bool __cdecl CFunc_SetLevelStructureName(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005A1270
bool __cdecl CFunc_GetLevelStructureName(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005A12A0
bool __cdecl CFunc_RestartLevel(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005A12C0
bool __cdecl CFunc_GameFlow(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005A12E0
bool __cdecl CFunc_ClearOmnigons(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005A1320
bool __cdecl CFunc_BroadcastOmnigon(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005A1540
bool __cdecl CFunc_SetScreenMode(Scripts::CStruct* params, Scripts::CScript* script);            // 0x005A1720
bool __cdecl CFunc_SetScreenModeFromGameMode(Scripts::CStruct* params, Scripts::CScript* script); // 0x005A1770
bool __cdecl CFunc_TryCheatString(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A1840
bool __cdecl CFunc_LevelIs(Scripts::CStruct* params, Scripts::CScript* script);                  // 0x005A1A90
bool __cdecl CFunc_SetStatOverride(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A1F80
bool __cdecl CFunc_ToggleRails(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005A1FB0
bool __cdecl CFunc_GetTimeLimit(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005A1FD0
bool __cdecl CFunc_GetGoalsMode(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005A1FF0
bool __cdecl CFunc_GetTeamGraffitiMode(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005A2010
bool __cdecl CFunc_GetGameMode(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005A2030
bool __cdecl CFunc_SetTesterScript(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A2090
bool __cdecl CFunc_KillTesterScript(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005A20F0
bool __cdecl CFunc_AutoRail(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x005A21A0
bool __cdecl CFunc_SpawnSkaterScript(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A4170

// --- the lobby and network options (net_state_cfuncs.cpp) -------------------------------------------------------

bool __cdecl CFunc_JoinWithPassword(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005A1680
bool __cdecl CFunc_LoadPendingPlayers(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A1820
bool __cdecl CFunc_LaunchQueuedScripts(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005A1830
bool __cdecl CFunc_StartNetworkLobby(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A1AC0
bool __cdecl CFunc_ObserversAllowed(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005A1AD0
bool __cdecl CFunc_NumPlayersAllowed(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A1AF0
bool __cdecl CFunc_AutoDNS(Scripts::CStruct* params, Scripts::CScript* script);                  // 0x005A1B30
bool __cdecl CFunc_UsingDefaultMasterServers(Scripts::CStruct* params, Scripts::CScript* script); // 0x005A1B70
bool __cdecl CFunc_UsingDHCP(Scripts::CStruct* params, Scripts::CScript* script);                // 0x005A1BB0
bool __cdecl CFunc_InInternetMode(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A1BF0
bool __cdecl CFunc_EnteringNetGame(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A1C00
bool __cdecl CFunc_DeviceChosen(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005A1C30
bool __cdecl CFunc_GameIsOver(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005A1C70
bool __cdecl CFunc_TryJoinInternetGame(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005A28A0
bool __cdecl CFunc_TryJoinServerIP(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A28D0
bool __cdecl CFunc_TryJoinServerIPCancel(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005A2960
bool __cdecl CFunc_ForceServerListRefresh(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A2990
bool __cdecl CFunc_InitPrefsBeenox(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005A4D90
// 0x005A4900: the network preferences set from the host settings' globals (server name, password, network id,
// players, team mode, skill level).
void __cdecl InitBeenoxPreferences();

// --- the viewer camera on the scenes, node objects (node_object_cfuncs.cpp) --------------------------------------

bool __cdecl CFunc_CenterCamera(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005A2D60
bool __cdecl CFunc_CreateFromStructure(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005A34D0
bool __cdecl CFunc_ParseNodeArray(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A34E0
bool __cdecl CFunc_ReLoadNodeArray(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005A39F0
// 0x005A2A00: the viewer's camera object placed to look at a scene's sectors (all, or those named `only`).
void __cdecl FitViewerCamera(uint8_t* scene, float scale, float angleX, float angleY, float angleZ, uint32_t only);
// 0x005A2E50 / 0x005A32A0: the object of a node (its structure / its name) made by its class.
bool __cdecl CreateFromNode(Scripts::CStruct* node);
bool __cdecl CreateFromNodeName(uint32_t name);
// 0x005A32C0: the object of the node `name` killed (switched off) by its class.
bool __cdecl KillFromNodeName(uint32_t name, Scripts::CScript* script);
bool __cdecl CFunc_Create(Scripts::CStruct* params, Scripts::CScript* script);                   // 0x005A4FF0
bool __cdecl CFunc_SetObjectColor(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005A5010
bool __cdecl CFunc_Kill(Scripts::CStruct* params, Scripts::CScript* script);                     // 0x005A5030
bool __cdecl CFunc_Visible(Scripts::CStruct* params, Scripts::CScript* script);                  // 0x005A5050
bool __cdecl CFunc_Invisible(Scripts::CStruct* params, Scripts::CScript* script);                // 0x005A5070
bool __cdecl CFunc_Shatter(Scripts::CStruct* params, Scripts::CScript* script);                  // 0x005A5090
// 0x005A3B80 / 0x005A3D10 / 0x005A3E80 / 0x005A3FD0 / 0x005A40B0: a node's thing (by its name) hidden or shown (in
// viewport 1, or 2 with `secondViewport` 1), alive?, coloured, shattered, given a terrain.
bool __cdecl SetNodeHidden(uint32_t name, bool hide, int secondViewport);
bool __cdecl NodeIsAlive(uint32_t name);
bool __cdecl SetNodeColor(uint32_t name, Scripts::CStruct* params);
bool __cdecl ShatterNode(uint32_t name);
bool __cdecl SetNodeTerrain(uint32_t name, Scripts::CStruct* params);
// 0x005A4DA0: one of those (or create / kill) on the node `name`: 0 create, 1 kill, 2 show, 3 hide, 4 show and
// shatter, 5 alive?, 6 colour, 7 terrain.
bool __cdecl NodeCommand(uint32_t name, int action, Scripts::CScript* script, Scripts::CStruct* params);
// 0x005A4EF0 (8 callers): NodeCommand on name= or the nodes of prefix=.
bool __cdecl NodeCommandByName(Scripts::CStruct* params, Scripts::CScript* script, int action);

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
bool __cdecl FlagCommandByName(Scripts::CStruct* params, Scripts::CScript* script, int action);
// 0x005A01B0: its manager walk's callback (`data`: {id, value, action, result, found}): the command done if the
// object has the id.
void __cdecl ApplyFlagCommandToObject(GameObjects::CEntity* object, void* data);
// 0x005A0120: the command done to an object (`data` from the value on, in ESI; the object in EAX): the naked
// adapter, and our code behind it.
void ApplyFlagCommandStub();
void __cdecl ApplyFlagCommandRegisters(void* data, GameObjects::CEntity* object);

extern bool g_tryingToJoin;
extern Gfx2D::CTODManager* g_todManager;
extern uint32_t g_lastBroadcastedOmnigon;

} // namespace ScriptCommands
