---
name: decomp-batch
description: Decompile the next batch of THUG2PM.exe functions into this repo - survey, read the disassembly, rewrite in C++, prove it matches the original with offline self-tests and deliberate-bug mutations, check registers and calls, record and commit. Use whenever continuing the decompilation ("continue", "next batch", a range or a subsystem).
---

# One decompilation batch

The goal is readable C++ that behaves like the original (not byte-identical code). A batch is a coherent group
of functions (a class, a subsystem slice), typically 10-40. Work autonomously; commit each batch; the user
tests in the game later (say "not yet tested in-game"). Never launch the game while the user may be playing
(check no THUG2PM.exe runs); never copy DLLs into C:\Games\THUG2\Game (the main install) - the user's Visual Studio build does that.

Paths: repo C:\Dev\thug2-decomp; game C:\Games\THUG2\Game\THUG2.exe (the user's main install, a stock no-CD exe; the
reference THUG2PM.exe - the same addresses - is in C:\Games\THUG2_PM_BAK and the workspaces) (base 0x400000, VC 7.1 with whole-program
optimisation: custom register conventions happen); Ghidra snapshot C:\Games\THUG2-ghidra-snapshot (refresh
with tools\ghidra\snapshot_project.ps1). `inspect` below means:

    GHIDRA_INSTALL_DIR='C:\ghidra' uv run --no-project --python 3.13 --with pyghidra python tools/ghidra/inspect_range.py C:\Games\THUG2-ghidra-snapshot

## 1. Choose and survey
- docs/decomp-progress.md ends with "- [ ] Next: ..." - start there.
- `inspect survey <from> <to>`: sizes, callers, REPLACED, strings. Prefer whole clusters (a function and the
  helpers only it uses); leave a call to an un-rewritten function as `GameFunction<...>(address)`.
- Skip nothing: the game will eventually be built from this source. Functions shorter than 5 bytes (can't
  take the hook's jump) or that shouldn't be hooked get `REPLACE_SOURCE_ONLY` (still compiled, counted and
  argument-checked; test them by calling ours directly against `Original<>`). Shared stubs (e.g. one
  `return true` used by many script commands) are written once and registered at their one address.
  Tiny functions (`tools/ghidra/find_stubs.py` lists them) are mostly real class code - getters, setters,
  `MOV [ECX],vtable; RET` destructor bodies: write them with their class; only truly empty ones go in
  src/game/core/stubs.cpp.

## 2. Read the original - the disassembly, not the decompiler
- `inspect disasm <addr>...` (write long ones to a scratch file and Read it); `inspect decompile` only as a
  map. `inspect table ...` for every switch; `inspect strings ...` for messages.
- Code Ghidra never disassembled (jump-table targets outside function bodies):
  `uv run --no-project --python 3.13 --with capstone python tools/capstone_disasm.py <from> <to>`.
- For each function establish, from the instructions:
  - **convention**: arguments in ECX (`__thiscall`) or other registers (EAX/EBX/EDX/ESI/EDI...);
    `RET n` = callee pops n bytes; plain `RET` + `ADD ESP,n` at callers = cdecl. Register arguments or
    caller-popped register+stack mixes need a naked stub (see parse.cpp / load.cpp): a C++ function plus
    `__declspec(naked)` adapter registered with `REPLACE_UNCHECKED`, declared in the header so tests can
    call it.
  - **return value**: EAX/AL/AX (or ST0) at every RET - Ghidra's `void` is often wrong; constructors and
    many setters return `this`; `mov al,1` -> `bool`, `mov eax,1` -> `int`.
  - **float precision**: x87 at 53 bits; mixed int/float math: compute in double, round once (eval.cpp).
    Copy constants as exact bit patterns from the exe (or read them with GameVariable<float>(addr)).
  - **every branch and table case**, including the odd ones (error strings are the game's own: use their
    addresses). Faithfully keep original quirks (infinite loops on bad data, reading freed memory) and say
    so in a comment.
  - **calls into game code we don't replace**: check THAT function's convention too (object in ECX? RET n?).

## 3. Write
- Files under src/game/<subsystem>/; match the surrounding style (comments say what, not how; short names
  of real concepts). Layout structs with `static_assert(offsetof...)`. Globals via `GameVariable<T>(addr)`.
- Calls to the empty function 0x5480A0 (compiled-out debug code) stay in the source, with their arguments,
  as named empty inline functions: `Mem::PushMemProfile(name)` / `PopMemProfile()` around allocations,
  `Script::PrintContents(struct)`, `Mem::DebugTrackBlock(block)` - add a new one when a call fits none.
  The strings they're passed document the code.
- Functions PartyMod changed have source with PartyMod's behaviour (it is part of the game's source
  now). Where PartyMod replaced or changed the code, keep the original's logic visible too - in a comment, or
  as code if it's worth keeping - so the source shows what the game did before PartyMod (the user wants this).
