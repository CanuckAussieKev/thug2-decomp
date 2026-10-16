# CScript group C - waits, the call stack, interrupts, events (branch wt/csC)

NOT run in the game yet.

`src/game/script/cscript_wait.cpp`, 22 functions (all of group C in docs/pending/cscript-plan.md); test
`src/hook/selftests_csC.cpp` (Early, `TestCScriptWait`).

| addr | function | notes |
|---|---|---|
| 0x00473050 | CScript::UpdateWait | jump table checked (`473170:B:473188:B:1`); see the semantics below |
| 0x004728F0 | StopWaiting | |
| 0x00472900 | IsBlocked(print) | `print` -> Dbg::Printf(0x00648410 "m_wait_type == %i\n"); EAX = 0/1 |
| 0x00472BD0 / 0x00472BF0 | Wait / WaitOnePerFrame | |
| 0x00472C10 | WaitTime(ms) | `(float)((double)(unsigned)ms * 0.001f)` (0x3A83126F at 0x006461D8), started = false |
| 0x00473310 | WaitRealTime(ms) | type first, then the clock: arg = 0x0044ECE0() + *0x00696D60; timer = (float)(unsigned)ms |
| 0x00472FD0 | Block(eventType) | 0: no tracker registration, no flag |
| 0x004739A0 | SetWaitOnComponent | component +0x08 \|= 2, no null check |
| 0x00472D20 | ComponentDestroyed | registered through a naked stub `Script::ComponentDestroyedStub` (REPLACE_UNCHECKED) that keeps ECX - see below |
| 0x00472E70 | ComponentDestroyedAllScripts | cdecl; head to tail |
| 0x00473800 | ReceiveEvent | always true |
| 0x00472F40 | UnblockInterruptedScript | |
| 0x004748E0 | CallScript | |
| 0x00473E70 | ReturnFromScript | |
| 0x00475750 | Interrupt | |
| 0x00472EA0, 0x00472EC0, 0x004734E0, 0x004735D0, 0x00473690, 0x004736C0 | the event handler methods | the table's own methods stay the game's (named in the .cpp) |

## Header changes (src/game/script/cscript.h)
- Added (group C section of the free functions): `void ComponentDestroyedStub();` - the naked entry point of
  0x00472D20. Nothing else changed; no offsets touched.

## For the merger
- Calls into other groups, to turn into direct calls: `CScript::SetScript(name, code, params, object)` 0x00474540
  (group A) in CallScript; `CScript::Update()` 0x004750B0 (group B) in Interrupt.
- ECX contract: the only caller of 0x00472D20 is 0x00472E70 (Ghidra xrefs), which reads ECX after the call. Both
  are ours, so it only matters if 0x00472E70 alone is disabled in thug2-decomp.ini - and the stub keeps ECX
  anyway. Once everything is source, the stub can go (register `&CScript::ComponentDestroyed` instead). The
  register scan lists it as "caller is replaced too - safe"; the only RISK line is the known 0x004EFF92.
