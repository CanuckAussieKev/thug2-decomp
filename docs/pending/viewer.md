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

- [x] Batch VW-2 (NOT run in the game yet; early tests pass offline): cars made in code, the King of the Hill crown
      and two list entry types of the skater's trick code (0x5780E0-0x578670, 0x578740, 0x5787F0, 0x579570-0x579820).
      New src/game/obj/game_objects.*: InitCar 0x5780E0 (the motion components (0x57B2F0 / 0x57B420 - later
      batch), the motion's speeds 216 / 384 / 528, 0x00423C90 / 0x00423510's 240, car_add_components, Finalize, the
      model's update 0x00420480) and CreateCar 0x578170 (OBJECT_TYPE_CAR, base components 0x57B250, one more 0x58-byte
      component 0x0041BAD0, at the node's position, registered); Obj::CCrownObject (0x114 bytes, vtable 0x0064EC44):
      Construct 0x578240 (type 0xD2156C50, id crown_object), PutOn / TakeOff / IsWorn 0x5782B0 / 0x5782D0 /
      0x5782E0, DestroyBody / Destroy 0x5782F0 / 0x578460, InitFromNode 0x578490 (registered with the given
      manager, a model component, never suspended (the suspend component's 1000000-1000003), crown.mdl through
      0x004879D0 (returns a String: made in raw storage), at the node), OwnUpdate 0x578310 (vtable +0x28: the skater's
      head bone matrix (0x004A36E0), turned 90 about x, raised 18 (0x0048A260), in the skater's frame, at its
      position). New src/game/skater/trick_lists.*: TrickListEntry (0x64 bytes, vtable 0x0064EC80: Construct
      0x578740 with its own script array, DestroyBody 0x579570, Destroy 0x579820, SetFlag 0x578670) and TrickNameEntry
      (0x34 bytes, vtable 0x0064EC84: Construct 0x5787F0, DestroyBody 0x579630, Destroy 0x579600). Found: our String
      constructor clears m_capacity where the original's leaves it (string.cpp says so) - the test zeroes that word.
      Self-tests (new src/hook/selftests_viewer2.cpp): TestGameObjects, TestTrickLists. 28 deliberate bugs: all caught
      (4 after strengthening: constructors now run on 0xCD-filled memory and their objects are compared right after
      construction).

- [x] Batch VW-3 (NOT run in the game yet; early tests pass offline): the gap checklist (new
      src/game/skate/gap_checklist.*, Game::CGapChecklist and CGapCheck), 0x578680-0x5796C0 incl. the missed
      0x578AA0, 0x578CF0, 0x578D50: CGapCheck (0x50 bytes, vtable 0x0064EC7C: Construct 0x5786E0, DestroyBody 0x579510,
      Destroy 0x5794E0) and its Lst::Node / Lst::Head destructors 0x578680 / 0x5786A0 / 0x5794B0 (the head's: vtable
      0x0064ED20); CGapChecklist: Construct 0x579690, Clear 0x578840, AddGap 0x578880 (found again by name (case
      counts) and score; else sorted after the last lower score / same score and name sorting first, ignoring case;
      script heap), AddGapFromNode 0x578A00 + its callback 0x578AA0 and Build 0x578AC0 (every EndGap the TriggerScripts
      reach; PrintGaps), FindGap 0x578B30, SetGapPositions 0x578BC0 (two Vectors by value), GotGap 0x578C70,
      ClearPending 0x578CF0, Count 0x578D20, AllGot 0x578D50, Reset 0x578D90, GetGapTotals 0x578DD0, AddGapsToMenu
      0x578E40, CreateGapList 0x579080, GetGap / GetGapName / GetGapCount / GetGapScore / GetGapPositions
      0x5793D0-0x579460, AwardPendingGaps 0x5796C0 (the goal manager told; got_gap_for_first_time,
      multiple_new_gaps from the second on - a signed byte count); the script commands AddGapsToMenu 0x5792E0,
      CreateGapList 0x579300, GetLevelGapTotals 0x579320 (found: level= uninitialised in the original if missing;
      ours 0), GiveAllGaps 0x579360. Names from the .qb checksum tables. Self-test (new src/hook/selftests_viewer3.cpp):
      TestGapChecklist (every add order case, found again, case-only differences, the queries, landing and awarding
      with the goal manager listening or not, the four commands with no / one / many gaps, building from stand-in
      node scripts with and without PrintGaps, a fresh build, the destructors). 32 deliberate bugs: all caught (3
      after strengthening: a fresh Build with no gaps, Reset with a gap out of the level, a gap landed twice).
      viewer_testkit.h: InstallCommon(s, deleteStruct) and g_printfArgs (shared kit, my files only).

- [x] Batch VW-4 (NOT run in the game yet; early tests pass offline; mutation run pending): the balance of a balance
      trick (new src/game/skater/manual.*, Obj::CBalance - a manual, grind, lip or skitch; 0x58 bytes, vtable
      0x0064ED24), 0x579850-0x579CC0 incl. the missed 0x579900 / 0x579910 / 0x579940 / 0x5799A0 / 0x579A00:
      DestroyBody 0x579850, Init 0x579860 (SkaterBalanceTrick, SkaterScore, input), Reset 0x5798A0
      (reset_balance_indicator), ClearBestTime / HideDisplay / ShowDisplay / KeepBestTime / ShowMeter 0x5798F0-0x579940,
      Construct 0x579950, Destroy 0x5799A0, Stop 0x5799D0, Start 0x579A00 (grinds: Same_Grind_Add_Time /
      New_Grind_Sub_Time by the rail (0x0057C200), robot_rail_add_time / robot_rail_nudge for runs under half a
      second; the lean speed repeated (Repeat_Multiplier, at least Repeat_Min; manual / Nosemanual / random side), the
      lean repeated (Lean_Repeat_Multiplier + the cheese), kept within 0.9 x Lean_Bail_Angle), Update 0x579CC0
      (CheeseFrames, the score's trick time 0x00564270, the stats manager, the lean's growth (Lean_Gravity_Stat,
      Instable_Rate / Instable_Base), the buttons (Lean_Acc after BalanceIgnoreButtonPeriod or a release - found: the
      safe-period scale is an unsigned integer division, so 0 inside it), the random shake (Lean_Min_Speed /
      Lean_Rnd_Speed), the bail (OffMeterTop / OffMeterBottom), the anim blender's meter (lean + 4096 over 8192,
      flipped), the score's displays 0x00563370 / 0x00563390 (Up / Down buttons)). x87 rounding followed: sums kept
      unrounded where the original keeps them on the stack. Self-test (new src/hook/selftests_viewer4.cpp):
      TestBalance (starts for each balance type and state, 96 frames with every button / period / flip / display
      case; the physics values, GetFloat, the frame time and Sign stood in by C++ functions jumped to).

- [x] Batch VW-5 (NOT run in the game yet; early tests pass offline; mutation run pending): the movie manager (new
      src/game/obj/movie_manager.*, Obj::CMovieManager: a CObject, vtable 0x0064ED34, 0x12C bytes - the skate module's
      +0x368), 0x57A180-0x57B240 incl. the missed 0x57B070 / 0x57B0B0, and the list / copy helpers there: UseKeptCamera
      0x57A180, CObject's and Gfx::Camera's compiler-made copy assignments 0x57A190 / 0x57A220, Lst::Node<CMovie> /
      Lst::Head<CMovie> destructors 0x57A200 / 0x57A2C0, the shared Lst::Head DeleteAllNodes 0x57A2F0 (11 callers),
      Count / GetMovie / FindMovie 0x57A330-0x57A3A0, KeepCamera 0x57A450, Construct 0x57A4B0, GetCamera 0x57A530,
      StopAll 0x57A5A0, ClearMovies 0x57A640, EndMovie 0x57A6B0 ("Details have been removed %ld" with the 64-bit
      render frame; the end script with a copy of its parameters), EndMovies 0x57A7E0 (skippable chains; a
      compiled-out walk kept), the by-name calls 0x57A940 / 0x57A9B0 / 0x57AA20 / 0x57AA70 / 0x57AF60, KillMovie
      0x57AAC0, AddMovie 0x57AB60 (asset camera animations of kinds 5 / 9, cutscenes, skater cameras clearing the
      queue; found: a missing asset returns without restoring the kept-camera flag; an unknown type is a null movie),
      Update 0x57AE60 (vtable slot 8), CallMemberFunction 0x57AFC0 (slot 3), GetPlayingName 0x57B030 (used by
      menu_backdrop.cpp), PlayingFlag24 0x57B070, AnyMovies 0x57B0B0, Playing2C / Playing30 / Playing34
      0x57B0E0-0x57B160 (tail jumps through the movie's vtable; found: 0x57B120 passes its caller's argument on),
      DestroyBody / Destroy 0x57B1A0 / 0x57B220. Self-test (new src/hook/selftests_viewer5.cpp): TestMovieManager
      (stand-in movies with a recording vtable; every add / query / end / kill / update case, the helpers, the
      destructors). viewer_testkit.h's debug output records %s arguments as text, the others raw.

- [x] Batch VW-6 (NOT run in the game yet; early tests pass offline; mutation run pending): the rail manager (new
      src/game/obj/rail_manager.*, Obj::CRailManager and CRailNode - 0x54 bytes), 0x57C170-0x57E960 incl. the missed
      0x57C200: the box / interval helpers Span4 0x57C170, BoxPair::Swap 0x57C210, BoxOf 0x57C260, Overlap 0x57C360,
      NearlyEqual4 0x57C3A0, NodesTogether 0x57C5A0 (stdcall), Record24 / Record34 copies 0x57C500 / 0x57C540;
      Cleanup 0x57C330, MoveNode 0x57C3F0 (found: its previous-node fix-up tests the moved node's own link count
      times - never fires), SetObjectMatrix 0x57C4F0, RailClear 0x57C600 (cdecl, two Vectors by value: feelers across
      the rail and down 12 on each side; always clear in the park editor's level), ToWorld / GetWorldPos 0x57CB20 /
      0x57CB60 (both return their out), FindNode 0x57CBD0, Init 0x57CC10 (returns this), CRailNode::GetWallSide
      0x57CC30, GetClimbMatrix 0x57CFB0, AddNode 0x57D090 (Class / type / flags / TerrainType / climbing angles),
      RemoveOverlaps 0x57D260 + RemoveOverlapsIfNotPark 0x57D7A0, SetActive 0x57D7C0, IsActive 0x57D840, FindLadder
      0x57D880, FindLadderOnLine 0x57DA20 (Climb_Max_Snap; TRG_Ladder03 counted 4 times as far), FindRail 0x57DDA0,
      FindJoiningRail 0x57E170, FindGrindRail 0x57E2D0 (Rail_Max_Snap; found: a better-scoring rail out of reach still
      takes the best score and drops what was found), AddFromNodeArray 0x57E960 (RailNode / ClimbingNode, a name hash
      table with linear probing, TrickObject clusters, Links; DefaultLine wins). Every feeler's x87 rounding followed
      (some sums kept unrounded, as the original's stack). Self-test (new src/hook/selftests_viewer6.cpp):
      TestRailManager (a level of rails, an overlapping rail, a ladder, a lone node; feeler hits scripted call by
      call; the checkers stood in; building from a stand-in node array with a hash collision). The x87 original of
      ClosestPointsBetweenLines differs from ours by an ulp now and then: both runs use ours there.

- [x] Batch VW-7 (NOT run in the game yet; early tests pass offline; mutation run pending): the component set-up
      helpers and peds (game_objects.*: AddBaseComponents 0x57B250 - suspend and a 0xB87C8E43 component; AddMotionComponents
      0x57B2F0 - motion, objectproximity, sound, stream, lockobj; AddNodeComponents 0x57B420 - found: with no motion
      it adds the motion components but goes on with a null motion; CreatePed 0x57B4E0 - OBJECT_TYPE_PED,
      ped_add_components / ped_init_model / ped_disable_bones, the motion's speeds) and the player profile manager (new
      src/game/skate/profile_manager.*, Obj::CPlayerProfileManager, 0x40 bytes: a Lst::LookupTable of template
      profiles - its generic code 0x557F10 / 0x561430 / 0x5628B0 / 0x562970 called - two profiles in use, the current
      index (what sio/device.cpp's MenuPort 0x553E90 reads), a lock flag, three extra profiles), 0x57B620-0x57C120:
      SetCurrentIndex / GetCurrentProfile / GetProfile / GetExtraProfile / SetLocked 0x57B620-0x57B680, Init 0x57B690
      (init_pro_skaters; the first player's profile is the first template itself, the second's a copy),
      GetTemplateName 0x57B770 (found: returns the key, not the profile), ResetSecondProfile 0x57B790, GetTemplate
      0x57B7E0, ResetTemplates 0x57B800, WritePros 0x57B840 (CurrentSkater, DisplayName), WriteCustomSkaters
      0x57B920, AddExtraProfile 0x57B970, the table's Clear 0x57B9E0 and destructor 0x57BBF0, SetCurrentSkater
      0x57BA20, ReadPros 0x57BAA0, AddTemplate 0x57BB40, Construct 0x57BC00, DestroyBody 0x57BC20, ReadCustomSkater
      0x57BD70 (version 2 saves only: the appearance, FaceTexture, the info; the ten stats and max_specials and the
      specials put back unless the save's are kept; returns the last lookup's result), Create / Release 0x57C0D0 /
      0x57C120 (0x7CE4B4 / 0x7CE4B8). CSkaterProfile's own methods (0x585xxx-0x586xxx) called through the game for now.
      Self-tests (new src/hook/selftests_viewer7.cpp, selftests_viewer9.cpp): TestProfileManager (the real lookup
      table, stand-in profiles; every path above), TestObjectSetup. selftests_viewer2.cpp's stand-ins for the helpers
      now cover ours too.

- [x] Batch VW-8 (NOT run in the game yet; early tests pass offline; mutation run pending): the records (new
      src/game/skate/records.*, namespace Records), 0x57ECD0-0x57FE27 incl. the missed 0x57EF20: CRecord (0x10 bytes:
      initials buffer, value, number, new) Construct 0x57EF20 (XXX, 1000, 0), MaybeNewRecord 0x57ECD0,
      ReadFromStructure 0x57ED00; CRecordTable (count + a new[]-style array) Construct 0x57EF60, MaybeNewEntry 0x57EFE0
      (the later ones moved down, the last dropped), UpdateInitials 0x57ED80, ReadFromStructure 0x57F0C0 ("RecordTable",
      only if the sizes match), WriteToStructure 0x57F4E0; CLevelRecords (0x1C: high scores 5, best combos 5, arcade
      scores 10, the longest combo / grind / lip trick / manual - their values 0) Construct 0x57F120, DestroyBody
      0x57F670, UpdateInitials 0x57EDD0, ReadFromStructure 0x57F340 (found: one structure pointer for all - a missing
      one reads the one before again), WriteToStructure 0x57F7B0; CGameRecords (0xC) Construct 0x57EE70 (ABC),
      SetDefaultInitials 0x57EEA0, GetLevelRecords 0x57EEC0, InitLevels 0x57FBC0, ReadFromStructure 0x57F440
      ("GameRecords", "DefaultInitials"), WriteToStructure 0x57FC50, Create / Release 0x57EED0 / 0x57FDB0 (0x7CE4BC /
      0x7CE4C0). The CRT's vector constructor / destructor iterators written as loops (same order). Self-test (new
      src/hook/selftests_viewer8.cpp): TestRecords.
      Tests strengthened after VW-4/5/6's mutation runs: the balance (a repeated lean speed under the minimum, the
      meter shown before a start, the slow random shake, buttons from the start through the ignore period, the up/down
      displays on one side), the movie manager (the other by-name call on an empty queue, ending with nothing queued,
      one movie's counts and its last update), the rail manager (Cleanup clears the on-object flag).

- [ ] Next: (state for resuming)
      - Batch VW-2 (cars / KOTH crown 0x5780E0-0x578490 in src/game/obj/game_objects.*; the skater trick code's two
        list entry types 0x578670 / 0x578740 / 0x5787F0 / 0x579570-0x579820 in src/game/skater/trick_lists.*) and
        VW-3 (the gap checklist 0x578680-0x5796C0, src/game/skate/gap_checklist.*): written, built, early tests pass
        (selftests_viewer2.cpp, selftests_viewer3.cpp). Test holes the mutation run found (fix, then rerun those
        mutations): constructors tested on zeroed memory (fill with 0xCD first: crown_worn_init, list_key, list_38),
        the constructor's vtable overwritten by the destructor before the arena is compared (push the object's
        words right after construction: list_vtable), Build on an already-built list (build_flag: use a fresh
        checklist), Reset with every gap already in the level (reset_inlevel: clear one first).
      - Written, NOT in CMakeLists / not built yet: src/game/skater/manual.* (VW-4, the balance class
        0x579850-0x579CC0, test selftests_viewer4.cpp, mutation spec in my scratch mut4.py), src/game/obj/movie_manager.*
        (VW-5, 0x57A180-0x57B240 incl. missed functions 0x57B070 / 0x57B0B0, test selftests_viewer5.cpp - its Printf
        needs the kit to record %ld arguments raw), src/game/obj/rail_manager.* (VW-6, in progress: 0x57C170-0x57CC10,
        0x57CC30, 0x57CFB0, 0x57D090, 0x57D260, 0x57D7A0-0x57D840, 0x57D880, 0x57DA20, 0x57DDA0 written; still to
        write: 0x57C600 RailClear (feelers; by-value Vector args), 0x57E170, 0x57E2D0, 0x57E960, 0x57ECD0, 0x57ED00,
        0x57ED80, 0x57EDD0, 0x57EE70; no test yet).
      - Then: 0x57B250 / 0x57B2F0 / 0x57B420 / 0x57B4E0 (component helpers and CreatePed: add to game_objects.cpp),
        the profile manager 0x57B620-0x57BD70 + its singleton 0x57C0D0 / 0x57C120 (with the profiles 0x585xxx-0x586xxx),
        then 0x57EE70 on (records 0x57EF60-0x57FEA1, then 0x57FEB0-0x58808F, 0x5880E0-0x58817C).
      - Speed: mutation runs take ~2.5 min each (build + link + check); keep batches to ~20 mutations, or run them in a
        copy of the tree so building can go on.
