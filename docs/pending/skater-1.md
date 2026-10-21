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
  src/game/skater/core_physics_feelers.cpp, skitch.cpp (new).
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
- [ ] Next: turning (0x005002D0, 0x00503560, 0x005039F0, 0x00501B00, 0x00504730-0x00504DF0), wall plants, the acid drop
  search.
