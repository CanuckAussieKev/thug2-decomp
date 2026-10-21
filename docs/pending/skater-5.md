# Skater subsystem, fifth range (0x521B90-0x529F62) - skater-5 agent (SK5)

Branch worktree-agent-ad7146704ab7493b5. The range holds four classes: the trick component's queue, button buffer and
script member functions (0x00521BA0-0x00524411, also 0x005222F0 / 0x005232E0, not Ghidra functions), the
VehicleCamera component (0x00524420-0x00524C1E), the WalkCamera component (0x00524C20-0x00525E3F) and part of the
Walk component (0x00525E40-0x00529F62: its member function handlers, the walking physics' pieces). Names checked
against the game's own .qb checksum names (out/scripts/qbnames.py reads the 0x2B name tokens of Data\**\*.qb).

Source: src/game/skater/vehicle_camera.cpp, ... (one file per class / topic); x87 helpers in
src/game/skater/x87_math.h (FSIN / FCOS / FPATAN results kept in registers as the original does - they're not
rounded to 53 bits by the precision control - and the game's C runtime acos / asin). Tests:
src/hook/selftests_skater_*.cpp on a kit of their own, src/hook/sk5_testkit.h/.cpp (one arena both runs start from,
every callee stood in for by a recording handler that can give a float in ST0, NaNs compared as NaNs).
Shared files touched: CMakeLists.txt (new files listed, additive).
Switch off: `[replacements] disable=vehicle_camera.cpp` (and the other files listed per batch).

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
