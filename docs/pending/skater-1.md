# Skater 1 (SK1): the start of the skater range, 0x00500020-0x00508773

The skater's core physics component (Skater::CorePhysicsComponent: SkaterCorePhysics 0x5BD63B29, vtable 0x0064BC48,
0x153C bytes) - every function in the range is one of its methods. 48 functions; Spin 0x00504060 and BreakVert
0x00507120 were already rewritten (rotation.cpp). Fields by offset: src/game/skater/core_physics_fields.h (shared by
the new files). Self-test harness: src/hook/selftests_skater_physics_harness.cpp/.h (recording stand-ins over every
callee, as selftests_skaterA.cpp; adds pointer and unrounded-double results). Checksum names come from the game's
scripts (out/scripts/names.ts, a scratch helper reading Data\pre's .qb names).

Shared files touched (additive): src/game/skater/core_physics.h (declarations added to the class), CMakeLists.txt.

- [x] Batch SK1-1 (NOT run in the game yet; the component's setup, reset, timers and speed code, 15 functions):
  src/game/skater/core_physics_state.cpp, core_physics_speed.cpp (new).
  - 0x00505440 Create (Calloc 0x153C), 0x00500CA0 Construct (the base's constructor, vtable, type; feelers at +0xA8 /
    +0x158 and a collision cache at +0x4F4 made; +0x3F0's constructor 0x005902E0 only returns it, so it isn't called),
    0x00500DB0 Destroy (the body is 0x004FC500, not rewritten), 0x00506BE0 Reset (positions kept unless in a network
    game; every timed flag 23..0 set then cleared, flag 7 telling +0x1514's +0x1E; the ride's smart pointer
    released and freed; +0x350 kept at -2), 0x00506BA0 SetLastGroundFeeler (CFeeler copy 0x0043A930 into +0x158).
  - Timers: 0x00500710 UpdateFreeRevert ("You earned a free revert!!!!" through Dbg::Printf with the game's own
    strings; Physics_Time_Before_Free_Revert * 1000 truncated by _ftol, compared unsigned), 0x005007A0
    UpdateFocusButton (focus_button_pressed_time -> toggle_bullettime), 0x00500820 UpdateProjectiles (every 30
    frames; walk_ / skate_maybe_throw_projectile), 0x00500900 UpdateModelRotation (the countdown compared unrounded
    with 20 and 0, as the x87 left it), 0x00500BC0 UpdateSpecialTime (the score's timer; KillSpecial with
    TimeLimit), 0x00500DE0 GetAirTimeLeft (script member: AirTimeLeft from the quadratic; NaN kept, -0 kept, 0.00001
    when it never comes down).
  - Speed: 0x00501850 LimitSpeed (the horizontal speed - y taken out while it's measured - capped at
    Skater_Max_Max_Speed_Stat and slowed by Physics_Heavy_Air_Friction above Skater_Max_Speed_Stat; +0x350's override
    timer, -1 / -2 never counted; +0x320 puts Physics_Rolling_Friction back), 0x005019E0 Brake
    (Physics_Brake_Acceleration; stopped when less than two frames' worth is left - a NaN speed keeps braking, as
    the original's JNZ), 0x00506DF0 ApplyGroundFriction, 0x005021D0 StartRidingHitObject.
  - Found: 0x004FF470 (gravity) can return an unrounded product (FLD; FMUL) - declared double here
    (core_physics.cpp's DoAirPhysics reads it as float: a possible rounding difference there).
  - Test (selftests_skater_state.cpp TestSkaterPhysicsTimers, TestSkaterPhysicsSetup): 8000 + 2000 variants on
    random state around each threshold (timers ending exactly, the 20.00000006-rounds-to-20 countdown, NaNs, -0 air
    time, a take-off height the subtraction loses bits of, zero velocity with a zero brake), every callee stood in
    (ours and the game's). 54 deliberate bugs: 52 caught (6 after strengthening the test), 2 equivalent (+0x350's
    timer reset at exactly 0 - it already is 0; the sum order of the speed's squares - y is 0 in LimitSpeed, and the
    float result hides it in Brake).
- [x] Batch SK1-2 (NOT run in the game yet; the feelers, the member functions, skitching, 10 functions):
  src/game/skater/core_physics_feelers.cpp, core_physics_skitch.cpp (new; skitch.cpp is SK4's skitch component).
  - 0x005003A0 CheckFeeler (the component's feeler +0xA8 along +0x208..+0x218: +0x228 skatable / +0x229 vert /
    +0x22A wall from the hit face's flags 1 / 2 / 4 / 8 in that order, else no steeper than Wall_Non_Skatable_Angle -
    its x87 sine against the normal's y; the hit's +0x104 cleared for an object hit with face flag 0x40),
    0x00500020 CanGrindRail (not the last rail +0x22C or its neighbours while still in the air; a new rail sets flag
    +0x50 - in a created park only when all four end-to-end distances, unrounded lengths, are over 6; the wait
    +0x23C since +0x234 for the same rail; state 4; on vert only moving up; the second argument unused),
    0x005012B0 NearGround (script member; x of the side offset left unrounded), 0x00501600 CallMemberFunction (Ledge,
    Obj_MoveToNode: the motion component's member function, then display matrix / shadow / +0x98 / old position /
    teleported / the camera's update; NoReset and orient), 0x005009C0 FindNearestWallSide (a local feeler: nose /
    tail / left / Right 12 along the forward or side row -> +0x2D4), 0x00501C90 CheckSideCollision and 0x005055B0
    CheckSideCollisions (Skater_side_collide_height; the original keeps some products unrounded - x of the base, w of
    each end - and checks the opposite side's time three times in a loop), 0x005054A0 ApplyModelRotations (the Model
    component's rotations: the velocity turned by the quaternion-like 0x00420AC0 / 0x00420B50).
  - Skitching: 0x00503EC0 StartSkitch (skitch_hold_time compared unsigned; the nearest skitch point of the
    composite manager's skitch components within Skitch_Max_Distance - unrounded lengths, a tie keeps the first;
    +0x364 the car through 0x0041D1D0; Skitched to both), 0x00501DB0 Skitch (the four buttons pick a direction; 6 at
    a time short of 120 along the side for another point; held Skitch_Offset behind the point; pulled at
    skitch_suck_speed - a NaN distance keeps pulling, as the original's TEST AH,1; 180 away: OffMeterBottom; the
    skater's position w zeroed; its matrix = the car's display matrix).
  - Test (selftests_skater_feelers.cpp TestSkaterPhysicsFeelers / Members / Skitch, 9000 + 4000 + 9000 variants):
    the harness now records a stand-in's `this` words (the feeler's line), fills outputs at `this` (the hit), can
    return its first argument, fill fixed outputs (ties, exact distances: 5 from every point, 1 away at a speed of
    1, 180.001 away) and points the game's math functions at ours (OurMath). 71 deliberate bugs: 69 caught (11
    after strengthening), 2 equivalent (NearGround's x * 16 is exact anyway; the side turn's |angle| against the
    rounded instead of the unrounded turn - only for an angle within a rounding of it).
- [x] Batch SK1-3 (NOT run in the game yet; the turns, 9 functions): src/game/skater/core_physics_turning.cpp (new).
  - 0x005002D0 TurnForRailJump (Rail_Jump_Angle), 0x00503560 TurnOnGround (Physics_Ground_Rotation /
    Physics_Ground_Sharp_Rotation; the sharp turn ramps up over 600 ms below a speed of 10; +0x3A5; the velocity
    turned unless +0x388), 0x005039F0 BounceOffWall (acos of the side row against the wall normal - the clamp and
    the 90-degree test on the unrounded values, as the x87 left them; Wall_Bounce_Angle_Multiplier; the velocity
    turned by the unrounded turn, the matrices by the rounded one; returns the angle), 0x00501B00 TurnDownSlope
    (Skater_Slow_Turn_on_slopes), 0x00504730 NatasSpin (natas_spin_factor less the balance; CHEAT_PERFECT_RAIL;
    the rotation total shown as Spin does), 0x00504910 LeanInAir (Physics_Air_Lean_stat after
    Physics_Air_No_Lean_Time, ramping to Physics_Air_Ramp_Lean_Time), 0x00504A60 RotateInAcidDrop (the slerp at
    +0x3F0 eased 3t^2 - 2t^3; the change since +0x4A0 applied to the skater's matrix), 0x00504B40 RecoverInAir
    (ground within 500 below, not vert, normal y at least 0.2: flag +0x80; tipped past 0.9: turned back at
    Physics_recover_rate_stat), 0x00504DF0 KeepUpright (skater_upright_sideways_speed beyond +-1.2 a second).
  - Each turn of the velocity is the original's inline x87 fsin / fcos (Physics::X87Sin / X87Cos); the matrices'
    turns are RotateY/X/ZLocal (the original's CreateRotate*Matrix + D3DXMatrixMultiply).
  - Test (selftests_skater_turning.cpp TestSkaterPhysicsTurning, 27000 variants): speeds around 10, a total of
    exactly 360 with a slop of 0.1 and no spin, a wall normal a hair past 90 degrees (the acos rounds to 90), tipped
    and full-mantissa matrices. 43 deliberate bugs: 40 caught (4 after strengthening), 2 equivalent (the slope turn's
    sign at an angle of exactly 0 - the step is the angle then; the lean ramp's end at exactly the end time - the
    ramp gives the full lean), 1 only with a NaN script value (LeanInAir's start time: x87 and SSE give NaNs different
    signs, so the harness can't feed NaNs).
  - Build note: the verify build now embeds its debug info (/Z7, CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded in
    this worktree's out\scripts\build.cmd) - the shared vc140.pdb kept breaking (C1033 / C2471).
- [x] Batch SK1-4 (NOT run in the game yet; walls, ceilings, the grind trick, 7 functions):
  src/game/skater/core_physics_walls.cpp (new), core_physics_state.cpp.
  - 0x00503710 VertWallplant (the trick button, or within Wall_Ride_Triangle_Window; on vert with no rotation and
    none done; the wall within Physics_vert_wallplant_distance - an int against the unrounded distance - at least the
    velocity limit up, 60 over the take-off height, steep or vert; 0x0051DDF0; triggers; Physics_vert_wallplant_boost
    when not falling; Vert_WallPlant), 0x00503B30 LeanPushOffWall (the held time read unsigned; the Skate_wall_lean_push
    values), 0x00504EE0 KeepOffCeiling and 0x005051B0 KeepOffCeilingAtWall (a ceiling within the head height: slid
    along it below it - nudged 0.001 along the second hit's normal, not the ceiling's -, the velocity into its plane),
    0x00506E50 CheckWallAhead (Skater_First_Forward_Collision_Height / _Length; x of the far end left unrounded), 0x005059D0
    BounceOffWallAhead (BounceOffWall; slowed beyond Wall_Bounce_Dont_Slow_Angle; FlailLeft / FlailRight by flag +0x28
    against the turn's side, the raw byte; the sound call's word carries the surface byte under the angle's upper bytes,
    as the original's stack slot; backed out 6; a second wall: turned back by the unrounded pi - turn, half speed),
    0x005004B0 StartGrindTrick (GrindTrickList[type 0-8][buttons], or Trick_Motoskateboard_Grind; the skater's script made
    from the pool when it has none, set and run).
  - Test (selftests_skater_walls.cpp TestSkaterPhysicsWalls, 36000 variants): fixed hits for exact distances (7 from
    the wall), normals exactly 0.01 into the ground, zero speeds, only w moved, flag bytes of 2, the script pools set
    up. 62 deliberate bugs: 62 caught (7 after strengthening).
- [x] Batch SK1-5 (NOT run in the game yet; the wall plant and the wall ride's start, 2 functions):
  src/game/skater/core_physics_wallplant.cpp (new).
  - 0x00507500 WallPlant(late, checkOnly, walking) (the callers: (0, 0, 0) on the ground's check, (1, 0, 0) from
    DoAirPhysics's late frames, (0, 0, 1) from 0x00509BB0, (0, 1, 0) from another component): canwallplant tag when
    driving; Physics_Disallow_Rewallplant_Duration (the time read unsigned), Physics_Wallplant_Delay, Physics_Min_Wallplant
    _Height, steep enough unless late; a Wallplant_Trick (0x0051FDA0) - none: Physics_late_wallplant_frames (+0x14CC),
    the velocity / position saved, backed off 24 - a late one prints "late wallplant" (the game's string) and looks
    again from the saved position; the walking one 36 behind the facing (the facing's x, z times the double -1.0).
    Physics_Wallplant_Min_Approach_Angle; a check above the hit moving up with pad +0x1A0; 0x0051DDF0(0, walking,
    late); the bounce (Physics_Wallplant_Speed_Loss, LargerOf with the minimum exit speed, Vertical_Exit_Speed,
    Walking_Wallplant_Speed), the ride on what was hit (0x00427CB0 / 0x00427C70 / 0x0043EB80), placed Distance_From_Wall
    off it (0.1 off anything nearer), upright facing the velocity; Wallplant, flags, state 5.
  - 0x00508140 StartWallRide (triangle; Wall_Ride_Delay against the last ride and plant as signed times; the same probe
    above the hit moving up; Wall_Ride_Min_Speed along the wall; Upside_Down / Max_Incident / Max_Tilt; triggers on the
    last ground feeler +0x158; nothing in the way along the move turned onto the wall; WallRideLeft / Right, state 2,
    StartRidingHitObject).
  - Test (selftests_skater_wallplant.cpp TestSkaterPhysicsWallplant, 30000 variants): values at each threshold (up
    0.1, times 0 / 1 / 7 / 200 / 1000 against the script values, speed 0.01, a horizontal speed of exactly 0.0001,
    square-on walls), SetLength's vector recorded. 63 deliberate bugs: 61 caught (11 after strengthening), 2
    equivalent (the velocity's y zeroed again before SetLength - it already is; LargerOf's arguments swapped - equal
    values either way).
- [x] Batch SK1-6 (NOT run in the game yet; the acid drop, 2 functions): src/game/skater/core_physics_acid_drop.cpp (new).
  (Written by the first SK1 agent before it stopped at a usage limit; reviewed, its test strengthened and committed by
  the second.)
  - 0x00502240 FindAcidDrop(pop, position, lastPosition, velocity, drop): along the horizontal velocity (or the walk's
    direction), from the higher of the two positions; walking on the ground the reach is
    Physics_Acid_Drop_Walking_On_Ground_Search_Distance, 200 up, else 500 (walking in the air: backed off 12); the
    search's box through a collision cache (CCollCache 0x0040AB50 / 0x0040ABA0 / 0x0040AB40); feelers 4000 down at
    0.01, then +3 (+24 more beyond 100) - the next step compared unrounded with the reach; a vert face (flag 8) no
    flatter than 0.707 facing away (>= 0.05) or, walking in, towards (<= -0.25). The arc: the velocity projected onto
    the direction to the hit (walking), at least the walk's speed (0x0043DAA0), Physics_Acid_Drop_Pop_Speed up with
    `pop`, at most twice it; the peak at the wall (walking up: the arc's top when it comes first);
    Physics_Acid_Drop_Min_Air_Time; then up the wall 24 at a time for a point reached with nothing in the way (two
    feelers via the arc's middle). The velocity put back when none; AcidDrop (0x28 bytes: point, normal, the first
    hit's height, the hit object alive) filled when found.
  - 0x00502F20 StartAcidDrop(drop): the horizontal speed to reach the point in the fall's time into the flags block
    (+0xE4; a copy at 0x007CCDBC), +0x37C the point's height, flags 0x78 / 0x30 / 0xA8 set and 0x38 / 0x68 cleared,
    +0x14C0, the wall's matrix (+0x3B0: along the drop in the wall's plane, the normal, across; turned round when
    along points up), +0x490, the slerp (+0x3F0) from the skater's matrix to it turned by the facing's angle, +0x488 /
    +0x48C / +0x4A0; SkaterAcidDropTriggered with DropHeight; flag 0xB0; the Ollied handler cleared.
  - Test (selftests_skater_acid_drop.cpp TestSkaterPhysicsAcidDrop, 20000 variants): the harness's stand-ins got two
    optional hooks (StandIn::pickFloat - a Ret::Float stand-in's value; StandIn::after - called with `this` after the
    outputs: records the feeler's cache pointer, sets a vert wall at the edges). Edges: the facing exactly 0.05 /
    -0.25, the hit at the skater's own height (the peak equal to it, an air time of -0), the reach exactly one of the
    search's steps. 64 deliberate bugs: 62 caught (6 after strengthening), 2 equivalent (the box corner's w rounded
    twice - BBox::AddPoint takes x, y, z only; the wall direction's normalising at a length of at most 1 instead of
    above 0 - the length is never below 1 by more than a rounding, and the Normalize after it gives the same vector).
- Merge note: SK1's names that clashed with SK2's (both are CorePhysicsComponent methods) were renamed when the
  branches met: 0x005055B0 CheckSideFeelers, 0x00501C90 CheckSideFeeler, 0x00503EC0 TryStartSkitch; SK1's calls of
  the state change helper are Physics::SetState (the class has the script member SetState now).
- [x] Batch SK1-7 (NOT run in the game yet; the spine transfer, 1 function): core_physics_acid_drop.cpp.
  - 0x00505F70 TrySpineTransfer (from BreakVert 0x00507120, which now calls ours): the skater's up flattened and
    normalised; the velocity's opposite (x, y, z normalised, w only negated) turned a quarter round (0x00420AC0 /
    0x00420B50) about the horizontal across the wall; a feeler 4000 down from half a unit along it finds the wall
    (its point, its normal flattened). 0x004FEE10 (not rewritten: this, look, normal, &hip, &target, &targetNormal,
    &objectHit; RET 0x18) looks back for a target; failing that, with exactly one of input +0x60 / +0x80 held, back
    and along the wall that way (left: -back + (-n.z, 0, n.x, 1) inline; right: (n.z, 0, -n.x, -1) + back through
    operator+). The target's height is the skater's own (the original overwrites its y). A target facing the wall's
    way out by less than 0.9, or more than 24 from the wall: all the speed straight up. The fall time (as the acid
    drop's FallTime) at least 0.1; beyond 24 (+1) too slow to cover it: false. A feeler to the target at the
    skater's height hitting something: +0x2D0 = 1 (BreakVert tries again), true. Then the landing direction (a
    spine: along the target's slope, -1 / n.y times its normal against the offset, unrounded; a hip: straight up
    or down by the side of the target's normal it lies on), the orientation [cross; normal; along projected to its
    plane] turned by the facing against the old velocity (under 30 degrees: none), the slerp at +0x3F0 (at least
    0.9 seconds; the long way round when the halfway matrix is upside down), +0x490 / +0x488 / +0x48C / +0x4A0 /
    +0x37C, the flags block's +0xE4 (the offset over the time; w left unrounded; y 0), SkaterAwardTransfer or
    SkaterAwardHipTransfer (node 0), Ollied cleared, flags 0x78 set and 0xA8 / 0x38 / 0x48 cleared, +0x14C0.
  - Test (selftests_skater_acid_drop.cpp TestSkaterPhysicsSpineTransfer, 30000 variants; the real 0x00420AC0 /
    0x00420B50 / Matrix copy run, the math helpers pointed at ours, 0x004FEE10 a stand-in recording `look` and
    `normal` and making up the rest): still velocities, an up with no horizontal part, full-mantissa vectors, the
    target exactly 24 / 23 from the wall, straight across the skater (a hip's side exactly 0), the target facing
    exactly 0.9 (the up flattened to (0.9, 0, 0.436) normalises back to 0.9f). selftests_skaterB.cpp's BreakVert test
    now stands in for ours as well as the original. 53 deliberate bugs: 51 caught (1 after strengthening), 2 not:
    the velocity's w (equivalent - 0x00420B50 takes the vector's w as 1), the speed check at exact equality (a sum of
    float squares equal to the square of the time's inverse times a float: not constructible).

## The range's summary
- 48 functions, all rewritten (46 here, Spin and BreakVert before): find_gaps lists nothing in 0x00500020-0x00508773.
- Checks: thug2_check on THUG2PM.exe and the stock THUG2.exe: 0 MISMATCHED, all 579 early self-tests pass;
  check_code_writes: no overlaps; audit_game_function_calls: no MISMATCH; scan_register_assumptions: only the known
  RISK lines (0x004EFF92; Ed::InitEditedGoalStub's, not in this range).
- Game runs (workspace C:\Games\THUG2-2 under the lock, this branch's DLL after merging main, automation on): `-level bo`
  (rolled forward, ollied, R2, square: the skater skated off, SWITCH shown) and `-level nj` (forward, ollie, turns,
  triangle, spins, circle: 1365 points, a trick in progress by a wall) - all 3696 replacements installed, no crash, no
  run-time check or debug CRT report in the log. Vert, wall plants, acid drops and spine transfers weren't driven
  (too hard to steer by posted buttons): those want the user's play test.
- Switching it off: `[replacements] disable=` core_physics_state.cpp, core_physics_speed.cpp, core_physics_feelers.cpp,
  core_physics_skitch.cpp, core_physics_turning.cpp, core_physics_walls.cpp, core_physics_wallplant.cpp,
  core_physics_acid_drop.cpp (by file name).
