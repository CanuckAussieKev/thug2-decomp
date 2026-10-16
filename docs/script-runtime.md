# The QB script runtime: how scripts are scheduled and run

This is the behaviour spec for replacing the QB interpreter with embedded JavaScript (QuickJS), where scripts
become generators driven by a scheduler (see CLAUDE.md, "Later"). A port has to reproduce what is described
here. It was put together from the CScript batch (batch 74 in decomp-progress.md: the plan and the records of
groups A-D), and the code was checked wherever they disagreed. The code is `src/game/script/cscript.h` (class,
fields, flags) plus `cscript_core.cpp` (A), `cscript_exec.cpp` (B), `cscript_wait.cpp` (C) and
`cscript_manager.cpp` (D). For the script cache (GetScript / ReleaseScript use counting) see "What the cache
does" in decomp-progress.md (batch 70). For the token format see `src/game/script/qb.h`.

Conventions in this document:
- "Update" means one call of `CScript::Update` (0x004750B0). A "count" is one call of `UpdateWait` (0x00473050).
- **[quirk]** means original behaviour a faithful port must reproduce. **[bug]** means a crash, hang or
  use-after-free in the original. A port should not reproduce these, but it must not change what happens in
  the cases that work.
- "(test)" means an offline self-test compared original against ours for that case. "(code)" means it was read
  from the disassembly only, with no test.

## 1. The object and its state

A `CScript` (0xF4 bytes, a pool of 300 at 0x006F3548) is one running script plus its call stack. The full
layout is in cscript.h. The parts that matter for scheduling are:

| field | meaning |
|---|---|
| `m_pc` +0x0C | the next token. Null means not running ("finished"). |
| `m_params` +0x14 | the current level's parameters: the defaults from the script's first line, plus the caller's arguments, plus `name = value` assignments, plus whatever C functions leave there. |
| `m_functionParams` +0x10 | the arguments of the statement being run, parsed again for every statement. |
| `m_currentLoop` / `m_loops` | one loop stack shared by all call levels: 1 inline entry, moved to 10 on the script heap. |
| `m_numReturnAddresses`, `m_returnAddresses` | the call stack: saved caller levels (`SReturnAddress`, 0x30 bytes). 2 inline, moved to 16 on the script heap. Nothing checks the limit. |
| `m_waitType` +0xA4 with `m_waitCounter`, `m_waitTimer`, `m_waitTimerStarted`, `m_waitArg`, `m_waitComponent` | the current level's wait (section 5). |
| `m_flags` +0xB4 | bit 0 `SCRIPT_RUN_INTERRUPTED`: the top level was entered as an interrupt. Bit 1 `SCRIPT_RUN_WAIT_UPDATED`: the wait has already been counted in this Update. |
| `m_spawnFlags` +0xCC | `SPAWNED` 1, `NOT_SESSION_SPECIFIC` 2, `PAUSED` 4, `PAUSE_WITH_OBJECT` 8, `TRACKER_BLOCKED` 0x10. |
| `m_object` +0xBC | the object the current level runs on (a smart pointer; saved levels hold plain pointers). |
| `m_onExitScript` +0xE8 | the current level's OnExitRun script. Each saved level has its own copy. |
| `m_eventHandlers` +0xE0 | the event handler table, created on demand. |
| `m_callbackScript` / `m_callbackParams` | for spawned scripts: run when the script finishes. |
| `m_spawnId`, `m_uniqueId`, `m_spawnerFile`, `m_node` | identity, used by the kill walks (section 10). |

Globals: the list head 0x006F3538, the current script 0x006F3530 (GetCurrentScript 0x004728C0), the count
0x006F353C, the next unique id 0x006F3540, "updating spawned scripts" 0x006F3544, "delete pending" 0x006F3545,
and "one-per-frame released" 0x006F352C.

**The list.** Every script is on one doubly linked list. Construct (0x004738E0) puts a new script at the
**head**, so the list runs from newest to oldest. Only Destroy (0x00474420) unlinks a script. Stopping a script
does not unlink it. Construct zeroes the whole object after constructing `m_refCounted`, which leaves that
member's vtable null for the script's whole life **[quirk, harmless unless something calls through it]**.
`m_uniqueId` is the counter's value before the increment.

## 2. The frame loop: UpdateSpawnedScripts (0x00475920)

It is called once per frame from 0x0054F380. Scripts owned by objects (not spawned) are not updated here: their
owners call Update themselves (section 12 lists this as open).

1. Set "updating" (0x006F3544) and clear "one-per-frame released" (0x006F352C).
2. Walk from the head (newest) to the tail (oldest). For the script at the cursor, **first** look up the next
   *spawned* script after it, skipping scripts that aren't spawned. The walk goes on from that script whatever
   the Update does. (test: a non-spawned next script that becomes spawned during the Update is still skipped.)
