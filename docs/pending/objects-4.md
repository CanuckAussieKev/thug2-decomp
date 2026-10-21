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
