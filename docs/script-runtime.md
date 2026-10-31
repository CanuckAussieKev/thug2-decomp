# The QB script runtime: how scripts are scheduled and run

This is the behaviour spec for replacing the QB interpreter with embedded JavaScript (QuickJS), where scripts
become generators driven by a scheduler (the project's long-term plan, see the README's Status). A port has to reproduce what is described
here. It was put together from the CScript batch (the plan and the records of
groups A-D), and the code was checked wherever they disagreed. The code is `src/game/script/cscript.h` (class,
fields, flags) plus `cscript_core.cpp` (A), `cscript_exec.cpp` (B), `cscript_wait.cpp` (C) and
`cscript_manager.cpp` (D). For the script cache (GetScript / ReleaseScript use counting) see "What the cache
does" (batch 70). For the token format see `src/game/script/qb.h`. Sections 11 and 12
(events, objects) come from batches 75-77 (`src/game/obj/event.*`, `tracker.*`, `object.*`, `objman.*`), the
front end's part of the frame (section 2) from batch 78 (`src/game/front/front_end.*`), the frame order, the
object walk, pausing objects and composite objects (sections 2 and 12) from batches 79-81
(`src/game/obj/compositemgr.*`, `composite.*`, `src/game/lst/head.*`).

Conventions in this document:
- "Update" means one call of `CScript::Update` (0x004750B0). A "count" is one call of `UpdateWait` (0x00473050).
- **[quirk]** means original behaviour a faithful port must reproduce. **[bug]** means a crash, hang or
  use-after-free in the original. A port should not reproduce these, but it must not change what happens in
  the cases that work.
- "(test)" means an offline self-test compared original against ours for that case (during the decompilation). "(code)" means it was read
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
`m_refCounted` (+0xEC) is what smart pointers to a script point at: LaunchEvent takes them around a broadcast
(section 11), and Destroy clears them.
`m_uniqueId` is the counter's value before the increment.

## 2. The frame loop: UpdateSpawnedScripts (0x00475920)

It is called once per frame from the menu front end's Update (0x0054F380, see "Where it runs" below). Scripts
owned by objects (not spawned) are not updated here: their objects update them, earlier in the same frame (the
object walk, "The whole frame" below; section 12).

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

**Where it runs: the menu front end** (Mdl::FrontEnd, src/game/front/front_end.cpp; batch 78) (code + test).
The front end's logic task (0x0054FCC0, priority -2000 on the main loop's logic task list) pushes the front-end
heap and calls `FrontEnd::Update(paused)` (0x0054F380), then the screen element manager's update (0x004ABD00 on
0x00701440). Update, in order:
1. Works out the script globals "menus active" (paused, in the skateshop level unless overridden, or the
   on-screen keyboard up), "keyboard up" and "menu up" (by anchor ids: keyboard_anchor, current_menu_anchor,
   dialog_box_anchor, quit_dialog_anchor).
