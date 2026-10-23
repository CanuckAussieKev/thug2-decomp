# GameFunction calls into rewritten code -> direct calls, part 2 (agent GC2)

Branch worktree-agent-a9f64278608fad003. The calling files: src/game/skater/ (every file not named core_*),
src/game/sfx/, src/game/file/, src/game/sio/. `node tools/find_game_calls.ts --list` listed 253 calls there
(248 + controls.cpp's 5); none is left: every one is a direct C++ call, a field read (folded getters) or written
out (stubs, a jump). No new decompilation; no shared header changed (no declarations needed adding). Only tests and
the calling files were edited, plus a test helper or two inside the test files.

- [x] Batch GC2-1 (NOT run in the game yet; file, sfx, sio - 31 calls).
  - file/async.cpp: Mdl::Module Construct / DestroyBody, Tsk::BaseTask::Construct, Tsk::Stack::AddTask. async.h's three
    stubs (0x005B6570 true, 0x005BA1D0 0, 0x005B9E50 false - folded, source only) are written as their values
    (`AsyncLoadingAvailable` true, `CachedFileSize` 0, `LoadCachedFile` false). file.cpp: Nx::ShowDiscError (the error
    code it was pushed is unused). pre.cpp: Pcm::UsingCD, Tmr::GetTime.
  - sfx.cpp: the call to the folded stub 0x004504F0 (returns false) after setting the pitch is left out (a comment
    says so). sfx_voices.cpp: the cameras' GetPos / GetMatrix (source only - checked: lea +0x5C / +0x6C), Mth::GetDistance,
    Mth::GetAngle, Vector::RotateAboutY, and the object sound calls - the "object" the sound manager is handed is the
    object's CSoundComponent (sound_component.cpp passes AsSoundObject(this)), so they call GetSoundExtent /
    GetSoundPosition / GetPreviousSoundPosition / KeepLoopingSound / UpdateProps on it. terrain.cpp: GameNet::Manager::GetClient.
  - sio: inpman.cpp (Tsk::Stack Construct / Process / Destruct, BaseTask::Construct, Inp::Manager OverridePadWithStick /
    Destruct), device.cpp (BaseTask::Construct), sdl_input.cpp's InParkEditor (CScreenElementManager::GetElement into an
    ElementPtr; its destructor releases it as 0x004637E0 did).
  - Tests: selftests_objsys9.cpp (TestSfxPositional / TestSfxVoices: the sound component stand-ins, and the
    GetDistance / GetAngle byte jumps, now over ours too), selftests_mva.cpp (TestInpManager), selftests_obj_terrain.cpp.
