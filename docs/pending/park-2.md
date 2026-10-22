# Park editor & graffiti, second quarter (0x0058CD40-0x0058FBD0) - PE2 agent

Branch worktree-agent-aee5cf12d9ed9c993, workspace C:\Games\THUG2-2. Source in src/game/ed/gap_manager.cpp (the gap
manager's members in the range), src/game/ed/park_editor.h / park_editor.cpp (the park editor module, Ed::CParkEditor)
and src/game/ed/park_editor_cfuncs.cpp (its script commands); tests in src/hook/selftests_park_editor.cpp. Shared files
touched (additive): CMakeLists.txt (the new files), src/game/ed/park.h (CGapManager: its layout and the record type
CGapManager::Gap, declarations in place of the GameFunction wrappers of 0x0058CDA0 / 0x0058CDF0 / 0x0058CE90 /
0x0058CF20 / 0x0058CF30 / 0x0058D070; a forward declaration of CParkManager).
Switch off: `[replacements] disable=gap_manager.cpp` (and park_editor.cpp, park_editor_cfuncs.cpp).

- [x] Batch PE2-1 (NOT run in the game yet; the park editor's gap manager, 9 functions): src/game/ed/gap_manager.cpp
  (new), CGapManager in src/game/ed/park.h.
  - The layout (0xA8 bytes): +0x00 the next GapID, +0x04 32 gap records, +0x84 32 flags "the gap's first half
    (StartGap) was placed first", +0xA4 the park manager (null in a copy). A gap record (0xEC bytes, Calloc'd by
    StartGap / EndGap, freed by RemoveGap): its two ends' GapIDs, trigger pieces and metapieces, its name (+0x18,
    0x80 bytes) and its GapInfo (+0x98).
  - 0x0058CD40 UpdateChecklist (the current level's gap checklist - the career's, through the skate module's +0x20 -
    cleared and every gap added with 0 landed, from the GapInfo's text and score), 0x0058CDA0 SetGapInfo
    (sprintf "%s" of the info's text into the record's name, the info copied, the checklist made again), 0x0058CDF0
    RemoveGap(index, forget) (the two trigger pieces deleted through the park manager's generator; with `forget`, the
    goal editor (0x004E8470) drops the gap's checklist index from the park's gap goals (0x004E8420); every slot
    holding the record cleared, the record freed), 0x0058CE90 RemoveAllGaps, 0x0058CEC0 SetTriggerLength(piece)
    (the first gap with the piece as an end: its length on that half (0x0058CBF0) / 120 to the piece (0x005906B0);
    0 if none), 0x0058CF20 Destroy (RemoveAllGaps, the singleton cleared), 0x0058CF30 CopyFrom (the next id, each
    record copied without its pieces and metapieces, the flags; the copy's park manager null), 0x0058D070 StartGap,
    0x0058D120 FireGap(index, script) (for both ends, the one placed first first: a new structure with GapID, score,
    text - the record's name - and, for the first, combined_flags = the gap's cancel flags or the global integer
    CANCEL_GROUND; StartGap / EndGap run on the script's object through its vtable slot 3).
  - Test (selftests_park_editor.cpp TestParkGapManager): every function, original vs ours on a stand-in world
    (static, same address in both runs) with the career / checklist / goal editor / generator / memory / structure /
    object calls stood in for by recording code: checklist null / two / three gaps, RemoveGap forgetting found / not
    found / a null piece / a record in two slots, the trigger length for each end / none / a piece in two gaps, a copy
    over stale slots, FireGap both orders. 33 deliberate bugs: 30 caught, 2 more after adding the two-gaps piece and
    the singletons to the compared state; 1 equivalent (the inline constructor's m_cancelFlags = 0 after a Calloc).
- [x] Batch PE2-2 (NOT run in the game yet; the park editor module Ed::CParkEditor, 35 functions):
  src/game/ed/park_editor.h / park_editor.cpp (new).
  - The class (0x7C bytes; Mdl::Module, vtable 0x0064F234 = the scalar deleting destructor 0x0058DAA0, start
    0x0058DAD0, stop 0x0058D340; an Obj::CEventListener at +0x14, vtable 0x0064F22C = the adjustor 0x0058DA90 and the
    event filter 0x0058DE40): +0x30 a command was carried out, +0x34 the logic task (0x0058FFC0, priority 0), +0x38 an
    empty display task (priority -1000), +0x3C the input handler (0x0058DB70), +0x40 the state (off 0xD443A2BC, edit
    0x7ECA21E5, test_play 0xA40F0B8A, regular_play 0xCD3682C1), +0x44 the frame's commands, +0x48 flags (2: paused),
    +0x4C / +0x50 move 480 / turn 120 speeds, +0x54-+0x60 the sticks, +0x68 / +0x6C / +0x70 the camera's angle, tilt
    (10-90) and distance (500-1500), +0x74 the last time, +0x78 tod_script. The singleton 0x007CE5D8 / 0x007CE5DC:
    0x0058D290 is Spt::SingletonPtr's Release (singleton.cpp's Singleton590160).
  - The module: 0x0058D2D0 destructor body (the park manager deleted; the tasks and the handler are leaked, as in the
    original), 0x0058DAA0 / 0x0058DA90 the deleting destructors, 0x0058DAD0 / 0x0058D340 start / stop (stop leaves the
    display task in its list), 0x0058DB30 Leave (back to a normal level), 0x0058D360 AccessDisk (save / load; after a
    load in the editor the park rebuilt, or change_level level=load_sk5ed if the shell size changed; blockRebuild),
    0x0058D4E0 ApplyMap (a received map; the same while editing / test playing), the pass-throughs 0x0058D600-0x0058D720
    (thiscall members not using `this`, or cdecl), 0x0058DCB0 ShowEditorView, 0x0058DD70 SwitchCamera, 0x0058DDC0
    IsParkFull, 0x0058DDE0 RefreshPieceMenu, 0x0058DEC0 IsInsidePark (cdecl: a Vector by value and a margin),
    0x0058DF80 FindRestart, 0x0058DFA0 GetMaxPlayers.
  - The controls: 0x0058DB70 HandleInput (Square / Circle turn, X place, Triangle remove, L1 / L2 raise / lower and
    widen / narrow a gap's right side, the extra buttons 0x10000 / 0x20000 its left side when pressed and zoom when
    held; the commands cleared each frame), 0x0058DE40 the menus' pad_up / down / left / right to root_window (sets /
    pieces), 0x0058EBE0 ChangeSelection, 0x0058ED50 Update (the parked_cam camera: (0, 0, distance) through
    D3DXMatrixMultiply with X(-tilt) and Y(angle) rotations, over the cursor 300 up; then the sticks: dead zone 50 and
    1/78, the left stick's move turned by the angle - the sine stored as a float, the cosine kept on the FPU, done in
    inline x87 as the original -, the right stick turning (wrapped 0-360) and tilting (10-90); the commands with their
    sounds; the rail editor's and goal editor's refreshes after a change; the cursor's Update and highlights),
    0x0058F640 UpdateMenuStick, 0x0058F6C0 BindToController, 0x0058F8A0 LogicUpdate (PauseSkaters {Hide} while editing,
    UnPauseSkaters in test play; the rail editor's SwitchOn / SwitchOffRailEditor scripts around rail placement - as
    the original, SwitchOnRailEditor runs when the rail editor's flag bit 0 is set), 0x0058FA50 SetState (the state
    compared is the argument; 0 keeps the state; the front end's auto-repeat 320 ms with 100 ms editing, else 50).
  - Found by Ghidra's gaps: 0x0058DB70 (the input handler, only reached by address) and 0x0058F8A0 (reached by a tail
    jump from 0x0058FFC0) - neither was a Ghidra function.
  - Tests (TestParkEditorModule, TestParkEditorUpdate): every function, original vs ours, every called park /
    cursor / generator / menu / tracker / camera / task / rail and goal editor function stood in for by recording code;
    the singletons (front end, input manager) real with stand-in instances; D3DXMatrixMultiply and the rotation
    matrices real. Update over 57 cases (each mode and command, the sticks round the dead zones, the angle and tilt and
    distance limits, paused, no camera object), LogicUpdate over 11, IsInsidePark on each edge exactly. 124 deliberate
    bugs (65 module + 59 Update): all caught, 13 after strengthening (the edges exactly, SetState(0) with a cursor,
    the limits just past them); 1 equivalent (the dead zone `>= 50` - zero either way at 50).
- [x] Batch PE2-3 (NOT run in the game yet; the park editor's script commands and resizing, 40 functions):
  src/game/ed/park_editor_cfuncs.cpp (new).
  - The CFunctions 0x0058D730 FreeUpMemoryForPlayingPark, IsParkUnsaved, SetEditedParkGapInfo (name, score = -1,
    cancel_flags), ParkEditorSelectionAreaTooBigToCopy, CopyParkEditorSelectionToClipboard, CutParkEditorAreaSelection,
    ParkEditorAreaSelectionDeletePieces, ContinueParkEditorAreaSelection, SetEditorTheme (theme -> the shell size),
    SetCustomParkName, OptimizeParkGeometry, GetParkEdMemStats (compiled out), ParkEdMemSnapshot (Ed::Marker),
    EnableParkEdCursor, ExitParkEdTestPlay, ParkUpdateUsageInfo, 0x0058DFB0 SetParkEditorTimeOfDay /
    GetParkEditorTimeOfDayScript (tod_script, the editor's +0x78; false when none), RebuildParkNodeArray,
    GetParkEditorCursorPos (pos), CustomParkMode (editing / testing / just_using), SetParkEditorMaxPlayers (default 1),
    CanCleanlyResizePark, ResizePark, GetCurrentParkBounds (x, z, w, l), CanChangeParkDimension ([w l], or up / width
    flags against the shell and 16), SaveParkToDisk / LoadParkFromDisk (slot, block_rebuild), FireCustomParkGap
    (gap_index), GetEditedParkGapName, GetEditorTheme (the shell size), GetEditorMaxThemes (5), GetCustomParkName (an
    unnamed integer: that many characters; "unnamed park" when none - its copy into a 64-byte buffer isn't bounded, as
    in the original), IsCustomPark, IsParkFull, SwitchParkEditorMenuPieceToMostRecentClipboard,
    0x0058F750 SwitchToParkEditorCamera (parked_cam made from parkedcam_composite_structure if missing),
    0x0058F870 BindParkEditorToController.
  - Resizing: 0x0058E170 GetResizeArea (cdecl, returns `out`: w= / l= or inc_w= / inc_l=, the corner moved by half
    the change), 0x0058E490 ResizeTo (cdecl, the area by value: the collision flag 0x0067F398 off, the shell and the
    park rebuilt, the rails' points 10 inside the park kept (0x004F74B0) and the goals inside it (0x004E8390), the
    clipboards too big deleted, the cursor hidden, the collision rebuilt). ResizePark: an area structure (0xB4F88771:
    x z w l, one step high) or GetResizeArea; flag 0x937EB5C8 keeps the area in a static (0x007CE5E0, guard
    0x007CE5E8), 0x6F2C496C uses the kept one; no memory for it: script 0xA9967819 and false. CanCleanlyResizePark:
    no pieces, rail points or goals would be cut off.
  - Kept / changed: values the original reads without a default from an uninitialised local (the area's x z w l,
    FireCustomParkGap's gap_index, BindParkEditorToController's controller) are 0 here when the script leaves them out.
    The parameter names the original passes as the exe's strings are string literals (as park_cfuncs.cpp).
  - Test (TestParkEditorCommands): every command, original vs ours, on hand-built parameter structures (the real
    getters) with everything they call stood in for: each flag / state / branch, resizing by structure, w / l,
    inc_w / inc_l (odd, negative), kept and reused areas, no memory, no cursor; CanChangeParkDimension at each limit;
    GetCustomParkName with 0, 1, 7, 20 characters and none. 65 deliberate bugs: all caught, 2 after recording the
    collision flag while the shell is rebuilt and testing IsCustomPark in test play.
