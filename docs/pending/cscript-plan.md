# CScript batch - phase 1: the class map and the split (branch wt/cscript)

The header is `src/game/script/cscript.h` (layout static_asserted, 0xF4 bytes; every method declared with its
convention). Nothing in this range is rewritten yet; the only code change is `ScriptRefersTo` (load.cpp,
0x00472C50, already ours) now reading the fields by name instead of raw offsets - the existing loader test
(original vs ours on fake scripts) still passes, which checks `m_scriptName` +0xC8, `m_numReturnAddresses` +0x30,
`m_returnAddresses` +0x94 and the 0x30-byte return address.

Range: 0x004728F0-0x00475920 - 79 un-rewritten functions, 11,609 bytes (0x00472C50 and 0x00472E60 are done).
0x0047AC10-0x0047AD80 (5 functions, 462 bytes) are NOT script code: they are the sound code's volume struct
(5 floats + a type at +0x14) and a stream lookup - see group A's note. Two CScript functions outside the range
belong with it: CPoolable new / delete 0x00449970 / 0x004499B0.

## Conventions (all checked in the disassembly)
- Every CScript method is `__thiscall`, callee-pops (`RET 4*n`); the free functions are `__cdecl`. No register
  arguments anywhere - no naked stubs needed.
- Returns: bool in AL for 0x00472900, 0x00472940 (SETNZ of m_pc: the upper EAX bytes are pc bits - callers use
  AL), 0x00472980, 0x004731A0, 0x004731E0, 0x00472C90, 0x00472DE0, 0x00473800 (always 1), 0x00473E70,
  0x00474B40. Values in EAX: 0x004750B0 / 0x00475750 (EScriptReturnVal), 0x004739D0 (CStruct*), 0x004738E0
  (this), 0x00472E00, 0x00472E20, 0x004734B0, 0x00475420 / 0x00475670 (CScript*), 0x00472EC0 (passes on
  0x0045F880's EAX), 0x00472EE0. The rest are void (check each caller with the register scan anyway).
- ECX preservation: 0x00472E70 calls 0x00472D20 and then uses ECX as the loop variable (0x00472E88). Rewrite
  both in the same group (C) - or keep 0x00472D20 ECX-preserving. Run `scan_register_assumptions.py` after
  each group.
- x87: 0x00472C10 (`(float)(unsigned)ms * 0.001f` - FILD, +2^32 if negative, FMUL float [0x006461D8], FSTP),
  0x00473310 (the same conversion, no scale), 0x00473050 (timer compares: FCOM with AH&0x41, FCOMP with AH&1).
  0x0047AC50 (`* 0.01f` [0x0064544C]), 0x0047ACF0 (FUCOMPP against 0.0f [0x0066D520], JP).
- Jump tables (decoded with `inspect table`; counts/indexes are hex):
  - 0x004750B0 Update, on token-1 (`475264:D:475298:49:1`): 01 28 3D -> pc++; 02 -> pc += 5; 20 begin; 21 repeat;
    22 break; 24 '$'; 29 return; 3C switch; 47 if; 48 else (jump by the 2-byte offset); 2E 2F 37 40 41 49 ->
    ResolveRandomJumps; 25 26 3E 3F -> nothing (pc not moved: the original loops forever - these never occur in
    compiled code; keep); everything else (incl. 00, 03-1F, 23, 27, 2A-2D, 30-36, 38-3B, 42-46, > 0x49) ->
    ExecuteCommand.
  - 0x00473C60 switch, on token-0x24 (`473E34:6:473E4C:1C:24`): 24 '$' -> re-read without moving (endless loop;
    keep); 3C nested switch -> pc++ and SkipToEndOfSwitch; 3D endswitch -> pc++, done; 3E case; 3F default ->
    pc += 4 (default + 0x49 + offset); 25-3B -> SkipToken.
  - 0x00473050 UpdateWait, on m_waitType-1 (`473170:6:473188:B:1`): 1 counter; 2 timer; 3 nothing; 4 real
    timer; 5-A component; B one-per-frame; 0 and > 0xB -> nothing.
- Memory: SetScript / CallScript / Restart / the CFunction temporaries allocate CStructs from the CStruct pool
  (0x006F3614 pair; `NewStruct`, falls back to Mem::Manager::Allocate(8)); CComponents from 0x006F2338 (pair
  for 0x00473C60 / 0x00474B40's expression value: 0x0044C130). Loop / return address blocks and code copies:
  Mem::Malloc on the script heap (Manager +0x124, PushContext / PopContext around). Event handler tables:
  0x10 bytes on the front-end heap (Manager +0x11C). RunScript / SpawnScript push the script heap around making
  the CScript too.
- SEH frames (`PUSH -1; PUSH handler; FS:[0]`) are just new/constructor unwinding - nothing to port.
- Existing source that touches this range (unify, don't duplicate):
  - load.cpp's GameFunction wrappers RestartScript 0x004747F0, ScriptsStillDefined 0x00472C90, ClearScript
    0x00473A80, ScriptStopped 0x00473010 -> group A switches them to the methods (selftests_qb.cpp's TestUnloadQB
    stands in for them by address - keep that working).
  - parse.cpp's GetCurrentScript 0x004728C0 / GetNextScript 0x004728D0 (done; can use `CurrentScriptVar()` /
    `ScriptListHead()` / `mp_next`).
  - memcard_cfuncs.cpp's `ResultParams` (+0x14) -> `script->m_params`.
  - GameFunction calls of RunScript 0x00475790 (chat.cpp x2, service.cpp, music.cpp, memcard_cfuncs.cpp),
    SpawnScript 0x00475420 (service.cpp), RunScript(string) 0x004758F0 (sio/device.cpp) -> group D. The tests
    that intercept those addresses (selftests_misc.cpp ~117-121 and ~385, selftests_gapsA.cpp ~770) must still
    see the calls (call through the address in tests, or move the stand-in).
  - The 5 sound functions: nothing in src yet.

## How the four agents work in parallel
- Each owns its .cpp (add it to CMakeLists.txt) and REPLACEs only its own addresses. The header is shared: add
  nothing to it except, if unavoidable, a private helper declaration in your own section (tell the merger).
- A call into ANOTHER group's function stays `GameFunction<...>(address)` with a comment
  `// -> CScript::Name (group X)`, so each branch links on its own; the merger turns those into direct calls.
  Within your group call your own C++ functions directly.
- Fields: use the header's names. If you find a field wrong or a meaning to add, note it in your record
  (docs/pending/<name>.md) - don't silently change offsets.
- Tests: Early self-tests in `src/hook/selftests_cscript_<x>.cpp`. Stand-in CScripts are 0xF4-byte blocks in a
  test arena (link them into a stand-in list by setting `ScriptListHead()` for the test, restore after); pools
  and the script cache as in selftests_scache.cpp (stand-in Mem::Manager with a script heap), symbols as in
  the loader / qb tests. The original methods call each other (original-to-original), so compare one function
  at a time where they nest, and stand in (JumpTo/CodePatch) for the tracker (0x006F0FC0 methods), the event
  handler table (0x0045Fxxx/0x0046xxxx), GameNet (0x0044E4F0) and timers (0x005BEAA0 GetSlomoFrameSeconds,
  0x0044ECE0 the clock) where needed.

## Group A - the object: construction, destruction, reset, setting a script (cscript_core.cpp)
20 functions in range + 2 + the 5 sound functions; ~2,450 + 150 + 460 bytes.

| addr | name | notes |
|---|---|---|
| 0x004738E0 | Construct | `m_object` links +0xC0/+0xC4 and +0xBC zeroed, m_refCounted constructed (0x004637B0: vtable 0x006478E4, list 0), then REP STOSD 0x3D dwords = the whole 0xF4 (so m_refCounted's vtable is 0 - keep), link at head (`mp_next = head; head->mp_previous = this`), `m_uniqueId = (*0x006F3540)++`, `++*0x006F353C`, m_returnAddresses = inline, m_loops = inline, head = this; returns this |
| 0x00474420 | Destroy | ClearScript; ReportStopped (inlined copy); free m_callbackParams (0x00477130 + DeleteStruct); unlink (head = next if no previous); ForgetStoredRandomsOf(this) 0x0046F870; event table: if +0x0C != 0 then +0x08 = 0 else 0x0045FB70 + Manager::Free; `--count`; ~m_refCounted (0x00463870, game's); m_object.Release() |
| 0x00449970 / 0x004499B0 | operator new / delete | the CPoolable pair for 0x006F3548 (like NewStruct / DeleteStruct; delete tries both pools, then Manager::Free) |
| 0x00473A80 | ClearScript | RunOnExitScripts first; Mem::FreeMalloc(m_codeCopy); if m_pc: cache->ReleaseScript(m_scriptName) (even for a code copy); m_pc = 0; free m_params, m_functionParams; for each return address: ReleaseScript(its name), free its params; free the heap blocks if not inline (return addresses, loops), back to inline; m_currentLoop = 0; counter/type/component/arg = 0; m_object released; m_scriptName = 0; m_flags &= ~3 |
| 0x00473880 | RunOnExitScripts | m_onExitScript (cleared first) then each return address' mOnExitScript from the TOP down (cleared first), each via Interrupt(name, null) (group C) |
| 0x00473010 | ReportStopped | see header |
| 0x004739D0 | CopyParamsIfOwned | `m_functionParams->References(p) || m_params->References(p)` (each only if non-null) -> NewStruct + AppendStructureIfAny |
| 0x00474540 | SetScript(name, code, params, object) | CopyParamsIfOwned, ClearScript, m_functionParams = NewStruct, m_scriptName, m_object = object, m_params = NewStruct, code or cache->GetScript(name) (none: free the copy, ClearScript, ReportStopped, return), `m_pc = ParseArguments(m_params, m_pc, null)` (the defaults), then AppendStructureIfAny(copy, then delete it) or (params) |
| 0x004747A0, 0x004747C0 | SetScript overloads | the string one hashes with Crc::GenerateCRCFromString (0x00401B90) |
| 0x00474720 | SetScriptCopy | size = SkipToEndOfScript(code) - code; Mem::Malloc on the script heap; byte copy; SetScript(symbol->name, copy, ...); THEN m_codeCopy = copy (after SetScript, so ClearScript inside doesn't free it) |
| 0x004747F0 | Restart | params copy (NewStruct + Assign 0x00478650) from return address 0 (if any) else m_params; name/object from return address 0 else current; SetScript(name, null, copy, object); free copy |
| 0x00472C90 | AllScriptsStillDefined | FindSymbol 0x00478CC0 (not LookUpSymbol); depth 0: code copy -> true, else symbol(m_scriptName) != 0; else symbol(m_scriptName) must exist, then levels from 1 if m_codeCopy else 0 |
| 0x00472940 | GotScript | |
| 0x00472F90 | SetSpawnerFile | `FindSymbol(spawner's level-0 name or m_scriptName)->unknown8` - no null check |
| 0x00473BE0 | ObjectDestroyed | null: nothing; if m_object == it: Release; every return address' mpObject == it -> 0; then m_waitType = 0 (always, when object != null) |
| 0x00472ED0/EE0/EF0, 0x00472F00 | on-exception / on-exit accessors | 0x00472F00 also clears every return address' mOnExitScript |
| 0x004734B0 | FindScriptWithUniqueId | cdecl, walks the list |

Sound (put in `src/game/sfx/volume.cpp` + .h, namespace Sfx; they have nothing to do with CScript - only the
address range is shared): 0x0047AC10 volume type from a checksum (0xF221474C -> 1, 0x9706104B -> 2,
0x0CA64C41 -> 3, else 0), 0x0047AC50 `sVolume::PercentageAdjustment(float)` (thiscall RET 4: channels = type 2
? 5 : 2 (`(type != 2) - 1) & 3) + 2`), each `channel = pct * channel * 0.01f` in that order, unrolled by 4),
0x0047ACF0 `IsSilent` (thiscall: channels 0,1 (and 2-4 when type 2) == 0.0f; then `type != 3`; NaN -> false),
0x0047AD60 the constructor (5 floats zeroed, type untouched, returns nothing - EAX = 0), 0x0047AD80 find a
stream by checksum (`__stdcall`, RET 4 - no ECX; table 0x006F7A68 of 0x3C-byte entries, two passes: entry +0,
then entry +4, each hit only if 0x0047C7E0(index); returns the index, -1 if none; a second-pass hit also calls
0x00478F60(0x6D2E270E, 0) and, when that gives 0, 0x00478F60(0xD7BB618D, 0) - results otherwise unused). Check them with find_gaps
neighbours 0x0047AE00+ (sound CFunctions) - a later sound batch may prefer to own them.

Calls into others: C's Interrupt (RunOnExitScripts). Called by everyone.

## Group B - the interpreter (cscript_exec.cpp)
13 functions, ~3,200 bytes (0x00474B40 is 1,294 of them).

| addr | name | notes |
|---|---|---|
| 0x004750B0 | Update | see the semantics below; jump table above; returns 0/1/2/5 |
| 0x00474B40 | ExecuteCommand | branches in the order: token 0x44 (member function by name: object->vtable[3](name, m_functionParams, this), no object -> GameNet 0x0044E4F0 called and ignored (a compiled-out assert), false); 0x46 (entry: CallMemberFunction free fn); 0x43 (C function pointer: `fn(m_functionParams, this)`); else `(` expression (CComponent from 0x0044C130 + 0x0046D140; ParseExpression into it; name result -> its checksum is the name to run, else return value != 0 - note `m_params` is the args) or ReadName; then token 0x42 (`obj:`): FindObject 0x00460AD0 (cdecl, tracker) - none: return true WITHOUT moving pc (see open questions); then ResolveRandomJumps, a 0x44 name -> CallMemberFunction(name, obj), else ReadName + ParseFunctionParams + LookUpSymbol: none -> false (silent only when m_scriptName == 0xFB11E6CD... see below), script -> CallScript(name, cache code, m_functionParams, obj, 0), member function -> entry ? CallMemberFunction(entry, obj) : CallMemberFunction(name, obj); token 0x07 after the name (`name = value`): name re-read at pc-4, pc += 5, ResolveRandomJumps, ParseExpression or ParseValue into a new component, a typed result gets the name and goes into m_params (AddComponent), true; otherwise ParseFunctionParams + LookUpSymbol: none -> unless m_scriptName == 0xFB11E6CD, Dbg::Printf(0x00648424 "WARNING: script %s not found, ignoring in default level.", FindChecksumNameOrUnknown(name)); false; script -> CallScript(name, code, m_functionParams, m_object, 0) true; C function -> fn(m_functionParams, this); member -> as above with m_object |
| 0x00475050 | ExecuteIf | target = (address of the 2-byte offset) + offset; token 0x39 after it = `not`; condition = ExecuteCommand; if m_pc still set and the (negated) result is false: m_pc = target |
| 0x00473C60 | ExecuteSwitch | value = the first component of the parsed line (GetNextComponent(null)) copied with 0x004773E0 into a new CComponent; cases compared with 0x004791A0 (Equals); after a match, following `0x49 xx xx 0x3E` (fall-through cases) are skipped; frees the component (0x004772A0 + 0x0044C170) |
| 0x00472A70 | SkipToEndOfSwitch | recursive on 0x3C |
| 0x00473220 | ExecuteBegin | the loop block growth (10 loops, `mpLoop` of every return address rebased) |
| 0x00472AB0 | ExecuteRepeat | count read once: GetInteger(0, &mCount) 0x00476630; pops via `m_currentLoop == m_loops ? null : m_currentLoop - 1` |
| 0x00472B50 | ExecuteBreak | skips to the matching repeat (nesting by begin), parses and drops repeat's arguments, pops |
| 0x00472950 | ParseFunctionParams | Clear + ParseArguments(m_functionParams, m_pc, m_params) |
| 0x004729E0 | ReadName | 0x16 name; 0x2C `<...>` -> GetChecksum(0) from m_params; 0x2D `<name>` -> GetChecksum(name) from m_params (pc skips 0x2D 0x16 + 4); else 0, pc unchanged |
| 0x00472980 | CallMemberFunction (free, cdecl) | object->GetComponent 0x0045DB90 (thiscall) - no null check |
| 0x004731A0 / 0x004731E0 | CallMemberFunction(entry / name, object) | object null -> m_object; none -> GameNet call ignored, false |

Calls into others: C (UpdateWait 0x00473050, CallScript 0x004748E0, ReturnFromScript 0x00473E70, Interrupt
0x00475750). The '$' / return cases read/clear m_onExitScript.

## Group C - waiting, the call stack, interrupts, events (cscript_wait.cpp)
22 functions, ~2,670 bytes.

| addr | name | notes |
|---|---|---|
| 0x00473050 | UpdateWait | see below |
| 0x004728F0, 0x00472900 | StopWaiting, IsBlocked | IsBlocked: `print` -> Dbg::Printf(0x00648410 "m_wait_type == %i\n", m_waitType); returns type == 3 |
| 0x00472BD0, 0x00472BF0, 0x00472C10, 0x00473310 | Wait, WaitOnePerFrame, WaitTime, WaitRealTime | 0x00472C10 also clears m_waitTimerStarted; 0x00473310 sets type before reading the clock: m_waitArg = 0x0044ECE0() + *0x00696D60 |
| 0x00472FD0 | Block | type 3, m_waitArg = arg; arg != 0: tracker(0x006F0FC0)->0x00463370(arg, this) and SCRIPT_FLAG_TRACKER_BLOCKED |
| 0x004739A0 | SetWaitOnComponent | |
| 0x00472D20 + 0x00472E70 | ComponentDestroyed (+ all scripts) | together (ECX) - clears frames' mWaitType bits only (`&= ~0xF`) |
| 0x00473800 | ReceiveEvent | then m_eventHandlers->0x00460470(event, this) |
| 0x00472F40 | UnblockInterruptedScript | from the top: the first level whose callee was an interrupt gets its saved wait type cleared (the top's own flag is m_flags bit 0, a frame's is mFlags bit 1) |
| 0x004748E0 | CallScript | not running: SetScript + m_flags bit0 = interrupted; else grow (inline full at 2 -> 16 on the script heap, copies both), push {name, params, pc, object, loop, onExit, waitComponent, waitTimer, waitArg, waitCounter, waitType bits, timerStarted bit0 + interrupted bit1 of mFlags (other bits kept)}, then m_scriptName, m_pc = code, m_params = NewStruct, `m_pc = ParseArguments(m_params, m_pc, null)`, AppendStructure(params) 0x00478480 if any, m_object = object, m_flags bit0 = interrupted, m_waitType = 0, m_onExitScript = 0. code == null with a running script is not checked (ParseArguments then reads from null) |
| 0x00473E70 | ReturnFromScript | ReleaseScript(m_scriptName) first; result = m_flags bit0; depth 0 -> m_pc = 0 (params kept); else free m_params and restore; the wait fields only if the restored type != 0; m_flags bit0 = frame's bit1; `results` -> m_params->AppendStructureIfAny 0x00478540 |
| 0x00475750 | Interrupt | cache GetScript(name), CallScript(name, code, params, m_object, true), return Update() |
| 0x00472EA0, 0x00472EC0, 0x004734E0, 0x004735D0, 0x00473690, 0x004736C0 | event handlers | through the table's methods 0x0045F820 (ctor), 0x0045FA60, 0x0045FA10, 0x0045FBC0 (add: this + 5 args), 0x0045F840, 0x0045FDF0, 0x0045FED0, 0x0045FF50, 0x004600E0, 0x004601A0, 0x0045F880 (cdecl) and the tracker's 0x00463450 - all still the game's; 0x004736C0 reads each table element's components in order (type, script, group, params) and compares the first with 0x80367192 (the `exception` flag) |

Calls into others: A (SetScript, from CallScript), B (Update, from Interrupt). Names: the four event methods'
names follow the CFunctions that call them (SetEventHandler 0x0044AB20 / SetException 0x0044ABD0 -> 0x004735D0,
ResetEventHandlersFromTable 0x0044AC70 -> 0x004736C0, ClearEventHandler 0x0044ACD0 -> 0x00473690,
ClearEventHandlerGroup 0x0044AD00 -> 0x00472EA0, PrintEventHandlerTable 0x0044AD90 -> 0x00472EC0; 0x004734E0
from 0x00461710 with an array and a flag) - check the argument meanings against 0x0045FBC0.

## Group D - the script manager (cscript_manager.cpp)
24 functions, ~3,290 bytes (mostly simple list walks).

| addr | name | notes |
|---|---|---|
| 0x00475920 | UpdateSpawnedScripts | see below |
| 0x004753C0 | DeleteSpawnedScripts | |
| 0x00475420 / 0x00475670 | SpawnScript | 10 cdecl args (CFunc_SpawnScript 0x0059F330 shows them: name, params, callback, callback params, node = the caller's m_node, id, net, permanent, notSessionSpecific, pauseWithObject); C function case first (LookUpSymbol type 8); m_spawnFlags = (old & 0xF1) \| 1 \| nss<<1 \| pwo<<3 (paused cleared); net message 0x00473420(name, null, node, permanent) when net && 0x0044E4F0(GameNet) |
| 0x004756C0, 0x004756F0, 0x00475700 | KillSpawnedScript, StopAllScriptsUsingThisObject, DeleteFinishedScripts | |
| 0x00475790 / 0x004758F0 | RunScript | types 7/8/9 only; member function: entry ? CallMemberFunction(entry, params, object, null) : object->vtable[3](name, params, null) (no null checks); script: `do {} while (Update() != 0)` then delete |
| 0x004752F0 | RunScriptOnce | one Update; results <- AppendStructure(m_params) |
| 0x00473350, 0x00473420 | net messages | 0x004549F0 message-desc ctor, 0x00532EA0, 0x00459E50, WriteToBuffer 0x00479E20 (0x400-byte buffer, stack cookie 0x0068EC38 / 0x00622FCF in 0x00473350); object id at object +0x14 (-1 if none); ids 0x48 / 0x49 |
| 0x00472D80, 0x00472DC0, 0x00472DE0, 0x00472E00, 0x00472E20 | pause / exists / ids | 0x00472D80's pause bit: `flags ^ ((pause << 2 ^ flags) & 4)` |
| 0x00473FA0, 0x00474040, 0x004740D0, 0x00474190, 0x00474250, 0x004742B0, 0x00474350 | the kill/stop walks | each: ClearScript + the inlined ReportStopped; the list cursor (next) is read BEFORE stopping |

Calls into others: A (new/Construct/SetScript/ClearScript/ReportStopped/Destroy/delete), B (Update, the free
CallMemberFunction 0x00472980). Its callers in src (chat, service, music, memcard, device) switch to ours.

## Scheduling semantics (the spec for the QuickJS port)
- **The list.** Every CScript is on one doubly-linked list; Construct puts it at the HEAD (0x004738E0), so the
  list runs newest to oldest. Destroy unlinks it. The current script (0x006F3530) is set by Update while it runs
  statements and cleared when it returns 0/1/2 (but left set on return 5 - 0x0047518E).
- **Per frame (0x00475920, called once per frame from 0x0054F380).** Sets "updating" (0x006F3544), clears the
  one-per-frame flag (0x006F352C). Walks from the head: before updating a script, the cursor moves to the next
  SPAWNED script (non-spawned ones are skipped - objects update their own scripts). A spawned script is
  skipped when paused and still has code, or when SCRIPT_FLAG_PAUSE_WITH_OBJECT and its object's vtable +0x1C
  says false. Otherwise one Update; if it returns FINISHED (0): its callback (if any) is run to completion with
  RunScript(callback, callbackParams, object), then it is deleted. Scripts spawned during the walk are inserted
  before the cursor and first run NEXT frame. A DeleteSpawnedScripts requested during the walk is deferred to
  the end (0x006F3545). A script deleted outright during the walk (KillSpawnedScript 0x004756C0 on the cursor's
  next) would be read after being freed - original behaviour; the kill/stop walks only stop scripts (pc = 0),
  and stopped spawned scripts are deleted - with their callback run - at their next turn.
- **Update (0x004750B0).** Clears SCRIPT_RUN_WAIT_UPDATED; no code -> 0. Loop while m_pc: current = this; unless
  the wait was already counted, UpdateWait; if still waiting: set SCRIPT_RUN_WAIT_UPDATED, clear the interrupt
  flag bit 0 (!), current = null, return 1 for BLOCKED/REAL_TIMER else 2. Else run one token / statement
  (table above). So a wait set by a statement is counted at once in the same Update: `Wait n` (frames) makes this Update
  return WAITING (n -> n-1) and the script goes on during the n-th following Update (`Wait 1` = next frame);
  `Wait 0` doesn't wait at all.
- **Waits (0x00473050)**, counted once per Update: COUNTER: `counter ? --counter : done`. ONE_PER_FRAME: the
  same, but at 0 it is done only if no other script was released this frame (then it sets the flag); else it
  waits on with 0. TIMER: dt = Tmr::GetSlomoFrameSeconds(); `dt <= timer` -> timer -= dt, started; else started
  ? done : (timer = 0, started) - i.e. a wait shorter than a frame still waits one Update more. REAL_TIMER: done
  when `(float)(unsigned)(clock() + offset - start) >= ms`. BLOCKED: nothing - something else unblocks it
  (ReceiveEvent of its type, StopWaiting, UnblockInterruptedScript, ObjectDestroyed). 5-10: the component's
  vtable +0x18(script, arg) decides (and clears the type). A blocked/component wait is never counted down.
- **Calls.** A statement naming a script calls it in the same Update (CallScript pushes the caller's state; the
  callee's first-line defaults then the call's arguments are its parameters; its object is the caller's, or
  `obj:`'s). `$` or `return [args]` pops (ReturnFromScript): return's arguments are merged into the caller's
  parameters (at the bottom level, into the script's own parameters - how RunScriptOnce returns results). A
  script call in an `if` condition counts as true (the condition is the call's result, and the callee runs
  first; the body runs after it returns). The return-address stack holds 16 (2 inline), loops 10 (1 inline) -
  no checks.
- **Interrupts.** Interrupt(name, params) (0x00475750: event handlers via 0x00460470, OnExitRun, 0x00461500,
  0x005228B0, 0x00522670) pushes the current state (whatever it was doing or waiting for - saved and restored),
  runs the script at once (a nested Update) and returns that Update's result. When the interrupt script ends,
  ReturnFromScript says "interrupted" and Update returns 5; the interrupted level resumes - with its old wait -
  at its next Update. If the interrupt script waits, the nested Update returns 1/2 with the interrupt level on
  top - and that return clears m_flags bit 0, the "this level is an interrupt" flag (0x0047522B): the script
  continues in the interrupt at later Updates, and when it ends ReturnFromScript says "not an interrupt", so
  the interrupted level carries on in the same Update (no 5).
  UnblockInterruptedScript (0x00472F40) clears the saved wait of the level an interrupt interrupted.
- **OnExitRun (+0xE8, per level).** At `$` / `return` of a level that registered one, it is cleared and run as
  an interrupt BEFORE the level returns; ClearScript runs every level's (top first). If the on-exit script
  waits, the outer Update's following ReturnFromScript pops the on-exit level instead - it is cut off at its
  first wait - and (the interrupt flag having been cleared by the nested Update) says "not an interrupt", so the
  outer Update goes on at once with the original level, still at its '$' / return token, which runs again with no
  on-exit script. For `return`, the arguments parsed in between are read from the on-exit script's pc (after its
  wait) and merged into the original level's parameters - an original quirk.
- **Spawning.** SpawnScript makes a script (at the head), SetScript (defaults + params; no statement runs yet)
  and flags; its first Update is the next UpdateSpawnedScripts that reaches it (the next frame when spawned from
  a script). A C function "spawned" is just called with a temporary script. RunScript runs a script to its end
  synchronously (Update in a loop: counter/timer waits spin, a block never ends - original), C functions and
  member functions directly.
- **Stopping.** ClearScript (stop) runs the on-exit scripts, releases the code; ReportStopped tells the event
  handler table (event 0x8B713E0E) and the tracker. Kill walks: by name at any level (0x00473FA0), by spawn id /
  unique id / spawner file (0x00474040), by object (+ id or name, except one script) - spawned only; StopAllScripts
  and the object walks (0x004742B0, 0x00474350) stop any script. DeleteSpawnedScripts deletes spawned scripts
  that aren't "not session specific" (no callback). DeleteFinishedScripts (Cleanup) deletes non-spawned scripts
  with no code and no event handler table.

## Open questions
- 0x00474B40: `obj:` with no such object returns true without moving m_pc; the next statement is then the same
  0x42 token -> ExecuteCommand -> ReadName gives 0 -> FindObject(0)... looks like an endless loop unless
  FindObject(0) (0x00462120) finds something. Check 0x00462120 before writing a test that hits it.
- m_refCounted (+0xEC): which smart pointers point at scripts (CSmtPtr<CScript>)? Its vtable is null between
  Construct and Destroy (memset after its constructor) - harmless unless something calls through it.
- m_spawnerFile (+0xD0) is compared with an id in KillSpawnedScriptsWithId (0x00474040) - maybe that's how a
  level's spawned scripts are killed by the level's .qb checksum; check the callers 0x00496590, 0x00494E40,
  0x004963B0. SetSpawnerFile's `this` is SpawnScript's result (Ghidra lost ECX in 0x0059F330 - confirm).
- 0xFB11E6CD (the script name whose missing calls aren't reported) - presumably the default-level script.
- SpawnScript argument 8 (`permanent`) only reaches the network message; check 0x00473420's message layout.
- The event handler table methods' argument meanings (group C's names are from the CFunctions).
- 0x0047AC10-0x0047AD80 are sound code (Sfx volume / streams), not CScript: fine in group A's batch as a separate
  file, or leave them to a sound batch.
