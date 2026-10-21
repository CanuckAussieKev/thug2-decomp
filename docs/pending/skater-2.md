# Skater core physics, the large functions (0x00508773-0x00510823) - skater-2 agent (SK2)

Branch worktree-agent-a33e573f49a630974. Source: src/game/skater/core_*.cpp (methods of Skater::CorePhysicsComponent,
declared in core_physics.h - additive blocks) and core_fields.h (the component's fields by offset and the small game
calls they share). Tests: src/hook/selftests_skater_*.cpp, kit src/hook/skater_core_testkit.h (original vs ours on one
stand-in component, every callee - and our rewrite of it where ours calls it directly - a recording stand-in; float
results left on the x87 stack; traces compared with the whole arena). Shared files touched: CMakeLists.txt (new files
listed), src/game/skater/core_physics.h (new declarations, a block before `private:`).
Switch off: `[replacements] disable=core_jump.cpp` (and the batch files below).
The physics states (+0x1528's +0x18): 0 ground, 1 air, 2 wall ride, 3 lip, 4 rail, 5 wallplant (core_fields.h).
PartyMod: none of these functions is patched by PartyMod (its 48 patched functions were done in the PartyMod rounds).

- [x] Batch SK2-1 (NOT run in the game yet; jumping, skitching, the rail's TriggerScript, the wallplant state and
  seven script member functions, 12 functions): src/game/skater/core_jump.cpp (new).
  - 0x005092E0 TriggerRailScript(const int* events) (thiscall, RET 4): the rail node being ground (+0x22C,
    Obj::CRailNode) - its script node's TriggerScript, else the first node back along m_prev that has one (the walk
    stops at a loop: a second pointer advanced every other step; a missing script node there is used as is - a
    crash in the original too); the rail manager +0x230 filled from the skate module's +0x24 when unset; flag +0xC8 =
    whether the node's Name differs from +0x244; +0x240 / +0x244 = the name; TriggerNode(events, name, null, the
    ride tracker's object - or null, stopping the ride when it's gone). Events are const int[3] in .rdata
    (0x0064BBD4 off a lip, 0x0064BC10 jumping, 0x0064BC1C TriggerGrindOff).
  - 0x00509450 DoJump(CStruct* params) (RET 4): by state - ground (trigger feeler 0x0064BB8C on +0x158 first: a
    trigger's script taking over ends it; +0x338 = the position; the jump sound; 0x004FEBA0; flag +0x48), air (the
    sound), rail (state 4: the sound with flag +0xE1 as terrain; ground / air use +0x1AA) - no sound with no_sound. The jump
    sound 0x00518EE0 on the sound component: |velocity| / Skater_Max_Max_Speed_Stat. +0x378 (held time) capped at
    Skater_max_tense_time; the speed = min + (max - min) * held / maxTense (double), the pair by BonelessHeight and
    "from vert" (flag +0x30, or ground with +0x40): Physics_{,Air_,Boneless_,Boneless_Air_}Jump_Speed{,_min}_Stat;
    params' speed overrides. On vert going down: velocity += +0x58 * speed and speed 0, off vert; else a downward
    velocity is zeroed; vy += speed; up.y < -0.1: vy -= 1.5 * speed and position += up * 12. State rail (or air
    after a rail, +0x94 the last state): 0x005002D0. +0x2EC: TriggerRailScript(jumping). Into the air, +0x4EC = now, SkaterJump
    broadcast. x87 roundings kept (x / y products unrounded where the original keeps them on the stack).
  - 0x00509920 BeginSkitch / 0x00509990 EndSkitch: ride the object at +0x364 (0x0043EB80), flag +0x88, +0x24A,
    0x00501DB0, event 0x35224F25 to it / flag off, event 0x2739B86D, the smart pointer released (inlined Release),
    the ride tracker freed (its object kept at ride +0x6C, its CSmtPtr released, Mem::Manager::Free, +0x70 = now).
  - 0x00509050 DoWallplantPhysics (state 5): after Physics_Wallplant_Duration (elapsed ms as an unsigned -> float)
    into the air with a feeler 12 ahead / 36 behind along the facing (trigger events 0x0064BBF8 on a hit); else no
    rail node, carried by the ride tracker (gone: stop riding), its turn applied to the object's and the display
    matrix (D3DXMatrixMultiply 0x005BFA8C).
  - Script member functions found outside Ghidra's functions (0x00509A40-0x00509BCF, in the component's member
    table at 0x006826C8): Jump 0x00509A40, StartSkitch 0x00509A60, StopSkitch 0x00509A70, SetState 0x00509A80
    (unnamed checksum: AIR / Ground (+0x1C5 cleared) / Lip -> 0x004FDED0 state 1 / 0 / 3; leaving a lip runs the
    rail's TriggerScript; on a rail 0x004FE860 with (0,1,0,1) and the display matrix reset), TriggerGrindOff
    0x00509B70, 0x00509B90 (member 0x675CF35E, name unknown: ForceBreakVert - BreakVert(true) + 0x00504DF0),
    WalkingWallplant 0x00509BB0 (0x00507500(0, 0, 1)). All return 1.
  - Test (selftests_skater_jump.cpp): TestTriggerRailScript (rails of 1-8 nodes, ending / looping to the first / into
    a loop, script nodes missing, scripts anywhere, the manager set or not, same name or not, ride states; 4000
    variants), TestDoJump (8000: every state, the flags, held times incl. at / over the max, a speed param, going
    down, steep ramps), TestSkitch, TestDoWallplantPhysics (durations around the elapsed times, wrapping times, ride
    states, turned or not), TestCoreMemberFunctions (the seven, SetState with every state name in every state).
    Coverage counters fail the test when a branch isn't reached. 36 deliberate bugs caught of 36.
- [x] Batch SK2-2 (NOT run in the game yet; the wall checks, 3 functions): src/game/skater/core_collision.cpp (new).
  - 0x00508780 CheckAirForwardCollision(const Vector* from) (RET 4, bool; DoAirPhysics' "stopped"): the move's
    direction (z's difference kept unrounded as the original), a feeler from `from` + up * First_Forward_Collision_
    Height to the new position raised + direction * First_Forward_Collision_Length. A skatable hit counts only when
    its normal . ground normal < 0.8 and normal.y < 0.5. The component's feeler copied to the stack (0x004F8200);
    the skater tried at the feeler's end lowered back - 0x004FFDB0 true and (vy > 10 or the wall steep, n.y < 0.1):
    stopped there (true; the stack feeler's smart pointer let go inline). Else put back, the feeler copied back
    (0x0043A930); a ledge (up.y > 0.5, vy > 0, n.y < 0.01): the stack feeler raised by Physics_Air_Snap_Up, lowered
    2 at a time while the snap stays over 4 (the unrounded value compared), the skater raised by it (true). On vert:
    at the hit, Skater_Min_Distance_To_Wall off it, the velocity rotated (wall ride, +0x78) or projected along it
    (false). Otherwise a bonk unless a wallplant (0x00507500(0,0,0), not with +0x302) or 0x00508140 takes over
    (unless flags +0xE0 / +0x70): flag +0xB0, the trigger {9,0,0}, a sound for a wall-ish hit (n.y < 0.05: 0x00518DD0
    by speed / max max speed and terrain, or the script vehicle_play_land_sound on a vehicle +0x2ED), the velocity
    along the wall (its y kept unless the wall faces down) plus a tenth of its length along the normal, +0x14F0 =
    the normal, the facing turned 0.05 off the wall and the matrix rebuilt (cross products, normalised), a moving
    hit object's velocity (into the skater) carried: position += v * frame * 2, velocity += v.
  - 0x0050A2C0 CheckSideCollision(float side, float length) (RET 8, bool): never on a rail; the feeler from the
    side height (Skater_side_collide_height) along right * side * length (+ Skater_air_extra_side_col in the air;
    x, y products rounded, z, w unrounded - as the original). In the air: 0x00508140 taking over -> true; else
    pushed out by a tenth of the overlap (distance - the extra reach, its vector's length set by 0x0042BBF0), false
    (the 0x005039F0 call there needs a state other than air: dead code, kept). On the ground, for a near-vertical
    wall (|n . up| < 0.25): back to +0x98, 0x005039F0(normal, an uninitialised float in the original - ours 0,
    0.2), a stack feeler from hit + n to hit + n * length: clear and no trigger taking over -> at its end less up *
    side height (true).
  - 0x0050C930 CheckSideCollisions: both sides (Skater_side_collide_length), -1 first; hit on both -> the position
    put back.
  - Test (selftests_skater_collision.cpp): 12000 / 16000 variants each, full-precision values (roundings show) and
    values on every comparison's edge, axis-aligned normals (dot products exactly at their limits), the stack
    feelers' lines recorded at each GetCollision, the vector functions the original calls pointed at ours; coverage
    counters for each path. 46 deliberate bugs: 42 caught (5 after adding full-precision values, exact edges and a
    hit object moving along the wall), 4 equivalent: the snap loop's unrounded compare (6 - 2 is exact), depth > 0
    vs its float (differs only below the smallest denormal), the ground path's length vs reach (equal off the air),
    the length's summation order in the forward feeler (a one-ulp double change that the float direction absorbs).
- [x] Batch SK2-3 (NOT run in the game yet; getting onto a rail, 1 function, 6 KB): src/game/skater/core_grind.cpp (new).
  - 0x0050A770 StartGrind(const Vector* point, CRailNode* node, CRailManager* manager, bool noLipTrick, bool
    fromObject) (RET 0x14; the Update passes 0, 0, 0x0050C670 1, 1): one REPLACEd function split into helpers
    (KickOffPointRail, SpinOnPointRail, StartLipTrick, GrindAlongRail and their shared pieces). First the
    flip-and-rotate component's 0x00511570; a new rail: the node's index ((node - manager +0x40) / 0x54) to
    0x005627B0 of +0x1520's +0x18; +0x22C / +0x230 = node / manager. A feeler from the skater to the point hitting
    more than 6 from it stops everything (two forms of the distance, rounded as the original).
    Point rail (no prev, no next): a spin button (+0x100 / +0xA0) held and (unnamed script float 0x0A477668) * 1000
    ms since +0x1504 -> the spin (0x0A477668 is Physics_Natas_Delay: a Natas spin): stopped on the point, +0x2EC, +0x1500 = +0x100 held, state 4, 0x004FC470 with the
    terrain, the landing sound, PointRailSpin, TriggerRailScript {spin}, +0x1504 = now. Else the kick: the velocity
    level (fast ones turned level), the kick direction at Physics_Point_Rail_Kick_Upward_Angle, Name to +0x240 and
    0x00522370, a TriggerScript run, onto the rail (state 4), terrain + sound, the speed along the move plus
    Point_Rail_Speed_Boost (from an object and too slow against the facing: from standing along the facing),
    Rail_minimum_rerail_time (+0x23C, truncated), the velocity turned to the kick, the TriggerScript again (kicked),
    into the air 1 higher, PointRail.
    Lip trick (unless +0x375: going up, |up.y| < sin(LipPlayerHorizontalAngle), facing up, |ground normal y| <
    cos(LipRampVertAngle), |right.y| < sin(LipAllowAngle - _Override for nodes flagged 1); not with noLipTrick or
    on a vehicle +0x2ED): stopped, flags off, 0x004F8EE0 of +0x1524, +0x258 = where it was, state 3, at the point
    upright along the ramp top (up the level ramp normal, facing up, right across), the gap component's
    0x00511F60(0, 0x100000), TriggerRailScript {lip}, the object's own script (made if none) set to LipTrick and run,
    flag +0xE0 set raw, the terrain.
    Grind: the segment's direction (node to next, world positions), the acid drop's hold velocity added when flag
    +0xA8 (with its two compiled-out debug prints - Dbg::Printf with the game's strings), still: x / z from the
    facing, the way along the velocity (a tie: Mth::Rnd(2)), nothing at the rail's end that way (the point is the
    end node), the TriggerScript, onto the rail, the sound (not in the trigger state's same millisecond), the speed
    along the rail (height kept) plus Rail_Speed_Boost (from an object, too slow against the facing: from standing,
    the facing's way), which side the jump started (+0x338), the bad ledge feelers either side
    (Physics_Ground_Snap_Up, Rail_Bad_Ledge_Drop_Down_Dist, Rail_Bad_Ledge_Side_Dist: +0x249 bad, +0x248 any),
    sideways (|dir . right| under Rail_Tolerance) or turned round (+0x34C), the trick from the skater component
    (0x00522E80) or by the d-pad (Obj::DPadDirection, 0x005004B0); +0x234 = now.
  - Test (selftests_skater_grind.cpp TestStartGrind): 16000 variants: rails of 2-5 nodes and point rails, the
    point at nodes / the rail's ends, every state and flag, the lip conditions met, still / barely moving / fast /
    level-speed-exactly-10 velocities, no facing (a tie, the coin), no right (the tolerance's limit), a hit exactly
    6 away, the jump started at the point, full-precision values; GetWorldPos and the rest stood in, the vector
    functions pointed at ours. 49 deliberate bugs: 44 caught (8 after adding those values), 5 equivalent: a node
    with no next but a prev (the original reads the missing next's link: a crash either way), the summation
    order of the two squared lengths compared with constants (one-ulp changes), the jump-adjust speed rounded to a
    float before squaring (same), and sign < 0 vs <= 0 (the sign is always +-1).
- [x] Batch SK2-4 (NOT run in the game yet; the lip state, back on the board, trying for a rail, 3 functions):
  src/game/skater/core_states.cpp (new), core_grind.cpp (TryGrind). +0x1524 is the balance trick component (+0x18
  the trick's kind, +0xD4 a lip's CBalance).
  - 0x0050BF50 DoLipPhysics (state 3): flags +0x78 / +0xA8 / +0x80 off, +0x2E4; riding something: the position and
    +0x258 from 0x00427A00 of the ride, its turn applied to both matrices (else stop riding); a Lip balance trick:
    the Perfect Rail cheat (its script integer's global flag on the career, the lean +0x104 / +0x108 cleared,
    0x007CE4FD set) and CBalance::Update of +0xD4; button +0x1C0 -> flag +0x20; 0x004FFB10 (off): flag +0xD8 and the
    rail's TriggerScript twice (0x0064BBE0, then the off-a-lip events); the node's Name to +0x240 and 0x00522370 (only
    with the rail manager's node array); +0x238 = now.
  - 0x0050C670 ResumeSkating(bool onGround, const uint8_t* grind, void* acidDrop) (RET 0xC; called by 0x00517B20):
    0x004FDE40; state 0 or 1 (+0x94 the same; the air: flag +0xC0, flag +0xB0 when +0x1534's +0x9B); +0x90 = now;
    the display matrix, the ground normals (+0x68 / +0x78 / +0x58) from the up, +0x98 and the old position from the
    position; +0x1C5, +0x2C0, +0x378, +0x39C, +0x3A0, +0x370, +0x310 cleared, +0x311 set, flag +0xF4 cleared raw,
    +0x350 cleared unless 0xC0000000, +0x3AC = 1, +0x4E0 / +0x4E4 / +0x4EC / +0x4E8 cleared. A rail (the point; +0x10
    its manager, +0x14 its node, +0x18 an object): the old ride let go (as EndSkitch), the object ridden,
    StartGrind(no lip trick, from an object), the positions kept again. Else an acid drop: 0x00502F20.
  - 0x0050DAE0 TryGrind(bool always) (RET 4; from the wall ride update - the main update inlines the same): only in
    the air or `always`, not with flag +0x70 / +0x3A4, the grind button +0x1A0, not within
    Physics_Wallplant_Disallow_Grind_Duration of +0x4E0 (unsigned); Rail_Corner_Leave_Angle's cosine as the
    alignment while 0x00420250(+0x234) < +0x23C (not in the park editor, 0x007CE5D8 +0x40), else 1. The level's rails
    (skate module +0x24): FindGrindRail from the old position to the new; found: 0x004FC4A0(2, ...) (debug), stop
    riding, StartGrind if 0x00500020 allows. Else each RailManager component (the composite object manager's list,
    +0x14 the next): its rails (vtable slot 13), the move into its object's space (0x0042BA10's matrix, inverted),
    found: the point back to the world, StartGrind, ride the component's object, 0x00427DD0, the velocity less the
    object's.
  - Tests (selftests_skater_states.cpp): TestDoLipPhysics (ride states, turned, the Lip kind or not, the cheat, the
    off check, the node array), TestResumeSkating (ground / air, +0x9B, +0x350, rail with or without an object and
    an old ride, acid, neither), TestTryGrind (every early return, the corner angle's two conditions, the level's
    rail found and allowed, 0-2 moving rail managers found on the first / second / none - stand-in vtables, a
    turned and moved object matrix; the matrix functions pointed at ours). 34 deliberate bugs: 33 caught, 1
    equivalent (flag +0x20 tested before setting it to 1: SetTimedFlag does nothing when it's set already).
- [x] Batch SK2-5 (NOT run in the game yet; grinding, 2 functions, 5 KB): src/game/skater/core_rail.cpp (new).
  - 0x0050DDF0 DoRailPhysics (state 4; one REPLACEd function with the private GrindAlong / FinishRail for its
    second half and its common end): flags +0x78 / +0xA8 / +0x80 / +0xB0 off, +0x2E4; riding a moving rail: its
    RailManager component's matrix asked for (0x0042BA10 - the result isn't used), then carried by the ride tracker
    (its turn on both matrices). Rail_minimum_rerail_time to +0x23C (truncated), flag +0x50 off. 0x004FFB10 (off):
    TriggerScripts {2,4,0} and {13,4,0}, Rail_jump_rerail_time, flags +0xD0 / +0xD8, up 1, the end. A grind / slide
    balance trick: CBalance::Update of +0x7C, then the Perfect Rail cheat (+0xAC / +0xB0 cleared). The node's next
    missing: 0x00504730 and nothing else. The segment's direction - x scaled by 1 / length unrounded, y, z, w by it
    rounded to a float, as the original; Physics_Rail_Gravity projected onto it, over the frame into the velocity;
    the way along it before and after (a dot product's sign; a change flips +0x34C). The next segment that way
    (backward: the previous node flagged 4; forward: the next's next flagged 4) through a corner gentler than
    Rail_Corner_Leave_Angle (both segments level, normalised) - or none. Past the segment's end (the distance from
    the node it started at over the length + 0.1): the overshoot; onto the next segment when its node (backward: the
    previous; forward: the next) is flagged 4, RailClear and a gentle corner (the position at the corner node,
    0x004FC470 with its terrain, the TriggerScript), else TransferOrLeaveRail at the corner node. Still on the rail
    (state 4): the velocity along the (new) segment the same way; the facing that way (+0x34C: the other), the display
    facing +0x38 turned 0.3 towards it (0x0043D990), level rights, ups from cross products for the display (+0x18 /
    +0x28) and the object's matrix; the overshoot dropped when this segment isn't longer; the move (x unrounded),
    plus the overshoot along the facing (x, y unrounded), w + 1. With a next segment (outside the park editor) the
    feeler only when it climbs as this one does (|dy| < 0.01): a feeler from the last position to 6 past the new one
    along the move, raised by the up; a hit (not on faces flagged 0x10 at +0xF9, outside the park editor): off the
    rail - back to the old position, up 1, the velocity along the hit, the air, OffRail. The end: the ground normal
    from the up, the score's rail time (0x00564270 of +0x1520's +0x18: +0x3A8 * its +0x154C, truncated), the node's
    Name (0x00476EC0, GetChecksum's bool form) with the node array.
  - 0x0050C200 TransferOrLeaveRail(const Vector* end) (RET 4): the line from `end` (park editor: back along the
    velocity, w unrounded) raised 6 to the skater raised 6.1; FindGrindRail with this node excluded, the
    corner angle's cosine and this node's GetWallSide; a node with a next, not this one (nor, outside the park editor,
    its next or previous): between its two nodes (the two vectors to them pointing apart) -> onto it; else (not in the
    park editor) when the move is at least the segment's length -> onto its middle. Onto: the velocity along it
    (still: x / z from the facing; Sign of the dot product), +0x22C, the point, TriggerScript, 0x004FC470. Else off:
    TriggerScript {13,4,0}, the air, up 1, OffRail.
  - Tests (selftests_skater_rail.cpp): TestDoRailPhysics (16000 variants: rails of 6 nodes - straight climbing ones
    and random ones, flags 4 on some, the skater at each node and the last, near and far, along and against, still;
    the off check, ride states, every balance kind, the park editor; TransferOrLeaveRail stood in, leaving the rail
    or not) and TestTransferOrLeaveRail (12000: what FindGrindRail finds - nothing, a node, this one, the last, the
    neighbours -, a node at NaN). GetWorldPos returns the node's position; RailClear, GetWallSide, FindGrindRail,
    the lerp 0x0043D990 stood in (recording their vectors); the vector functions pointed at ours. 54 deliberate
    bugs: 49 caught (3 after adding a still-in-x-only skater, a NaN rail position and other states after a
    transfer), 5 equivalent in practice: exact equalities that random data can't reach (the move's length vs the
    segment's, the overshoot vs the next segment's length, the climb difference at 0.01) and two summation orders
    of squared lengths that are then rounded or compared (one-ulp changes).
- [x] Batch SK2-6 (NOT run in the game yet; the ground state, 1 function, 2.5 KB): src/game/skater/core_ground.cpp
  (new); core_grind.cpp's point rail spin wait named (Physics_Natas_Delay).
  - 0x0050EE70 DoGroundPhysics (state 0; the move in the private MoveOnGround): flags +0x48 / +0x30 / +0x38 / +0x78 /
    +0xA8 / +0x80 / +0xB0 off, no rail (+0x22C, +0x244), +0x4E8 / +0x2D0 / +0x2E4 / +0x14CC cleared. The heading
    +0x348 = FastACos(facing.x) in degrees (360 less it when facing.z < 0; the product unrounded for that). Moving
    (|v| > 0.001): the velocity turned into the ground's plane (+0x68). Physics_Ground_Gravity - or +0x360 when +0x350
    isn't 0 and going up - projected onto the ground's plane, over the frame into the velocity (x unrounded). The
    balance trick (+0x1524's +0x18): none - 0x004FE320 ? 0x005019E0 : (+0x324 cleared, 0x004FE490 ? 0x004FE530);
    skitch - its balance (+0x12C) and CHEAT_PERFECT_SKITCH (lean +0x15C); a manual (0x0AC90769 or Manual) - +0x324
    cleared, with +0x302: +0x303 set and 0x004FE490 / 0x004FE530, its balance (+0x24), 0x005232A0(true),
    CHEAT_PERFECT_MANUAL (lean +0x54). 0x00506DF0, 0x005055B0. The step = velocity * frame; skitching (flag +0x88):
    the velocity the skitched object's * skitch_speed_match, no step of its own, 0x00501DB0. Riding: the ride
    tracker's carry (not while skitching) with the old position and +0x98 kept, its turn on both matrices. The move:
    the step, 0x00506E50 (not skitching), then CheckForGround - skitching instead a feeler (the component's own feeler
    line, +0xA8 / +0xB8: 36 above to 200 below along the up) for the terrain. Stopped more than 0.1 short of the step
    (on the ground, once): the rest of the step's length along the velocity (normalised: x / y unrounded, z rounded;
    w as it is) times what's left (1 - moved / step, rounded except for x) - and again. Still on the ground: 0x00503560
    (not +0x2D8), 0x004FF590 with the velocity (not +0x388), CheckSideCollisions and 0x00503B30 (not skitching),
    0x004FF920 (not +0x388), 0x004FFB10 -> 0x004FEBA0 and flags +0x48 / +0xD8; 0x00522370(+0x1B0), +0x30C cleared.
  - Test (selftests_skater_ground.cpp TestDoGroundPhysics): 16000 variants - every balance kind, the cheats,
    skitching with and without the object, ride states, frame lengths, still / exactly-0.001 / full-precision
    velocities; CheckForGround stood in (moving the skater back, stopping it dead, leaving the ground), FastACos
    returning full-precision angles. 40 deliberate bugs: 39 caught (3 after adding the full-precision angles, the
    exactly-0.001 speed and a dead stop short of the step), 1 equivalent in practice (the moved distance's
    summation order).
- [x] Batch SK2-7 (NOT run in the game yet; the wall ride state, 1 function, 4 KB): src/game/skater/core_wallride.cpp
  (new).
  - 0x0050F830 DoWallRidePhysics (state 2; the private OffWallEdge for its three ways off the edge): flags +0x78 /
    +0xA8 / +0x80 / +0xB0 off, +0x2E4, no rail, +0x39C = now. The ride tracker's carry (and turn). The move: velocity
    * t + (0, Wall_Ride_Gravity, 0, 1) * t^2 / 2 (z, w and x's velocity part unrounded as the original), plus the
    carry; the velocity += (0, gravity, 0, 1) * t. Up the wall: the wall normal's (+0x38C) level tangent (normalised;
    none for a level wall) crossed with the normal; not heading up it more than 0.68 (nor, heading up, within
    Wall_Ride_Turn_Speed's cosine): the velocity turned (0x00421440, about the wall normal, the way the right says)
    by Wall_Ride_Turn_Speed, the matrix rebuilt from it (right in double precision). A feeler from the last position
    to past the new one (Skater_First_Forward_Collision_Length along the move): at the hit + its normal; a wall
    (+0x22A) at an angle to this one (|dot| >= 0.01): round the corner (the new wall normal, 0x004FE860, along it);
    else facing up over 0.9: onto the top - at the hit, upright, the matrix queries component's copy, the ground
    normals, TryGrind(true), the ground and Landed unless it grinds; else off the edge. 0x005051B0: off the edge too.
    A feeler down the up (Physics_Ground_Snap_Up to Wall_Ride_Down_Collision_Check_Length): the hit's normal as the
    wall, the skater just off it, along it; not a wall: pushed away (100), into the air (the other feeler's trigger);
    a wall: triggers when +0xFC differs from +0x1AC, 0x005232A0, the feeler kept (0x0043A930), +0x1D of the sound
    component. No hit: off the edge (18 out), the trigger. Button +0x1C0 -> flag +0x20; 0x004FFB10 (jump): flag
    +0xD8, the trigger {2,4,0}, out (Wall_Ride_Jump_Out_Speed) and up (Wall_Ride_Jump_Up_Speed), upright
    (OrthoNormalizeAbout(1)), +0x88 cleared, the ground normals. 0x00522370 with +0x1B0.
  - Test (selftests_skater_wallride.cpp TestDoWallRidePhysics): 16000 variants - walls level / straight up /
    axis-aligned (heading up exactly 0.68), facings up / down / across, the feelers hitting the top (incl. exactly
    0.9), the same wall, a corner at exactly 0.01, other walls, or nothing; ride states, frame lengths, TryGrind
    taking it to a rail; the vector and matrix functions pointed at ours. 42 deliberate bugs: 38 caught (2 after the
    exact 0.68 heading and the 0.01 corner), 4 equivalent: two "+ 0 * t^2/2" terms (only a signed zero could show),
    a heading exactly at a turn's cosine (unreachable with the game's floats), and the display matrix reset on one
    way off the edge (set again before anything reads it).