- [x] Batch GC2-2 (NOT run in the game yet; the skater's smaller components - 61 calls): controls, end_run,
  finalize_physics, flip_and_rotate, floating_name, local_net_logic, looping_sound, manual, physics_control, rotation,
  run_timer, skater_score, skater_sound, special_item, tagging, trick_component, trick_member_functions, vehicle_sound.
  - Folded / odd ones: manual.cpp's 0x005480C0 (FoldedGetters::Word04) is the network manager's m_server; run_timer's
    call of 0x005BAB90 (the base's empty GetDebugInfo) is left out with a comment, as vehicle_sound.cpp already did;
    tagging.cpp's tag settings come from walk_parameters (0x6775E538 is kWalkParameters) - CWalkComponent::Param.
  - controls.cpp's field constants were misnamed: kSound (0x177C) is the walk component's trigger component,
    kTrickComponent (0x1784) its movable contact component, kTrickName (0x17B4) its trigger feeler (walk_fields.h has
    them right) - renamed kTrigger / kMovableContact / kTriggerFeeler; the calls are CorePhysicsComponent::FindAcidDrop,
    CMovableContactComponent::StartRiding, CTriggerComponent::CheckFeeler, CWalkComponent::SetState / Param.
  - Types: the terrain table index is a byte (GetTableIndex returns uint8_t; callers held it as a pointer - only its
    byte was ever read); physics_control's SetWalking passes 0 / 1 to SetPhysicsMode (an int; the original pushes 1 / 0);
    the folded PlayerInfo getters return 0 / 1 ints (compared != 0 where callers want bools).
  - GetScaledPhysics (0x00580790) returns a float in ours; callers that kept the original's unrounded x87 value
    (rotation.cpp's StatValue, finalize_physics, run_timer) now get it rounded - as they already did through the hook.
    Not a change made here, but worth knowing.
  - Tests: selftests_skaterB.cpp (break vert / spin: our GetPhysicsFloat / Int, GetScaledPhysics, GameClockElapsed,
    GetAngleAbout patched too), selftests_viewer4.cpp (TestBalance: the 0x005480C0 stand-in replaced by the fake
    manager's +4 word set per frame, read by both), selftests_skater_end_run / flip_name / physics_control / sound_net /
    state_component / tagging / timers / trick / trick_queue / vehicle_sound.cpp, selftests_pc.cpp (TestSkaterAcidDrops:
    CodePatches over ours too). The terrain stand-ins record the table's byte (state_component) / answer a byte
    (sound_net); timers no longer stands in for 0x005BAB90.
- [x] Batch GC2-3 (NOT run in the game yet; state history, non-local net logic, walk - 67 calls): state_history.cpp,
  nonlocal_net_logic.cpp, nonlocal_playback.cpp, walk_collision / movement / moves / physics.cpp.
  - state_history.cpp's helper names don't all match what the functions are: IsObserving calls
    PlayerInfo::IsInvulnerable, IsIt IsKing, OnATeam IsCarryingFlag, Team GetFlagTeam, MarkTagTime MarkJustJoined, MakeIt
    MarkAsKing, TagOnTeam RetrievedFlag, ConvertToTeam StoleFlag, ManagerFlag4 InInternetMode, ManagerFlag3A IsGameOver
    (source only: mov al,[ecx+0x3A] - checked), LocalSkater the manager's +0x6C - what the network code calls GetCrown
    (source only: mov eax,[ecx+0x6C] - checked; `IsNearLocalSkater` may really be "near the crown"). Kept the local
    names, documented what they call. ManagerApp is the manager's m_server (folded 0x005480C0).
  - nonlocal_net_logic.cpp: 0x00532EB0 ("Manager::GetMatchClient", source only) read the Nx model's +0x2C (its light
    object) - written as that field read (the folded getter the HANDOFF warned of).
  - nonlocal_playback.cpp: the model's three parts are the model component's m_spin axes (CModelSpinAxis Add / Clear);
    Add's base / range were passed as raw words: read as floats now (the same bits). HideGeom's hide and Add's hold are
    bools: the message's byte (any value) is passed as `!= 0` - the original passes the byte; both functions only use it
    as a truth value (HideGeom passes it on to the model's 0x00493xxx, not rewritten - a byte above 1 would be stored
    as 1 now: the network messages carry 0 / 1).
  - walk_moves.cpp: FirstChannel47E850's int "send" argument is passed as its bits (std::bit_cast, as walk_physics /
    trick_queue already do); GameClockTimeSince read as unsigned as before.
  - Tests: selftests_skater_state_history.cpp, selftests_skater_nonlocal.cpp (0x00532EB0's stand-in replaced by the Nx
    model's +0x2C set in IsHitTriangleCollision's stand-in, the model moved to its own arena slot kNxModel),
    selftests_skater_nonlocal_playback.cpp (0x0053AEA0 records the id's low 16 bits - the original passes the whole id,
    PlayerByObjectId reads 16; HideGeom / Add record their bools as truth values), selftests_skater_walk /
    walk2 / walk_collision / walk_pieces.cpp (sk5 kit: `{Code(&ours)}` added).
- [x] Batch GC2-4 (NOT run in the game yet; skater.cpp 86 calls, trick_queue.cpp 10).
  - skater.cpp: the lights 0x00532EB0 (Nx model +0x2C) and 0x00456B30 (a naked jump through vtable slot 5 -
    Net::VirtualSlot5Jump, REPLACE_UNCHECKED) are a field read and a vtable call; 0x005480F0 (FoldedGetters::Word18) on
    the collision component is its +0x18; 0x00411840 (Nx's VectorTurn::TurnBy, a struct local to coll_rect.cpp) is
    written out as the RotateVector it calls. PlayAnimation's second word was passed as 0.3f: bit_cast to uint32_t (the
    same bits). ReplaceTexture: skater.cpp passes a checksum (modelSwap2) where ours declares `const char* dest` - passed
    on as the same bits; one of the two is misnamed (not resolved here). SetActive / SetBoardRotated / HideGeom get
    `byte != 0` where the original passed the byte: the original 0x0043D560 compares the byte with its stored 0 / 1 and
    tests it - the same outcome (checked in its disassembly).
  - trick_queue.cpp: MatchesIgnore's `ignore` word goes to TrickNamed's `const char* name` (a string pointer kept in a
    word) - passed as the same bits.
  - Tests: selftests_viewer10.cpp (TestSkater: the lights' getter / jump stand-ins replaced by the model's +0x2C and a
    stand-in vtable whose slot 5 records the same), selftests_viewer11-15.cpp (an `OursOf` / `AlsoOurs` helper: each
    `Simple` stand-in also covers ours for the addresses skater.cpp now calls directly - only those: covering every
    Simple address hung TestSkaterProfile; viewer12's 0x005480F0 stand-in replaced by the collision component's +0x18,
    its SetActive stand-in records the bool as a truth value; viewer13's 0x00411840 stand-in removed - the original's
    runs and calls the stood-in 0x0040B090 as ours does), selftests_skater_trick_queue.cpp.
- Checks after each batch: thug2_check on THUG2PM.exe and the stock THUG2.exe - 0 MISMATCHED, 771 early self-tests all
  passed (main's count); check_code_writes "No overlaps". At the end on C:\Games\THUG2-ghidra-snapshot-gc2:
  scan_register_assumptions 5 RISK (the 5 known), audit_game_function_calls 0 MISMATCH.
- Mutations: 26 converted call sites over the four batches (a wrong argument or the call dropped), each with only the
  tests that cover it (out/scripts/gc2_mutations.ts + gc2_mutrun.sh in the worktree; GC2_GROUP picks the tests) - all
  26 caught. GC2-1: GetDistance's arguments swapped, RotateAboutY dropped (sfx positional / voices), GetClient(1)
  (terrain manager), OverridePadWithStick dropped, a task's priority + 1 (Inp). GC2-2: GetAngleAbout's arguments
  swapped (spin), CheckBalanceTime's last argument (balance), SendGameEnded dropped (end run), the physics mode
  reversed (physics control), AnyChannel's last argument (flip and rotate), ExpireButton's ms (trick member
  functions), SetState(0) (acid drops), PlayTerrainSound's propagate (skater sound). GC2-3: FirstPlayerInfo without
  observers, MarkAsKing(!it) (state history collisions), the light field +0x30 (non-local lighting), MarkAsFullyIn
  dropped (non-local interpolate), WallPlant's checkOnly (walk collision), the send word converted instead of
  bit-cast (walk animation speed). GC2-4: PlayAnimation's word 0 (skater init), the light's slot-5 argument
  (TestSkater), the collision flag 4 (skater init), SetGoalStatus(i, 0) (TestSkater), SetActive's truth reversed
  (skater init; `SetActive(true)` survives - every test byte is nonzero, an equivalent mutation for that data),
  SetPendingTrick's flag, QueueAllDoneTricks dropped (trick queue). Four (expire-ms, lights-slot, goal-status,
  fully-in) were run by hand (out/scripts/gc2_mutone.py): their test sets take longer than mutate.ts's 60 s timeout
  with four agents' checks running (TestSkater* is 42 tests, ~77 s), which mutate.ts reports as a hang.
- Left for later (not GameFunction calls, so not counted by find_game_calls.ts): about 95 calls in these files still go
  to rewritten functions through address-taking helpers - walk_moves.cpp's `Call(0x..., ...)` (~75: the walk
  component's own methods, StartRiding / StopRiding, CheckFeeler, Assign...), trick_member_functions.cpp's
  `Call` / `CallOn` (11), skater.cpp's reset loops over trick component addresses (2 loops), physics_control.cpp's
  CameraCall (2: the walk camera's Save / RestoreState). skater.cpp's network handler table holds addresses as data
  (registered with the dispatcher) - fine until the exe goes.
