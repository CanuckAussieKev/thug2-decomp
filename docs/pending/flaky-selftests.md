# Flaky early self-tests (agent "flaky-selftests")

- [x] Fix FS-1: the early self-tests that failed now and then in `thug2_check.exe` (and in the game's early stage).
  Test-harness changes only - no game code changed (no rewritten function had a bug behind these). Not a game run:
  offline (`thug2_check.exe`, THUG2PM.exe and the stock THUG2.exe).
  Three root causes, all in the tests:

  1. **Arena relocation by the running run's arena only** (the six front-end tests the lead saw: "screen element:
     parameters", "screen element: properties, morph, member functions", "text elements: updates", "text blocks:
     construction, fonts, lines", "anim controllers: partial", "texture / sprite / window elements: updates").
     The original and ours each run in their own arena of a pair (fe_testkit.h: one VirtualAlloc of 2 x 0x40000);
     traces and arena images turn pointers into offsets. Only the current run's arena was mapped, so a *value* that
     merely lies inside one of the two arenas - the packed colour 0x04030605 (ResolveRGBA with six bytes, the
     shifts wrapping), 0x04061400, 0x00FF0006, 0x01000006, flags 0x01000000, a font word 0x01A53A61 - became an
     offset in one run and stayed itself in the other. VirtualAlloc put the arenas anywhere between ~0x00F90000
     and ~0x04080000 offline, so it hit about one run in thirty, more often as the program grows. (Selftest.h
     already had the fix, ArenaWord / ArenaWordOrHigh, from the nodes / script-loading tests' failures in the game -
     01d6701 - but only a few tests used it, and the decomp-batch test template still taught the per-run form.)
     Proof: 300 filtered runs of the front-end tests with the arena address logged - 10 failures, every one with the
     arenas over one of those constants; after the fix 0 of 300, 19 of them with the arenas over a constant.
     - fe_testkit.h (all the front-end tests): Rel / InArena / Snapshot map either arena (ArenaWord,
       ArenaWordOrHigh).
     - The same latent bug, fixed the same way, in every other pair that mapped one arena: selftest.cpp (allocator
       layer, memory manager, heap creation's 2 x 12 MB pair, struct writing / adding, arrays, symbols, expressions,
       checksum names, tokens, symbol bookkeeping, parser, memory cards), selftests_cobj, coim, el3d, obj, tod,
       tod2, tod3, tod4, qb (UnloadQB); csA, csC, evt, scache, trk, skaterB (now ArenaPair storage: 64K-aligned,
       so the half-pointer pass sees whole blocks of either arena; skaterB's keyboard arenas grew 0x8000 ->
       0x10000); netF, objsys3, objsys4, objsys6 (both VirtualAlloc'd arenas made at once and mapped - new
       two-arena overloads ArenaWord(first, second, size, word) / ArenaWordOrHigh / ArenaOf in selftest.h). The
       static pairs were fixed per build offline (thug2_check is linked at a fixed 0x20000000) but moved with
       every rebuild and with ASLR in the game. Single-arena tests (the same arena for both runs: anim*, appmain*,
       gapsA, lfxc*, netA-D, objsys2/5, pcmplay, skaterA...) and viewer_testkit / nodes / csB (already fixed) were
       left as they were.
     - decomp-batch skill: test-template.cpp uses ArenaPair + ArenaWord / ArenaWordOrHigh; pitfalls.md says why.
  2. **The watchdog thread ran into a test's Sleep stand-in** (newly found: "DEBUG CRT REPORT ... vector insert
     iterator out-of-range", exit 3, in TestStreamPlay; 1 of 44 full runs). StandInApi (standins.h) patches the
     Windows function itself, process-wide; the stream thread's and main loop's tests stand in kernel32's Sleep.
     The 45 s watchdog (selftest_runner.cpp) called Sleep(1000): when it woke during such a test its next Sleep
     went into the stand-in - recording into g_trace from a second thread (the corrupted vector), counting down the
     test's scripted sleeps, and returning at once, so it spun until the test ended. It now waits with ntdll's
     NtDelayExecution (looked up once; SleepEx as a fallback). Proof: with the watchdog's period cut to 1 ms the old
     wait failed 10 of 10 runs of TestStreamPlatform / TestStreamPlay / TestMainLoop, the new one 0 of 20, and 3 full
     runs passed.
  3. **"PC async handle (synchronous I/O)" compared a Windows handle by value** (newly found; 2 of 50 full runs:
     "pc async step 2: original 'read 0: result 844 ...' ours '... result 848 ...'"). After the open the handle's
     result field is the Windows file handle and the synchronous read leaves it there; the open and size steps
     only noted whether it was set, the read step its value - which differs between the two runs whenever anything
     else in the process takes a handle in between. All steps now note "the file's handle" when the result is
     the handle at +0x88 (stronger for open / size than "set or not"), the number otherwise.
  - Mutations (tools/mutate.ts, all caught): screen element SetParent linking the parent instead of the last child;
    ResolveRGBA masking 0x7F; Region's end one word short (end pointers map to offset 0 in either run now); a
    removed script-cache entry keeping a stale self-pointer (only the arena image shows it); the tracker's
    OBJECT_ADDED log entry's first value; the PC async open's result not the handle; a synchronous read clearing
    the result.
  - Rates: before - the front-end tests 10 of 300 filtered runs (~3 % of full runs); full runs with only cause 1
    fixed: 3 of 50 (1 watchdog CRT assert, 2 PC async handle). After all three (merged with origin/main 3890d8f): 50
    of 50 consecutive full runs on THUG2PM.exe and 20 of 20 on the stock THUG2.exe passed (exit 0); the front-end
    tests 0 of 300 filtered runs. (TestScriptChecksumNames' name block, also checked: one block shared by both
    runs, the same address - nothing to convert.)
  - Commits: af713c3 (fe_testkit), 39765a6 (every other arena pair), f39ea09 (watchdog, PC async handle), 06af815 (the
    template, pitfalls, this record) and the final rates.
