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
- [x] Batch GS1-5 (NOT run in the game yet; Omnigons, the screen mode, cheat codes, the lobby, network options - 20
  functions): src/game/script/game_flow_cfuncs.cpp, net_state_cfuncs.cpp.
  - 0x005A1320 ClearOmnigons (the career flags named by 23 globals), 0x005A1540 BroadcastOmnigon (message 0x7F of 8
    bytes to each remote connection of the server: the Omnigon and the flag - the 3 bytes after the flag unset in the
    original, zero here), 0x005A1680 JoinWithPassword, 0x005A1720 SetScreenMode, 0x005A1770 SetScreenModeFromGameMode
    (the mode set unless it only swaps the two split modes), 0x005A1820 LoadPendingPlayers, 0x005A1830
    LaunchQueuedScripts, 0x005A1840 TryCheatString (Ghidra split it: 0x005A18A9 is its second half, registered source
    only - the cheat hash: 100000 rounds of a checksum over a buffer whose terminator is overwritten with 'X', so the
    earlier strings' leftover bytes count - the buffer is first filled with "12345678901234567890"; c1 / c2 matched
    against the global array 0x75A23DA4; always false; the text used as a sprintf format, kept), 0x005A1A90 LevelIs,
    0x005A1AC0 StartNetworkLobby, 0x005A1AD0 ObserversAllowed (not over a modem), 0x005A1AF0 NumPlayersAllowed,
    0x005A1B30 AutoDNS, 0x005A1B70 UsingDefaultMasterServers, 0x005A1BB0 UsingDHCP, 0x005A1BF0 InInternetMode,
    0x005A1C00 EnteringNetGame, 0x005A1C30 DeviceChosen, 0x005A1C70 GameIsOver.
  - Test TestLobbyCFuncs: original vs ours (the game's sprintf stood in for by ours; the cheat's numbers the original
    logs used for a matching entry in the next call). 27 of 27 deliberate bugs caught (1 after strengthening: a cheat
    text shorter than the round's number, so the first fill's bytes are read).