- Event handler table methods used (all the game's; their meanings, from their code): 0x0045F820 constructor
  (returns this); 0x0045FA60(script) tracker forgets the table's types / 0x0045FA10(script) tracker told them;
  0x004601A0(array, replace) add handler structures; 0x0045FF50 drop removed entries; 0x0045FBC0(script, type,
  scriptName, group, exception, params) add or replace one (6 arguments, RET 0x18); 0x0045FDF0(type) remove a type;
  0x004600E0(script, group) remove a group (0x8B713E0E = all); 0x0045F840(group) mark a group /
  0x0045FED0(script) remove marked; 0x00460470(event, script) pass an event; 0x0045F880 (cdecl) print. Table:
  {entries, count, +8 alive, +9 changed, +0xC in use}, entry 0x14 = {type, script, group, params, flags (bit 0
  exception, bit 1 marked)}. Tracker (0x006F0FC0): 0x00463370(type, script) listen, 0x00463450(type, script) stop.
- ResetEventHandlersFromTable's element structures are {flag, type, script, [params]}: the FIRST component's value
  is compared with `exception` (0x80367192); type and script are the 2nd and 3rd; the 4th (if any) is the
  parameters structure; the group is the function's argument. Fewer than three components: read through null
  (not checked - kept).
- SetEventHandlers' `replace` is tested as a whole dword by the original (TEST EDX,EDX; SETNZ), its one caller
  0x00461710 passes MOVZX of a byte - so `bool` is equivalent.

## Test
Original vs ours on identical stand-in scripts in an arena; every call out of the group is stood in for by
recording code (SetScript, Update, the script cache, ParseArguments, CStruct Init / Clear / AppendStructure /
AppendStructureIfAny / GetNextComponent, DeleteStruct, Manager::Allocate / PushContext / PopContext, Mem::Malloc,
the smart pointer add / remove, the clock, Dbg::Printf, the tracker and every event handler table method;
GetSlomoFrameSeconds by `FLD qword` code), then the trace and the whole arena are compared. Covers: counter
(n and 0), one-per-frame (two scripts, the released flag), timer (several frames, shorter than a frame, exactly
a frame, unsigned and huge ms, NaN frame time, already started), real timer (before / at the time, unsigned
elapsed, a 4294967280 ms wait, NaN), components 5-10 (the check called with the script and argument; one ends the
wait), blocked / none / unknown types, IsBlocked with and without printing, StopWaiting, Block 0 / non-0,
ComponentDestroyed through the stub (ECX checked; null component; frames' other bits), on every script and on an
empty list, ReceiveEvent (type match, arg 0, frames' masked type, with a table), UnblockInterruptedScript (8 flag
layouts incl. the bottom frame's flag), CallScript (not running x3; running up to depth 5 with the stack moved
to dirty heap memory; objects, params null / not; a level without parameters), ReturnFromScript down to and past
the bottom with and without results, a second growth (none), an already-moved stack at depth 2, Interrupt
(running / not), every event handler method with and without a table (allocation failure too), the reset table
with 0 / 1 (inline element) / 3 elements.

70 deliberate bugs (tools/mutate.py): 65 caught at once, 4 more after strengthening (the timer exactly a frame
left after a first Update; the wait fields traced after every return; timer-started and interrupt bits made
different in the saved frames). 1 equivalent: growing the stack at depth `>= 2` instead of `== 2` with the inline
stack - an inline stack deeper than 2 can't happen (it is moved at the third call).
Checks: thug2_check 0 MISMATCHED, all 99 early tests pass; register scan: only the known 0x004EFF92 RISK, and
0x00472D20 "caller replaced too - safe"; IsBlocked (AL) and ReturnFromScript (AL) are the EAX/AL readers, both
return values; call audit: no MISMATCH (0x0044ECE0 and 0x0045F880 "?": tail jumps, cdecl confirmed by their
callers); check_code_writes: no overlaps; find_gaps: the gaps in the range are all other groups' functions.

## Scheduling semantics (from these functions; the spec for the QuickJS port)
Update (group B) calls UpdateWait once per Update, before running statements, unless the wait was already counted
in that Update (SCRIPT_RUN_WAIT_UPDATED). A wait set by a statement is therefore counted for the first time in the
same Update that set it.

- **Counter (`Wait n` frames, type 1).** Each count: `counter != 0 ? --counter : done`. The wait ends at the
  count that finds 0 - the (n+1)-th count - and that Update carries on with the next statement. The first count
  is in the Update that ran `Wait n`, so the script goes on during the n-th following Update: `Wait 0` doesn't
  wait at all, `Wait 1` = the next Update.
- **One per frame (type 11).** Like the counter, but the count that finds 0 ends the wait only if no other
  one-per-frame script has ended its wait since the flag 0x006F352C was last cleared (UpdateSpawnedScripts clears
  it at the start of each frame); it then sets the flag. Otherwise it stays at 0 and tries again next Update. So
  at most one such script per frame continues; the order is the list order (newest first) among spawned ones.
- **Timer (`Wait n seconds`, type 2; set as ms * 0.001f, started = false).** Each count takes dt =
  Tmr::GetSlomoFrameSeconds() (slomo / 60, unrounded double): if dt <= timer (or either is NaN): timer -= dt
  (rounded to float), started. Else (less than a frame left): if started -> done; else timer = 0, started (so the
  next count ends it). Consequences: the Update that starts the wait already subtracts a frame; a wait shorter
  than one frame still costs two counts (this Update and the next); the wait ends at the first count where the
  remaining time is less than one frame AND at least one count has already happened; a remaining time exactly
  equal to dt is subtracted to 0 and ends at the following count. The time is game (slomo) time.
- **Real timer (type 4).** Start = clock() + offset (0x0044ECE0 + 0x00696D60) when set; done at the first count
  where `(unsigned)(clock() + offset - start) >= ms` (compared in double; never if ms is NaN). Not slomo-scaled.
  Update returns BLOCKED (1) while it waits (like a block), not WAITING.
- **Blocked (type 3, `Block`).** Never counted down. Ends only from outside: ReceiveEvent with an event whose type
  equals m_waitArg (non-zero; Block(0) waits until StopWaiting / ObjectDestroyed / UnblockInterruptedScript of a
  level above), StopWaiting, ObjectDestroyed (group A). Block(type != 0) also registers the script with the tracker
  for that event type (and sets SCRIPT_FLAG_TRACKER_BLOCKED). ReceiveEvent also ends the saved blocked waits of the
  lower levels with that type (they then go on as soon as they are returned to), and then passes the event to the
  script's event handler table (which runs handlers as interrupts or spawned scripts).
- **Component waits (types 5-10).** Each count calls the component's vtable +0x18 (script, m_waitArg); the
  component ends the wait itself (sets the type to 0). ComponentDestroyed ends the wait of any level waiting on
  that component (the current level's type -> 0; saved levels' type bits -> 0).
- **Calls (CallScript).** With no code running it is just SetScript (the interrupt flag set from the argument).
  Otherwise the current level's whole state is pushed (name, params, pc, object, loop pointer, on-exit script,
  every wait field, timer-started, its interrupt flag), and the callee starts at once in the same Update: its
  parameters = a new structure with its default first-line parameters, then the call's parameters appended; its
  object = the given one; no wait; no on-exit script; m_currentLoop is NOT reset (the callee's loops stack above
  the caller's); m_functionParams is kept. The stack is 2 inline levels; the third call moves it to 16 on the
  script heap (never shrinks back until ClearScript; no check past 16).
- **Returns (ReturnFromScript, `$` / return).** The level's cached code is released. At the bottom level the
  script ends (pc = 0; its parameters stay and get the results). Otherwise the callee's parameters are freed and
  the caller's state restored - the wait fields only when the saved wait type is non-zero (so a level that was
  not waiting resumes immediately, in the same Update, at the statement after the call). Results (return's
  arguments) are appended (AppendStructureIfAny) to the restored level's parameters. It returns whether the level
  that ended was entered as an interrupt; Update then returns 5 (FINISHED_INTERRUPT) instead of going on.
- **Interrupts (Interrupt).** CallScript(name, cached code, params, the current object, interrupted = true), then
  a nested Update, whose result is returned. The interrupted level is pushed with its wait intact (it is "frozen":
  nothing counts a saved wait - only ReceiveEvent / ComponentDestroyed / UnblockInterruptedScript change saved
  waits). When the interrupt script returns, ReturnFromScript says "interrupted" and the nested Update returns 5:
  the interrupted level resumes, with its old wait and counting where it left off, at its next Update (the wait
  is counted again from that Update). If nothing was running, Interrupt just starts the script (SetScript) with
  the interrupt flag set, so (unless a wait clears the flag first) its ReturnFromScript at the bottom level says
  "interrupted" too - group B's Update decides what that returns. See
  the plan for what Update's wait return does to the flag (bit 0 cleared when the nested Update returns waiting).
- **UnblockInterruptedScript.** From the top level down, finds the first level that was entered as an interrupt
  (the top's flag is m_flags bit 0, a lower level's is its saved frame's bit 1) and clears the saved wait type of
  the level below it (the one it interrupted), so that level goes on without waiting when the interrupt returns.
  The bottom level's own flag is never looked at (it interrupted nothing).
