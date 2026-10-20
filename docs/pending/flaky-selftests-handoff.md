# Handoff: flaky early self-tests (branch worktree-agent-a024a86de15fee1b4)

Stopped at the lead's request (account switch) during the final verification. The full record is
docs/pending/flaky-selftests.md.

## Root causes found
1. **Arena relocation by the running run's arena only.** Tests that run the original and ours in a pair of arenas
   turned only the *current* run's arena addresses into offsets. A plain value that happens to lie inside one of the
   two arenas (packed colour 0x04030605, 0x04061400, 0x00FF0006, 0x01000006, flags 0x01000000, 0x01A53A61) then
   compared differently. This was every front-end flake the lead saw (screen element: parameters / properties,
   text elements, text blocks, anim controllers: partial, texture / sprite / window elements). Proof: in 300 runs
   with the arena address logged, all 10 failures had the arenas over one of those constants. The same latent bug
   was in ~25 other test files, and the decomp-batch test template taught it.
2. **The watchdog thread ran into a test's Sleep stand-in.** StandInApi patches kernel32!Sleep itself, for the whole
   process. The watchdog's Sleep(1000) then recorded into g_trace from a second thread: the debug CRT reported
   "vector insert iterator out-of-range" (exit 3) in TestStreamPlay. With a 1 ms watchdog period this reproduced
   10 of 10 times.
3. **"PC async handle (synchronous I/O)" compared a Windows handle by value.** The read step's result is the file
   handle from the open. Its value (844 against 848) differs whenever something else takes a handle in between.

## Fixes made (commits)
- af713c3: fe_testkit.h maps either arena (ArenaWord / ArenaWordOrHigh from selftest.h).
- 39765a6: every other per-run pair does the same:
  - selftest.cpp (15 sites);
  - cobj, coim, el3d, obj, tod 1-4, qb;
  - csA, csC, evt, scache, trk, skaterB, now using ArenaPair storage;
  - netF, objsys3/4/6: both arenas allocated together; new two-arena overloads ArenaWord / ArenaWordOrHigh /
    ArenaOf in selftest.h.
- f39ea09:
  - the watchdog waits with ntdll NtDelayExecution (selftest_runner.cpp);
  - the PC async test notes "the file's handle" (the handle at +0x88) instead of its value.
- 06af815: the decomp-batch test-template.cpp and pitfalls.md are updated; pending record flaky-selftests.md.
- c257b90: main merged into the branch (level editor phase 2; its new selftests_editor_nodes.cpp has no arenas).
- The last commit (this file): pending-record rates and this handoff.

Mutations, all caught (tools/mutate.ts; the specs were in the scratchpad):
- SetParent linking the parent;
- ResolveRGBA masking 0x7F;
- Region end one word short;
- a script-cache entry's stale self-pointer;
- the tracker log value;
- the PC async open result not being the handle;
- a synchronous read clearing the result.

## Failure rates so far
| Stage | Runs | Failures |
| --- | --- | --- |
| Before: front-end tests, filtered | 300 | 10 |
| Before: full runs on THUG2PM.exe, only fix 1 applied | 50 | 3 (1 watchdog CRT assert, 2 PC async) |
| After: front-end tests, filtered | 300 | 0 |
| After: merged build c257b90, full runs on THUG2PM.exe | 14 | 0 |
| After: merged build c257b90, full runs on the stock THUG2.exe | 14 | 0 |

The final 50 / 20 full-run verification was stopped while run 15 of each was in progress.

## What's left (next steps)
1. Build (own build folder and PDB endpoint): in a VsDevCmd x86 shell, run `set _MSPDBSRV_ENDPOINT_=<name>`, then
   `cmake --build out\build\verify -j 2` (or tools\verify_build.cmd).
2. Run the full verification again: 50 consecutive runs of `out\build\verify\thug2_check.exe
   C:\Games\THUG2_PM_BAK\THUG2PM.exe` and 20 of `... C:\Games\THUG2\Game\THUG2.exe`; every run must exit 0. A loop
   script that stores only the failing runs' output helps.
3. If anything still fails, read its "entry N: original X ours Y" / "step" lines. A value that looks like an
   address near the arenas points to a single-arena or heap-vector relocation that was not converted, for example
   selftest.cpp's TestScriptChecksumNames nameBlock (a per-run std::vector mapped with 0xB0000000).
4. Put the final rates into flaky-selftests.md, then the lead merges.

## Files touched
- src/hook: fe_testkit.h, selftest.h, selftest.cpp, selftest_runner.cpp, and selftests_{cobj, coim, csA, csC, el3d,
  evt, netF, obj, objsys3, objsys4, objsys6, qb, scache, skaterB, tod, tod2, tod3, tod4, trk}.cpp
- .claude/skills/decomp-batch/test-template.cpp and pitfalls.md
- docs/pending/flaky-selftests.md and this file

## Commands to check
- One full run: `out\build\verify\thug2_check.exe C:\Games\THUG2_PM_BAK\THUG2PM.exe`. It should end with "early
  self-tests: all passed", exit 0.
- Only the front-end tests, many times: `set THUG2_SELFTESTS=TestAnimControllers,TestScreenElement,TestMenus,TestScrollingMenus,TestSpriteElements,TestEngineGlue,TestTextElements,TestTextBlocks`
- The watchdog-race tests: `set THUG2_SELFTESTS=TestStreamPlatform,TestStreamPlay,TestMainLoop`
- The PC async test: `set THUG2_SELFTESTS=TestPcAsyncFileHandle`