- Register: `REPLACE(addr, Function)` / `&Class::Method` / adapters for members of other types; pure
  functions may use `REPLACE_SHADOW`; unhookable ones `REPLACE_SOURCE_ONLY`. New .cpp files go in
  CMakeLists.txt.

## 4. Build and check
- `cmd //c "C:\Dev\thug2-decomp\tools\verify_build.cmd"` - no errors, no warnings (warnings fail the user's
  build).
- `out\build\verify\thug2_check.exe` (use `timeout 120`): "0 MISMATCHED" argument sizes, all early
  self-tests pass. New "not checked" lines must be only your naked stubs or tail-jump functions.

## 5. Test (this is where bugs are found)
- Write an Early self-test in src/hook/selftest.cpp from [test-template.cpp](test-template.cpp): original
  vs ours on identical stand-in state, trace every result (return values too), compare the whole arena.
  Cover every case/branch you read in step 2; drive register stubs through inline asm.
- **Mutations**: write a spec (MUTS = [(file, name, old, new), ...]; TESTS = "YourTest,YourOtherTest" - only the
  batch's own self-tests run, a second instead of ~25 s; BUILD = a .cmd that only runs `cmake --build` after
  VsDevCmd - no re-configure: a mutation then costs ~3-4 s) of 10-40 deliberate bugs - one per
  branch/constant/order/convention - and run `python -I tools/mutate.py spec.py`. Every non-equivalent
  mutation must be CAUGHT; for each miss, add the test case that exposes it (a miss is a hole in the test,
  not luck). Note truly equivalent ones in the record.
- Tests never touch the user's real state: files go in a scratch temp folder (SetCurrentDirectory there,
  restore after; never the game's Save\), registry writes go under a stand-in HKEY_CURRENT_USER
  (RegOverridePredefKey onto a scratch key, skip the test if that fails, delete through the scratch key's
  own handle - see TestLeftovers). Originals that use the game's C runtime (sprintf, _stricmp...) only run
  in a Late test (in the game); offline, check ours against an independent expectation.
- Calls the original makes directly (platform, renderer, C runtime) are scripted by writing a jump over the
  callee for the test (CodePatch / JumpTo in selftests_pc.cpp); where PartyMod changed a function, its patch
  bytes are written into the original the same way, so ours is compared with the original *as PartyMod left it*.
- `tools/mutate.py` names the failing test on each CAUGHT line (check it's yours; a bare CAUGHT is a crash),
  reports mutations that don't build, and rebuilds the restored code at the end.
- Read [pitfalls.md](pitfalls.md) when a test crashes, hangs or fails strangely.

## 6. Safety checks
- `GHIDRA_INSTALL_DIR='C:\ghidra' uv run --no-project --python 3.13 --with pyghidra python tools/ghidra/scan_register_assumptions.py C:\Games\THUG2-ghidra-snapshot`:
  only known false alarms in the "RISK" lines (0x4EFF92 PUSH ECX reserve); every function listed as
  "N site(s) read EAX/AL" must return a value.
- `... python tools/ghidra/audit_game_function_calls.py C:\Games\THUG2-ghidra-snapshot`: no MISMATCH.
- `python -I tools/check_code_writes.py`: "No overlaps" (no run-time write into the exe - GameSpy host names,
  the IsPS2 table entry - inside a rewritten function).
- `... python tools/ghidra/find_gaps.py C:\Games\THUG2-ghidra-snapshot`: nothing new skipped next to this
  batch's functions (small helpers, setters, jumps to what you rewrote - give each source, source-only if it
  can't be hooked), and "0 thunks".

## 7. Record and commit
- `python -I tools/progress_report.py`; add a "- [x] Batch N (NOT run in the game yet; ...)" entry to
  docs/decomp-progress.md before "- [ ] Next:" (what was rewritten with addresses and findings; what the
  test covers; "N deliberate bugs caught, M after strengthening"), update "Next:".
- `git add -A && git commit` "Batch N: ..." ending with the Co-Authored-By line from the system prompt.
- When the user reports "game runs": check thug2-decomp.log (replacements installed, all stages passed,
  no "RUN-TIME CHECK FAILURE"/"DEBUG CRT REPORT"), add "- [x] Batches ... tested in the game" and commit.
- Tell the user briefly what the batch covers and how to switch it off (`[replacements] disable=` file
  name in thug2-decomp.ini) if something breaks.
