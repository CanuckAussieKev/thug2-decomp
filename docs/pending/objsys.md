# objsys: 0x45EFD0-0x47DC40 ("Object system & script runtime")

Agent "objsys". Branch worktree-agent-ac4285dede37d3675, game workspace C:\Games\THUG2-4.

## The range

685 Ghidra functions, 467 rewritten before this agent; 218 left (~48 KB) in clusters: the cutscene system
(0x463AA0-0x467707), the cutscene-object / path manager code 0x467710-0x46AAA1, the proximity nodes and the control
pad 0x46AAC0-0x46C26C, the preferences class 0x46C270-0x46C6A0, the structure buffer writer / reader 0x479840-
0x47A8D1 and the sound effects manager 0x47AC10-0x47DC31.

Shared files touched (small, additive): CMakeLists.txt (new sources and selftests_objsys*.cpp); tools/mutate.py
(a hanging check is killed by its process id, not by name - other checkouts' checks may be running).

## Batches

- [x] Batch OS-1 (NOT run in the game yet; the controller pad): src/game/obj/control_pad.{h,cpp} (new).
  - The pad as the skater reads it (THPS's CSkaterPad / CSkaterButton; here Obj::CControlPad / CControlButton): 16
    buttons of 0x20 bytes (UP, DOWN, LEFT, RIGHT, L1, L2, L3, R1, R2, R3, CIRCLE, SQUARE, TRIANGLE, X, START,
    SELECT at +0x000-0x1E0), the raw sticks +0x200, the scaled sticks +0x210, each stick as angle + length
    +0x220, two timed holds +0x230.
  - Buttons: 0x0046B7E0 ClearTrigger, 0x0046B7F0 ClearRelease, 0x0046B800 SetName (the checksum), 0x0046B820
    Update(pressure) (down above 0x40; the press / release times are the game clock + its offset 0x696D60; while
    +0x1C (a Tmr::GetTime time as a float, 0 = none) is set, a press only counts once that time has passed - any
    other update clears it; compared unsigned, NaN holds), 0x0046B8B0 SetPressed(byte) (the byte itself is stored;
    a release here isn't timed), 0x0046B900 / 0x0046B920 the time since the release / press.
  - Pad: 0x0046B940 Init (the names; the holds cleared; returns this), 0x0046BB50 Reset (every button released, in
    the original's order; sticks zeroed), 0x0046BC60 Clear (Reset, then every button's events taken - the
    original's slip kept: R2's press is taken twice and its release kept, R3's release twice and its press kept),
    0x0046BDA0 Update(data) (PS2 pad layout: pressures 4-0xF as full 0xFF / 0, L3 / R3 / SELECT from bits 0x200 /
    0x400 / 0x100 of +0x1C; START isn't read; the scaled sticks: data 0-3 - 0x80 moved 50 towards 0 (crossing it:
    no dead band) / 77, -0x80 counts as -0x7F, exactly centred = 0; the raw sticks data 0x12-0x15 - 0x80; the
    angles atan2(x, -y) through the FPU's FPATAN; the second stick under 0.001 long stands in the d-pad's angle at
    length 127; the two holds end once the time has passed or the second stick is centred / leaves the up / down
    quarter), 0x0046C270 GetButton(name) (in the original's search order).
  - 0x0046BA50 DPadDirection and 0x0046BA80 DPadAngle (the d-pad's 16 combinations; the angle table is the
    original's static 0x6F2270, filled on first use - a constant table here), 0x0046C370 StickLength(x, y, angle)
    (__stdcall: x / 127 or 128 by sign, the length times |cos| of the angle brought into the quarter around up /
    down / left / right (FCOS) times 1/0.85, at most 1).
  - Test (selftests_objsys1.cpp): TestControlPad - 40 random pads (buttons pressed / not / holding a 2, locks
    none / 1500 / 1200.5 / NaN / 3e9; pad holds 0 / 1200 / 1500 / past 2^31) driven through 24 random calls each
    (updates from random data - centred sticks, the d-pad standing in, stick pointed up / down -, pressures around
    0x40, SetPressed with 0 / 1 / 2 / 0x55 / 0x100, the times, names, lookups with duplicate names, Reset, Clear),
    the clocks stood in (scripted, unsigned values past 2^31, exactly the lock time), the whole pad compared after
    every call; directed updates at exactly the holds' time with the stick up / down. TestControlPadMath: every
    d-pad combination, StickLength over each quarter / boundary / NaN / both signs, the clamp just over 1.
  - Mutations: 36 deliberate bugs; 32 caught (29 at first; the holds at exactly the time, duplicate names and the
    clamp were added for 3). The other 4 are equivalent: `value >= 0` for `> 0` in the scaling (0 returns
    earlier), `x >= 0` for `x > 0` in StickLength (0 / 127 = 0 / 128), `a >= -pi/4` for `a > -pi/4` (cos is even:
    the same length either way).

- [ ] Next: OS-2 (written, not built yet: src/game/obj/proxim.{h,cpp}, src/game/script/preferences.{h,cpp},
  src/hook/selftests_objsys2.cpp - add them to CMakeLists.txt, build, test, mutate), then OS-3 the structure
  buffer (src/game/script/struct_buffer.{h,cpp} written; needs its test on a stand-in memory manager), then the
  sound effects manager 0x47AC10-0x47DC31, 0x467710-0x46AAA1, the cutscene system 0x463AA0-0x467707.
