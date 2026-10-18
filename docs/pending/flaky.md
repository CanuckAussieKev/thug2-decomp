# flaky: three early self-tests failing now and then in the game

Three early self-tests passed every time offline (thug2_check) but failed in some game runs:

1. "SkateScript nodes: prefix tables, prefixed nodes, the nearest by prefix" - entries 51, 59, 62, 177:
   original A001D122 / A003D122, ours 01DD6B72.
2. "script loading: .qb files, redefinitions, restarts" - entry 5924: original A0051EA9, ours 7A0F10E9;
   entry 23237: original A00575CF, ours 7A0F680F.
3. "script interpreter: Update and every statement token" - entry 4672: original A0000001, ours 02000001.

## Cause: the tests, not our code

All three were the same bug in how the tests compare. Each test runs the original and ours in its own
arena (`uint8_t arenas[2][size]`, in the DLL's .bss) and makes the results relative to the arena before
comparing: a word in [the run's own arena] became 0xA0000000 + offset. Only the run's own arena was used, so a
value that is **the same in both runs but isn't a pointer** (a checksum, flags, a word in the DLL) got mapped
in the original's run when it happened to fall inside the original's arena, and stayed raw in ours. The DLL
loads at a different address each game run (ASLR), and the arenas move with it, so this hit only some runs.
Offline the DLL sits elsewhere, so it never did.

The logged numbers prove it:
- 1: 0x01DD6B72 is CRC("Ped_") - the checksum AddNodeArrayPrefixes gives AddChecksumName for "Ped_" (recorded
  four times). A001D122 and A003D122 put the original's arena at 0x01DB9A50 and 0x01D99A50 in those two runs:
  CRC("Ped_") was 0x1D122 / 0x3D122 into it. In the passing run the DLL was elsewhere.
- 2: both entries give the same arena start: 0x7A0F10E9 - 0x51EA9 = 0x7A0F680F - 0x575CF = 0x7A0BF240. Two
  values the same in both runs, inside the original's arena that time.
- 3: the arena image's "top half of a stale pointer" mapping (for uninitialised 16-bit fields) caught the flags
  word 0x02000001 when the original's arena started at 0x0200xxxx.

ParseQB's NoteQbName (KeepQbNames) doesn't touch the arena (it uses the CRT heap); it played no part. The
earlier change giving every node a pos= is still right (the original reads an unset stack value for the first
node without one - undefined) but wasn't the cause of failure 1; its comment no longer claims it was.

## Fix

`src/hook/selftest.h`: `ArenaWord(arenas, size, word)` and `ArenaWordOrHigh(...)` - pure functions of the word
and the arena pair, so a value that is the same in both runs always maps the same way: a word in either arena
becomes 0xA0000000 + its offset in that arena (pointers into a run's own arena map as before). The top-half
mapping needs the pair aligned to 64K so that no 64K block straddles the two arenas (otherwise a stale top half
from ours could land inside the original's arena as a whole word - seen offline while making this), so
`ArenaPair<Size>` gives 64K-aligned storage. Used by the three tests:
- selftests_nodes.cpp: `Rel` uses ArenaWord.
- selftest.cpp TestScriptLoading and selftests_csB.cpp TestScriptInterpreter: `relocate` uses ArenaWord, the
  arena image ArenaWordOrHigh, arenas from ArenaPair.

**Other tests have the same weakness** (about 45 other `0xA0000000u + (word - base)` sites in src/hook:
selftest.cpp, selftests_cobj/coim/csA/csC/el3d/evt/gapsA/lfxc/netB/netF/obj/parkedA/pcmplay/qb/scache/
skaterA/skaterB/trk). They can fail the same way when a constant they record lands in the original's arena;
switch them to ArenaWord / ArenaPair if one ever flakes (or as a sweep).

## Proof

- thug2_check: 0 MISMATCHED, early self-tests all passed (202).
- Mutations (tools/mutate.py, spec in the session scratchpad):
  9 of 9 caught. Nodes test: the "Ped_"-style name given AddChecksumName ('_' -> '-'), PrefixNodes::Add's
  grown size (+8: every later bump pointer moves), the digits prefix's index (+1), GetNearestNodeByPrefix's
  `<` -> `<=`. Loading test: a script block's size word (+1), CreateScriptSymbol's file checksum (0), the block's
  allocation (+4 bytes: pointers move). Interpreter test: ExecuteBreak's depth check (crash in the test, counted
  as a failure), ReadName's 0x2C parameter name (0 -> 1).
- In the game: NOT run with this build yet. The lead stopped game runs (the user was playing). The four runs
  made before then didn't use this build: the DLL copy failed because a game was already running, so they
  prove nothing. Still to do: 4 consecutive runs with all early, late and engine-started self-tests passing.