- [x] Batch GS1-6 (NOT run in the game yet; the viewer's model, animation reloads, debouncing, keyboard handling,
  the second controller check, game mode values - 16 CFunctions): game_cfuncs.cpp, game_flow_cfuncs.cpp.
  - 0x005A1C80 SetViewerModel (view mode 1 - its common part with SetViewMode is ApplyViewMode now; the viewer places
    its camera and makes the object), 0x005A1D40 GetViewerObjectID (the object isn't checked for null, as in the
    original), 0x005A1D80 ReloadAnim (by base + anim, or the file's checksum; the debug heap while 0x007CEFAC),
    0x005A1E70 Debounce (input components lock the button; trick components hold it off until the clock + time *
    1000 - the x87 sum rounded to a float), 0x005A1F80 SetStatOverride, 0x005A1FB0 ToggleRails, 0x005A1FD0
    GetTimeLimit, 0x005A1FF0 GetGoalsMode / 0x005A2010 GetTeamGraffitiMode (the same mode value, 0x005532D0),
    0x005A2030 GetGameMode, 0x005A2050 StartKeyboardHandler, 0x005A2080 StopKeyboardHandler, 0x005A2090
    SetTesterScript, 0x005A20F0 KillTesterScript, 0x005A2100 SpawnSecondControllerCheck (the task's data was a
    pointer to one of the original's locals - never read; null here), 0x005A21A0 AutoRail (the shared IgnoreArgument
    stub).
  - Test TestViewerModeCFuncs: original vs ours. 25 of 25 deliberate bugs caught.
- [x] Batch GS1-7 (NOT run in the game yet; moving nodes, the active camera, the tracking log, the face map, joining by
  address - 10 CFunctions): game_cfuncs.cpp, net_state_cfuncs.cpp.
  - 0x005A21C0 MoveNode (position= / pos= into the node and its object; RailNode / ClimbingNode also moved in the
    rail manager; Pedestrian, ProximNode, Vehicle, LevelObject, LevelGeometry, GameObject and 0x061A741E true; any
    other class false), 0x005A2370 SetActiveCamera (MakeCameraActive's answer), 0x005A23E0 ShowTracking (a debug
    tool: "\nTracking<type>" lines' six numbers - the line drawing is compiled out, only the counts are logged; right
    after a line's sixth number the parser already stands past the next line's newline, so a line straight after
    another is missed - kept; a missing file leaves the debug heap pushed - kept), 0x005A2790 ChooseFaceForFacemap
    (faces\<string> read with the game's C runtime into the first profile's face texture), 0x005A28A0
    TryJoinInternetGame, 0x005A28D0 TryJoinServerIP (inet_addr, else gethostbyname's first address), 0x005A2960
    TryJoinServerIPCancel, 0x005A2990 ForceServerListRefresh, 0x005A29A0 SkaterIsCustom, 0x005A29E0 SetSceneColor.
    The getter folded at 0x00576E60 (Viewer::GetViewerObject, the word at +0x54) is also a profile's face texture's.
  - Test TestNodeTrackingCFuncs: original vs ours (atof jumped to ours; the C runtime's file calls stood in for - no
    file is opened). 22 of 24 deliberate bugs caught (3 after strengthening: lines the parser can see); 2 equivalent:
    ShowTracking's flags and its numbers only feed the compiled-out drawing.
- [x] Batch GS1-8 (NOT run in the game yet; the viewer camera fitted to the scenes, node objects made and killed by
  class - 6 functions): src/game/script/node_object_cfuncs.cpp (new).
  - 0x005A2A00 FitViewerCamera (scene, scale, x, y, z, only: the box round the active sectors' vertices - the
    sector table walked with Lst::HashTable::IterateNext; the camera object 0xEB17151B turned X, Y, Z degrees from
    the identity through D3DXMatrixMultiply and set back along its forward row by max(box size, 1000) * scale^2 -
    the x87 order kept: the centre's z / w sums rounded to floats first, the offsets rounded before adding),
    0x005A2D60 CenterCamera (scale 0.9, x -45, y 45, z 0, id: each scene of the list 0x006FA7EC in turn),
    0x005A2E50 CreateFromNode (3 callers: by class - Pedestrian / 0x061A741E peds, Rail / ClimbingNode on,
    ProximNode, ParticleObject, EmitterObject, LevelObject (the time of day's level colour added as 0x99A9B716 while
    its params are set), Vehicle, GameObject, LevelGeometry: the sector switched on, or with 0xEDC063B9 a copy made
    under the node's name and placed; triggers read their name and are false; nodes the skate module skips
    (0x00565BB0) true), 0x005A32A0 CreateFromNodeName (a tail call), 0x005A32C0 KillFromNodeName (objects with
    the node's name deleted unless locked - through DestroyObjectWithId, 0x0059FB10; rails / proxims / emitters
    switched off; 0xA0E52802 by 0x0048FCA0; level geometry off, its 0x4E549FE1 effect off - twice when flagged, as in
    the original), 0x005A34D0 CreateFromStructure.
  - Test TestNodeObjectCFuncs: original vs ours over stand-in scenes (a real two-bucket hash table of fake sectors per
    scene), every class both ways, the camera's whole placing compared bit for bit. 33 of 35 deliberate bugs caught
    (1 after strengthening: a box over the 1000 minimum); 2 equivalent: the forward row's w is 0 for every rotation,
    and the default scale is only used when the node gives one.
- [x] Batch GS1-9 (NOT run in the game yet; the level's node array set up and reloaded - 2 CFunctions):
  node_object_cfuncs.cpp.
  - 0x005A34E0 ParseNodeArray (no_cleanup; the proxim / emitter / rail / trick cluster set-up; per node: rail nodes
    lose angles and 0x7321A8D6, level objects' sectors off with their terrain (0x7E4813A7, default 0xE3714DC1),
    level geometry's sector shown or off (only outside the park editor), its occluder (0x0049EA60) and terrain, the
    two failure messages, trick objects into their clusters (the name's local is kept from node to node in the
    original, so a nameless node reuses the last name read - kept); proxims and emitters added, the created-at-start
    objects made; the rails' overlaps, the gap checklist unless 0xF7ECD937, the scenes, the 0xA0E52802 nodes),
    0x005A39F0 ReLoadNodeArray (in the debug heap: the asset and the file's node array dropped, NodeArrayManLoad, the
    .qb (0x005A5960), the node manager's last array removed and added again; the local structure cleared).
  - Test TestParseNodeArrayCFuncs: original vs ours over a 16-node array (every class and flag), the park editor on
    and off, skipped nodes, multiplayer, every node a trick object, no checklist; the sectors' whole state compared.
    28 of 28 deliberate bugs caught.
