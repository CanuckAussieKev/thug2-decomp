# Test and harness pitfalls

Things that are true of this repo's testing and not obvious from the code; each cost real time.

- **Offline, thug2_check doesn't install the hooks.** Calling a game address runs the original there. To
  test a naked stub, call the stub function itself.
- **The game's own C runtime isn't started offline (nor in DllMain).** Originals reaching _stricmp, sprintf
  etc. crash at address 0 - e.g. AddChecksumName on a checksum it already has. Avoid that path offline or
  use a Late test (SELFTEST(Late, ...), runs in the game only).
- **Crash at 0x448386**: the game's pool-usage dump after an allocation failed - the stand-in heap is too
  small; use a bigger arena for that test.
- **Crash at 0xC0000142 at launch / log stops at "thug2-decomp loaded"**: an Early test crashed inside
  DllMain in the game. Never hold game resources (files) in both runs at once.
- **Uninitialised fields** (CSymbolTableEntry::unknown2...) hold halves of stale arena pointers: normalise
  the high 16 bits (the template does).
- **MSVC inline asm reserves `out`, `type`, `size`** (MASM words) - rename locals used in __asm.
- **Hangs**: the original sometimes never moves past a token (hex integer as a parameter, unknown tokens in
  arrays/expressions/.qb top level) - test data must avoid them. tools/mutate.py handles hanging mutations;
  running thug2_check with captured pipes hangs forever (its child keeps the pipe).
- **A test that disables a path proves nothing about it**: nulling a guarding global (the script cache
  0x6F3558) hid a wrong calling convention that the game then hit. Use stand-in objects.
- **Both builds are Debug** (tools/verify_build.cmd and the user's out/build/x86-debug), so the debug CRT and
  run-time checks are on; Debug builds log "DEBUG CRT REPORT" / "RUN-TIME CHECK FAILURE" with a call stack
  to thug2-decomp.log. When the user reports dialogs, read the log first.
- **Floating point**: originals run x87 at 53-bit precision in tests; operation order changes results
  (dot products), so follow the instruction order and compute in double.
- **Equal-looking data hides bugs**: mutations tend to survive with symmetric inputs (equal lengths, positive
  weights, a single use of stored state, names that don't collide). Make inputs asymmetric.
