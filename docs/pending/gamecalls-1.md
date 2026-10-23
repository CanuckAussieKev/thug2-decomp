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