3. The cursor script gets exactly one Update when all of these hold:
   - it is spawned;
   - it is not (paused **and** still has code). A paused script with no code is updated, finishes and goes;
   - it is not (`PAUSE_WITH_OBJECT` **and** it has an object **and** the object's vtable +0x1C returns false).
     With no object, it runs.
4. If that Update returns FINISHED (0): when `m_callbackScript` is set, the callback runs to completion right
   there with `RunScript(callback, m_callbackParams, the script's object, net = false)` (section 9). Then the
   script is deleted (Destroy + pool delete). The results 1, 2 and 5 keep the script for the next frame.
5. Clear "updating". If a DeleteSpawnedScripts arrived during the walk (the pending flag 0x006F3545 is set), run
   it now and then clear the flag.

What happens when the list changes during the walk:
- **Spawned during the walk** (by a statement, a callback or an event): the new script goes to the head, which
  the walk has already passed, so its first Update is **next frame**. SpawnScript runs no statement itself.
- **Stopped during the walk** (any Kill*/Stop* walk, or a ClearScript): the script stays on the list with no
  code. If the walk hasn't reached it yet, it is reached this frame: Update returns FINISHED, the callback runs,
  and it is deleted. If the walk has already passed it, the same happens next frame. A stopped script that isn't
  spawned is never deleted here (see DeleteFinishedScripts, section 10).
- **Deleted outright during the walk**: KillSpawnedScript 0x004756C0, or a DeleteSpawnedScripts called from
  outside the walk. If the deleted script is the cursor's precomputed next, it is still visited after being
  freed **[bug: use after free]** (test). A script that deletes itself during its own Update is the same
  **[bug]**.
- **DeleteSpawnedScripts during the walk** (0x004753C0) is deferred to step 5. It deletes every spawned script
  that isn't `NOT_SESSION_SPECIFIC`. **Callbacks are not run** (Destroy runs the on-exit scripts, section 7).

## 3. One Update (0x004750B0)

```
Update():
  m_flags &= ~WAIT_UPDATED
  while m_pc:
    current_script = this
    if !(m_flags & WAIT_UPDATED): UpdateWait()           # count the wait (section 5)
    if m_waitType != NONE:
      m_flags = (m_flags & ~INTERRUPTED) | WAIT_UPDATED   # (!) clears the interrupt flag
      current_script = null
      return BLOCKED (1) if type is 3 or 4, else WAITING (2)
    run one token / statement (section 4)
    # '$' / return whose ReturnFromScript says "interrupted": return FINISHED_INTERRUPT (5) at once,
    # with current_script left set and bit 0 not cleared
  m_flags &= ~INTERRUPTED
  current_script = null
  return FINISHED (0)
```

What the caller does with each result:

| value | when | UpdateSpawnedScripts | RunScript loop |
|---|---|---|---|
| 0 FINISHED | `m_pc` is null: no code, ended at the bottom `$`/return, or stopped during this Update | callback, then delete | stop looping, delete |
| 1 BLOCKED | waiting with type 3 (blocked) or 4 (real timer) | keep | Update again |
| 2 WAITING | waiting with any other type (1, 2, 5-10, 11) | keep | Update again |
| 5 FINISHED_INTERRUPT | a level entered as an interrupt (with bit 0 still set) returned | keep | Update again |

3 and 4 (ERROR, STOPPED_IN_DEBUGGER) are never returned by this build.

**The two flag bits.**
- **WAIT_UPDATED (bit 1)** is set only by an Update that is returning because of a wait, and cleared at the
  start of each Update. Inside the loop it is therefore clear, except after a statement that ran a *nested*
  Update on the same script (an Interrupt: OnExitRun at `$`/return, or a C function calling Interrupt) that
  returned on a wait. The outer loop then skips counting and returns as soon as it finds a wait. Consequence
  **[quirk] (code)**: once a nested Update has returned on a wait, bit 1 stays set for the rest of the outer
  Update. So after the on-exit cut-off of section 7, the first wait the script sets later in that same Update is
  not counted in that Update: `Wait n` costs one Update more than usual, and `Wait 0` waits one Update.
- **INTERRUPTED (bit 0)** is the top level's "I am an interrupt" flag (each saved level keeps its own as frame
  `mFlags` bit 1). It is set by CallScript (from its `interrupted` argument) and restored by ReturnFromScript.
  It is cleared by every Update that returns 0, 1 or 2, and by ClearScript. Only the *top* level's flag is
  cleared, so an interrupt that waits stops being an interrupt (section 7).

**Current script.** 0x006F3530 is set at every pass of the loop and cleared on returns 0, 1 and 2, but left set
on 5 (0x0047518E) **[quirk]**. It follows that after a nested Update that returned 0, 1 or 2 (for example a C
function that calls Interrupt), GetCurrentScript is null for the rest of that C function, and after an Interrupt
that returned 5 it still points at the script, even outside any Update (for example after ClearScript's on-exit
scripts, section 7).

