# Skater subsystem, fifth range (0x521B90-0x529F62) - skater-5 agent (SK5)

Branch worktree-agent-ad7146704ab7493b5. The range holds four classes: the trick component's queue, button buffer and
script member functions (0x00521BA0-0x00524411, also 0x005222F0 / 0x005232E0, not Ghidra functions), the
VehicleCamera component (0x00524420-0x00524C1E), the WalkCamera component (0x00524C20-0x00525E3F) and part of the
Walk component (0x00525E40-0x00529F62: its member function handlers, the walking physics' pieces). Names checked
against the game's own .qb checksum names (out/scripts/qbnames.py reads the 0x2B name tokens of Data\**\*.qb).

Source: src/game/skater/vehicle_camera.cpp, walk_camera.cpp, walk_physics.cpp, ... (one file per class / topic); x87 helpers in
src/game/skater/x87_math.h (FSIN / FCOS / FPATAN results kept in registers as the original does - they're not
rounded to 53 bits by the precision control - and the game's C runtime acos / asin). Tests:
src/hook/selftests_skater_*.cpp on a kit of their own, src/hook/sk5_testkit.h/.cpp (one arena both runs start from,
every callee stood in for by a recording handler that can give a float in ST0, NaNs compared as NaNs).
Shared files touched: CMakeLists.txt (new files listed, additive).
Switch off: `[replacements] disable=vehicle_camera.cpp` (and walk_camera.cpp, walk_physics.cpp, trick_queue.cpp, trick_member_functions.cpp, the other files listed per batch).

- [x] Batch SK5-1 (NOT run in the game yet; the VehicleCamera component, 12 functions):
  src/game/skater/vehicle_camera.cpp/.h (new), src/game/skater/x87_math.h (new).
  - 0x005248C0 Create (Calloc 0xA4, the base constructor, vtable 0x0064C5A0, type 0x2C747D8A, height 60, distance
    144, lerp 3), 0x00524420 RegisterMemberFunctions (the table 0x00684150: VehicleCamera_Reset; not a Ghidra
    function), 0x00524440 / 0x00524470 the destructor body and the scalar deleting destructor, 0x005244A0
    InitFromStructure (subject= - an object id: the object and its Vehicle component; then 0x9213625F (lerp),
    0x14849B6D (height), 0xBD3D3CA9 (distance), angle=, each kept when not given), 0x00524C00 Finalize (the
    CameraLookAround component, then Reset), 0x00524B40 Reset (the look-around back to straight ahead, the
    target from the vehicle's heading, the orientation built, given to the camera object, the Camera component's
    Update - a tail call through its vtable), 0x00524450 VehicleCamera_Reset (vtable +0x14 with the params,
    vtable +0x0C; true - a member function reached through its record's member function pointer, set by the static
    initialiser at 0x006437E0).
  - 0x00524550 UpdateTargetDirection: the vehicle's heading (its at-row negated, flat, normalised) turned toward its
    flat velocity by speed x 0.0037879 (at most 1) along the heading, with two wheels or more down (+0x114 of the
    Vehicle component); with fewer, its flat velocity, but only at a speed of at least 88000000 (0x0064C5D8 - in
    practice never; a NaN speed passes).
  - 0x00524780 UpdateOrientation (rows from the direction turned by the look-around heading - rows 0 and 1 get w = 1,
    which ApplyToCamera clears -, the position `distance` behind / `height` above the vehicle, w = the vehicle's
    w + 1; then x Rz(angle degrees) through D3DXMatrixMultiply), 0x00524720 ApplyToCamera (the w's cleared, the
    position, matrix and display matrix of the camera object).
  - 0x00524940 Update: the camera's old position kept; the angle between direction and target (acos of their
    clamped dot, folded below 90 degrees), the turn at most that angle at lerp a second toward the target's side;
    the position kept clear of the world (AvoidCameraCollisions, neither sideways nor refocused).
  - Finding: FSIN / FCOS / FPATAN results are 64-bit even with the 53-bit precision control: computing with them
    rounded to doubles first gave 1-ulp differences (in a near-cancelling turn); x87_math.h keeps them in registers.
  - Test (selftests_skater_vehicle_camera.cpp TestVehicleCamera / TestVehicleCameraLife): every function, original
    vs ours, 50-4000 random variants each (unit and random directions, speeds over the full-weight limit, the huge
    flat speed, zero and NaN speeds, 0-3 wheels), every callee stood in for (GetComponent / GetObjectByID giving the
    arena's objects, acos, the frame time, CreateRotateZMatrix, D3DXMatrixMultiply, AvoidCameraCollisions, Normalize
    recording its input, the CStruct getters, Calloc failing at times). 26 deliberate bugs caught (26 of 26).
- [x] Batch SK5-2 (NOT run in the game yet; the WalkCamera component and the Walk component's camera pieces, 19
  functions): src/game/skater/walk_camera.cpp/.h, src/game/skater/walk_physics.cpp/.h (new).
  - 0x00525DC0 Create (0xA8 bytes, vtable 0x0064C5E0, type 0xC7D37EC2, the target (0, 0, 0, 1)), 0x00524C20 /
    0x00524F20 the destructors, 0x00524C50 SetSkater (its Walk and SkaterPhysicsControl components; none: all
    null), 0x00524F50 InitFromStructure (the skater from 0x4C48DA58 - an object id, name not recovered -, the zoom
    and turn cosine 1, the orientation from the camera object), 0x00524FB0 Finalize (CameraLookAround, Camera,
    skatercamera), 0x00525D40 CallMemberFunction (WalkCamera_FlushRequest, WalkCamera_Reset; else 2), 0x00524C30
    ResetNow (Update through the vtable with the reset flag), 0x005250A0 FlushRequest, 0x00525F70
    SetOverrideFacing, 0x00525060 / 0x00524FF0 SaveState / RestoreState (a 0x58-byte state: orientation, target,
    turn cosine, zoom), 0x00524EF0 Param (stdcall: walk_camera_parameters' float - the original's value is
    uninitialised when missing, ours 0; it's asserted), 0x005250E0 GetTarget (eased at the skater camera's +0xE4
    across / +0xE8 up through FrameBlend), 0x00524CB0 GetZoomedOffsets (the zoom eased toward the look-around's
    factor while it turns - an integer compare of the factor's bits with 1.0 -, the distance x zoom, nearer when the
    look-around tilts up: ((tilt + 0.628) x 1.5924 x 0.6 + 0.4); the height from 3 when zoomed in), 0x00524D70
    LerpByRange (cdecl, returns `out`).
  - 0x005251D0 Update (2.9 KB): paused when the physics control's state says so (the camera object's PAUSED flag),
    three instant frames after the skater teleports; the facing swung to the skater's within lock_angle by its
    speed between min_slerp_speed and full_slerp_speed (the d-pad's pair when 0x00524E10 says so), at once when
    flushing / resetting / at full speed; the right stick turns it (facing_control: the sine rounded to float, the
    cosine kept in the register - its own x87 sequence); an override's facing turned by the look-around heading;
    the look matrix tilted by the look-around tilt + the skater camera's +0x12C, slerped toward at
    matrix_slerp_rate (run_slerp_factor running, doubled in walk states 3 / 4, control_slerp_factor x |stick|,
    flush_slerp_factor; an override's rate) unless all three rows are within 0.99 already; then placed behind /
    above the target, aimed at the point above the skater (right = up x at, up = at x right), kept clear of the
    world toward the point ahead plus the Walk component's offset; the skater camera's +0x74 matrix updated.
  - Walk component (walk_physics.cpp - only the class's pieces in this range; its constructor and layout are in the
    next range): 0x00524E10 UsingDpad, 0x00524E60 GetCameraOffset (facing x -18 in states 2-4 and for 150 ms in the
    air after 2 / 3).
  - Test (selftests_skater_walk_camera.cpp TestWalkCamera / TestWalkCameraLife): every function, original vs ours,
    50-8000 variants (flags, instant frames, teleports, paused physics, null skater / physics control, d-pad, walk
    states, stick on / off, the orientation already aligned with an identity multiply, the speed just under the full
    speed in double but equal as a float and exactly the least speed, the zoom just under 1, a state changed exactly
    150 ms ago), every callee stood in for (Param, the Walk pieces, the slerp's SetMatrices / GetMatrix recording
    their matrices, CreateRotateXMatrix, D3DXMatrixMultiply, FrameBlend, the clock...). 52 deliberate bugs: 51
    caught (4 after adding the edge variants), 1 equivalent (the factor's bit compare against 1.0 - using a factor of
    exactly 1.0 gives the same).
- [x] Batch SK5-3 (NOT run in the game yet; the Trick component's tricks, button buffer and trick object list, 30
  functions): src/game/skater/trick_queue.cpp/.h (new; the class Obj::CTrickComponent with only this range's
  methods - its fields by named offsets - so the next range's agent's declaration of the same class can be merged
  with it).
  - Life: 0x00523490 Create, 0x00522390 Construct (its trick object list +0x13B4, reset by 0x0051F320), 0x005234F0 /
    0x005243F0 the destructors (the trick mappings structure freed); 0x005231D0 Update (suspended while the object's
    +0xBC is clear; then the buttons, the balance trick's buttons, the extra tricks, 0x00521A10, the manual tricks,
    the extra grind tricks - each pending one dropped after its time - and button mashing); 0x00522410
    SetTrickMappings (the profile's 12 special trick slots: keys = trick, an integer when the slot's flag is set,
    else a name; then its trick_mapping info); 0x00521E50 UpdateButtons (the d-pad direction 1-8, then 13 buttons
    from the input's mask bits, in the original's order - 0xC before 0xB).
  - Tricks: 0x00522520 TryTrick (Scr; a Grind needs a rail; Params without IsExtra counted by the 0x4FF8DAC6
    component; the pending ones dropped; SwitchScript, the flag added unnamed, the extra params appended, one
    Update; the state's +0xE0; CountTrick), 0x00522670 DoNextTrick (the queue +0xA2C / +0xA30 of 100 entries; the
    first script as an interrupt; the slot's mapping: an integer - CreateATrick with trick_index -, or a structure,
    the goal manager told first), 0x005228B0 DoNextManualTrick (ScriptToRunFirst run on a copy of the params, copied
    back), 0x00522E80 DoExtraGrindTrick (four flags pick Scripts[n] or prefix + Template[n] through the game's
    sprintf and the CRC; the core physics' +0x3A8 cleared for a skater - the original's stack cookie isn't kept),
    0x00521BA0 UpdateManualTricks, 0x00521C10 / 0x00521D20 Check / UpdateExtraGrindTricks, 0x00522AA0
    TryExtraTricks, 0x00522C00 UpdateExtraTricks, 0x00522CD0 SetExtraTricks (duration in frames x 16.67 ms
    truncated; ignore= as a string is passed on as its address - a name nothing matches -, as the original does),
    0x00521D80 GetIgnoreMask (no check that the array exists, as the original), 0x00521DF0 IgnoreTricks.
  - Mashing: 0x00522030 GetBashSpeed (presses / releases but triangle within BashPeriod, marked used; x
    BashSpeedupFactor rounded to float, at most BashMaxPercentSpeedup %, + 1 - returned unrounded), 0x00522170
    CountTriangleBashes, 0x005230E0 UpdateBash (in RunOutQuick / 0xA4B7A992 the blender's speed; else over 1.5: 2 in
    the state's +0x1C case, or TantrumOff then, once with a bail end pose, the "ButtonMash - trigger quick getup"
    message and bail_quick_getup - otherwise the speed set as well).
  - Trick objects and counting: 0x00522340 CountTrick (past 3 the object's 0x94182A08 event), 0x00522370,
    0x005232A0, 0x005232E0 (not a Ghidra function), 0x00523310.
  - Member functions through records (table 0x00683148): 0x00522280 Pressed (dur= 15), 0x005222F0
    GetCurrentBailEndpose (not a Ghidra function). 0x00523350: the C function DoNextTrick (TrickParams copied into
    a new structure for the call).
  - Test (selftests_skater_trick_queue.cpp TestTrickQueue): every function, original vs ours, 50-4000 variants
    with every CStruct / CArray / script / object / clock / profile / trick-object call stood in for (structures
    and arrays as tokens; the inlined `new CStruct` and NewStruct through one stand-in pool), the button buffer, the
    queue, the four trick array sets and every flag randomised, the trick script sometimes killing the extra tricks.
    59 deliberate bugs: 58 caught (2 after strengthening: a direct TryExtraTricks test with scripts that kill the
    extra tricks, and the kit compared -1 / -5 as equal NaNs - fixed: only the quiet NaN's sign is ignored now), 1
    equivalent (GetBashSpeed's `>` against `>=`: equal values give the same result).
- [x] Batch SK5-4 (NOT run in the game yet; the Trick component's script member functions, 0x00523560 - 3.7 KB, 34
  names): src/game/skater/trick_member_functions.cpp (new).
  - Released / Held (a button through 0x0051ECF0, or buttons= by Inp::ButtonIndex into the held flags +0xA00),
    GetSpazzFactor (the triangle mashes as spazzfactor), 0x070E934D (the button x's events marked used, in buffer
    order), ClearPanel_Landed (a deferred trick scored when its spin is a whole turn - 360 on a rail, else 180 - less
    spin_count_slop with 0.1 to spare; the goal manager's 0x005587F0; SkaterExitCombo with skaterId when the score
    holds a combo; the gap and score components reset; SkaterLanded; the balance trick's two resets), ClearPanel_Bailed,
    GetSpin (rounded down to half turns with the slop) / GetActualSpin / ResetSpin, SetQueueTricks (one name that
    isn't an array becomes 0x9E22D268) / SetManualTricks / SetExtraGrindTricks / SetExtraTricks / KillExtraTricks /
    ClearTricksFrom, ClearTrickQueue, DoNextManualTrick, ClearManualTrick, ClearExtraGrindTrick, the trick mappings
    from a global structure (0xABFF7889), 0xB42A1230 (slot= mapped to the created trick trick= - a pair (13, its
    index) through 0x00478470, a jump to AddPair), ClearEventBuffer (buttons= / OlderThan=, else all),
    RestoreEvents (UsedBy= regular / extra / manual / 0xEC066B6A within duration= 200 ms), UseGrindEvents, BashOn /
    BashOff, NoQuickGetup / ClearQuickGetup, SetTrickName / SetTrickScore, display (Deferred; AddSpin= while the
    score shows the same trick - the score told the unrounded total; else the trick scored with its flags -
    Blockspin 2, switch 4, special 0x20 unless NoSpecial, NoDegrade 0x40, cat 0x80, Natas 0x100, NoMult 0x200,
    NoSwitch 0x400 - on a rail its score times the score's rail multiplier with the "RAIL score" debug line,
    SkaterTrickDisplayed and the spin scored), 0x36B2BE57 (a button to the trick object list), 0x72C926CA /
    0xDE98DE7A (0x0051F010 / 0x00521A10). 2 for other names.
  - Test (selftests_skater_trick_queue.cpp TestTrickMemberFunctions): every name (and two unknown ones), original vs
    ours, 250-2500 variants each (names displayed / deferred, spin totals at and just off whole turns, a slop of 0.1
    making the threshold exactly 180 / 360, the score showing the trick or not, rails, the event buffer with the
    button x, UsedBy's kinds). The kit's arena grew to 64 KB (the score component's +0x154C) and copies the arena
    out faster. 41 deliberate bugs caught, 4 after strengthening (the button x events, the exact thresholds, UsedBy's
    kinds, a spin whose sum rounds up to the next degree).
- [x] Batch SK5-5 (NOT run in the game yet; the Walk component's life, state and member functions, 26 functions):
  src/game/skater/walk_physics.cpp/.h.
  - Life: 0x00525E40 RegisterMemberFunctions (the table 0x006847F8, 0x18-byte records), 0x00528880 / 0x00528890 a
    contact's constructor (its feeler at +0x1C) / destructor (the smart pointer +0xB4), 0x005288B0 the destructor
    body (+0x184C let go, the collision cache +0x790, the eight contacts last first, the base's), 0x00526060
    InitFromStructure (camera= an object id: the object and its WalkCamera component; +0x710 cleared), 0x00525FB0
    Finalize (nine components: Input, animblender, Model, Trigger, SkaterPhysicsControl, MovableContact,
    SkaterState, SkaterCorePhysics, TaggingControl), 0x00525E60 SetCamera.
  - 0x005260E0 SetState (26 callers): the previous state and the time kept; leaving the ground / air clears the
    run flags; leaving the air its jump time and velocity and the rail flag; the ground resets +0x1870 to 10000;
    a climb (4) its offset; caught a ledge from the air: the pad's two hang controls debounced for
    hang_control_debounce_time (now - time x -1000, truncated). 0x005260C0 ClearModelOffset.
  - Queries: 0x00528560 GetFacing, 0x00528220 GetTurnedFacing (index sevenths of a turn: the cosine rounded to
    float, the sine kept in the register), 0x00528370 UpdateRunAdjustment (1 while the score holds a combo, else
    down at run_adjust_rate), 0x00528400 LimitSpeed (the speed kept from rising 100 ms after landing; a stopped run
    becomes Skid or Stand), 0x005284C0 GetGravity (cdecl; the moon cheat halves it and sets the global
    0x007CE4FD), 0x00528280 FindStandPosition (stdcall: feeler_height x 2 above to stand_pos_search_depth below).
  - Member functions (records of 0x006847F8): Walk_Ground, Walk_ScaleAnimSpeed (Off resets the blender's speed -
    its third argument the float with bits 1, as the original pushes -, Run / HangMove / LadderMove, StandardSpeed=),
    ReleaseHang (0x0043DB90), Walk_GetStateTime, Walk_GetSpeedScale (WalkSlow / WalkFast / RunSlow / RunFast by
    max_slow_walk_speed / max_fast_walk_speed / max_slow_run_speed), Walk_GetHangInitAnimType,
    Walk_GetStateDuration (seconds), Walk_GetPreviousState / Walk_GetState (the names at 0x0064C618),
    Walk_GetHangAngle (asin of the rail's climb between the hang nodes, in degrees, signed by the facing - z's square
    from its unrounded difference, as the x87 code), Walk_SetForwardControlLock.
  - Test (selftests_skater_walk.cpp TestWalkPieces): every function, original vs ours, 20-3000 variants each (states
    -1..5, clock values at and around 100 / 150 ms, d-pad / stick, the moon cheat, collisions found or not, -0 and +0
    speeds, the parameters sometimes equal to the component's values). The trick queue's test was split in two
    (TestTrickQueue / TestTrickQueueExtra): it ran past the watchdog's 45 s on a loaded machine.
    36 deliberate bugs caught, 1 after strengthening (Walk_GetSpeedScale's `<` against `<=`: the parameters and
    the speed now drawn from a shared short list half the time).
