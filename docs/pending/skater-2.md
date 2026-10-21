# Skater core physics, the large functions (0x00508773-0x00510823) - skater-2 agent (SK2)

Branch worktree-agent-a33e573f49a630974. Source: src/game/skater/core_*.cpp (methods of Skater::CorePhysicsComponent,
declared in core_physics.h - additive blocks) and core_fields.h (the component's fields by offset and the small game
calls they share). Tests: src/hook/selftests_skater_*.cpp, kit src/hook/skater_core_testkit.h (original vs ours on one
stand-in component, every callee - and our rewrite of it where ours calls it directly - a recording stand-in; float
results left on the x87 stack; traces compared with the whole arena). Shared files touched: CMakeLists.txt (new files
listed), src/game/skater/core_physics.h (new declarations, a block before `private:`).
Switch off: `[replacements] disable=core_jump.cpp` (and the batch files below).
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
    sound), wall (the sound with flag +0xE1 as terrain; ground / air use +0x1AA) - no sound with no_sound. The jump
    sound 0x00518EE0 on the sound component: |velocity| / Skater_Max_Max_Speed_Stat. +0x378 (held time) capped at
    Skater_max_tense_time; the speed = min + (max - min) * held / maxTense (double), the pair by BonelessHeight and
    "from vert" (flag +0x30, or ground with +0x40): Physics_{,Air_,Boneless_,Boneless_Air_}Jump_Speed{,_min}_Stat;
    params' speed overrides. On vert going down: velocity += +0x58 * speed and speed 0, off vert; else a downward
    velocity is zeroed; vy += speed; up.y < -0.1: vy -= 1.5 * speed and position += up * 12. State wall (or air
    after wall, +0x94): 0x005002D0. +0x2EC: TriggerRailScript(jumping). Into the air, +0x4EC = now, SkaterJump
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
