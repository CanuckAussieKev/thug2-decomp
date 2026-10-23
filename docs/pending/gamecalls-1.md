# GameFunction calls into rewritten code -> direct calls: skater core, skate, ed, park, script - GC1

Branch worktree-agent-acc27e85c77dda5ca. No new decompilation: calls under src/game/skater/core_*, src/game/skate/,
src/game/ed/, src/game/park/ and src/game/script/ that reached our rewritten functions through
`GameFunction<...>(0xADDRESS)` (or a local `Call(0x..., ...)` / `CallThis(0x..., ...)` helper taking an address) are
direct C++ calls now. Self-test stand-ins that stood in for a converted callee at its game address stand in for ours
too (the harnesses' `also` / `Ours(...)` fields). No game runs (the user tests in the game).
Shared files touched (additive): tools/mutate.ts (MUTATE_TIMEOUT_MS overrides the 60 s check timeout - a busy
machine runs a 33-test selection in ~110 s).

- [x] Batch GC1-1 (NOT run in the game yet; the skater core's shared helpers, core_physics.cpp, core_collision.cpp,
  core_jump.cpp - 59 GameFunction calls as the tool counts them, plus 15 `CallThis(0x...)` calls into rewritten
  functions):
  - core_physics_fields.h / core_fields.h (every core_* file uses them): the clock (Tmr::GameClockElapsed), the
    physics values (Obj::GetPhysicsFloat / GetPhysicsInt), the stat value (CSkater::GetScaledPhysics), Vector::SetLength,
    the pad buttons' times (CControlButton::GetPressedTime / GetReleasedTime / ClearTrigger), the trigger component's
    feeler check (CTriggerComponent::CheckFeeler), the feeler's assignment / destructor body (Nx::CFeeler::Assign /
    Destruct), the ride let go of (CMovableContactComponent::StopRiding) and the core's own feeler
    (CorePhysicsComponent::CheckFeeler).
  - core_physics.cpp (DoGroundPhysics' landing check, DoAirPhysics, LandFromAir): its own copies of those helpers, and
    SetTerrain, StartRidingHitObject, SetTrickObjects, FindAcidDrop / StartAcidDrop, WallPlant, CRideTracker::Update,
    CheckAirForwardCollision, KeepOffCeiling, GameClockTimeSince (still compared unsigned), PlayLand, SetLastGroundFeeler;
    the `CallThis` calls of LeanInAir, RotateInAcidDrop, RecoverInAir, KeepUpright, StopRiding, CheckSideCollisions,
    VertWallplant, StartRidingHitObject, FindNearestWallSide. Left: the unrewritten 0x004FE860, 0x004FEA50, 0x004FDED0,
    0x004FEBA0, 0x004FF470, 0x004FFB90, 0x004FE730, 0x004FFDB0, 0x004FC400, 0x004FFB10, 0x004FC470, 0x004FF590,
    0x004FC370.
  - core_collision.cpp: the helpers CheckLip / TryWallplant / Call5039F0 went - they were StartWallRide (0x00508140),
    WallPlant and BounceOffWall (0x005039F0; "not known yet" in the old comment); PlayBonk, Vector::SetLength.
    core_jump.cpp: PlayJumpInState, TurnForRailJump, Walk::MovableContact::StartRiding, Skitch, KeepUpright, WallPlant.
  - Types kept: bools where the callee takes bools (WallPlant(true, false, false) for the original's 1, 0, 0);
    GetScaledPhysics returns a float (StatValue's comment said the original's result is unrounded - through the hook
    our float version was already what callers got).
  - Tests: the stand-ins for these callees in selftests_skaterA, skater_acid_drop, _collision, _core_update,
    _feelers, _grind, _ground, _jump, _rail, _state, _states, _turning, _wallplant, _wallride, _walls stand in for
    ours too (`also` / `Ours(...)`; 76 + 58 entries, written by out/scripts/add_also.py / add_ours.py).
  - 23 deliberate bugs (out/scripts/mut1.ts, the 33 skater core tests): 22 caught; 1 equivalent (GetPhysicsInt's
    `required` changed: ours ignores it - the assert is compiled out).
  - Built, thug2_check on THUG2PM.exe and the stock THUG2.exe: 0 MISMATCHED, 771 early self-tests pass;
    check_code_writes: no overlaps.
- [x] Batch GC1-2 (NOT run in the game yet; the rest of src/game/skater/core_* - 85 GameFunction calls, plus 7
  `Call(0x...)` in core_ground.cpp): core_grind, core_physics_wallplant, _feelers, _state, _walls, core_rail,
  core_states, core_wallride, _turning, _skitch, _acid_drop, core_ground, _speed. No call into rewritten code is left
  in skater/core_* (find_game_calls and a scan for literal addresses of rewritten functions).
  - Odd / found: core_states.cpp read the point from GetContactPoint's return value (EAX: the original 0x00427A00
    returns `out`), but our CMovableContactComponent::GetContactPoint is `void` - through the hook the caller (ours)
    read whatever EAX held. Now the point is read from the out parameter. **If any original game code reads 0x00427A00's
    EAX, our void version breaks it in the game** - obj/movable_contact (not my file) should return `out`.
    0x00476EC0 (BoolAssertGetters::GetChecksum, the bool-assert wrapper) -> CStruct::GetChecksum(name, out, 1).
    0x0043DA70 (CWalkComponent::Param) for the grind's jump-adjust walk parameter - correctly named (a walk parameter).
    StartGrindTrick's fourth argument was passed as the raw byte +0x34C (only ever 0 / 1) - now `!= 0` for the bool.
    core_physics_walls passes PlayBonk a terrain word whose upper bytes are the angle's (as the original): kept.
  - Tests: 35 + 28 more stand-ins for ours (same files as GC1-1).
  - 15 deliberate bugs (out/scripts/mut2.ts): all caught.
  - Built, thug2_check both exes: 0 MISMATCHED, 771 early self-tests pass; check_code_writes: no overlaps.
- [x] Batch GC1-3 (NOT run in the game yet; src/game/skate/ - 40 GameFunction calls as the tool counts them, plus
  51 `Call(0x...)` calls and 7 handler addresses in skate_net.cpp): trick_goals.cpp (the screen element manager and
  elements, the text / container constructors, SetID, the profile's info structures, the clock, the beat goal's sound
  volume - Sfx::sVolume over the file's own SoundVolume layout - and PlaySound, GetComponent, CSkater::GetScore),
  victory_conditions.cpp (the network manager's player lists, local player, team score, PlayerByObjectId,
  PlayerInfo::IsEliminated, GetScore), gap_checklist.cpp (the career's checklist / level), skater_profile.cpp
  (WriteToBuffer / ReadFromBuffer, SetTrickMappings, the empty asserts as Stubs::DoNothing), profile_manager.cpp,
  skate_net.cpp (every GameNet::Manager / PlayerInfo / Net::Conn / CSkater / trick object manager call; the task's
  constructor and Tsk::Stack::AddTask; the handlers registered by address - HandleClientObjectUpdate, the state
  history's HandleCollisionMessages / HandleHitMessages, the local net logic's HandleGraffitiSteal - are our functions).
  - Folded getters written as what they do (the REPLACE name doesn't describe these callers): 0x00469490
    (Spt::PointerMember::Get) for the goal manager's goal count -> its first word; 0x00576E60 (Viewer::GetViewerObject)
    for a profile's face -> the appearance's +0x54; 0x005480C0 (FoldedGetters::Word04) for the server app -> the
    network manager's m_server; 0x005430D0 (PlayerInfo::SetReadyQueryOwed) called on the network manager -> its last
    leading team (+0x1D4, the field GetLastLeadingTeam 0x005331A0 reads) set.
  - int-returning callees read as bool (InNetMode, PlayerInfo::IsLocalPlayer / IsObserver / IsFullyIn): all return
    0 / 1, so `!= 0` keeps the AL reads. The source-only CheatingNoted / IsGameOver / GetJoinMode checked against the
    game's bytes (fields +0x3B, +0x3A, +0x3C).
  - Tests: tod2 / tod3 / tod4 / viewer3 / viewer18 / viewer19 stand in for ours too (29 `s.At` entries, tod4's
    `simple` helper takes ours from a table; its AddHandler stand-in maps the four newly direct handlers to their
    game addresses). Folded getters aren't stood in any more where ours reads the field: tod2 sets the goal manager's
    first word, viewer19 the profile's face word; viewer18's empty-function stand-in (0x005480A0) went (both run an
    empty function); tod4 lets 0x005430D0 run (it writes the arena's network manager +0x1D4, as ours does).
  - 14 deliberate bugs (out/scripts/mut3.ts): all caught.
  - Built, thug2_check both exes: 0 MISMATCHED, 771 early self-tests pass (checked together with GC1-4).
- [x] Batch GC1-4 (NOT run in the game yet; src/game/script/, src/game/ed/, src/game/park/ - 51 GameFunction calls,
  plus the park editor's display task code): script/cache (Mdl::Module's constructor / destructor body),
  cfuncs_lighting (Sun::HideFlare / UnhideFlare / SetSun - the file's SunProps as Sun::Settings -,
  Terrain::GetTerrainIndex / GetTerrainStructureInto), cfuncs_memory, cfuncs_render (GetBonePosition), cscript_exec /
  cscript_manager (GameNet::Manager::InNetGame / GetClient, Net::Client::EnqueueMessageToServer, WriteToBuffer),
  cscript_wait (the clock), node_manager (CPathManager::Instance()->NumberPaths()), nodes (Quat::GetMatrix),
  skate_cfuncs (Inp::ButtonIndex, the viewer's getter / velocities / RemoveViewerObject, GetPrefixedNodes, NodeExists,
  GetLastNodeArray); ed/cursor, park_accessors, park_floor (its Feeler is driven through Nx::CFeeler),
  park_io, park_manager, park_map, piece_menu (the tracker, the screen element manager and elements),
  park_editor_state (the display task's code: Stubs::DoNothing); park/park_cursor, park_editor
  (GridCoordinatesToWorld returns the corner by value: `Mth::Vector` locals instead of the float arrays).
  - Folded functions: 0x0047AE10 (the sound manager's free memory) and 0x005BA1D0 (setting the default heap) are
    `xor eax, eax; ret` - Stubs::ReturnZero; 0x005480C0 on a CMapListNode -> CMapListNode::GetMeta.
  - 0x0045BF90 (GetPrefixedNodes) was called with a third argument (exact or prefix) it never reads (its bytes:
    two arguments passed on) - ours doesn't pass it; lfxc's stand-in no longer records it.
  - Left (not a call): script/nodes.cpp compares a CFunction token with 0x0044AB20 (SetEventHandler's game address,
    what the game's CFunction table holds) - now also with our CFunc_SetEventHandler, for a table of ours.
  - Tests: stand-ins for ours in lfxc, lfxc2, csC, csD, nodes, cobj, engcmd1, engcmd2, parkedA, thugpro_guards
    (PlayerByConnection etc. from GC1-3), csB, park_editor_state (Stubs::DoNothing aliased to 0x005480A0); the
    `xor eax, eax` stand-ins in engcmd4 / lfxc3 went (both run the empty function).
  - 14 deliberate bugs (out/scripts/mut4.ts): all caught.
  - Built, thug2_check on THUG2PM.exe and the stock THUG2.exe: 0 MISMATCHED, all 771 early self-tests pass;
    check_code_writes: no overlaps; scan_register_assumptions on a snapshot copy: the 5 known RISK lines;
    audit_game_function_calls: 0 MISMATCH.

## Summary
- find_game_calls: 0 calls left in my files (225 at the start; 166 / 81 / 41 after GC1-1..3); with the literal-address
  helpers (CallThis / Call / the handler and task-code addresses) about 330 call sites in all.
- For the lead: **our CMovableContactComponent::GetContactPoint (0x00427A00) is `void` but the original returns
  `out` in EAX** - any original code that reads it gets garbage through the hook (obj/movable_contact.cpp, GC3's).
