# Object components, range 0x004351B2-0x0043D929 - objects-4 agent (tag OC4)

Branch worktree-agent-af2babd08ddaf146e. The range (85 Ghidra functions plus 9 it never made functions of: the vehicle's
member functions 0x004353F0 / 0x00435480 / 0x00435490 / 0x004354B0 / 0x0043B670, its 0x0043AA90, the wheel's
destructor 0x00437CF0, the vibration component's 0x0043D490 and member function 0x0043D6F0) is: the Vehicle component
(the driveable cars' rigid-body physics: wheels, suspension, engine, steering, collision - type 0xE47F1B79, vtable
0x00646770, 0x1334 bytes), its VehicleSound component, and three small components - Velocity, Vibration, WaitAndDie -
plus the feeler's assignment (0x0043A930). Source in src/game/obj/<class>.cpp + .h; tests in
src/hook/selftests_obj_<topic>.cpp. Shared files touched: CMakeLists.txt (new files listed, additive),
src/game/nx/feeler.h / feeler.cpp (CFeeler::Assign added).

- [x] Batch OC4-1 (NOT run in the game yet; the Velocity, Vibration and WaitAndDie components and the feeler's
  assignment, 21 functions): src/game/obj/velocity_component.cpp, vibration_component.cpp, wait_and_die.cpp (new);
  src/game/nx/feeler.cpp.
  - Velocity (type 0x41272956, vtable 0x00646830, 0x1C bytes): 0x0043D430 Create, 0x0043D2B0 / 0x0043D2C0 the
    destructors, 0x0043D2F0 InitFromStructure (the object's velocity from `vel`), 0x0043D340 Update: the object's
    position plus HALF its velocity times the frame (each step rounded, w's not) - unless, outside a network game
    (the manager copy at 0x006FA7D0), the Suspend component's +0x30 flag (0x00433E90) is set. Its Finalize is the
    shared 0x0041C330 (the Suspend component into +0x18; another range).
  - Vibration (type 0x404EB936 "vibration", vtable 0x00646864, 0x38 bytes): 0x0043D670 Create, 0x0043D490
    RegisterMemberFunctions (not a Ghidra function; the table at 0x00684EE8, whose member function pointer the static
    initialiser 0x006448C0 writes), 0x0043D4B0 / 0x0043D5E0 the destructors (both motors stopped first), 0x0043D610
    Update (off: suspended through the vtable) and its second half 0x0043D61F (Ghidra split it off; source only: the
    timed runs - a motor whose duration has passed on the game clock is stopped, `>=` unsigned), 0x0043D520 StopAll,
    0x0043D550 ResetDevice, 0x0043D560 SetActive (off: StopActuators + DisableActuators, suspended; on: unsuspended,
    EnableActuators), 0x0043D5C0 SetDevice, and the member function vibrate 0x0043D6F0 (actuator, percent truncated,
    duration in ms; "off" stops every motor; nothing while the game or the object is paused unless "off"). Quirk
    kept: an actuator above 1 writes past the two runs (as the original).
  - WaitAndDie (type 0x03AC8065, vtable 0x00646898, 0x28 bytes; its Create 0x0043D930 is the next range's):
    0x0043D840 / 0x0043D850 the destructors, 0x0043D880 InitFromStructure (an object id to wait for, 0x1869F559 - or
    the LifeTime in seconds; with an object the lifetime is 0), 0x0043D8E0 Update (dies - vtable slot 6 - when the
    object is gone, or past the lifetime, `>` unsigned).
  - 0x0043A930 CFeeler::Assign (operator=, 10 callers: the skater's core physics, the vehicle): every field, the hit
    by CollisionHit::CopyFrom, the smart pointer released before it's re-pointed (a feeler assigned to itself loses
    its object - kept).
  - Test (src/hook/selftests_obj_small_components.cpp; every call recorded by stand-ins): 4000 random cases over the
    three components (Create over dirty memory and out of memory, every destructor and flag, the updates with and
    without a network game / suspension / device / timers exactly over, every vibrate branch, the lifetime exactly
    reached), and 500 random feeler assignments (null / shared / self). 39 deliberate bugs caught, 2 of them after
    strengthening the tests (timers and lifetimes exactly reached).
- [x] Batch OC4-2 (NOT run in the game yet; the VehicleSound component, 12 functions): src/game/obj/vehicle_sound.cpp
  (new); src/game/obj/vehicle_component.h (new: the Vehicle component's layout, used from here on).
  - VehicleSound (type 0x28C97F3C, vtable 0x006467F4, 0xBC bytes): 0x0043CFC0 Create, 0x0043C590 Construct,
    0x0043C5F0 / 0x0043D020 the destructors (both sounds stopped), 0x0043C670 InitFromStructure (the park editor's
    level), 0x0043D050 Finalize (the Vehicle and Sound components, then the settings), 0x0043D290 Update, 0x0043C540
    GetDebugInfo (vtable slot 10), 0x0043CE70 ReadSettings (PlayerVehicleSounds' structure named by the vehicle's
    `sounds`; EngineSound, TireSound, Gears, CollideSound; the park editor's CAG ones), 0x0043C690 UpdateEngine (the
    skater's vehicle sound's engine model - game/skater/vehicle_sound.cpp - driven by the slowest driven wheel's
    |spin| * radius; the gear shifts while a wheel touches and drops to first after 0xC0F451D4 s in the air),
    0x0043CBC0 UpdateSkid (the fastest slip of a sliding wheel while braking or handbraking), 0x0043D090
    UpdateCollideSound (after collision_mute_delay, the vehicle's strongest contact impulse / FullCollision over
    collision_cutoff_factor: CollideSound at that percent through the Sound component's 0x00433240, and event
    0xC4A60423 to the skater with its Strength clamped to 0-1).
  - Quirks kept: the wheel loops always look at four wheels; out of memory the event structure's AddFloat is called
    on null (as the original).
  - Test (src/hook/selftests_obj_vehicle_sound.cpp): 6000 random components / vehicles (every function, the edge
    cases at the comparisons: shift points, the air delay, the recovery, the mute delay and the cutoff exactly met;
    ~2800 sounds played or updated, ~1000 crash sounds). 40 deliberate bugs: 36 caught (2 after strengthening: a
    shift down at exactly the speed, the air delay exactly met); 4 equivalent (min / max kept on equal values - the
    same value either way; a float product already float; loudness clamped at exactly 1).
- [x] Batch OC4-3 (NOT run in the game yet; the Vehicle component's body and member functions, 20 functions):
  src/game/obj/vehicle_component.cpp (new).
  - The wheel: 0x00435870 Construct (its feeler), 0x00437CF0 the destructor (not a Ghidra function). The body:
    0x00435520 UpdateVelocities, 0x004355B0 GetPointVelocity, 0x00435640 AddForceAtPoint (a debug line's colour,
    compiled out), 0x00435710 AddForceAtOffset, 0x004357C0 ApplyImpulseAtOffset, 0x00435850 IsKicked, 0x00435890
    KickRandomlyAlong, 0x004354C0 QuatProduct (cdecl). The wheels: 0x004351C0 UpdateWheelRotations (wrapped by 100 pi
    above 200 pi and below 0), 0x00435250 GetTyreFriction (returned unrounded in ST0), 0x00435380 GetGear.
  - Member functions (the table at 0x00684060): 0x00437A60 Vehicle_Kick, 0x004353F0 Vehicle_Wake, 0x00435410
    Vehicle_AdjustGravity, 0x00435480 Vehicle_HandbrakeActive, 0x00435490 Vehicle_AllWheelsAreInContact, 0x00437BC0
    (0x4C73526B) KickRandomly, 0x004354B0 Vehicle_IsSkaterVisible (the last four and Wake never made functions by
    Ghidra).
  - 0x004351C0 has a naked adapter keeping ECX: the original update 0x0043C1D0 calls UpdateSkeleton right after it
    without loading ECX again (found by a scan of the range's call sites; the register scanner doesn't see it).
  - Quirk kept: KickRandomlyAlong spins every axis through the first moment of inertia.
  - Test (TestVehicleMethods in src/hook/selftests_obj_vehicle.cpp): 6000 random vehicles over every function (the
    roll exactly at 0 and 200 pi, the grip speeds tied, gears at the upshift speed). 45 deliberate bugs, all caught
    (one after adding the roll exactly at 200 pi).
- [x] Batch OC4-4 (NOT run in the game yet; the Vehicle component's physics step, 19 functions):
  src/game/obj/vehicle_physics.cpp (new).
  - 0x0043AE10 UpdateDependents, 0x00438A20 UpdateInertia (R * diag * R^T through D3DXMatrixMultiply), 0x00435DA0
    UpdateCollisionCache, 0x0043B240 UpdateWheelContacts (the suspension lines; the trigger component told of
    landings, take-offs, moves to another object and, outside the park editor, trigger faces; stops when a trigger's
    script takes over or the object dies), 0x00435F00 UpdateSteering (Ackermann), 0x00438AE0 ApplyWheelForces,
    0x00436280 ApplyGravity, 0x00436340 ApplySuspension (the load averaged over ten steps), 0x00435B60
    UpdateWheelFriction, 0x004364B0 ApplyTyreForces, 0x00436900 ApplyDriveTorque, 0x004367E0 ApplyEngineBraking,
    0x00436A90 ApplyBrakes, 0x00436B60 ApplyHandbrake, 0x00436C20 ApplyBrakeExcess, 0x004360C0 ApplyDamping,
    0x00437550 CheckSleep, 0x004376B0 ReadControls, 0x004378B0 UpdateSkater (the steering animation, sent on every
    fifteenth time in a network game).
  - 0x00435F00 has a naked adapter keeping ECX (the original update calls ApplyDamping right after it without loading
    ECX again).
  - Test (TestVehiclePhysics): 5000 random vehicles (unit orientations, scripted suspension hits and trigger
    take-overs, the park editor on and off, network games), every call recorded.
- [x] Batch OC4-5 (NOT run in the game yet; the Vehicle component's collision and integration, 9 functions):
  src/game/obj/vehicle_collision.cpp (new), src/game/obj/vehicle_internal.h (new: what the vehicle's files share -
  the debug strings, the quaternion and trigger calls not rewritten yet, D3DXMatrixMultiply).
  - 0x0043BEE0 Integrate (a sub-step: momenta, the move through MoveBy, the turn, the wheels' roll, turning in the air,
    righting, the dependents), 0x00437120 MoveBy (no tunnelling: a hit puts it at the surface with the rest of the move
    reflected and the momentum bounced 1.5; nothing within 4800 below puts it back - "uber frig"; else the model lit
    from the ground through 0x00420670), 0x00438770 UpdateRighting, 0x00439D90 AlignInAir, 0x00438BB0 Collide (rect
    queries of the two colliders and lines to their corners; contacts in the array at 0x006951C8; impulses with
    restitution in up to six passes, depth pushes at most 2, friction, the strongest impulse, steering against walls),
    0x00436CF0 GetPenetration (stdcall), 0x00436E50 FindEdgeContact, 0x00437060 NormalFacesAway, 0x00437450
    GetImpulseDenominator.
  - Quirks kept: Collide marks the contact after a feeler's as from a feeler, not the feeler's own; UpdateRighting's
    target matrix's fourth row is the original's stack (ours the identity's; nothing reads it).
  - Test (TestVehicleCollision): 4000 random vehicles (rect hits on a grid so that edges meet, opposite and nearly
    opposite normals, ties at an edge's middle, righting exactly at 300 / 5300 ms, NaNs in the state and the contacts),
    the slerps stood in (ours and the original's x87 code differ in the last bit - mth/slerp.cpp, not this range).
- [x] Batch OC4-6 (NOT run in the game yet; the Vehicle component's life, 13 functions): src/game/obj/vehicle_component.cpp.
  - 0x0043AA30 Create, 0x00437C00 Construct, 0x0043B990 DestroyBody (the wheels as `delete[]`), 0x0043C1A0 Destroy,
    0x00437D10 InitFromStructure (debug modes, centre of mass, mass and moments of inertia, the body's settings,
    colliders - their points' average distance less 48 to the skater's 0x005802C0 -, engine and gear ratios, Wheels made
    the first time and read through 0x00434F20, all_wheels, drive count, Save's Orientation / Pos, the skater's place
    and animation, sounds, everything at rest), 0x0043BA20 Finalize (the wheels' places and the skater's bone from the
    model, wheelbase and track, the suspension's rest extension, the colliders' lowest point, the skater's components -
    hidden unless shown driving -, everything made relative to the wheels' centre), 0x0043C1D0 Update (the controls;
    asleep: steering, roll, bones, skater; else up to six sub-steps of at most 1/30 s; the gravity override's end), 0x0043A070
    UpdateSkeleton, 0x0043A2C0 GetWheelRimPoint, 0x0043A400 DebugDraw (compiled out of this build: nothing touched),
    0x0043AA90 MoveToNode (not a Ghidra function: on the ground under the node, sixty passive updates through the
    vtable, asleep, teleported), 0x0043B670 Vehicle_MoveToRestart, 0x0043B720 Vehicle_PlaceBeforeCamera.
  - Found while testing the whole update: the original's x87 compares keep a NaN on the "not greater" side; twelve
    compares in batches 4 and 5 written as `!(a <= b)` took the other side with one - now `a > b` (the brakes' caps, the
    spin damping, CheckSleep's limits, NormalFacesAway, AlignInAir's slow / fast, Collide's depth clamp and friction
    cap). The tests now put NaNs (and a step of no time) into the state.
  - Quirks kept: Finalize always reads four wheels from the model, and the wheelbase / track from wheel -1 when there's
    no wheel of a kind; MoveToNode always averages four wheels and uses wheel -1 with no fixed wheel.
  - Test (TestVehicleLife, TestVehicleRestart): 3000 random components over the thirteen functions (Create out of
    memory, wheel arrays made or kept, every structure name there or not, every frame length up to more than six steps
    and exactly 1/30 s, asleep and passive, bones in debug mode 2, no camera...), whole updates with every call stood in
    or real; Vehicle_MoveToRestart's three ways over 500 cases. The rotations about an axis / x / z are stood in in these
    tests: ours (mth/matrix.cpp, float code with the C runtime's cos / sin) and the original's differ in the last bits,
    and wholly for very large angles - worth a look in that file.
- [x] Range check (after merging main at 1af6002): find_gaps.py shows no gaps in 0x004351B2-0x0043D929; all 85
  functions Ghidra knows there are REPLACEd, plus the 9 it doesn't (94 REPLACEs). thug2_check on THUG2PM.exe and the
  stock THUG2.exe: 0 MISMATCHED, all 635 early self-tests pass; check_code_writes: no overlaps; the register scan and
  the GameFunction audit flag nothing new in the range.
- [x] Game runs (workspace C:\Games\THUG2-4 under the lock; all 4408 replacements installed). The Vehicle component
  only runs with the Anteara mod (the stock player_controlled_vehicle.qb is empty; the mod restores it): with
  `anteara_mod=1`, BO in free skate, `run goal_initialize_skater control_type=PoliceCar edited_goal` (then JunkerCar
  in a second run) - the car made under the skater, its camera and sounds, woken; driven with X held ~5 s, steered
  left and right: it drove about a block, across the street, up the kerb with two wheels on the State House steps,
  into a traffic car; square / triangle pressed. No exception, run-time check, CRT report or crash in the log.
  For the user: Anteara mod on, its vehicle menu in the pause menu - drive the cars (gas, brake, handbrake, jumps,
  crashes into walls, flipping over and righting, restarts). Disable with `[replacements] disable=vehicle_component.cpp,
  vehicle_physics.cpp,vehicle_collision.cpp,vehicle_sound.cpp,velocity_component.cpp,vibration_component.cpp,wait_and_die.cpp`.
- [x] Mutations, batches 4-6 (out/scripts/mut4.ts / mut5.ts / mut6.ts): 95 + 75 + 99 deliberate bugs. All caught except 5,
  each looked at: ApplyDamping's yaw stop `<` -> `<=` (at a tie both give +0), ApplyTyreForces' `>` -> `>=` (a tie
  random floats don't reach; half of it gives the same result), Collide's depth `<= 0` -> `< 0` (a zero depth pushes
  by zero), the depth push's w rounded first (the force's w isn't used), the depth clamp's NaN side (a NaN contact has
  already made the impulse pass's results NaN, which the traces compare as one value). Construct's type store was
  missed once (TraceLife didn't record the component's type): the life test now records it and sometimes leaves
  another type there first - caught.
