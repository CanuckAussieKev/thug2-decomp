# wt/trk: the object tracker, the event CFunctions, CRefCounted (0x00462000-0x004639E0)

NOT run in the game yet.

## What was rewritten

`src/game/obj/tracker.h` / `tracker.cpp` (new), `src/game/obj/smtptr.h` / `smtptr.cpp` (CRefCounted), 41 functions:

- **Obj::CTracker** (0xB21C bytes, the singleton 0x006F0FC0, use count 0x006F0FC4, vtable 0x006478E0):
  Construct 0x00462CF0, Destruct 0x00462E40 (frees the object table and the log's name table only: the alias and
  listener tables leak - original), ScalarDelete 0x00463780, CreateTracker 0x00463730 (cdecl, returns the tracker),
  LookUpObject 0x00462120 (id, then alias; named so because windows.h #defines GetObject), GetObjectByAlias
  0x00462190, GetUniqueId 0x004621D0 ("autoid%d", 0x006478CC; the counter wraps at 1000000), AddObject 0x00462980,
  RemoveObject 0x00462A20, RemoveAliasesOf 0x00462590, AddAlias 0x00462A90, RemoveEventListener 0x00462000,
  AddListener 0x00463370, RemoveListener 0x00463450, ForgetScript 0x00462B90, WaitForEvent 0x00462260,
  **LaunchEvent 0x00462F00**, LogHandled 0x00462B10, LogRead 0x00462B40 (was undefined in Ghidra; called from
  0x0045F994), LogFrame 0x00462B70 (called once a frame by 0x0054FB40).
- **Obj::CEventLog** (tracker +0x20, 0xB014 bytes): AddEntry 0x004627C0, Print 0x004620A0, Destroy 0x00462CB0.
- **Obj::CScriptNode / CScriptList** (Lst::Node / Lst::Head of CScript, vtables 0x006478D8 / 0x006478DC):
  Unlink 0x00462530, CScriptNode::ScalarDelete 0x00462550, CScriptList::ScalarDelete 0x00462710, Add 0x00462620,
  Remove 0x004626B0, operator delete 0x004624D0 (the pool pair 0x006F0FC8).
- **CFunctions**: WaitForEvent 0x00462310, ObjectExists 0x00462340, TerminateObjectsScripts 0x00462380,
  SetObjectProps 0x00462440, PrintEventLog 0x00462480, AssignAlias 0x00462C40, LaunchEvent / FireEvent 0x004634D0.
- **Obj::CRefCounted**: Construct 0x004637B0, ClearSmartPointers 0x00463820, Destruct 0x00463870,
  ScalarDelete 0x00463880 (AddSmartPointer / RemoveSmartPointer were already done).

Callers switched to direct calls: cscript_core.cpp (CRefCounted Construct / Destruct, Tracker()->ForgetScript),
cscript_wait.cpp (AddListener / RemoveListener), net/chat.cpp (CFunc_ObjectExists). The CScript tests (csA, csC,
csD) and the chat test (skaterB) stand in for ours next to the game addresses.

**Skipped** (in the range, not tracker code - they belong to the class after 0x004639E0, which uses +0x34xx
fields): 0x004638B0 (fastcall, EDX = a path: its file name without directory and extension into the static buffer
0x006F2160; callers 0x00466620, 0x004678A0), 0x00463910 (thiscall: print "%s%d\n" or a string through 0x00401BE0 to
the handle at +0x34B4; callers 0x004656E0, 0x00466DD0), 0x004639A0 (thiscall: `[+0x346C] <= [+0x347C]` as int),
0x004639C0 (thiscall: adds the time since +0x34A0 to the 64-bit +0x3498 with 0x005BE8E0). They should go with
0x00463A00+.

## Findings

- 0x36B2EE74 is the "no object" id (event target / source none, `kNoObjectId`); its name isn't known.
  0x08114F90 is a global array of event names the log keys by checksum (no check that it has at most 256).
- CEvent (0x14 bytes; wt/evt's): +0 type, +4 source, +8 target, +0xC flags (bit 1 handled - set by 0x0045F9B0,
  which also logs; bit 2 owns its data), +0x10 data. tracker.cpp uses a private `SEvent` with that layout and calls
  the event's methods through GameFunction (0x0045F7F0 constructor, 0x0045F9B0 MarkHandled, 0x00460150 destructor):
  **the merger should switch these to wt/evt's CEvent** if it defines one.
- CObject (wt/obj's) is used through offsets: +0x08 flags (0x20 = has a position), +0x14 id, +0x3C alias count,
  +0x4C position; vtable +0x04 SetProperties(params), +0x14 an event targeted at it. 0x004619F0 (the object
  manager 0x006F0FB8's lookup by id, wt/obj) and 0x004AC120 (the screen element manager 0x00701440's id lookup from
  params, outside all three ranges) stay GameFunction.
- `m_refCounted` (+0xEC) of CScript is what LaunchEvent's smart pointers point at (section 12's open question).
- Original crashes kept: a targeted event whose target is neither an object nor a spawned script prints the log and
  calls ReceiveEvent on null; AddAlias / SetObjectProps / TerminateObjectsScripts don't check the object; a radius
  broadcast doesn't check the source object; AddListener / CScriptList::Add don't check their allocations.
  WaitForEvent and AssignAlias read their type / alias uninitialised when missing (ours: 0).
- The singleton table (core/singleton.cpp `Singleton463730`) still goes through 0x00463730 by address (now ours
  when hooked); its Release 0x00461FA0 is wt/obj's. Whoever has both can give it a real struct.

## Test

`src/hook/selftests_trk.cpp`, TestTracker (Early): original against ours over stand-in scripts, objects, event
listeners and parameters in an arena; the memory manager, pools, hash item pool, CScript methods, object functions,
structure getters, Dbg::Printf and the game's sprintf are recording stand-ins; the event methods and hash table are
the real code. Covers: create (with / without, counting), objects and aliases (chained buckets, alias replaced,
alias dropped by AddObject), lookups, unique ids (taken id skipped, wrap), listeners (heap and pool lists, duplicate,
chained types), broadcasts (several listeners with one deleted by an earlier one's handler, radius equal / just
over / zero, a listener without object or position, one listener in / out of range, nobody listening, a handler
marking it handled), targeted events (event listeners and their busy counts, one marking it handled, object by id
and by alias, no target, spawned script), WaitForEvent (by object, without object, broadcast, gone script, all 40
taken, slot reuse), RemoveObject (renamed / not), RemoveAliasesOf, RemoveListener / ForgetScript (lists emptied,
pool / heap), RemoveEventListener (middle, first, absent, last), the log (handled / read / frames merged, Print
both headers, wrap), all seven CFunctions (LaunchEvent: one type, array, empty array, structure, empty structure,
none; target / source given, none, defaulted), the list destructors and CRefCounted.
MUTATIONS_PLACEHOLDER

## For docs/script-runtime.md section 11 (to paste)

### The tracker and event delivery

- **What the tracker tracks** (Obj::CTracker, 0x006F0FC0):
  - objects by id (+0x08) and aliases (+0x0C); a script command's `id=` is looked up by id first, then alias
    (0x00462120) (code). AddObject (0x00462980) drops an alias equal to the new id; RemoveObject (0x00462A20)
    drops the object's aliases (by id), or when the object is renamed moves WaitForEvent waiters to the new id
    (test).
  - **listeners**: for each event type, a list of scripts (+0x10, in the order they started listening; a script is
    in a list once) (test). A script is added by `Block(type)` with type != 0 (CScript::Block 0x00472FD0) and by its
    event handler table for each handler type (0x0045FA10), and removed by ClearEventHandler (0x00473690 ->
    0x00463450) and, for every type at once, when it stops with TRACKER_BLOCKED set (ReportStopped 0x00473010 ->
    ForgetScript 0x00462B90; the handler table's types also through 0x0045FA60) (code). An emptied list is deleted
    (test). **A Block(X) registration outlives the block [quirk]**: nothing takes the script off X's list when it
    is unblocked, so until it stops (or ClearEventHandler X) it keeps getting broadcasts of X, which then go only to
    its handler table (code).
  - **WaitForEvent waiters**: 40 slots {script unique id, type, the script's object id or 0} (+0xB038) (test).
  - **event listeners** (+0x14): C++ objects (CEventListener) that see every targeted event (code).
- **LaunchEvent(type, target, source, data, broadcast, radius)** (0x00462F00) delivers **at once**, nested inside
  the caller (nothing is queued for a later frame) (test):
  1. **Targeted** (not `broadcast`): every event listener gets it (0x004A1090), in list order; then, unless one of
     them marked it handled or the target is none (0x36B2EE74), the target: the object with that id or alias gets
     it through its vtable +0x14 (what an object does with it is outside this range), else the **spawned** script
     whose spawn id is the target gets ReceiveEvent; if there is neither, the game prints the event log and crashes
     calling ReceiveEvent on null **[bug]** (test, apart from the crash). **Listener scripts are not involved.**
  2. **Broadcast**: every script listening for the type, in the order they started listening, gets ReceiveEvent
     (0x00473800). With `radius` >= 0, a script whose object has a position (+0x08 bit 0x20) gets it only if the
     squared distance from the **source** object (looked up by id, not checked for null **[bug]**) is less than
     radius squared (equal: not delivered); scripts without an object or without a position always get it. NaN
     radius = no radius (test). Event listeners and the target are not involved.
     With several listeners, smart pointers to each script (its m_refCounted) are taken first, so a script deleted
     by an earlier listener's handler is skipped; scripts that start listening during the delivery don't get it
     (test). With exactly one listener there is no such protection (its script is called directly) (code).
  3. **Then the WaitForEvent waiters**: the first slot (in slot order) with this type whose object is the target,
     or that has no object, or the event has no target, or it is broadcast, is freed; if its script still exists it
     stops waiting (StopWaiting) and the event is marked handled (0x0045F9B0, `how` 2, the script's name) and the
     search ends - **one event releases at most one WaitForEvent waiter [quirk]**; slots whose script has gone are
     freed on the way and the search goes on (test). This happens after (and regardless of) the deliveries above.
  4. Returns whether the event was marked handled (by a handler, an event listener or a waiter) (test).
  So a script blocked with `Block type=X` is released by a **broadcast** of X, by an event of type X targeted at
  its spawn id (spawned scripts) or - depending on what objects do with it - at its object; a targeted event never
  reaches the tracker's listener lists (code).
- **WaitForEvent type=X** (CFunction 0x00462310 -> 0x00462260): takes the first free slot, else the first whose
  script has gone; if all 40 are held by live scripts it **silently doesn't wait [quirk]**. Then `Block(0)`: the
  script is blocked with no event type, so the tracker's listener lists don't have it and ReceiveEvent doesn't
  unblock it; only LaunchEvent's waiter step (or StopWaiting / ObjectDestroyed / UnblockInterruptedScript) releases
  it (test). The object id is the script's object when it starts waiting (renames follow, see RemoveObject).
- **LaunchEvent / FireEvent** (CFunction 0x004634D0): `type` is a name, an array of names or a structure of names
  (each launched in turn, same arguments; no type: nothing); `target` and `source` default to the calling script's
  object (none without one; `target=<none id>` gives none); `data` a structure (not copied: the event doesn't own
  it), `broadcast` a flag, `radius` a float (default -1 = none) (test).
- **AssignAlias id=X alias=Y** (0x00462C40): Y names X from now on (an alias naming another object is moved; the
  objects count their aliases at +0x3C) (test). **ObjectExists id=X** (0x00462340): X is an object id or alias
  (test). **SetObjectProps id=X ...** (0x00462440): the object's vtable +0x04 gets the parameters (test).
- **TerminateObjectsScripts id=X [scripts=[...] | script_name=Y]** (0x00462380): StopScriptsUsingObjectThatReferTo
  (0x00474350) for each name (no name: 0) on the object (null if X is unknown - not checked) (test).
- **The event log** (+0x20): a ring of 1024 entries {type, target, source, script, how, kind, depth}: LAUNCHED (0;
  `script` = whether the launch came from a script with a name), HANDLED (1: by 0x0045F9B0 with the handler's name
  and how - 2 WaitForEvent - or at the end of LaunchEvent with how 3 when nobody handled it), READ (2, 0x00462B40),
  FRAME (3; consecutive frames count up in one entry), OBJECT_ADDED / OBJECT_REMOVED (4 / 5). PrintEventLog
  (0x00462480; a count argument switches the header to "PRINTING EVENT LOG") prints only the header and footer in
  this build (code + test). It doesn't affect scheduling.

### For section 12 (open questions)

- **Where an event targeted at an object goes**: the object's vtable +0x14 (CObject, wt/obj / later) - presumably
  to its scripts' ReceiveEvent and its components. Undecided here.
- **The event listeners** (CEventListener, +0x14 list): who registers them (the registration is inlined elsewhere;
  0x00462000 unregisters) and what their vtable +0x04 does with the event.
- The WaitForEvent / handler interplay: a script blocked by WaitForEvent that also has a handler for the type gets
  ReceiveEvent first only if the event is broadcast (it's in the listener list through its handler table), then is
  released by the waiter step.
