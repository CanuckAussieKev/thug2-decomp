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

## Tiny functions are usually not stubs
`tools/ghidra/find_stubs.py` lists every game function of at most two instructions (~300). Most are real code
belonging to a class - getters (`MOV EAX,[ECX+n]`), flag setters, constant returns, and `MOV [ECX],vtable; RET`
(the destructor body of a class with nothing else to destroy). Write those with their class, not as generic
stubs. Only the truly empty ones (bare `RET`/`RET n`, a constant return ignoring its arguments) go in
`src/game/core/stubs.cpp`. They're linker-folded: one address stands for many source functions.
Don't trust a comment saying a call is "a no-op" without looking: CStruct's getters on a miss call 0x5480A0
(empty - compiled-out debug output, 528 callers). Bytes after a 1-byte function aren't necessarily padding:
0x5480A1-0x5480BE is the PC port's code cave (loads its DLL), written into THUG2PM.exe's padding.
When a function you rewrite calls 0x5480A0 (or another empty function), keep the call in the source under
a meaningful empty inline function (Mem::PushMemProfile/PopMemProfile, Script::PrintContents,
Mem::DebugTrackBlock, ...) with its arguments - the strings it's passed document the code.

## A test that passes at once can be testing nothing
Check that the code under test actually ran its main path (count the calls it makes, or break it on purpose
first). Batch 53's test passed while every call returned at the first line: stand-in data written for a
third port spilled over the "quit requested" flag. `tools/mutate.py` prints the failing test's name on each
CAUGHT line - make sure it's the test you wrote. Writing a jump over a game function's first bytes for the
length of a test (TestDetour in selftest.cpp) is how to script platform calls the original makes directly.
