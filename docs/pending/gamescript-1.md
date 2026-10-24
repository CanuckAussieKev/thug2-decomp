# Game script commands 0x0059F610-0x005A5360 (viewer, async load, game flow, pause, network state, game modes, debounce, create-from-structure) - gamescript-1 agent

Branch worktree-agent-a59c3dfa2addc181e. Code in src/game/script/game_cfuncs.h (declarations for the whole range) and
src/game/script/game_cfuncs*.cpp; tests in src/hook/selftests_gs1.cpp (TestGameCFuncs ...).
Shared files touched: CMakeLists.txt (new files listed).

- [x] Batch GS1-1 (NOT run in the game yet; the viewer, controllers, node arrays, preferences, network flags - 27
  functions): src/game/script/game_cfuncs.h / .cpp (new).
  - 0x0059F610 SetViewerAnim, 0x0059F650 SetViewerLODDist (the pair (x, y): the viewer object's model component's
    colour value x (truncated) set to y; false without the pair), 0x0059F6C0 ReloadViewerAnim, 0x0059F730
    CheckForHoles, 0x0059F740 StopSecondControllerCheck (the task's deleting destructor, vtable slot 2),
    0x0059F760 QuitGame, 0x0059F770 IsInternetGameHost, 0x0059F780 IsJoiningInternetGame, 0x0059F7A0
    TryingToJoinGame, 0x0059F7B0 TryJoinServerIPSuccess (the success flag forgotten once no join is tried),
    0x0059F7E0 GSEnableNet, 0x0059F7F0 GSDisableNet, 0x0059F800 WaitForAsyncLoadToFinish, 0x0059F820 (never made by
    Ghidra: the second controller check's task code - script 0x07C28B45 / 0x9C9DFCF8 as both controllers are plugged in
    or not), 0x0059F890 MakeCameraActive (7 callers: an object's camera component made a viewport's camera, the
    object first moved to that viewport's current view on request), 0x0059F930 / 0x0059F960 Get / SetCustomSkaterFilename
    (profile 0's 0xF36C1878 string), 0x0059F9A0 ControllerPressed (one controller or either), 0x0059FA40
    GetAnalogueInfo (leftx / lefty / rightx / righty: the pad's scaled sticks 2, 3, 0, 1; false unless plugged in and
    valid), 0x0059FB10 (never made by Ghidra: a manager walk's callback deleting the object with an id),
    0x0059FB30 NodeArrayManLoad, 0x0059FB90 NodeArrayManUnload, 0x0059FBC0 GetPreferenceString, 0x0059FC40
    GetPreferencePassword (a star per character, in the static buffer 0x007CEBD0), 0x0059FD00 GetPreferenceChecksum,
    0x0059FD80 ToggleNetMetrics, 0x0059FD90 SetViewMode.
  - Original quirks kept: the preference commands read the type, when pref_type is missing, from the argument slot it
    would have been written to (the `script` pointer: no such set - a null preferences object, then a crash); their
    value locals are unset when the field is missing.
  - Test TestGameCFuncs: original vs ours with every called function stood in for (the viewer, objects, devices,
    profiles, the structure adders, the pad, the asset and node managers, preferences, skaters...), each branch above.
    29 of 29 deliberate bugs caught (1 after strengthening: DestroyObjectWithId's id test).
- [x] Batch GS1-2 (NOT run in the game yet; cleaning up, object flags by name / prefix, skater scripts, pausing, the
  clock, the screen flash - 21 functions): src/game/script/game_flow_cfuncs.cpp (new).
  - 0x0059FE50 Cleanup (everything of a level unloaded; the calling script's SCRIPT_FLAG_SPAWNED off while two
    scripts run; the level's 15 .qb files (0x00696900) and the one the global 0xC472ECC5 names; the unused .pre files;
    without preserve_skaters the extra skater heaps; the network heaps rebuilt as the skate module says; the physical
    memory logged through wsprintfA / OutputDebugStringA), 0x005A0120 (register arguments: the command structure from
    its value in ESI, the object in EAX - a naked stub: 0 / 1 / 2 set / clear / test one of the 32 script flags
    (+0x24; the shift by the value's low 5 bits, as the CPU does), 3 / 4 object flag bits (+0x08), 5 an exception),
    0x005A01B0 (never made by Ghidra: its manager walk's callback), 0x005A01D0 FlagCommandByName (name= or the nodes of
    prefix=; the flags 0x2E7D5EE7 / 0xBCEB479A looked for and ignored), 0x005A0300 MakeSkaterGoto (only a local
    skater: +0xBC), 0x005A03C0 MakeSkaterGosub, 0x005A0460 SpawnSound (on the script's object; without one,
    SpawnScript's result), 0x005A0520 PauseSkater (vtable +0x2C, then Hide(hide)), 0x005A0580 UnPauseSkater (+0x30,
    shown), 0x005A05C0 PauseGame (the calling script unpaused), 0x005A05E0 UnPauseGame, 0x005A05F0 GameIsPaused (the
    flag 0x3BA1FDB9 added to the command's own parameters), 0x005A0620 / 0x005A0630 PauseObjects / UnPauseObjects,
    0x005A0640 ResetClock (on the server: the mode's time limit), 0x005A0670 PadsPluggedIn, 0x005A0680 DoFlash (the
    eight colour values read into one shared integer: a missing one repeats the one before - kept), 0x005A3B00
    ClearFlag, 0x005A3B20 SendFlag, 0x005A3B40 QueryFlag, 0x005A3B60 SendException.
  - Test TestGameFlowCFuncs: original vs ours with every call stood in for (Win32's GlobalMemoryStatus / wsprintfA /
    OutputDebugStringA too; the manager walk over three stand-in objects; the 0x005A0120 stub driven through inline
    asm). 44 of 44 deliberate bugs caught (5 after strengthening: the spawned flag's restore, flag numbers past 15,
    clearing an unset flag, a prefix query whose last node lacks the flag).
- [x] Batch GS1-3 (NOT run in the game yet; servers, the network mode and setup, going online, vibration, observing,
  slow motion - 20 CFunctions): src/game/script/net_state_cfuncs.cpp (new).
  - 0x005A0880 StartServer (in the bottom-up heap; LAN mode asked and not used, as in the original), 0x005A08D0
    LeaveServer, 0x005A08E0 JoinServer (the server: a local client per player of the game mode; a client: inet_addr
    of the address, both address and port needed), 0x005A0A00 SetNetworkMode (0-2 only), 0x005A0A60 ProfileTasks,
    0x005A0AB0 UseNetworkPreferences, 0x005A0AC0 TestNetSetup (success_script, or the net_error_* text of the last
    error added as 0xC4745838 and 0x6D51F203 run), 0x005A0BD0 NeedToTestNetSetup / 0x005A0BE0 CanChangeDevices (tail
    jumps to the shared stubs ReturnFalse / ReturnTrue), 0x005A0BF0 LoadNetworkDrivers (use_deci2: viewer_ip,
    viewer_subnet, viewer_gateway), 0x005A0C60 ConnectToInternet, 0x005A0CC0 CancelConnectToInternet, 0x005A0CD0
    IsOnline, 0x005A0CE0 DisconnectFromInternet, 0x005A0D20 EnableActuators (the skate module's two controller entries,
    +0x394 / +0x39C), 0x005A0D80 InNetGame, 0x005A0D90 IsObserving (4 callers through GameFunction - left as they
    are), 0x005A0E50 SetSlomo, 0x005A0EB0 TogglePlayerNames, 0x005A0EF0 SetCurrentGameType.
  - Test TestNetStateCFuncs: original vs ours with the network, net library, memory, input and main loop managers'
    calls stood in for (their inline getters read from stand-in objects). 26 of 26 deliberate bugs caught (1 after
    strengthening: a stand-in memory manager, so the heap pushed is told apart).
- [x] Batch GS1-4 (NOT run in the game yet; the server list, observing, split screen, game modes, the level - 21
  CFunctions): src/game/script/net_state_cfuncs.cpp, game_flow_cfuncs.cpp.
  - 0x005A0F00 DumpNetMessageStats (the server's - else client 0's - connections' received / sent metrics read twice
    per message id; the printing compiled out), 0x005A1020 SetServerMode [off], 0x005A1050 NotifyBailDone (a local
    skater's client sends message 0x45, sequenced, group 8), 0x005A10D0 EnterObserverMode, 0x005A10E0
    ObserveNextSkater, 0x005A10F0 RefreshServerList [force_refresh], 0x005A1120 StartServerList, 0x005A1130
    StartLobbyList (service state 13, after state 1 when it's in 2), 0x005A1160 StopServerList, 0x005A1170
    FreeServerList, 0x005A1180 InSplitScreenGame and 0x005A11B0 InMultiplayerGame (12 / 5 callers; 1 or 0 in all of
    EAX - declared int), 0x005A11C0 RoundIncrement, 0x005A11D0 GameModeEquals (any bare name the mode's),
    0x005A1220 OnServer, 0x005A1230 GetCurrentLevel, 0x005A1270 / 0x005A12A0 Set / GetLevelStructureName (the skate
    module's +0x380), 0x005A12C0 RestartLevel, 0x005A12D0 ToggleScores, 0x005A12E0 GameFlow.
  - Test TestServerListCFuncs: original vs ours over stand-in connections, clients, the server list's states and the
    game mode. 19 of 19 deliberate bugs caught (1 after strengthening: an unnamed integer equal to the mode).