**A script stopped mid-statement** (a C function calls ClearScript on its own script): `m_pc` becomes null, the
statement's result is read before anything is freed (test), ExecuteIf sees `m_pc == 0` and doesn't jump, and
the loop ends with FINISHED.

## 4. Statements

### 4.1 Update's token dispatch (jump table 0x00475264 / 0x00475298, indexed by token - 1)

| token | meaning | action |
|---|---|---|
| 01 | end of line | pc += 1 |
| 02 | end of line + 4-byte line number | pc += 5 |
| 28 | endif | pc += 1 |
| 3D | endswitch | pc += 1 |
| 20 | begin | ExecuteBegin (4.4) |
| 21 | repeat [n] | ExecuteRepeat (4.4) |
| 22 | break | ExecuteBreak (4.4) |
| 24 | `$` end of script | if this level has an OnExitRun: clear it, `Interrupt(it)`. Then `ReturnFromScript(null)`; "interrupted" means return 5 (section 6) |
| 29 | return [args] | the same on-exit step, then `++pc`, parse the arguments into m_functionParams **from wherever pc is now**, `ReturnFromScript(m_functionParams)` (see the quirk in section 7) |
| 2E, 2F, 37, 40, 41, 49 | jump / random blocks / short jump | `pc = ResolveRandomJumps(pc)` |
| 3C | switch | ExecuteSwitch (4.3) |
| 47 | if | ExecuteIf (4.3) |
| 48 | else, reached at the end of an if's true part | jump by its 2-byte offset (to the endif) |
| 25, 26, 3E (case), 3F (default) | | nothing: pc doesn't move and Update **loops for ever** **[bug, kept]**. Compiled code never starts a statement with these (25 and 26 are probably the older long-form if/else; not verified) |
| everything else (00, 03-1F, 23, 27, 2A-2D, 30-36, 38-3B, 42-46, > 49) | | ExecuteCommand (4.2) |

### 4.2 ExecuteCommand (0x00474B40): one statement

The result is a bool and is only used by `if`. Its branches, in order:

1. **`44 <name> args`**: a member function by name on `m_object`, through the object's vtable +0x0C
   `(name, m_functionParams, script)`. With no object: GameNet 0x0044E4F0 is called and its result ignored (what
   is left of a compiled-out assert), and the result is false.
