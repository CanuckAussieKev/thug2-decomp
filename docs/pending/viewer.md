# viewer - the range 0x577300-0x588180 (viewer / gaps / records / career save info)

Agent "viewer" (branch worktree-agent-a12992d40fe5be1e3). The lead's request: decompile docs/PROGRESS.md's "Viewer /
gaps / records / career save info" range start to finish, in batches. Batch records below, in the style of
decomp-progress.md (the lead numbers them when merging). Game workspace: C:\Games\THUG2-3.

Functions Ghidra never made (found with code-reference and padding scans; each gets source like the rest):
0x00577380, 0x005774C0, 0x005775A0, 0x00577670, 0x00577870, 0x00577CD0 (the viewer's tasks and PC tool handlers),
0x00578AA0 (the gap list's node callback), 0x00578CF0, 0x00578D50, 0x00579900, 0x00579910, 0x00579940, 0x005799A0,
0x00579A00; still to look at: 0x0057A190-0x0057A2C0's neighbours, 0x0057B070, 0x0057C200, 0x0057EF20, 0x00580830,
0x005809C0, 0x00584BC0, 0x00587480, 0x00587540, 0x00588090, 0x00588120 (and the 3-6 byte tails between functions:
some are jump-table / function tails Ghidra cut off).

Shared files touched: CMakeLists.txt (each new .cpp listed, additive).

- [x] Batch VW-1 (NOT run in the game yet; early tests pass offline): the rest of the viewer module (Mdl::Viewer,
      src/game/sys/viewer.*), 0x577380-0x577F20 - the constructor 0x577F20 (Module::Construct, vtable 0x0064EBC4, no
      commands; unless the debug checks 0x00684FA8 are on - they are in this exe - the PC tool's app server "THUG2
      Viewer" (port 10000, flags 0x10; a first inet_addr(local IP) and a 0x005B9760 call on the net manager are
      unused); the free camera task (0x20 bytes, vtable 0x006476F0), the input handler (0x2C, vtable 0x0064D7D8,
      controller = the script integer 0x702247A5), speeds 400 / 120, "Creating shift input handlers\n)", the Select
      combo task and the shift handler (priority 1000), the singleton 0x007CE4A8), v_start_cb 0x577DA0 (in the front-end
      heap: the PC tool's seven message handlers (0x20 ReloadScene, 0x21 0x0052DAC0, 0x22 the script reload, 0x2A
      AddToScene, 0x23 / 0x24 / 0x2B with the viewer as data, priority 0x80), the tasks onto the main loop's list, each
      handler onto its controller's list (+8), its control data (+0x44) and device (+4)); the tasks Ghidra never made
      into functions: the free camera 0x577870 (the frame's time from Tmr::GetTime into 0x007CE4AC; command 0x4000
      toggles the mode - to 1 only with a viewer object; dead zone: raw sticks within +-50 give 0, else (v -+ 50) / 78;
      on viewer_cam: mode 0 the left stick pitches about its own x (RotateLocal) and yaws about the world's y (its
      matrix times a y rotation), mode 1 the viewer object's 0x0058A2E0(leftX, leftY); the right stick moves it along
      its own axes (MoveObjectLocal, x and z); commands 2 / 4 up / down; unless the script integer 0xD8C91578, commands
      0x10000 / 0x20000 roll (a z rotation before its matrix, -+ half a degree a second times the speed); the camera
      unpaused; command 0x1000 the next of six settings (0x004A0710 / 0x004A0AD0, a table at 0x00649CC8); the commands
      cleared. Found: without viewer_cam the original goes on with a null object (the moves and the unpause write
      through it - a crash), kept as is) and the Select combo task 0x577380 (with select_shift, the first of
      UserSelectTriangle / Square / Circle / Start / X / L1 / L2 / R1 / R2 / Up / Down / Left / Right whose button was
      pressed with Select held, spawned if it exists); the PC tool's handlers 0x577CD0 (ReloadScene, scene = the
      message), 0x5774C0 (AddToScene, scene + Add at +0x80), 0x5775A0 (a viewer object from modelname = the message,
      skeletonName = the checksum at +0x84 - found: the viewer is taken from the message's first word, not the
      handler's data at +0x18, like the remove handler 0x577140 already rewritten; kept), 0x577670 (the script named by
      the message's first word, with the structure the script reader 0x0047A0C0 reads after it); and 0x577740
      (PlaceCamera, called by CFunc_LaunchViewer's neighbour 0x005A1C80 with the viewer: with the script global
      ResetCameraToViewerObject, viewer_cam's matrix the identity and its position ViewerObjPos + (0, 35, 100) - (0, 35,
      100) without one; RET 4, returns true). Names from the game's .qb checksum tables (UserSelect*, viewer_cam,
      AddToScene, ReloadScene, modelname, skeletonName). Debug mode's keyboard flying (Sys::DebugMode::KeyboardViewer in
      HandleViewerInput) is kept. Self-tests (new src/hook/selftests_viewer.cpp and the shared stand-ins
      src/hook/viewer_testkit.h): TestViewerFreeCamera (132 frames: every dead zone edge and NaN, modes 0 / 1 / 2, every
      command and combinations, with and without a viewer object and the roll lock; PlaceCamera off / no camera / with
      and without ViewerObjPos), TestViewerModule (the combos: 24 button sets x select_shift x scripts there or not; the
      four handlers; the start callback with and without the debug checks, handlers on either controller; the
      constructor both ways) - matrix builders and the D3DX product stood in for both alike, every call recorded, the
      whole arena compared. 41 deliberate bugs: 39 caught, 1 equivalent (the dead zone's edge: exactly 50 gives 0 both
      ways), 1 didn't build (rerun with batch VW-2).

- [ ] Next: batch VW-2 - cars made in code and the KOTH crown (0x5780E0-0x578490, src/game/obj/game_objects.*), the
      skater trick code's two list entry types (0x578670, 0x578740, 0x5787F0, 0x579570-0x579630,
      src/game/skater/trick_lists.*), written, not built yet; then the gap checklist (0x578680-0x5796C0,
      src/game/skate/gap_checklist.*, written) and the balance class (0x579850-0x579CC0, src/game/skater/manual.*,
      written); tests selftests_viewer2.cpp / selftests_viewer3.cpp written (viewer_testkit.h needs
      InstallCommon(s, bool deleteStruct) and g_printfArgs for viewer3). Then 0x57A180 on.
