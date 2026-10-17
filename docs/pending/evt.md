# wt/evt: events and the event handler table (0x0045F7F0-0x00460792)

NOT run in the game yet. To switch off: `[replacements] disable=event.cpp`.

## What was rewritten

`src/game/obj/event.h` / `event.cpp` (new), 21 functions, all `REPLACE`d:

| address | function |
|---|---|
| 0x0045F7F0 | `CEvent::Construct(type, source, target, data)` (flags 0; returns this) |
| 0x0045F9B0 | `CEvent::MarkHandled(how, script)`: the tracker's event log (0x00462B10), then flags bit 1 |
| 0x00460150 | `CEvent::Destroy` (the destructor: the data goes if flags bit 2) |
| 0x0045FAA0 / 0x0045FAC0 / 0x00460170 / 0x0045FB30 | `CEventListener`: Construct, Destroy, DeletingDestructor (vtable 0x006477D8 slot 0), RegisterWithTracker(object) |
| 0x0045F820 / 0x0045FB70 | `CEventHandlerTable::Construct` / `Destroy` |
| 0x0045FBC0 | `AddHandler(script, type, name, group, exception, params)` |
| 0x004601A0 | `AddHandlers(array, replace)` |
| 0x0045FDF0 / 0x0045FE60 / 0x004600E0 | `RemoveType(type)`, `RemoveGroup(group)`, `RemoveGroup(script, group)` |
| 0x0045F840 / 0x0045FED0 / 0x0045FF50 | `MarkGroup`, `RemoveMarked(script)`, `Compact` |
| 0x0045FA10 / 0x0045FA60 | `RegisterWithTracker(script)` / `UnregisterWithTracker(script)` |
| 0x00460470 | `PassEvent(event, script)` - the dispatch |
| 0x0045F880 | `PrintTable(table)` (cdecl) |

CScript (cscript_core.cpp, cscript_wait.cpp) now calls these directly (the GameFunction wrappers are gone);
csA/csC/csD tests stand in for ours next to the game addresses (AddressOf). cscript.h: forward-declaration
comments now point at game/obj/event.h; ReportStopped's comment corrected (see "Corrections").