2. **`46 <entry> args`**: a member function table entry on `m_object`, through the free CallMemberFunction
   0x00472980. For the component types 0x57525413 / 0x0ADA4E74 the object runs it itself (vtable +0x08);
   otherwise its component of that type (0x0045DB90, then the component's vtable +0x2C) does. No object: as in 1.
3. **`43 <fnptr> args`**: a C function, `fn(m_functionParams, this)`. Its result is the statement's result.
4. Otherwise a **name** is read first:
   - `0E ( expression )`: evaluated into a temporary component (with `m_params` supplying `<x>` values). If the
     result is not a name, the statement's value is `raw != 0` and **nothing runs**. If it is a name, that
     checksum is the name to run; if the name is 0, ReadName reads one after the expression.
   - else ReadName (0x004729E0): `16 <checksum>`; `2C` `<...>` gives the unnamed checksum parameter of
     m_params; `2D 16 <p>` (`<p>`) gives m_params' checksum parameter p. Anything else gives 0 and pc doesn't move.
5. Then, depending on the token after the name:
   - **`42` (`name:` on an object)**: `FindObject(name)` (0x00460AD0, through the tracker).
     - **Not found**: return true **without moving pc**. The next pass runs the `42` again as a statement:
       ReadName gives 0 and `FindObject(0)` (0x00462120, a hash lookup of id 0, presumably the first skater) is
       tried. If object 0 exists, the rest of the statement runs on object 0 **[quirk]**. If it doesn't, the
       script loops for ever **[bug]**.
     - **Found**: ResolveRandomJumps after the `:`, then either `44 <name>` (that member function on the object)
       or a name parsed with its arguments and looked up:
       - a script: CallScript on that object, with the code from `GetScript(symbol->name)` (an alias is
         followed for the code, but the call keeps the written name), result true;
       - a member function: by entry or by name on the object;
       - a C function or any other symbol: false, and **a C function is not run**.
       A missing symbol is false inside script 0xFB11E6CD; anywhere else its type is read through a null
       pointer **[bug: crash]**.
   - **`07` (`name = value`)**: the target name is re-read from the 4 bytes before the `=` (so `<p> = 1` sets
     `p`), then `pc += 5`, ResolveRandomJumps, then an expression or a single value. A typed result is added to
     **m_params** (replacing an existing one). An empty result (for example a missing `<param>`) is dropped.
     Result true.
   - **otherwise a call**: ParseFunctionParams (0x00472950: clear m_functionParams, parse the rest of the line
     with m_params supplying `<x>`), then LookUpSymbol(name):
     - missing: unless the current script is 0xFB11E6CD (presumably the default level's), print `WARNING:
       script %s not found, ignoring in default level.`; result false;
     - a script: `CallScript(name, GetScript(symbol->name), m_functionParams, m_object, false)`, result true
       (section 6);
     - a C function: its result;
     - a member function: on m_object (by entry, else by name); with no object, the GameNet call and false;
     - any other type: false.

### 4.3 if and switch

- **if (0x00475050)**: `47 <2-byte offset> [39 not] <condition statement>`. The target is the address of the
  offset plus the offset. The condition is one ExecuteCommand. If `m_pc` is still set and the result (negated
  if `not`) is false, jump to the target (the else body or the endif). **A script call as the condition counts
  as true and the callee runs first**: the if's body runs after the callee returns, perhaps frames later.
- **switch (0x00473C60)**: the value is the first component of the switch line, copied. The scanner then looks
  at each token:
  - `3E 49 <next> value-line` (a case): if the line's first component equals the value (ComponentsEqual
    0x004791A0), any case labels that follow at once (`49 xx xx 3E 49 xx xx value`: fall-through labels) are
    skipped and the body runs. The `49` jump at the end of the body goes to the `3D`. If not equal, go to `<next>`.
  - `3F` default: pc += 4 and run.
  - `3D`: pc += 1, done (no match and no default).
  - a nested `3C`: skip to its end (SkipToEndOfSwitch 0x00472A70, recursive).
  - any other token is skipped with SkipToken.
  - a `24` (`$`) met while scanning is re-read without moving: an endless loop **[bug, kept]**.

### 4.4 Loops

- There is **one loop stack for all call levels**. CallScript doesn't reset `m_currentLoop`, so a callee's loops
  stack above its caller's. Each saved level remembers its loop pointer.
- **begin (0x00473220)**: pushes a loop that starts after the token. When the inline loop is in use (the second
  nested loop, even if the inline loop belongs to a calling level), the stack moves to 10 entries on the script
  heap and the saved levels' loop pointers are moved with it (test). Nothing checks beyond 10.
- **repeat (0x00472AB0)**: the count is read **the first time only**. `mpEnd` is set past the arguments and pc
  stays after the 21. No count: jump back to the start for ever (until a break). With a count: `--count`; jump
  back while it is not 0; at 0, pc = mpEnd and pop. `repeat 0` or a negative count goes round 2^32 times
  **[quirk]**.
- **break (0x00472B50)**: skips forward to the matching repeat (counting nested begin/repeat), parses its
  arguments **without** the script's parameters and drops them, then pops.

## 5. Waits (UpdateWait 0x00473050, jump table 0x00473170)

A statement (a C function) sets a wait. Update counts it **once per Update, before running any statement**, and
the first count happens **in the same Update that set it** (section 3). While a wait is on, the script runs
nothing. When a count ends the wait, the script carries on with its next statement in that same Update.

| type | set by | each count |
|---|---|---|
| 1 COUNTER | Wait(n) 0x00472BD0 | `counter ? --counter : done` |
| 11 ONE_PER_FRAME | WaitOnePerFrame(n) 0x00472BF0 | `counter ? --counter : (released_this_frame ? stay at 0 : (released_this_frame = true, done))` |
| 2 TIMER | WaitTime(ms) 0x00472C10: `timer = (float)((unsigned)ms * 0.001f)`, started = false | `dt = Tmr::GetSlomoFrameSeconds()` (0x005BEAA0; a double, not rounded): if `dt > timer`: `started ? done : timer = 0`; else `timer = (float)(timer - dt)`. Then started = true |
| 4 REAL_TIMER | WaitRealTime(ms) 0x00473310: type set first, then `start = clock() + offset` (0x0044ECE0 + 0x00696D60), `timer = (float)(unsigned)ms` | done when `(double)(unsigned)(clock() + offset - start) >= timer` |
| 3 BLOCKED | Block(type) 0x00472FD0: `m_waitArg = type`; if type != 0, the tracker is told (0x00463370) and `TRACKER_BLOCKED` is set | nothing (never counted down) |
| 5-10 component | SetWaitOnComponent(type, component, arg) 0x004739A0 (marks component +0x08 \|= 2; no null check) | calls the component's vtable +0x18 `(script, m_waitArg)`; the component ends the wait itself by setting the type to 0 |
| 0, > 11 | | nothing |

What these rules mean, per type:
- **Counter**: `Wait n` lets the script go on during the **n-th following Update**. `Wait 0` doesn't wait at all,
  and `Wait 1` means the next Update. Because the first count is in the setting Update, n+1 counts are needed.
- **One per frame**: counted like the counter, but at 0 only the **first** such script per frame to reach 0
  continues. The others stay at 0 and try again at their next Update. The flag is cleared only at the start of
  UpdateSpawnedScripts, so the winner is the first in walk order (newest first) among spawned scripts. Object
  scripts updated before the walk in the same frame compete with the previous frame's flag (open, section 12).
  Inside RunScript (section 9) a second release never happens and the loop spins for ever **[bug] (code)**.
- **Timer**: game time (slomo-scaled), counted per Update. With a constant dt and t the requested seconds, the
  script goes on during the **max(1, floor(t/dt))-th following Update** (subject to float rounding of each
  subtraction). Put generally, the wait ends at the first count *after the first* that finds less than one dt
  left. A remaining time exactly equal to dt is subtracted to 0 and ends at the next count. A wait shorter than
  one frame still costs two counts **[quirk]**. A NaN timer or dt goes down the subtract branch every time and
  never ends **[quirk]**. `ms` is taken as unsigned.
- **Real timer**: wall-clock milliseconds, not scaled by slomo. Update returns **BLOCKED (1)**, not WAITING,
  while it waits. NaN never ends. The subtraction is unsigned (wraps).
- **Blocked**: ends only from outside, by one of:
  - **ReceiveEvent(event)** (0x00473800; always returns true): if blocked with `m_waitArg != 0` and the event's
    type equals it, the wait ends. **Saved levels** blocked on that type have their saved wait type cleared too
    (they go on as soon as they are returned to). Then the event goes to the script's own handler table
    (0x00460470).
  - **StopWaiting** (0x004728F0): type = 0.
  - **ObjectDestroyed(object)** (0x00473BE0): for any non-null object, `m_waitType = 0` **even if the script
    never referred to that object** **[quirk]**. It also drops the object from every level.
  - **UnblockInterruptedScript** (0x00472F40, section 7).

  `Block(0)` can only end by one of the last three. IsBlocked (0x00472900) is `type == 3`; with `print` it prints
  `m_wait_type == %i`.
- **Component**: **ComponentDestroyed(component)** (0x00472D20, for every script through 0x00472E70) ends the
  current level's wait if it waits on that component, and clears the saved wait type bits (`&= ~0xF`) of saved
  levels that wait on it. The component pointers are left in place.
- **Saved waits are frozen**: a level on the call stack is never counted. Only ReceiveEvent, ComponentDestroyed
  and UnblockInterruptedScript change saved waits.

## 6. Calls and returns

**CallScript(name, code, params, object, interrupted)** (0x004748E0):
- **Nothing running** (`m_pc == 0`): it is just SetScript (section 8), with bit 0 set from `interrupted`.
- **Otherwise**, push the current level. The frame saves: name, m_params (ownership moves to the frame), pc,
  object, loop pointer, m_onExitScript, wait component / timer / arg / counter, wait type (low 4 bits; the
  frame's other bits are left as they were), timer-started (bit 0 of `mFlags`) and bit 0 of `m_flags` (bit 1 of
  `mFlags`; its other bits are kept). The third call moves the stack from the 2 inline frames to 16 on the
  script heap. It never shrinks back before ClearScript.
- **The callee**: `m_scriptName = name`; m_params = a new structure holding the defaults from the callee's
  first line (ParseArguments with no parameter source), then `params` appended (AppendStructure 0x00478480);
  `m_object = object`; bit 0 = `interrupted`; no wait; no on-exit script. `m_currentLoop` and m_functionParams
  are kept. **The callee's first statement runs in the same Update.**
- `code == null` while a script is running is not checked: ParseArguments reads from null **[bug: crash]**. This
  happens with Interrupt or OnExitRun of a name that isn't a script. ExecuteCommand only calls CallScript for
  real script symbols.
- Call arguments are evaluated in the caller (m_functionParams, with the caller's m_params for `<x>`). The callee
  gets a copy appended over its defaults, so arguments win.
- The object: a plain call uses the caller's `m_object`; `obj:Script` uses that object.

**ReturnFromScript(results)** (0x00473E70), for `$` and `return`:
1. `ReleaseScript(m_scriptName)` (the cache use).
2. `interrupted = m_flags bit 0` (this is the function's result).
3. At the **bottom level**: `m_pc = 0` (the script ends) and **m_params is kept**. Otherwise: free m_params,
   pop the frame and restore name, params, pc, object, loop pointer and on-exit. Set `m_waitType = the saved
   type`, and **only if that type is non-zero** also restore the other wait fields (otherwise the callee's values
   stay in those unused fields). Bit 0 = the frame's interrupt bit.
4. If `results`: `m_params->AppendStructureIfAny(results)`. These are return's arguments, merged into the
   restored level's parameters. At the bottom level they go into the script's own parameters, which is how
   RunScriptOnce returns results.

A normal (non-interrupt) return resumes the caller **in the same Update**, at the statement after the call (a
restored level that was executing a statement has no wait).

## 7. Interrupts and on-exit scripts

**Interrupt(name, params)** (0x00475750): `CallScript(name, GetScript(name), params, m_object, interrupted =
true)`, then a **nested Update**, whose result is returned. Callers: the event handler table (0x00460470), the
`$`/return OnExitRun step, RunOnExitScripts, 0x00461500, 0x005228B0 and 0x00522670.

- The interrupted level is pushed **with its wait intact** (frozen). If the interrupt runs to its `$`/return
  inside the nested Update, ReturnFromScript says "interrupted" and the nested Update returns **5**. The
  interrupted level is restored with its old wait and resumes counting it **at its next Update**.
- **If the interrupt waits**, the nested Update returns 1 or 2 and **clears bit 0**, so from then on the
  interrupt is an ordinary called level. It continues at later Updates; when it ends, ReturnFromScript says "not
  interrupted" and the interrupted level carries on **in the same Update** (its restored wait is counted
  straight away in that Update, unless bit 1 is set) (test). When the nested Update was run from a statement of
  the same script (a C function calling Interrupt), the outer Update sees the interrupt's wait and returns too.
- **[quirk] (code)**: only the top level's bit 0 is cleared on a wait. An interrupt that *calls* a script which
  waits keeps its own flag in the saved frame. When the callee returns, bit 0 comes back as 1, and the
  interrupt's later `$` makes that frame's Update return 5. The interrupted level then resumes at the *next*
  Update instead of in the same one.
- **Nothing running**: CallScript is just SetScript with bit 0 set. If the script ends in the nested Update,
  that Update returns **5 with `m_pc == 0`**, and the next Update returns 0.
- **UnblockInterruptedScript** (0x00472F40, the CFunction UnBlockInterruptedScript): from the top level down,
  find the first level entered as an interrupt (the top's flag is m_flags bit 0, a saved level's is frame bit 1)
  and clear the **saved wait type of the level just below it**, the level it interrupted. That level then goes
  on without waiting once the interrupt returns. The bottom level's own flag is never examined.

**OnExitRun scripts** (`m_onExitScript` per level, set by 0x00472EF0, all cleared by ClearOnExitScripts
0x00472F00):
- **At `$`/return** of a level that has one: the field is cleared, then the script runs as an Interrupt
  **before** the level returns.
  - If it finishes in the nested Update: 5 comes back to the `$` handler, which ignores it. The level is
    restored with pc still at its `$`/return, and the outer handler goes on with its own ReturnFromScript, as
    normal.
  - **If it waits** **[quirk] (test)**: the outer handler's ReturnFromScript pops **the on-exit level instead**,
    so the on-exit script is **cut off at its first wait**. Bit 0 was cleared by the wait, so that return says
    "not interrupted", and the outer Update goes straight on with the original level, still at its `$`/return
    token. That token runs again, this time with no on-exit script, and returns properly. For `return`, the
    first pass's `++pc` and argument parse ran on the **on-exit script's pc** (its line after the wait), and
    those arguments are merged into the original level's parameters (test: an on-exit script's next line
    became return's arguments). The original level then parses its own arguments and returns them. Bit 1 stays
    set (section 3).
- **ClearScript** (0x00473A80) runs **RunOnExitScripts** (0x00473880) first: the current level's, then each
  saved level's **from the top down** (the depth is read once). Each field is cleared before its Interrupt, so
  each runs once. They run while the stopping script's state is still all there, pushed on top of it. One that
  waits is cut off when ClearScript then frees the stack, and nothing resumes it.
- Since SetScript, Restart and Destroy all start with ClearScript, all of them run pending on-exit scripts too.

## 8. Starting and stopping a script

- **SetScript(name, code, params, object)** (0x00474540; overloads 0x004747A0 by checksum and 0x004747C0 by
  string):
  1. Copy `params` if they are (inside) this script's own m_params or m_functionParams (CopyParamsIfOwned
     0x004739D0), because the next step frees those.
  2. ClearScript (which runs the on-exit scripts).
  3. Create new m_functionParams and m_params; set the name and the object.
  4. `m_pc = code` if given, else `GetScript(name)`.
     - **No code**: free the copy, ClearScript again, then ReportStopped. The script ends at once, and the
       report is the same as for a kill.
     - Otherwise parse the first line as the defaults, then AppendStructureIfAny(params) over them (later wins).

  **No statement runs**: the first statement runs at the next Update.
- **SetScriptCopy** (0x00474720): runs a private copy of a script held in a structure (an `SStructScript` from
  0x00476900, used by 0x0048B200). `m_codeCopy` is set after SetScript, so that SetScript's own ClearScript
  doesn't free it.
- **Restart** (0x004747F0): SetScript with the bottom level's name, a copy of its parameters, and its object (what
  the script was started with). The call stack is dropped and its on-exit scripts run.
- **ClearScript** (0x00473A80, "stop"):
  1. RunOnExitScripts.
  2. Free m_codeCopy.
  3. If `m_pc` was set, `ReleaseScript(m_scriptName)`. This happens **even for a private copy or for code passed
     to SetScript**, which never took a cache use, so a use someone else took is given back **[quirk]**.
  4. Clear pc; free m_params and m_functionParams.
  5. For every saved level, release its script and free its parameters.
  6. Go back to the inline stacks (heap blocks freed); `m_currentLoop = 0`.
  7. Clear wait counter, type, component and arg; release the object; clear the name; clear `m_flags` bits 0
     and 1.

  It does **not** clear m_onExceptionScript, the timer or timer-started, the spawn flags, the identity fields,
  or the handler table.
- **ReportStopped** (0x00473010), after a stop: the handler table forgets the script (0x0045FA60) and gets event
  0x8B713E0E ("script stopped", 0x0045FE60). With `TRACKER_BLOCKED`, the tracker forgets the script (0x00462B90).
- **Destroy** (0x00474420):
  1. ClearScript, then ReportStopped.
  2. Free the callback parameters (the pointer isn't cleared).
  3. Unlink; forget the script's stored randoms (0x0046F870).
  4. The handler table: if it is busy (+0x0C != 0), only its "alive" byte (+0x08) is cleared and whoever is
     running it frees it; otherwise it is destroyed and freed.
  5. `--count`.

## 9. Spawning and running scripts

- **SpawnScript(name, params, callback, callbackParams, node, id, net, permanent, notSessionSpecific,
  pauseWithObject)** (0x00475420; 0x00475670 takes the name as a string; the CFunction is 0x0059F330):
  - **A C function**: it is called at once as `fn(params, temp)`, with a temporary script made on the script
    heap and deleted afterwards. SpawnScript returns null.
  - **Anything else**, including a missing name: a new script at the head, `SetScript(name, null, params,
    **object = null**)`; `m_spawnId = id`; `m_node = node`; `m_spawnFlags = (old & 0xF1) | SPAWNED |
    nss<<1 | pwo<<3` (paused is cleared). With a callback: `m_callbackScript` is set and `m_callbackParams` is
    a **fresh copy**, an empty structure when no callback parameters are given.
  - **Network**: with `net` in a network game, message 0x49 is sent ({name, object id -1, node, permanent}, 16
    bytes); `permanent` is only used there.

  SpawnScript returns the script. A missing name still makes a script, which SetScript stops; it is deleted at
  its first Update, and its callback still runs **[quirk]**. Its first Update is the next UpdateSpawnedScripts
  that reaches it (next frame, when spawned from a script). The CFunction calls `SetSpawnerFile(spawner)` on
  the result (m_spawnerFile = the .qb file checksum of the spawner's bottom-level script, `FindSymbol(...)
  ->unknown8`, no null check), unless its flag 0x01917DF2 is given. So spawning a **C function** from script
  without that flag calls through null **[bug: crash]**.
- **RunScript(name, params, object, net)** (0x00475790; 0x004758F0 by string): only for symbol types script,
  C function and member function (anything else, or a missing name, does nothing). With `net`, message 0x48 is
  sent first ({name, object id or -1, params written to a 0x400-byte buffer}).
  - A member function runs directly with **script = null**: with an entry through 0x00472980, otherwise the
    object's vtable +0x0C. No null check on the object **[bug: crash with no object]**.
  - A C function runs directly as `fn(params, **null**)`.
  - A script: a new script (with the script heap pushed around new + Construct + SetScript), then
    **`while (Update() != FINISHED) {}`**, then delete. Everything is **synchronous**, and every loop turn is one
    Update, so **waits count loop turns, not frames**:
    - `Wait n` takes n extra turns;
    - a timer wait subtracts the same, unchanging slomo frame time each turn, so it ends after about t/dt turns
      with no real time passing;
    - a real-time wait busy-waits on the clock;
    - one-per-frame waits, BLOCKED waits and component waits that never say done spin for ever: **the game
      hangs** **[bug]**;
    - a 5 just loops again.

  Callbacks of spawned scripts go through RunScript, with the same consequences.
- **RunScriptOnce(name, params, object, results)** (0x004752F0): a new script, SetScript, **one** Update, then
  `results->AppendStructure(m_params)` (the script's whole parameter set: defaults, arguments, assignments,
  C function outputs and return's results; `results` isn't null-checked), then delete whatever state the
  script is in. A wait means it is cut off there, and Destroy runs its on-exit scripts.

## 10. The manager's walks

All of these go head to tail. "Stop" means ClearScript followed by ReportStopped (inlined in the walks).
Stopped scripts stay on the list.

| function | which scripts | `next` read |
|---|---|---|
| KillSpawnedScriptsThatReferTo(name) 0x00473FA0 | spawned, with `name` at any level (ScriptRefersTo 0x00472C50) | before the stop |
| KillSpawnedScriptsWithId(id) 0x00474040 | spawned, with spawn id, unique id **or spawner file** == id | before |
| KillSpawnedScriptsWithObjectAndId(obj, id, except) 0x004740D0 | spawned, not `except`, obj at any level, spawn id == id | before |
| KillSpawnedScriptsWithObjectAndName(obj, name, except) 0x00474190 | spawned, not `except`, obj at any level, top-level name == name | before |
| StopAllScripts 0x00474250 | **every** script (spawned or not) | **after** |
| StopScriptsUsingObject(obj, name) 0x004742B0 (StopAllScriptsUsingThisObject 0x004756F0 = name 0) | every script with obj at any level, and (name 0 or top-level name == name) | after |
| StopScriptsUsingObjectThatReferTo(obj, name) 0x00474350 | every script with obj at any level, and (name 0 or name at any level) | after |

- A null `obj` matches every level that has no object. Reading `next` before or after only matters if an on-exit
  script run by the stop changes the list; the original's order is kept.
- `KillSpawnedScriptsWithId` with a .qb file's checksum kills every script spawned by that file's scripts. The
  front end's element code (0x00494E40, 0x00496590, 0x004963B0) passes the name of the script it spawned
  (which has spawn id 0 and no spawner file), so only children that script code spawned with `id = <that name>`
  match. The element's own script does not.
- Other walks:
  - **KillSpawnedScript(s)** 0x004756C0: deletes s at once if it is spawned (see the hazard in section 2).
  - **DeleteSpawnedScripts** 0x004753C0: section 2.
  - **DeleteFinishedScripts** 0x00475700 (Cleanup): deletes non-spawned scripts that have no code and no handler
    table.
  - **PauseSpawnedScripts(pause)** 0x00472D80 and **UnpauseSpawnedScript** 0x00472DC0: the PAUSED bit of
    spawned scripts.
  - **FindSpawnedScriptWithId** 0x00472E20 (first match from the head), **GetSpawnId** 0x00472E00 (0 when not
    spawned), **ScriptExists** 0x00472DE0 (the symbol is a script), **FindScriptWithUniqueId** 0x004734B0.

## 11. Events and event handlers

- A script's handlers live in its table `m_eventHandlers` (0x10 bytes on the front-end heap: {entries, count,
  +8 alive, +9 changed, +0xC in use}). An entry (0x14 bytes) is {type, script, group, params, flags: bit 0
  exception, bit 1 marked}. The CFunctions:
  - SetEventHandler / SetException (0x0044AB20 / 0x0044ABD0) use 0x004735D0, which adds or replaces one entry
    (0x0045FBC0).
  - 0x004734E0 (from 0x00461710) adds an array of handler structures (0x004601A0, with `replace`), then drops the
    removed entries and re-registers the table's types with the tracker.
  - ResetEventHandlersFromTable (0x0044AC70) uses 0x004736C0: it marks the group, then adds each element's
    {flag, type, script, [params]} (the first component compared with `exception` 0x80367192; fewer than three
    components is read through null **[bug]**), then removes the marked entries that weren't added again.
  - ClearEventHandler (0x0044ACD0) uses 0x00473690: remove a type, and the tracker stops listening.
  - ClearEventHandlerGroup (0x0044AD00) uses 0x00472EA0; group 0x8B713E0E means all.
  - PrintEventHandlerTable (0x0044AD90) uses 0x00472EC0.
- The **tracker** (0x006F0FC0) maps event types to listening scripts: Block(type) and the handler types.
- An event reaches a script through ReceiveEvent (section 5): first the blocked levels are released, **then**
  the table's 0x00460470 runs the matching handlers. As far as the plan could tell they run as Interrupts on the
  receiving script, or as spawned scripts (the table's code is still the game's; section 12). An Interrupt
  handler runs at once, nested inside whatever sent the event, even in another script's Update.
- A stopped script sends its own table event 0x8B713E0E (ReportStopped).

## 12. Open questions

- **The event handler table's dispatch** (0x00460470, 0x0045FE60; still the game's): exactly when a handler
  runs as an Interrupt or as a spawned script, what `exception` handlers change, and what the "in use" / "alive"
  protocol does when a handler deletes its own script. These need decompiling before the port's event model is
  final.
- **Who updates object scripts, and when** relative to UpdateSpawnedScripts (0x0054F380) in the frame. This
  decides the one-per-frame flag order and which frame a cross-script event lands in.
- **The CFunctions that set waits** (Wait / WaitTime / Block / the object waits, and how their arguments map to
  frames vs seconds vs real time): outside this batch.
- **m_onExceptionScript** (OnExceptionRun, +0xE4): set and read by accessors only. Who runs it?
- **Spawned scripts' object**: SpawnScript always passes object null, yet PAUSE_WITH_OBJECT needs one. Who sets
  `m_object` on a spawned script (the CFunction 0x0059F330 after the call, or object member functions)?
- **m_refCounted** (+0xEC): which smart pointers point at scripts, given that its vtable is null.
- **0xFB11E6CD**: presumably the default level's script (missing calls are silent there). Not confirmed.
- **Untested, from the code only**: bit 1 staying set after a nested wait (section 3), the interrupt that calls
  a waiting script returning 5 later (section 7), and RunScript spinning on one-per-frame waits.
- **The 0x49 message's last 3 bytes** are uninitialised stack in the original (ours zeroes them), and MsgDesc
  bytes 0x12-0x13 are never set. The meaning of MsgDesc +0x0C = 2 and +0x10 = 8 is unknown.
