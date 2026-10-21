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
Switch off: `[replacements] disable=vehicle_camera.cpp` (and walk_camera.cpp, walk_physics.cpp, the other files listed per batch).

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
