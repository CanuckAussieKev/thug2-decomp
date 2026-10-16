# skaterB (PartyMod round, skater controls part 2 + the chat keyboard task)

- [x] Batch (PartyMod round; NOT run in the game yet): two skater air functions with PartyMod's PS2 controls
      built in (new src/game/skater/rotation.*, `Skater::CorePhysicsComponent` - the component the air update
      0x50CA00 runs on; fields by offset: object +0xC (matrix +0x6C, velocity +0xAC), display matrix +0x18,
      input +0x150C (buttons of 0x20 bytes from +0x20: {down, time, ..., +0x10 pressed}), trick +0x1518
      (rotation total +0x13A4), +0x1520 ([+0x18] shows the total), state flags +0x1528 ({bool, time} pairs,
      cleared with time = 0x44ECE0() + [0x696D60])) and the network keyboard task with PartyMod's F1:
  - Spin 0x504060 (thiscall, void): tap mode (+0x2DA: L1/R1 taps queue +-pi in +0x30C, turned at the stat
    0x580790(0xF7A2ACC1) per frame length +0x8C); else L1/R1 held past the spin delay (0x580400(0xA5A02449);
    held time read unsigned) turn at the stat 0x4957DB43 - PartyMod's 2-byte NOPs at 0x504239 / 0x50430B drop
    the delay check (the delay is still read); then (flag +0xC0 clear) the second pair (input +0x60/+0x80, held
    time signed) ramped between 0xA092B2BE and 0x5D756349; with no turn, face the ramp (flags +0x30 and +0x68:
    acos(clamp(forward.y)) vs 0x61224F2E degrees, 0x406630's offset angle, Sign * 0x9DB33213, the last bit
    exactly); the turn: +0x3A5 = turn > 0, RotateYLocal on both matrices, total += degrees, shown truncated
    (0x564BE0) unless flag +0x30 and not +0x78 and |total| < 360 - 0x50C5CC2F + 0.1. Script floats come back
    unrounded on x87: GameFunction<double(...)> keeps that.
  - BreakVert 0x507120 (thiscall, 1 bool arg `force` - all callers pass 0, RET 4, void): the jump button held
    past 0x580440(0x9D2F8CC8) with none of 4 others, or both triggers (input +0xC0 / +0x120; PartyMod's JNZs at
    0x507184 / 0x5071AE make it either), or +0x2D0, or force. Triggers or +0x2D0: if 0x505F70 (thiscall bool)
    only copy the matrix; else velocity x/z -= wall normal (+0x58/+0x60) * 24, RotateXLocal(0xD757F4BB deg),
    flags 30/38/48 cleared, 80 set (if not exactly 1), face along velocity (no display copy). Otherwise:
    velocity x/z -= normal * |v| (length squared stored as float first - the original stores it in the
    argument's slot) * 0x13F33B41, y *= 0x848C5CD6, the same pitch, flags 30/38/48/B0 cleared, face along
    velocity, display matrix = object's.
  - GameNet::KeyboardTask 0x5334D0 (cdecl task callback, in chat.cpp): Enter (0x19) - unless ObjectExists
    (0x462340) for ids 0x31631B98 / 0xF53D1D83 / 0x3B56E746 / "quit_dialog_anchor": run 0x3B4548B8 and clear
    the typed keys (the parameters are leaked, as in the original); F1-F4 taunts (0xB53D0E0F = 0x0E5FD359 /
    0xBEEA3518 / 0x4525ADBD / 0xA36DBEE1, script 0x2C43B5BA) at most every 2000 ms (0x7CCE20). PartyMod's byte
    at 0x533652 (unconditional, patchInput) makes F1 0x1F, since its SDL keyboard input sends 0x1B for Escape.
  - check_partymod_overlap.py HANDLED: 0x504239, 0x50430B, 0x507184/5, 0x5071AE/F, 0x533652. PartyMod's runtime
    patches stay in src/partymod (as for batch 57: they still apply to the original when a replacement is
    disabled in thug2-decomp.ini).
  - Early tests (src/hook/selftests_skaterB.cpp): spin over 6000 and break vert over 4000 random cases (every
    branch input: tap mode, buttons with held times around the script times incl. negative, flags, queued
    turns, tilts, totals, velocities incl. zero, frame lengths incl. 0) x PS2 controls off/on - the original
    with PartyMod's bytes written in for "on" - with the callees (clock 0x44ECE0, 0x580400/0x580440/0x580790,
    0x564BE0, 0x406630, 0x505F70) redirected to recording stand-ins; break vert's original also gets our math
    helpers (CreateRotateXMatrix, D3DXMatrixMultiply 0x5BFA8C - stdcall, Normalize, OrthoNormalizeAbout)
    since those differ from the x87 originals in the last bit (spin compares exactly without that). The
    keyboard task: no key, Enter x each dialog / none, F1 (0x1F), F2-F4, 0x1B, another key, at 2000 / 2001 ms,
    on a stand-in memory manager (arenas compared), the original with PartyMod's byte.
  - 52 deliberate bugs: 49 caught (5 after strengthening the tests: held times exactly at the jump time and
    the ramp start - a stand-in clock that stops - a ramp end before its start, a queued turn just over one
    frame's step, a tilt limit just above the angle, a total within the snap's 0.1), 3 equivalent (float vs
    double rounding of queued -+ pi: exact in double for these magnitudes; `queued <= 0` inside `queued != 0`;
    `speed >= 0` once speed is known non-zero).
  - Checks: build clean, thug2_check 0 MISMATCHED and all early tests pass; register scan only the known
    0x4EFF92 false alarm; call audit no MISMATCH; check_partymod_overlap "No overlaps", all handled;
    find_gaps nothing new next to these (its 1 thunk, 0x4B2350, was there before).
  - Shared files touched: CMakeLists.txt (src/hook/selftests_skaterB.cpp after selftest_runner.cpp;
    src/game/skater/rotation.cpp after src/game/obj/smtptr.cpp), tools/check_partymod_overlap.py (HANDLED,
    after the 0x527636 line), src/game/net/chat.cpp/.h (KeyboardTask). src/partymod unchanged.
