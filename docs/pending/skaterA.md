# pm/skaterA: skater core physics with PartyMod's PS2 controls (NOT run in the game yet)

- [x] (PartyMod round; NOT run in the game yet) Two skater core physics functions with PartyMod's PS2 controls
      built in (new src/game/skater/core_physics.*; `Skater::CorePhysicsComponent` - the class skaterB's
      rotation.* uses - now declared there, rotation.h includes it):
      - LipPushOff 0x004FCBA0 (thiscall, two unused stack arguments - a script member function, in the table
        a static initialiser writes at 0x00682970; RET 8; returns int 0/1): the facing (object +0x8C) flattened
        to x, z and normalised (only if its length > 0; w copied, not scaled); the side vector (z, 0, -x, 0).
        Input +0x80 held: 0. Both triggers (+0xC0, +0x120; PartyMod's JZ -> JNZ at 0x004FCC59 makes it either):
        velocity (object +0xAC) += facing * physics float 0x1F96C224 (0x00580400). +0x60 held longer than
        physics int 0x3080E9E7 (0x00580440, timer 0x0046B920): += facing * 0xDC818B7E. +0x20 held longer than
        0x9B25142A: -= side * 0x1AB3809F. +0x40: += side * the same (through Mth's operator* / +=). x87 detail
        kept: in the three inlined pushes the x component's product isn't rounded to float before the add
        (computed in double). Slot map (entry ESP = R): R-0x20.. facing (x, 0, z, w), R-0x10.. side; the +0x40
        branch's product goes into the facing's slot (hidden return of 0x004024A0), its scale into the pushed
        argument slot. The handoff's "slot left from an earlier call's arguments" is that scale; nothing reads
        a stale value.
      - CheckForGround 0x00509BD0 (thiscall, void): the feeler line (+0x208 = pos + up * physics 0xE4D79235,
        +0x218 = pos + up * -200) and the feeler check 0x005003A0(0x10, 0). Hit, not skatable (+0x228 clear):
        if the distance along up < 0.001, position = hit point + hit normal; then leave. Hit, skatable: the
        facing rotated into the hit's plane and into +0x68's (Mth::RotateToPlane); leave if facing . normal > 0
        and the rotated facings' dot is in (0, cos(physics angle 0x4138283E with input +0x20 held, else
        0xEF161C2A)); if above the hit, leave if the distance > max(tan(acos(clamped dot)) * frame movement,
        physics 0x899BA3D0) (the original's FCOS / CRT acos + FPTAN on x87: ours in double - only compared).
        Kept: timed flag +0x40 = +0x229, 0x004FE860(normal by value), position = hit point, timed flag +0x98
        set when flags +0x18 == 0, distance > 2 and the dot > 0.99, 0x005196A0(+0xFA byte), +0x1514's +0x1C,
        0x004FEA50, 0x005021D0, the two trigger checks 0x00434BF0 (0x0064BBA4 / 0x0064BBB0 - with an early
        return when +0x1530's +0x26 is set) when +0x1AC != +0xFC, 0x005232A0(0) unless +0x1518's +0x13B0,
        0x0043A930, +0x1514's +0x1D. Leaving: 0x004FDED0(1), the object's 0x00460A20 / 0x004609F0 events,
        0x004FEBA0; flags +0x30 set: timed flag +0x48 on, BreakVert(false) unless both triggers - PartyMod only
        flips the first JZ (0x00509E47), so under its PS2 controls the call is skipped only when +0x120 is held
        without +0xC0 (ported as is) - then +0x48 off once input +0x20's release and press times both exceed
        script int 0x6BB5B751; flags +0x30 clear: both triggers (PartyMod's 0x0050A257: either) -> the acid
        drop search 0x00502240 and, if found, 0x00502F20; then the trigger check 0x0064BB80.
      check_partymod_overlap.py: the six patch bytes are HANDLED. PartyMod's run-time byte patches in
      src/partymod/input.c are left in place (like batch 57's): they only touch the originals, which the game
      no longer runs while ours are installed, and keep PartyMod's behaviour if ours are disabled in the ini.
      Early tests (new src/hook/selftests_skaterA.cpp): a generic recording stand-in harness - each callee's
      first bytes get a thunk to one naked entry that records the call (`this`, stack arguments with arena
      pointers made relative and stack pointers marked, data behind input pointers), fills output buffers,
      returns seeded values (int / bool / float in ST0) and pops what the real callee pops, keeping every
      register but EAX; our own BreakVert is stood in through its code address too. The original's
      RotateToPlane is pointed at ours (not bit-identical, tested separately). LipPushOff: 3000 variants
      (facing flat / zero, buttons, timers) x PS2 off / on; CheckForGround: 6000 variants (hit or not,
      skatable, angles, distances, flags, triggers, a stand-in symbol table for 0x6BB5B751) x PS2 off / on
      (at least 300 must land). The whole arena and the call trace compared. 54 deliberate bugs: 50 caught,
      4 equivalent at float precision (the order of the distance's double sum, sqrt in float vs double,
      `<` vs `<=` against an irrational cosine, the clamp at -1 - tan(acos(-1)) is ~0 like tan(acos(1))).
