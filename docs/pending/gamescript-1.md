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