Test: `src/hook/selftests_evt.cpp` (TestEvents, Early): every function above, original vs ours on stand-in
tables / scripts / objects in an arena, all outside calls recorded (tracker, memory manager, CStruct functions,
smart pointers, Interrupt / SetScript / Update, SpawnScript, Printf), then the arena compared. 48 deliberate bugs (tools/mutate.py) caught, 7 of them after strengthening the test (removed entries in MarkGroup / Register / Unregister, RemoveGroup(all_groups) on a filled table, Compact's `changed`, the spawn-on-object path).

Header changes: new `game/obj/event.h` (Obj::CEvent, Obj::CEventListener, Obj::CEventHandlerTable, constants
kNullScript / kAllGroups / kDefaultGroup). `cscript.h` still forward-declares `struct CEvent` and `class
CEventHandlerTable` (matching).

## Checksums identified

- 0xC377C572 = `null_script`: the script of a removed handler entry.
- 0x8B713E0E = `all_groups` (not "script stopped").
- 0x1CA1FF20 = `default` (the group of a handler structure without `group`).
- 0x7031F10C = `params`, 0x923FBB3A = `group`, 0x80367192 = `exception`.

## Corrections to docs/script-runtime.md

- Section 8 ReportStopped: it does **not** pass an event 0x8B713E0E. It is `UnregisterWithTracker(script)`
  (0x0045FA60: the tracker forgets the script for every entry's type) then `RemoveGroup(all_groups)`
  (0x0045FE60): **every handler is removed** (marked null_script, parameters freed; the array stays until a
  Compact). So a stopped script (ClearScript + ReportStopped: Destroy, the kill/stop walks, SetScript with no
  code) loses its event handlers. SetScript itself (a script starting another, Restart, an exception handler)
  does NOT call ReportStopped, so handlers survive those.
- Section 11, first bullet: the entry's flags byte has bit 0 exception, bit 1 marked; bits 2-7 are never
  initialised (garbage in new entries) **[quirk, harmless]**.
- Section 12: the dispatch is no longer open (below); m_onExceptionScript is answered (below); m_refCounted is
  answered (below, from the tracker's code - wt/trk's range).

## Section 11 text (to paste into docs/script-runtime.md, replacing its last two bullets)

### 11.x How an event reaches a script (wt/evt)

**The event** (`Obj::CEvent`, game/obj/event.h; built on the stack by the tracker's launch function 0x00462F00
through CEvent::Construct 0x0045F7F0): `{type, source id (+4), target id (+8), flags (+0xC), data (+0x10)}`.
Flags: bit 1 HANDLED (something handled it: MarkHandled 0x0045F9B0), bit 2 the event owns `data` (freed by
CEvent::Destroy 0x00460150). (The +4 / +8 roles are from 0x00462F00 delivering to the +8 id; the LaunchEvent
CFunction's `target` / `source` parameters default to the launching script's object id, else 0x36B2EE74 = "no
target" - wt/trk's code, read only.)

**Delivery** (the tracker 0x00462F00, wt/trk's range - summarised from the code only, to be confirmed there):
1. Not `broadcast`: every **event listener** (CEventListener, the tracker's list) sees it first (its +0x18 busy
   count is raised around the call). Then, unless the target is "no target" or a listener marked it HANDLED,
   it goes to the **target**: an object with that id (its vtable +0x14, presumably to its script), else the
   spawned script with that spawn id, through `ReceiveEvent` (0x00473800).
2. `broadcast`: every script registered with the tracker for the event's **type** (registration comes from the
   handler table, below, and from Block) gets `ReceiveEvent`, optionally only within `radius` of the source
   object (receivers whose object has flag 0x20). With more than one receiver, they are first copied into an
   array of smart pointers to each script's `m_refCounted` (+0xEC), so a script **deleted by an earlier
   receiver's handlers is skipped** (this is what m_refCounted is for - answers section 12).
3. Then the tracker's table of scripts blocked on an event (40 entries at tracker +0xB03C): the **first** entry
   matching the type (and the target, or with no target, or for a broadcast / "no target" event) is cleared and
   its script (by unique id) gets `StopWaiting` + MarkHandled(how 2, its name).
4. The launch returns whether the event was handled (bit 1).

**One script** (`ReceiveEvent`, section 5): first the levels blocked on the type are released, then, if the
script has a table, `PassEvent(event, script)` (0x00460470). ReceiveEvent always returns true.

**The table** (`Obj::CEventHandlerTable`, 0x10 bytes on the front-end heap): `{entries, count, alive (+8),
changed (+9), in use (+0xC)}`; an entry (0x14 bytes) is `{type, script, group, params (an owned copy), flags}`
with flags bit 0 exception, bit 1 marked. A **removed** entry keeps its slot: its script becomes `null_script`
(0xC377C572) and its parameters are freed; only `Compact` (0x0045FF50) drops removed slots (it builds a new
array on the front-end heap). (code + test)
- **Adding one** (`AddHandler` 0x0045FBC0, from SetEventHandler / SetException / ResetEventHandlersFromTable):
  the entry of the **same type** is overwritten in place (even if removed), else the first removed slot is
  reused, else the array grows by one (new entry last). The old parameters are freed, the new ones copied, the
  mark cleared. The tracker is told the script listens for the type unless the overwritten entry was live.
  Sets `changed`. (test)
- **Adding an array** (`AddHandlers` 0x004601A0, from SetEventHandlers via 0x004734E0): elements are
  `{type, script, [params = {...}], [group = g] (default `default` 0x1CA1FF20), [exception]}` (type and script
  are the first two components, by position). **The new entries go in front of the old ones, in array order**;
  with `replace` the old entries of each new type are removed first (two entries of one type are possible only
  without `replace`, or from two elements of one array). An empty array with `replace` frees every entry at
  once. The tracker isn't told here: SetEventHandlers unregisters the whole table before and registers it after
  (and compacts). Sets `changed`. (test)
- **Removing**: by type (`RemoveType` 0x0045FDF0, also zeroes the type; ClearEventHandler then tells the
  tracker), by group (`RemoveGroup` 0x0045FE60 quietly, 0x004600E0 telling the tracker - ClearEventHandlerGroup;
  `all_groups` 0x8B713E0E matches every group), by mark (`MarkGroup` 0x0045F840 marks a group's live entries,
  `RemoveMarked` 0x0045FED0 removes the marked ones - ResetEventHandlersFromTable re-adds the group's new set in
  between, and AddHandler clears the mark of an entry it overwrites). Removing never sets `changed`. (test)
- **Tracker registration** (0x0045FA10): one `AddListener(type, script)` per live entry (type and script
  non-zero). Unregistration (0x0045FA60) is one `RemoveListener` per entry with a type, removed ones included.
  (test)

**PassEvent(event, script)** (0x00460470) (test):
1. `wasChanged = changed; changed = false; ++inUse`. The entry pointer is taken **once**.
2. For each entry **in table order** (the count re-read each time): if `entry.type == event.type` and the
   entry isn't removed:
   - The parameters: the entry's own; or, if the event has data, a **new structure = entry params, then the
     event data appended (the event's data wins)**, freed after the handler.
   - **Exception handler** (bit 0): if the script has an OnExceptionRun script (`m_onExceptionScript`), it is
     **cleared** and run first as an `Interrupt`, with an unnamed checksum component `1` as its only parameter.
     Then, if the entry wasn't removed meanwhile, **`SetScript(handler, params, the script's object)` + one
     `Update`**: the handler **replaces the whole script** (the call stack is dropped and its OnExitRun scripts
     run, through ClearScript; the handler table and m_onExceptionScript survive). It's a "goto", not a call:
     nothing resumes what the script was doing.
   - **Normal handler, the script's object has flag 0x10000** (CObject +0x08): `SpawnScript(handler, params)`
     (no callback, id 0, not networked), the spawned script's object is set to the receiving script's object,
     and it gets **one Update at once**; later Updates come from UpdateSpawnedScripts. The receiving script is
     not disturbed. A C-function handler here returns no script, which is used anyway **[bug: crash]**.
   - **Normal handler otherwise**: `script->Interrupt(handler, params)` - the handler runs **now**, nested, on
     the receiving script (section 7), pushed over whatever level was running; it returns into that level
     (which keeps its wait) or, if it waits, stays on top as an ordinary called level.
   - Then the event is marked HANDLED and logged (`MarkHandled(0, entry.script)`, the entry re-read).

   After every entry (matched or not): if the table is no longer **alive** (the script was destroyed by a
   handler: CScript::Destroy only clears `alive` while `inUse != 0`), `--inUse` and, if that was the outermost
   PassEvent, the table is destroyed and freed; return. If **changed** (a handler *added* entries, which may
   have moved the array), `--inUse` and **stop** - the remaining entries don't run for this event (`changed`
   stays set).
3. At the end: `changed = wasChanged; --inUse`.

Consequences for the port:
- **Every** matching handler runs, in table order; matching doesn't consume the event, and nothing in the table
  stops it reaching other scripts (the tracker decides who gets it). A type normally has one entry, so in
  practice one handler per event per script.
- Handlers removed by an earlier handler (or by the OnExceptionRun script) are skipped; handlers **added** by a
  handler end this event's walk at once (the new ones don't see it).
- Handlers interrupt synchronously, inside whatever sent the event - possibly inside another script's Update or
  a C function. A handler that waits leaves the receiving script running the handler at its next Update.
- `changed` is also a re-entrancy signal: a nested PassEvent that added entries stops the outer one too.
- An entry's `group` only matters for removal; `exception` is the only per-entry behaviour switch; the object
  flag 0x10000 switches all of a script's normal handlers to spawning.
- [bug, harmless while freed front-end memory isn't reused] after a handler added entries, the entry is
  re-read from the old (freed) array for the log; the exception path re-reads it after the OnExceptionRun
  interrupt the same way.
- A stopped script (ReportStopped) loses all its handlers (see "Corrections").

Open (for section 12): what sets CObject flag 0x10000 (spawn handlers) and 0x20 (radius checks); the object's
event entry point (vtable +0x14, wt/obj); what 0x004620A0 does when no target is found (wt/trk).

## Not done (left in the range)

- 0x0045EFD0-0x0045F7A0 (18 functions): the composite object instance manager (CCompositeObjectInstanceMgr,
  "COIM heap", singleton 0x006F0FB8 / 0x006F0FBC) - a different subsystem from events, too much for this batch.
- 0x004607A0-0x00460840 (7 functions): Obj::CObject methods (+0x10 the object's script, +0x20 its tags
  structure). Left for wt/obj, which owns CObject (writing them here would mean a second CObject definition):
  - 0x004607A0 `if (mp_script) mp_script->ClearEventHandler(type)` (tail jump to 0x00473690), RET 4.
  - 0x004607B0 `if (mp_tags) mp_tags->RemoveFlag(flag)` (tail jump to 0x00477860), RET 4.
  - 0x004607C0 `mp_tags ? mp_tags->GetInteger(name, out, 0) : false` (0x00476630), RET 8, returns AL.
  - 0x004607E0 `mp_tags ? mp_tags->GetChecksum(name, out, 0) : false` (0x00476950), RET 8, returns AL.
  - 0x00460800 `mp_tags ? mp_tags->ContainsFlag(flag) : false` (tail jump to 0x00476B40), RET 4.
  - 0x00460820 `if (mp_tags) dest->AppendStructureIfAny(mp_tags)` (dest the stack argument), RET 4.
  - 0x00460840 `script->ObjectDestroyed(this); return true;` - (params, script) RET 8; its address is stored
    into 0x006835D0 by a static initialiser at 0x0064296E (a member-function pointer table; 0x006835A0 /
    0x006835B8 get 0x004617C0 / 0x00460FF0 the same way).

## For the merger

- wt/trk may also define `Obj::CEvent` (LaunchEvent builds events on the stack): keep one definition - ours is
  in game/obj/event.h with Construct / MarkHandled / Destroy. MarkHandled's first argument is a "how" code
  (0 = an event handler ran, 2 = a blocked script was released; the log's own final entry uses 3).
- Calls into the other worktrees' ranges, still GameFunction in event.cpp: 0x00463370 / 0x00463450 (tracker
  add / remove listener, wt/trk), 0x00461FE0 (tracker register listener, wt/obj), 0x00462000 (unregister
  listener, wt/trk), 0x00462B10 (event log, wt/trk). Switch them to direct calls once merged.
- CEventListener +0x18 is the tracker's "busy" count around the listener's filter call (0x00462F00).