2. `Pcm::Update`, then the sound updates on 0x006F85A8 (0x0047B000, and 0x0047C560 unless paused; read in Ghidra -
   front_end.cpp's comment calls it "the object manager", but both walk sound voices / streams).
3. **UpdateSpawnedScripts** (on the bottom-up heap).
4. The skate module's 0x00564D60, then, if a pending flag is set, `RunScript(0xFB11E6CD, null, the first
   skater, false)` - synchronous (section 9).

Pausing (PauseGame 0x0054F960) sets the PAUSED bit of every spawned script (PauseSpawnedScripts), pauses the
skaters, the composite objects (0x0045F5D0 on the composite object manager 0x006F0FB8, "Pausing objects" below)
and the screen elements, and sets the main loop's logic list mask (Mlp::Manager +0x08) to 0xC, which **skips** the
logic tasks whose kinds (+0x14) have bit 2 or 3 set (see "Task lists" below; earlier versions of this document had
it backwards). Neither the front end's logic task nor the object walk's task has kinds, so both still run while
paused; paused spawned scripts with code are skipped by the walk (step 3 above). Pad input is turned into events
by the front end's input handlers (section 11, "Pad input").

### The whole frame (code; the list behaviour also test)

**The main loop** (Mlp::Manager::MainLoop 0x0044EEF0) runs, every frame, the **system** task list
(Mlp::Manager +0x7C), then the **logic** task list (+0x04), then renders.

**Task lists** (add 0x00406C90 -> 0x00406A80 -> Lst::Head::AddNode 0x0045A540; run 0x004070A0 -> 0x00406AC0):
- Tasks are kept by priority, **highest first**; a task added with the same priority as others goes **before**
  them (newest first) - AddNode inserts before the first node whose priority is <= its own (test).
- A run goes from the front and runs each task **at most once** (the run's counter is stamped into the task,
  +0x10). If a task is added during the run (the list's +0x1C set), the walk starts again from the front,
  skipping tasks already stamped.
- A task is skipped when its +0x04 is non-zero, or when the list's mask (the list's +0x04; the logic list's is
  Mlp::Manager +0x08) shares a bit with the task's kinds (+0x14): a task runs only when `(mask & kinds) == 0`
  (0x00406B61-0x00406B6D). Tasks made by the constructor 0x00406890 have kinds 0 and are never masked.

**The frame, in order:**
1. **System tasks**, among them each CGeneralManager's FlushDeadObjectsTask (0x00461BB0, priority 1000): objects
   marked dead during the previous frame are deleted here, before any of this frame's logic (section 12).
2. **Logic tasks**, highest priority first: the input manager's task (priority 2000; made at 0x0044E76A, added by
   Sys::Main; the front end's pad handlers run in it, section 11.5) ... **the composite object manager's task
   (0x0045F6D0, priority -900): the object walk** (below) ... **the front end's logic task (0x0054FCC0, priority
   -2000): FrontEnd::Update -> UpdateSpawnedScripts** (above). Other logic tasks are not all placed yet - e.g. the
   skate module's two tasks of priority -1000 made at 0x0056E2B0 run between the object walk and the front end
   (section 13).
3. **Render.**

Consequences (code):
- **Objects' own scripts run before the spawned scripts every frame.** Whatever an object's script does in a frame
  (events it launches, globals it sets) happens before that frame's UpdateSpawnedScripts; what spawned scripts do
  reaches an object script's own statements only at its next Update, next frame (an event handled at once, section
  11, is the exception: its handler runs inside the spawned script's statement).
- A script **spawned during the object walk** (by an object's script or a component) is at the head of the list
  before UpdateSpawnedScripts starts, so its **first Update is in the same frame** (unlike a script spawned during
  UpdateSpawnedScripts, which waits for the next frame).
- "One-per-frame released" (0x006F352C) is cleared at the start of UpdateSpawnedScripts, after the objects have
  run: an object script's one-per-frame wait (section 5) sees the flag as the previous frame's spawned walk left
  it.
- `PAUSE_WITH_OBJECT` spawned scripts are checked against their object's vtable +0x1C in UpdateSpawnedScripts,
  after the object has updated this frame.

### The object walk (CCompositeObjectManager::Update 0x0045F4A0) (code + test)

- Every object on the composite object manager's list, **in list order: node priority highest first, equal
  priorities newest first** (CBaseManager::RegisterObject -> AddNode 0x0045A540). A new object's node priority is
  0 (CObject's constructor); CBaseManager::SetObjectPriority moves it.
- It is CBaseManager::ForEach's walk (section 12) with the work inline: one bit of the manager's +0x18 per walk in
  progress (nested walks take the next free bit), every object's bit cleared first, the next object looked up
  **before** the current one updates. When the list changes during an update (an object registered or
  unregistered: the manager's +0x1C set), the walk starts again from the front, skipping objects already updated.
  So an object **created during the walk is updated in the same frame**; an object unregistered (e.g. marked dead)
  before the walk reaches it is not; a list node with no object ends the walk.
- Each object gets one CCompositeObject::Update (0x0045D920): its own script, its vtable +0x28, then its
  components (section 12).
- **Other places an object's update runs** (code): Obj_ForceUpdate (0x0045DC40, a script member function: the
  whole update, script included, nested inside the calling statement - re-entrantly when the object's own script
  calls it); 0x00501600 (a skater component's member function 0x8819DD8B: after moving the skater it calls
  SetTeleported(true) and, when the object's +0xBC byte is set and 0x00580880 returns non-null, updates the object
  at once); 0x00583DA0 (vtable 0x0064EF48's class, on a restart: the object named 0x967C138C or 0xE17B231A - a
  camera, by the look of it - gets SetTeleported(false) and an immediate update). An object updated there runs its
  script **twice** that frame.

### Pausing objects (code + test)

- `CCompositeObjectManager::Pause(bool)` (0x0045F5D0) sets or clears composite flag +0x104 bit 0 (PAUSED) on
  **every composite object that exists at that moment** (the same walk). Callers: PauseGame (0x0054F960) and the
  script commands PauseObjects / UnPauseObjects (0x005A0620 / 0x005A0630). The member functions Pause / Unpause
  (0x0045E9B0 / 0x0045E9D0) set or clear it on one object.
- The object walk's task isn't masked, so it still runs every frame; a paused object's update returns at once -
  **its script doesn't run and its components don't update**.
- `PAUSE_WITH_OBJECT` for a composite object means "while the object is paused": its vtable +0x1C is 0x0045E500
  (`!(flags & PAUSED)`); CObject's own (0x005B9760) is always true.
- [quirk] An object created while paused is **not** paused (Pause only touches the objects it walks). Unpausing
  clears the bit on every object - also on objects paused by PauseObjects / Pause before the game was paused.

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
  scripts are updated earlier in the frame (the object walk, section 2), so they see the flag as the previous
  frame's spawned walk left it: if a spawned script was released then, no object script is released this frame;
  if not, the first object script in walk order to reach 0 is - and the spawned walk clears the flag again anyway
  (code).
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

  `Block(0)` can only end by one of the last three. `Block(type)` also puts the script on the tracker's list
  for that type, and **it stays there after the block ends** (section 11). WaitForEvent is `Block(0)` plus a
  tracker slot (section 11). IsBlocked (0x00472900) is `type == 3`; with `print` it prints
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
`$`/return OnExitRun step, RunOnExitScripts, CObject::CallScript 0x00461500 (section 12), 0x005228B0 and
0x00522670.

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
- **ReportStopped** (0x00473010), after a stop: if the script has a handler table, the tracker forgets the
  script for every entry's type (UnregisterWithTracker 0x0045FA60) and **every handler is removed**
  (RemoveGroup(all_groups 0x8B713E0E) 0x0045FE60: entries become null_script, parameters freed; the array stays
  until a Compact). With `TRACKER_BLOCKED`, the tracker forgets the script for every type (ForgetScript
  0x00462B90; the bit itself stays set). So a stopped script (ClearScript + ReportStopped: Destroy, the
  kill/stop walks, SetScript with no code) loses its handlers, but SetScript of real code - a script starting
  another, Restart, an exception handler - does not call ReportStopped, and the handlers survive (code + test).
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
  - The new script has **no object**. The callers that want one set `m_object` themselves right after:
    CObject::SpawnScript / SpawnAndRunScript / Obj_SpawnScript (section 12) and the spawning event handlers
    (section 11).

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

The code: `src/game/obj/event.*` (the event, event listeners, the handler table; batch 75), `tracker.*` (the
tracker, LaunchEvent, the event CFunctions; batch 76), and CScript's handler methods in `cscript_wait.cpp`.
Everything here runs **synchronously**: an event is delivered, and its handlers run, inside the call that launched
it. Nothing is queued for a later frame (test).

### 11.1 The pieces

- **The event** (`Obj::CEvent`, built on the stack by LaunchEvent through CEvent::Construct 0x0045F7F0):
  `{type, source id (+4), target id (+8), flags (+0xC), data (+0x10)}`. Flags: bit 1 HANDLED (set by MarkHandled
  0x0045F9B0, which also logs it), bit 2 the event owns `data` (freed by CEvent::Destroy 0x00460150; LaunchEvent's
  events never own it). 0x36B2EE74 is the "no object" id (`kNoObjectId`, name unknown): a target or source of
  none.
- **The tracker** (`Obj::CTracker`, singleton 0x006F0FC0) keeps:
  - objects by id (+0x08) and their aliases (+0x0C) (section 12);
  - **listener lists**: for each event type, the scripts listening for it (+0x10), in the order they started
    listening, each script at most once; an emptied list is deleted (test);
  - **WaitForEvent slots**: 40 of `{script unique id, type, the script's object id or 0}` (+0xB038) (test);
  - **event listeners** (+0x14): C++ objects (`CEventListener`, vtable 0x006477D8) that see every targeted event
    through their vtable +0x04 (0x004A1090; who registers them is open, section 13) (code);
  - the event log (+0x20, below).
- **The handler table** (`Obj::CEventHandlerTable`, a script's `m_eventHandlers`, 0x10 bytes on the front-end
  heap, created on demand): `{entries, count, alive (+8), changed (+9), in use (+0xC)}`. An entry (0x14 bytes) is
  `{type, script, group, params (an owned copy), flags}`; flags bit 0 exception, bit 1 marked; bits 2-7 are never
  initialised (garbage in new entries) **[quirk, harmless]**. A **removed** entry keeps its slot: its script
  becomes `null_script` (0xC377C572) and its parameters are freed. Only `Compact` (0x0045FF50) drops removed
  slots (it builds a new array on the front-end heap) (code + test).

### 11.2 Handlers: adding, removing, registering

The CFunctions and the CScript methods they use:

| script command | CScript method | table function |
|---|---|---|
| SetEventHandler / SetException (0x0044AB20 / 0x0044ABD0) | 0x004735D0 | AddHandler 0x0045FBC0 |
| SetProps event_handlers=[...] [replace_handlers] (CObject::SetProperties 0x00461710) | SetEventHandlers 0x004734E0 | AddHandlers 0x004601A0 |
| ResetEventHandlersFromTable (0x0044AC70) | 0x004736C0 | MarkGroup 0x0045F840, AddHandler, RemoveMarked 0x0045FED0 |
| ClearEventHandler (0x0044ACD0; also CObject 0x004607A0) | 0x00473690 | RemoveType 0x0045FDF0, then the tracker's RemoveListener |
| ClearEventHandlerGroup (0x0044AD00) | 0x00472EA0 | RemoveGroup(script, group) 0x004600E0 |
| PrintEventHandlerTable (0x0044AD90) | 0x00472EC0 | PrintTable 0x0045F880 |

- **AddHandler** (from SetEventHandler / SetException / ResetEventHandlersFromTable): the entry of the **same
  type** is overwritten in place (even a removed one), else the first removed slot is reused, else the array
  grows by one (the new entry last). The old parameters are freed, the new ones copied, the mark cleared. The
  tracker is told the script listens for the type unless the overwritten entry was live. Sets `changed` (test).
- **AddHandlers** (SetEventHandlers): the elements are `{type, script, [params = {...}], [group = g] (default
  `default` 0x1CA1FF20), [exception]}` (type and script by position). **The new entries go in front of the old
  ones, in array order.** With `replace` the old entries of each new type are removed first; two entries of one
  type are possible only without `replace`, or from two elements of one array. An empty array with `replace`
  frees every entry at once. SetEventHandlers unregisters the whole table from the tracker before, and compacts
  and re-registers it after. Sets `changed` (test).
- **ResetEventHandlersFromTable**: marks the group's live entries, adds each element's `{flag, type, script,
  [params]}` (the first component compared with `exception` 0x80367192; fewer than three components is read
  through null **[bug]**), then removes the entries still marked (AddHandler clears the mark of an entry it
  overwrites) (code + test).
- **Removing** by type (RemoveType, which also zeroes the type; ClearEventHandler then tells the tracker), by
  group (RemoveGroup 0x0045FE60 quietly, 0x004600E0 telling the tracker; `all_groups` 0x8B713E0E matches every
  group) or by mark. Removing never sets `changed` (test). An entry's `group` only matters for removal.
- **Tracker registration**: RegisterWithTracker (0x0045FA10) is one `AddListener(type, script)` (0x00463370) per
  live entry with a type and script; UnregisterWithTracker (0x0045FA60) one `RemoveListener` (0x00463450) per
  entry with a type, removed ones included (test).
- **Block(type)** with type != 0 (CScript::Block 0x00472FD0) also calls AddListener and sets `TRACKER_BLOCKED`.
  **The registration outlives the block [quirk]**: nothing takes the script off the type's list when the block
  ends, so until it stops (or a ClearEventHandler of that type) it keeps getting broadcasts of the type, which
  then only reach its handler table (code).
- **A stopped script loses everything** (ReportStopped 0x00473010, section 8): the tracker forgets the table's
  types, every handler is removed, and with `TRACKER_BLOCKED` the tracker forgets the script for every type
  (ForgetScript 0x00462B90). SetScript of real code (a script starting another, Restart, an exception handler)
  doesn't report a stop, so handlers survive it (code + test).

### 11.3 LaunchEvent: who gets an event

**LaunchEvent(type, target, source, data, broadcast, radius)** (0x00462F00) (test):
1. **Targeted** (not `broadcast`):
   1. Every **event listener**, in list order (its busy count +0x18 raised around the call).
   2. Then, unless a listener marked it HANDLED or the target is none (0x36B2EE74): the **object** with that id
      or alias gets it through its vtable +0x14 - for a plain CObject that is PassEventToScript (0x00461140):
      its own script's ReceiveEvent (section 12); else the **spawned script** whose spawn id is the target gets
      ReceiveEvent; if there is neither, the game prints the event log and calls ReceiveEvent on null
      **[bug: crash]**.
   **The listener lists are not used**: a targeted event never reaches a script just because it listens for the
   type (code). Composite objects (and the skater, ped and level objects built on them) keep CObject's +0x14, so
   the event reaches only the object's own script, never its components; only screen elements override +0x14
   (section 12).
2. **Broadcast**: every script on the type's listener list, in the order they started listening, gets
   ReceiveEvent (0x00473800). With `radius` >= 0, a script whose object has a position (CObject +0x08 bit 0x20,
   set by CCompositeObject's constructor)
   gets it only if its squared distance from the **source** object (looked up by id, not checked for null
   **[bug]**) is less than radius squared (equal: not delivered); scripts without an object, or whose object has
   no position, always get it; a NaN radius means none. Event listeners and the target are not involved.
   With two or more listeners, smart pointers to each script's `m_refCounted` are taken first, so a script
   **deleted by an earlier receiver's handlers is skipped**; scripts that start listening during the delivery
   don't get it. With exactly one listener there is no such protection (its script is called directly) (test /
   code).
3. **Then the WaitForEvent slots**, after and regardless of the above: the first slot in slot order with this
   type whose object is the target, or that has no object, or the event has no target, or is broadcast, is freed;
   if its script still exists it stops waiting (StopWaiting) and the event is marked HANDLED (`how` 2, the
   script's name), and the search ends - **one event releases at most one WaitForEvent waiter [quirk]**. Slots
   whose script has gone are freed on the way and the search goes on (test).
4. It returns whether the event was marked HANDLED (by a handler, an event listener or a waiter); when nobody
   handled it, the log gets a HANDLED entry with `how` 3 (test).

So a script blocked with `Block type=X` is released by a broadcast of X, by an X targeted at its spawn id (a
spawned script), or by an X targeted at its object if it is that object's own script (through
PassEventToScript; screen elements handle targeted events their own way, section 12). A spawned script running on an
object does **not** get events targeted at the object (code).

**One script, ReceiveEvent** (0x00473800, section 5): first the levels blocked on the type are released (the
current one and saved ones), then, if the script has a table, `PassEvent(event, script)` (11.4). Always true.

**The CFunctions** (test):
- **LaunchEvent / FireEvent** (0x004634D0): `type` is a name, an array of names or a structure of names (each
  launched in turn with the same arguments; no type: nothing); `target` and `source` default to the calling
  script's object (none without one; `target=<the none id>` gives none); `data` a structure (not copied: the
  event doesn't own it); `broadcast` a flag; `radius` a float (default -1 = none).
- **WaitForEvent type=X** (0x00462310 -> 0x00462260): takes the first free slot, else the first whose script has
  gone; if all 40 are held by live scripts it **silently doesn't wait [quirk]**. Then `Block(0)`: the script is
  blocked with no type, so it is on no listener list and ReceiveEvent doesn't release it; only LaunchEvent's
  waiter step (or StopWaiting / ObjectDestroyed / UnblockInterruptedScript) does. The slot keeps the script's
  object id from when it started waiting (an object rename moves it, section 12). A missing `type` is read
  uninitialised in the original (ours: 0). A WaitForEvent script that also has a handler for X gets ReceiveEvent
  first only if X is broadcast (it listens through its table), and is then released by the waiter step (code).
- **The object CFunctions** - AssignAlias, ObjectExists, SetObjectProps, TerminateObjectsScripts: section 12.
- **The C++ senders** seen so far: CObject::SelfEvent (0x004609F0: targeted at the object itself, source itself),
  CObject::BroadcastEvent (0x00460A20: broadcast with source the object; **its radius argument is ignored**, -1 is
  passed [quirk]), the skater's physics (core_physics.cpp), and the front end's pad input (11.5).
- **The event log** (+0x20): a ring of 1024 entries {type, target, source, script, how, kind, depth}: LAUNCHED (0),
  HANDLED (1, how 0 a handler / 2 a waiter / 3 nobody), READ (2, 0x00462B40), FRAME (3, one per call of LogFrame
  0x00462B70, consecutive ones counted in one entry), OBJECT_ADDED / OBJECT_REMOVED (4 / 5). PrintEventLog
  (0x00462480) prints only the header and footer in this build. It doesn't affect scheduling (code + test).

### 11.4 PassEvent: how a script's handlers run

**PassEvent(event, script)** (0x00460470) (test):
1. `wasChanged = changed; changed = false; ++inUse`. The entry pointer is taken **once**.
2. For each entry **in table order** (the count re-read each time), if its type is the event's and it isn't
   removed:
   - The parameters: the entry's own; or, if the event has data, a **new structure = the entry's parameters,
     then the event's data appended (the data wins)**, freed after the handler.
   - **Exception handler** (bit 0): if the script has an OnExceptionRun script (`m_onExceptionScript` +0xE4), it
     is **cleared** and run first as an `Interrupt` with an unnamed checksum `1` as its only parameter. Then, if
     the entry wasn't removed meanwhile, **`SetScript(handler, params, the script's object)` + one `Update`**: the
     handler **replaces the whole script** (ClearScript drops the call stack and runs its OnExitRun scripts; the
     handler table and m_onExceptionScript survive). It is a "goto", not a call: nothing resumes what the script
     was doing.
   - **Normal handler, the script's object has flag 0x10000** (CObject +0x08; what sets it is open):
     `SpawnScript(handler, params)` (no callback, id 0, not networked), the new script's `m_object` set to the
     receiving script's object, and **one Update at once**; later Updates come from UpdateSpawnedScripts. The
     receiving script is not disturbed. A C-function handler returns no script, which is used anyway **[bug:
     crash]**.
   - **Normal handler otherwise**: `script->Interrupt(handler, params)` - the handler runs **now**, nested, on the
     receiving script (section 7), over whatever level was running; it returns into that level (which keeps its
     wait) or, if it waits, stays on top as an ordinary called level.
   - Then the event is marked HANDLED and logged (`MarkHandled(0, entry.script)`, the entry re-read).

   After every entry (matched or not):
   - if the table is no longer **alive** (a handler destroyed the script: CScript::Destroy only clears `alive`
     while `inUse != 0`, section 8): `--inUse`, and if that was the outermost PassEvent the table is destroyed and
     freed; return;
   - if **changed** (a handler *added* entries, which may have moved the array): `--inUse` and **stop** - the
     remaining entries don't run for this event, and `changed` stays set.
3. At the end: `changed = wasChanged; --inUse`.

Consequences for a port:
- **Every** matching handler runs, in table order; matching doesn't consume the event, and nothing in the table
  stops it reaching other scripts. A type normally has one entry, so in practice one handler per event per
  script.
- Handlers removed by an earlier handler (or by the OnExceptionRun script) are skipped; handlers **added** by a
  handler end this event's walk at once (the new ones don't see it). `changed` is also a re-entrancy signal: a
  nested PassEvent that added entries stops the outer one too.
- Handlers run synchronously inside whatever sent the event - possibly inside another script's Update or a C
  function. A handler that waits leaves the receiving script running the handler at its next Update.
- `exception` is the only per-entry behaviour switch; object flag 0x10000 switches all of a script's normal
  handlers to spawning.
- [bug, harmless while freed front-end memory isn't reused] after a handler added entries, the entry is re-read
  from the old (freed) array for the log; the exception path re-reads it after the OnExceptionRun interrupt the
  same way.

### 11.5 Pad input: how the menus get events

The front end (section 2) has an input handler per controller (InputLogic 0x0054FB40, an `Inp::Handler`, priority
-1000, run by the input manager) (code + test). Each call first logs a FRAME in the tracker's log, then loops:
the auto-repeat event of held directions (0x0054ECE0, first pass only) or the n-th pressed (button, event) pair
of SetButtonEventMappings (GrabControlEvent 0x0054F600; Start is `pad_start`; with the on-screen keyboard up,
controller 0's PC keys Escape -> `pad_back` / `pad_start`, Enter / Space -> `pad_choose`), until no event is left.
Unless the controller is inactive (SetMenuPadMappings) or menu input is blocked (`block_menu_input`), each event
is launched at once as a **targeted event with target and source "none"** (0x36B2EE74) and data `{device_num,
controller = 0}`. So pad events reach only the **event listeners** and the **WaitForEvent waiters** of the type -
never a script's handler table directly, and never Block(type) scripts (11.3). Which listener turns them into
menu actions (presumably the screen element manager's, 0x004A1090) is open (section 13).

## 12. Objects and their scripts

The code: `src/game/obj/object.*` (`Obj::CObject`, 0x4C bytes, vtable 0x006477E8; CCompositeObject's own fields
start at +0x4C) and `objman.*` (CBaseManager / CGeneralManager); batch 77. Composite objects: `composite.*`
(batch 80) and `compositemgr.*` (the composite object manager, batch 79).

- **Name -> object** (code): `name:` in a statement (section 4.2) and every CFunction that takes an object go
  through FindObject (0x00460AD0) or CBaseManager::GetObjectByID (0x004619F0, the same lookup, `this` unused) to
  the tracker's LookUpObject (0x00462120): by **id** first (+0x08), then by **alias** (+0x0C). Not found: null.
  An object's id is its name checksum, also kept as its tag `id` (its type as tag `type`).
  - **Ids** (code + test): an object registered with id -1 gets a unique one (GetUniqueId 0x004621D0:
    `autoid%d` checksummed, the counter wrapping at 1000000, retried while taken). SetID on a registered object
    re-registers it under the new id. AddObject (0x00462980) drops an alias equal to the new id; RemoveObject
    (0x00462A20) drops the object's aliases, or, when the object is being renamed, moves WaitForEvent slots to
    the new id (test).
  - **AssignAlias id=X alias=Y** (0x00462C40): Y names X from now on (an alias naming another object is moved;
    objects count their aliases at +0x3C). A missing alias is read uninitialised (ours: 0). **ObjectExists id=X**
    (0x00462340): X is an object id or alias. **SetObjectProps id=X ...** (0x00462440): the object's vtable +0x04
    (SetProperties) gets the parameters. **TerminateObjectsScripts id=X [scripts=[...] | script_name=Y]**
    (0x00462380): StopScriptsUsingObjectThatReferTo (section 10) for each name (no name: 0). None of them checks
    for an unknown object **[bug: crash]** (test).
- **An object's own script is not spawned** (code): an object has at most one script of its own (+0x10), made on
  demand: CreateScriptIfNeeded (0x00461290, starts it on `empty_script`), SwitchScript (0x00461350: SetScript
  with the object - the first statement at its next update), CallScript (0x00461500), and SetProps
  `event_handlers=` (SetProperties 0x00461710). It never goes through UpdateSpawnedScripts: it gets one Update
  per frame inside its object's update (CCompositeObject, below), which the object walk runs **before**
  UpdateSpawnedScripts (section 2, "The whole frame"; other callers of the update there too).
- **CallScript on an object** (0x00461500, code + test): `Interrupt(name, params)` on the object's script (made
  if needed, its object set): the script runs **at once**, nested over whatever the object's script was doing
  (section 7) - not a new script.
- **Spawning on an object** (code + test): CObject::SpawnScript (0x00461410) spawns with node -1, the given id and
  `PAUSE_WITH_OBJECT` as asked, then sets the new script's `m_object` to the object. The member function
  **Obj_SpawnScript** (0x004617E0): the first parameter must be a bare name (else nothing, true); `id` defaults to
  the calling script's spawn id; `params=` are its parameters; `PauseWithObject` a flag; unless 0x01917DF2 is
  given the new script records the caller's .qb file (SetSpawnerFile) - a C function's name returns no script and
  crashes there **[bug]**. SpawnAndRunScript (0x00461480) does the same with node / no id, gives the script **one
  Update at once**, and with `netEnabled` sends the network message with **no object** [quirk]; a C function
  crashes it too [bug]. Spawned scripts on an object are ordinary spawned scripts: UpdateSpawnedScripts updates
  them (skipped while `PAUSE_WITH_OBJECT` and the object's vtable +0x1C says so, section 2), and they don't get
  events targeted at the object (section 11.3). **Obj_KillSpawnedScript** (0x00460850): `name=` kills the
  object's spawned scripts with that top-level name, else `id=` those with that spawn id, never the caller
  (section 10). **Obj_SwitchScript** (0x004618A0): SwitchScript to the unnamed name with `params=`.
- **Events to an object** (code + test): CObject's vtable slot 5 (+0x14, PassEventToScript 0x00461140) hands a
  targeted event to its own script's ReceiveEvent, holding a smart pointer to the object; it returns false if the
  object was deleted meanwhile, and true (nothing happens) with no script. **CCompositeObject (vtable 0x00647650)
  and its three subclasses (vtables 0x0064EC44, 0x0064EF48, 0x0064F138: the skater, ped and level-object kinds,
  constructors around 0x0057826D, 0x00582DC3, 0x0058987F) keep 0x00461140**, so an event targeted at a composite
  object goes to that object's own script only - **never to its components** (code). Of the 21 vtables with
  CObject's slot 2 (0x0045D850), the only ones with a different +0x14 are screen elements: 0x004A8100 (vtable
  0x0064A048), 0x004AA560 (0x0064A0BC), 0x004AE6A0 (0x0064A154 / 0x0064A188 / 0x0064A1BC) - they handle targeted
  events themselves (not decompiled; likely where the menus' pad events become actions, section 11.5).
- **Obj_LeaveScript** (0x00460840; name checksum 0xB3C262EC, real name unknown): `script->ObjectDestroyed(this)` on
  the calling script - the object is dropped from all its levels and its current wait ends (section 5) (test).
- **Dying** (code + test): MarkAsDead (the member function `Die`, vtable slot 6, 0x00460A50): the object's own
  script is **blocked for good** (`Block(0)`), every script using the object is stopped
  (StopAllScriptsUsingThisObject, section 10 - their on-exit scripts run, they lose their handlers), the object
  is unlocked, unregistered (the tracker forgets it: FindObject stops finding it at once), put at the **front** of
  its manager's dead list and flagged dead, and the smart pointers to its +0x40 target are cleared. It is deleted
  later by the manager's task (FlushDeadObjectsTask 0x00461BB0, priority 1000 on the main loop's system task
  list), dead-list front first - so a dead object stays in memory until that task runs that frame. Deleting
  (DestroyBody 0x004615D0) frees the tags, unregisters, **drops its script's on-exit scripts (they don't run)**
  and deletes the script, then stops every script still using it.
- **Missing pieces** (code): with no tags, the tag getters return false. GetSingleTag with no unnamed checksum
  uses the **parameter structure's address as the name** [quirk]; the member function 0xEEEB7220 (the tags named
  in an array; real name unknown) with no unnamed array reads the parameter structure **as an array** [bug].
  RemoveTags with no array removes **every** tag.
- **Flags** (code + test): Obj_SetFlag / Obj_ClearFlag / Obj_FlagSet / Obj_FlagNotSet use +0x24 (never
  initialised by the constructor [quirk]). The bits: an unnamed array of names, each one's global integer a bit
  number (mod 32), else `1 << the unnamed integer`; none -> the function returns false. ClearFlag `all` clears
  every bit; FlagSet `clear` clears the bits after testing them.
- **The managers' walks** (test): CBaseManager::ForEach (0x00461CC0) visits every object once even if the callback
  registers or unregisters objects - any list change restarts the walk from the front, skipping objects already
  visited; up to 32 nested walks, one bit each. A manager keeps its objects sorted by node priority, highest
  first, equal priorities newest first (Lst::Head::AddNode 0x0045A540) (test).
- Member functions called from script (`obj:Name` / `44` / `46` statements) come from tables of 0x18-byte entries
  `{name, component type, a 16-byte member function pointer}` (`CMemberFunctionEntry`: mName, mComponentType,
  mCode, mThisAdjustment, two virtual-base offsets): CObject's at 0x00683550 (type 0x57525413), CCompositeObject's
  at 0x00683268 (type 0x0ADA4E74, 30 entries), both registered by the composite object manager's constructor.
  An entry with a member function pointer is called directly (CObject's vtable slot 2, 0x0045D850, `this` moved by
  the entry's adjustment); with none, by name (vtable slot 3) (code + test).

### 12.1 Composite objects (CCompositeObject, code + test)

`Obj::CCompositeObject` (0x108 bytes, vtable 0x00647650, 11 slots): CObject's fields, then position +0x4C, old
position +0x5C, orientation matrix +0x6C, velocity +0xAC, a byte +0xBC, display matrix +0xC0, the component list
+0x100, flags +0x104. Skaters, peds and level objects are subclasses. It is what the object walk updates.
- **Making one**: CreateCompositeObject (0x0045F330: Calloc 0x108, the constructor 0x0045E510, type 0xCDEB9229,
  registered with the manager - so with the tracker: an id, a unique `autoid` if none, findable by FindObject at
  once). CreateCompositeObjectFromNode (0x0045F3A0; CFunc_CreateCompositeObject 0x00449540 with `components=` and
  `params=`): registered **before** its id/position are read from the node data (InitFromStructure 0x0045DAD0:
  `name` -> SetID, re-registering it), then its components made from the array (0x0045DA90), then Finalize
  (vtable +0x20, 0x0045DB30). The constructor sets **CObject flag 0x20** (has a position: the tracker's broadcast
  radius check, section 11.3).
- **Flags (+0x104)**: 1 paused, 2 suspended, 4 hidden, 8 teleported (cleared at the end of each update), 0x20 /
  0x40 finalized, 0x80 being destroyed.
- **Components** (`Obj::CBaseComponent`, base vtable 0x00647618): each belongs to one object, linked in the order
  they were added (+0x10), and on the manager's list of all components of its type (static 0x006F0DB8[128],
  through +0x14, newest first; GetFirstComponentByType 0x0045F2D0 walks it). Component types (81) are registered by
  0x004E4860 right after the manager is made; CreateComponent(type) calls the first matching maker. A type the
  manager can't make gives a null component that is added anyway **[bug: crash]**.
- **One update** (0x0045D920; test):
  1. Paused: return - nothing at all.
  2. Unless suspended: the object's own script gets **one Update** (if it has one; the result is ignored); if the
     object is now dead (MarkAsDead), **return** (no components, and the end step below is skipped too); then its
     own update, vtable +0x28 (the subclasses' - the skater's etc.; empty in the base).
  3. Each component in list order, skipping suspended ones (component +0x08 bit 0), **stopping as soon as a
     component kills the object**.
  4. Position w = 1.0, TELEPORTED cleared.
- **Hide / Suspend / teleport**: Hide(bool) (0x0045D990) and Suspend(bool) (0x0045D9E0) do nothing if the flag
  already has that value, else set or clear it and pass it to each component (vtable +0x20 / +0x1C). The base
  component's Suspend (0x0045D760) sets its suspended flag, so **suspending an object normally stops its script,
  its own update and its components** (a component class whose +0x1C doesn't set the flag keeps updating; a
  component added later isn't suspended). SetTeleported(bool) (0x0045E4C0) sets TELEPORTED and, with true, calls
  each component's Teleport (vtable +0x24). MarkAsDead (slot 6, 0x0045D7C0) tells each component (vtable +0x10),
  then CObject's MarkAsDead.
- **Member functions** (`obj:Name`; CallMemberFunction(name), slot 3 0x0045E450): first each component in turn
  (vtable +0x30: 0 = false, 1 = true, 2 = not mine), then CObject's names (SetProps, Die...). A component given a
  table entry (0x0045D720) always goes by the entry's name. CCompositeObject's own (table 0x00683268): Obj_GetPosition,
  Obj_SetPosition, Obj_GetVelocity, GetSpeed, Obj_GetOrientation, Obj_SetOrientation, Obj_GetQuat,
  Obj_GetOrientationToObject, Obj_GetDistanceToObject (feet), Obj_GetDistToNode, Obj_GetType, Obj_HasComponent,
  Obj_PrintDetails, Hide / Unhide / IsHidden, Suspend / Unsuspend, Pause / Unpause (section 2), **Obj_ForceUpdate**
  (0x0045DC40: a whole update at once, nested inside the calling statement - re-entrant when the object's own
  script calls it), and unnamed ones (section 13). [quirk] CreateComponentFromStructure
  and Obj_HasComponent with no type use the parameter structure's address as the type.
- **Dying and deletion**: as for every CGeneralManager object (above): MarkAsDead -> the dead list, deleted by
  FlushDeadObjectsTask at the start of the next frame.

## 13. Open questions

- **The rest of the logic task list**: the object walk (-900) and the front end (-2000) are placed (section 2),
  but not every other logic task - e.g. the skate module's two tasks of priority -1000 made at 0x0056E2B0 (they
  run between the two), and which tasks (if any) have kinds 4 / 8 and so stop while paused. The task list code
  itself (0x00406850-0x00406E00, Tsk::Task) isn't decompiled yet.
- **Screen elements' targeted events**: screen elements override CObject's +0x14 (0x004A8100, 0x004AA560,
  0x004AE6A0, section 12); what they do with an event is not decompiled.
- **Event listeners** (CEventListener): who registers them (CTracker::RegisterEventListener 0x00461FE0's callers)
  and what their filter (vtable +0x04, 0x004A1090) does with an event - in particular how the menus' pad events
  (section 11.5) become screen element actions.
- **CObject flag 0x10000** (`OBJECT_FLAG_SPAWN_HANDLERS`): what sets it (a script's normal handlers spawn instead
  of interrupting). (Flag 0x20 is set by CCompositeObject's constructor, section 12.1.)
- **Unidentified checksums**: 0xB3C262EC (the member function Obj_LeaveScript's real name), 0xEEEB7220 (the
  member function returning the tags named in an array), 0x01917DF2 (the spawn flag that skips SetSpawnerFile),
  0x36B2EE74 (the "no object" event target / source). CCompositeObject's unnamed member functions, by what they
  do: 0x406998A5 (a component from the parameters), 0x2BFFEAA3 (has a component of the type `name=`), 0x10C1C887
  (move along its own axes by x= y= z=), 0xF8CFD515 (moving backwards), 0x7DCC5FB9 / 0xE5DF66D7 / 0xDD468509
  (speed near / above / below), 0x37185D8A (the nearest object of type 0xBAF9B341 lies outside an angle),
  0x0168B09C (probably Obj_GetCollision, 0x0045E9E0, not decompiled); result names 0x976B04A3
  (Obj_GetDistanceToObject), 0x24495505 (Obj_GetType), 0xC97F3AA9 (Obj_GetOrientationToObject's angle); the script
  integer 0xBA2C7CCB read and ignored by CreateCoimHeap.
- **The CFunctions that set waits** (Wait / WaitTime / Block / the object waits, and how their arguments map to
  frames vs seconds vs real time): not yet decompiled.
- **0xFB11E6CD**: presumably the default level's script (missing calls are silent there). The front end runs it
  with RunScript on the first skater when a pending flag is set (section 2). Not confirmed.
- **Untested, from the code only**: bit 1 staying set after a nested wait (section 3), the interrupt that calls
  a waiting script returning 5 later (section 7), RunScript spinning on one-per-frame waits, and a Block(type)
  registration outliving the block (section 11.2).
- **The 0x49 message's last 3 bytes** are uninitialised stack in the original (ours zeroes them), and MsgDesc
  bytes 0x12-0x13 are never set. The meaning of MsgDesc +0x0C = 2 and +0x10 = 8 is unknown.
