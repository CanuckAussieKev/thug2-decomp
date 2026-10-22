# Object components 0x00423643-0x0042CECD - objects-2 agent (OC2)

Branch worktree-agent-a6dab05a26426d059. The range holds the motion component (0x00423650-0x004276BF), the
movable contact / node array / object hook manager components (0x004276C0-0x00428290), the 0x6DCFEC46 component
(0x00428290-0x004298B0), the particle component (0x00429940-0x0042B620), the rail manager component
(0x0042B620-0x0042BAA0) and the start of the rigid body component (0x0042BAC0-0x0042CECD). Besides Ghidra's 119
functions, the motion component's script member functions are code Ghidra never made functions of (the member
function table at 0x00681EB8 is filled by the dynamic initialiser 0x00640E30): 0x004237D0, 0x00423810, 0x00423830,
0x00423890, 0x00423B30, 0x00424B90, 0x00425150, 0x004251C0, 0x00425270, 0x004252C0, 0x00426770, 0x00426790,
0x004267B0, 0x00426880, 0x004268A0, 0x004272D0, 0x00427470, 0x00427490, 0x004274B0, 0x004276A0 (and 0x00428280,
0x00429940 elsewhere).

Tests: src/hook/selftests_obj_motion*.cpp, kit src/hook/motion_testkit.h (on skater_core_testkit.h's arena and
recording stand-ins: original vs ours on one stand-in component, every callee recorded, the script structures'
getters answering from a per-variant scenario; the whole arena compared).
Shared files touched: CMakeLists.txt (new files listed), src/game/mth/vector.h / .cpp (Vector::operator/=,
Mth::GetDistance added), src/game/obj/motion.h (the class now derives from CBaseComponent with its whole layout;
motion.cpp's RotateTo uses m_object; selftests_motion.cpp follows).
Switch off: `[replacements] disable=motion_component.cpp` (and the later files listed below).

- [x] Batch OC2-1 (NOT run in the game yet; the motion component's life, wait and simpler member functions, 19
  functions): src/game/obj/motion_component.cpp (new), motion.h (layout), motion_internal.h (new: checksums, x87
  helpers), vector.cpp (two helpers).
  - 0x00423650 Mth::Vector::operator/= (a copy among the motion code, also used by the skater: the reciprocal kept
    unrounded); 0x00423680 Mth::GetDistance (z, x, then y squared; unrounded; cdecl, ST0).
  - 0x00426650 Create (Mem::Calloc 0x24C, the constructor inlined), 0x00424AA0 Construct (also called by 0x0057B324),
    0x00424AE0 DestroyBody (path follower deleted through its vtable, the turn's interpolator and the follow trail
    freed - the trail's destructor is the empty 0x005480A0, the interpolator's none), 0x004266F0 Destroy, 0x00423D00
    Reset, 0x00423C90 InitPathFollower (game_objects.cpp calls it for peds and cars).
  - Vtable: 0x00426720 InitFromStructure (Name as both nodes, then 0x00425970 orients to it), 0x004236C0 ProcessWait
    (slot 6, the script's wait types 5 move, 7 jump, 8 path speed <= 0 or NaN, 9 rotation; 6 never), 0x004274D0
    CallMemberFunction (only Obj_MoveToNode by name: everything else is in the member table).
  - 0x00423740 UpdateHover (the clock's doubled count mod the period taken as unsigned; x87 FSIN), member functions
    Obj_StorePos 0x004237D0, Obj_StoreNode 0x00423810, Obj_SetConstantHeight 0x00423830, Obj_GetRandomLink
    0x00423890, Obj_Hover 0x004238D0 (period 1000 / Freq truncated, then varied by Rnd(period * 20 / 10) - period /
    10, at least 1), Obj_GetSpeed 0x004239F0 (moving: the move's speed; locked by a lockobj: this frame's distance /
    the slomo frame length; else |velocity|; mph unless ips, fps), Obj_Jump 0x00423B30.
  - Names found by checksum: the member functions Obj_IsMoving, Obj_IsRotating, Obj_StopMoving, Obj_StoreNode,
    Obj_SetConstantHeight, Obj_LookAtNodeStored, Obj_LookAtPos, Obj_LookAtRelPos, Obj_FollowPathStored,
    Obj_MoveLeft, Obj_WaitStop, Obj_SkipToRestart, Obj_FollowLeader (0x434EA1FB, 0xE8D121CB, 0x8B669799 unknown);
    parameters col_dist_above / col_dist_below, randomlink, originalnode, switchobjscript, gameobj, OrientY;
    0x26AF9DC9 is FLAG_MAX_COORDS (motion.cpp's "name unknown" corrected).
  - Test (selftests_obj_motion1.cpp): TestMotionLife (200 variants: allocation failing, follower / interpolator /
    trail there or not), TestMotionWait (3000: wait types 0-11, path speeds incl. -0 and NaN, names), TestMotionHover
    (periods 1 to 2^31-1, clock offsets; Obj_Hover with Off / Amp / Freq incl. 0, negative, tiny), TestMotionMembers
    (the member functions, every parameter given or not; GetSpeed's three sources and units), TestMotionVectorHelpers.
    37 deliberate bugs, 36 caught; the one missed (Obj_GetSpeed's sum order) is equivalent in practice: the double
    sums differ in the last bit at most and the result is rounded to a float.
- [x] Batch OC2-2 (NOT run in the game yet; the motion component's update, moving to points, paths, following a
  leader, 15 functions): src/game/obj/motion_path.cpp (new; renamed motion_update.cpp at the merge with main - OC1 has a motion_path.cpp of its own).
  - 0x004270E0 Update (slot 1): the frame length, hover (back to the base height, then the bob), the axes' turns,
    Obj_Rotate's turn, then move-to / path / leader (the first that applies), the jump (its position copied); the
    ground offset eased in at 384 per second squared (the speed zeroed on a change of direction or an overshoot -
    the rest of the offset then); objects with ids over 15 (not skaters) get the matrix as their display matrix.
  - 0x00424200 UpdateMoveTo (the speed capped only when >= the maximum: a NaN speed stays NaN), 0x00426E70
    UpdatePath (not for objects with a pedlogic component; the path's speed 0x00423530 is OC1's; heading kept level
    with flag2 0x400; right / at rebuilt by cross products unless sticking to the ground), 0x004246A0
    UpdateFollowLeader; the step velocity: x from the unrounded difference and reciprocal, y / z / w rounded.
  - 0x00423FC0 StartPath, 0x00424020 ArriveAtNode (SpawnObjScript spawned, else switchobjscript), 0x00424180
    FollowPathLinked, 0x004244C0 GetNextObjOnPath (the group's slots are 12-byte smart pointers; same direction,
    ahead, same segment, within range, nearest), 0x004246F0 FollowLeader (drops the object's lockobj lock: no null
    check), and Obj_GetNextObjOnPath (`ob`), Obj_SetPathTurnDist (`inches`), Obj_FollowPath, Obj_FollowPathLinked,
    Obj_FollowPathStored, Obj_FollowLeader.
  - Test (selftests_obj_motion2.cpp): TestMotionUpdate (6000 variants: flags, ground offsets incl. exact-step and
    NaN, ids), TestMotionMoveTo (6000: accelerating, capping exactly, arriving exactly, NaNs), TestMotionUpdatePath,
    TestMotionLeader, TestMotionPaths (StartPath, ArriveAtNode, FollowPathLinked, the members, GetNextObjOnPath over
    groups of up to 7 objects - empty slots, itself, NaN facings, distances tying with the range and each other).
    58 deliberate bugs caught, 7 after strengthening; one real bug found by them (the NaN cap).
- [x] Batch OC2-3 (NOT run in the game yet; the motion component's rotation and looking at things, 24 functions):
  src/game/obj/motion_rotate.cpp (new); motion.h (+0x70 is "height only" for StickToGround, +0x74 the cached
  ground triangle, +0x1CC a Mth::SlerpInterpolator; the members declared); motion.cpp's RotateTo now calls
  StartRotation as a member.
  - 0x00423F10 SetRotationDirection, 0x00424B90 / 0x00424D50 / 0x00424F50 Obj_RotX / RotY / RotZ (Angle in degrees -
    FLAG_MAX_COORDS swaps Y and Z and flips X's sign -, the deceleration, Acceleration and speed; for ever without an
    angle or a deceleration), 0x004258B0 RotateAboutAxis (orthonormalised about up first; the turn multiplied in
    front), 0x00425A80 UpdateAxisRotation (accelerating to the maximum, decelerating to 0 - which then stops it -,
    the step capped at the target, done when reached), 0x00425C70 UpdateRotations, 0x00425520 StartRotation (a
    slerp from the matrix to the target over the time given or 1 s: made on first use - the manager made ready -,
    or straight there), 0x00424440 UpdateTurn (a turn time of 0 or past it: the end).
  - Looking: 0x004240E0 SetupLookAxis (the angle in the plane, nothing below the threshold), 0x004252E0 LookAt (at
    once, over a time, or at a speed with an acceleration; the axes from 0xA4AECB20), 0x00425710 LookAtNodeLinked,
    0x004257A0 LookAtNode, 0x00425800 LookAtPosition, 0x00427500 LookAtObject (skater / ped / car / other types,
    not itself, the nearest), 0x00425970 OrientToNode (the node's angles: X, Y, Z multiplied in turn; the display
    matrix too); Obj_Rotate, Obj_LookAtNode, Obj_LookAtNodeLinked, Obj_LookAtNodeStored, Obj_LookAtPos,
    Obj_LookAtPosStored, Obj_LookAtRelPos, Obj_LookAtObject.
  - Test (selftests_obj_motion3.cpp): TestMotionRotAxes (4000 variants each), TestMotionAxisUpdate,
    TestMotionTurn (turn times of 0 with negative elapsed times), TestMotionLookAt (SetupLookAxis, LookAt 8000
    variants, the look-at members with the position looked at recorded, OrientToNode). 56 deliberate bugs, 55
    caught (2 after the position looked at was recorded, 1 after negative elapsed turn times were added); the one
    missed (UpdateAxisRotation's `<` as `<=` when capping at the maximum) only differs when |speed| equals the
    maximum, where both set the same value.
- [x] Batch OC2-4 (NOT run in the game yet; the motion component's jump, sticking to the ground and moving to
  points, 15 functions): src/game/obj/motion_ground.cpp (new); motion.h (declarations); motion_component.cpp /
  motion_update.cpp call the members instead of the game's addresses.
  - 0x00425CB0 GroundHit (register arguments: ECX the cached triangle, EDX the end, the start and the result on the
    stack, the caller pops - REPLACE_UNCHECKED naked stub over a cdecl body): the cached triangle first, then a
    feeler (the face hit, 8- or 10-byte faces, becomes the cached triangle). 0x00424870 UpdateJump, 0x00425F40
    StickToGround (height only: the ground offset; else three feelers 72 back / ahead and 30 to the side give
    the height and the axes), 0x004269F0 MoveTo (RET 0x1C: the target by value; at once, a time, or a speed in
    mph / fps / ips with an acceleration; the original reads `time` into the target argument's x), 0x00426C10
    MoveToNode, 0x00426CB0 MoveToPosition, 0x00426D80 MoveToLink (LinkNum 1-based, randomlink), Obj_StickToGround,
    Obj_SkipToRestart (its model component updated - none: a crash, as the original), Obj_MoveToPosStored,
    Obj_MoveForward, Obj_MoveLeft, Obj_MoveToPos, Obj_MoveToRelPos, Obj_MoveToLink.
  - Test (selftests_obj_motion4.cpp): TestMotionJump (8000 variants, one in seven landing exactly on land_height),
    TestMotionGroundHit (the original called with its register arguments), TestMotionStick (8000: both GroundHit
    conventions answered alike), TestMotionMoves. 40 deliberate bugs caught (1 after the exact landing case).
- [x] Batch OC2-5 (NOT run in the game yet; the movable contact, node array, object hook manager, rail manager,
  chain, particle and proximity components, 74 functions): src/game/obj/movable_contact.cpp, node_components.cpp,
  chain_component.cpp, particle_component.cpp, proximity_component.cpp (+ .h, all new); smtptr.h (CSmtPtr::Construct,
  the shared in-place default constructor 0x0042B580).
  - Movable contact (0x004276C0-0x00427DD0, 10): Create / DestroyBody / Destroy, FindContact (a feeler from 400 above
    to 6 below, nudged up 0.1 and retried up to 10 times while it hits a dead object), FindContactAhead,
    GetContactPoint, StartRiding / StopRiding (the stop time is the game clock plus its offset 0x00696D60) /
    IsRiding (stops riding when the contact is gone), UpdateContactPoint.
  - Node array, object hook manager, rail manager (0x00427EA0-0x00428220, 0x0042B880-0x0042BA90, 14): life,
    InitFromStructure / RefreshFromStructure (the model path "models\%s" - the original's unbounded strcpy / strcat
    as a bounded snprintf), UpdateRailMatrix.
  - Chain (0x00428250-0x004298B0, 14; the 0x6DCFEC46 component): a link's / a chain's constructors and destructor,
    CLineObject::SetBlendMode 0x00428270, the list's destructor, Create / Destroy / Finalize, InitFromStructure
    (collision_info, chains, segments, lengths, bones, obj_end), CallMemberFunction, Update (Verlet steps at the
    chain's rate, the ends on bones / another object, constraint passes, the collision plane, the lines shown when
    the object isn't hidden), a chain's UpdateLines.
  - Particle (0x00429940-0x0042B040, 30): RegisterMemberFunctions, life, Hide, the 19 script setters (the unnamed
    float read into the `params` argument's own slot: not given, the pointer's bits), InitFromStructure (0x15C-byte
    parameters: oriented to 0x0C4C809E, scaled, coloured with alphas capped at 128, rotated X / Y / Z, local
    positions, the model followed, the emit script spawned), Update (lifetimes against the clock + offset, the
    ground value, positions / orientations moved with the object), CallMemberFunction (0x2299A9F2: the system's
    slot 7; 0x96896315: a ground feeler 4800 down), GetParticleParams (the name, two floats and the colours read
    straight from the system's +4 after the result structure is made; positions, sizes and boxes through the
    getter 0x004AAD40, once each).
  - Proximity (0x0042B570-0x0042B850, 5 + CSmtPtr::Construct): RunScript, Update (in / out by the skater's
    capsule, the reach test squared in double), Proximity_Init / SetActive / EnterProximityNotHandled.
  - Test: selftests_obj_contact.cpp (TestContactLife, TestContactFind, TestContactRiding, TestNodeComponents),
    selftests_obj_particle.cpp (TestParticleLife / Members / Update / Init / GetParams, TestProximity,
    TestChainLife / Members / Init / Update). 66 deliberate bugs, 65 caught (19 after strengthening: the particle
    Init and GetParams tests registered - they had been written but never run -, the clock offset given values,
    lifetimes ending exactly now, the skater exactly the reach away, UpdateLines tested without its own stand-in,
    0x0C4C809E always given - without it both versions orient by their own stack's leftovers); the one missed
    (alpha `>` 0x80 as `>=`) is equivalent: both give 0x80. Found by the tests: GetParticleParams' order (rewritten
    to match).
- [x] Batch OC2-6 (NOT run in the game yet; the rigid body's code before its life and physics, 15 functions):
  src/game/obj/rigidbody_support.cpp, src/game/mth/rigidbody_math.cpp (new); OC3's rigidbody_component.h (the
  members declared), rigidbody_physics.cpp / rigidbody_component.cpp call them instead of the game's addresses
  (their tests stand in for ours too); object.h (CBaseComponent::RunMemberFunction), quat.h (Quat::GetMatrix),
  vector.h (Vector::SetLength) - declarations added.
  - 0x0042BAC0 ToggleRigidBodyDebug (off, the shapes, the details too, off), 0x0042BB00 RegisterMemberFunctions,
    0x0042BB20 CBaseComponent::RunMemberFunction (most components' vtable +0x2C: the entry's member function on
    `this` moved by the entry's adjustment, else +0x30 by name; EAX passed on whole), 0x0042BB60
    RigidBody_IgnoreSkater (the pushers' cooldown; frames / frame: sixtieths), 0x0042BBB0 RigidBody_Disable,
    0x0042BBD0 RigidBody_Reset (InitFromStructure again) - names from their checksums -, 0x0042BD60
    SetRigidBodyPusherReach (0x006939B0 is a float per pusher - OC3's AddRigidBodyPusher clears it as an int),
    0x0042BD90 Finalize (the sound component; each spring's anchor where its point is), 0x0042BE90 Sleep
    (SettleScript), 0x0042BF00 UpdateWorldPoints (three different sum groupings, kept), 0x0042C140 FeelPushers (the
    first pusher in reach pushes it - one per frame; a pusher not moving relative to it ends the look for all;
    the push's shadow sector "<id>_Shadow" hidden; the collide sound with its mute delays divided unsigned),
    0x0042C6D0 Damp (velocities: w too), 0x0042C890 CheckSleep (the die timer once 48 from where it woke),
    0x0042BBF0 Vector::SetLength, 0x0042BC40 Quat::GetMatrix.
  - Test (selftests_obj_rigidbody_support.cpp): TestRigidBodyMembers, TestRigidBodyWorld (Finalize, Sleep, the world
    points, Damp with 0 / NaN / exact drags, CheckSleep on the sleep points and 48 exactly), TestRigidBodyPushers
    (20000: random and exact cases - on the radius, the band's edges, straight at / across / up, negative mute
    delays, an infinite rotation factor), TestRigidBodyMath (20000 each, zeros and NaNs). 85 deliberate bugs, 74
    caught (6 after strengthening); the 11 missed are equivalent or below float precision: the double sums'
    grouping in Finalize / UpdateWorldPoints (4) and GetMatrix (2, one a commuted sum), FeelPushers' mid boost
    (2s either way), the zero-length replacements (h and g: multiplied by 0; across: the impulse is NaN then).
