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
  leader, 15 functions): src/game/obj/motion_path.cpp (new).
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
