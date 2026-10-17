# wt/obj: the object base and its managers (0x004607A0-0x00461FE0, 0x0045DB90)

NOT run in the game yet. To switch off: `[replacements] disable=object.cpp,objman.cpp`.

## Rewritten (75 functions + 2 source-only thunks)

`src/game/obj/object.h/.cpp` - `Obj::CObject` (0x4C bytes; CCompositeObject's fields start at 0x4C):
- construction / destruction: Construct 0x00460B80, DestroyBody 0x004615D0, Destroy 0x00461900,
  DestroyIfUnlocked (member, no callers) 0x00460930; `CObjectRef` (the second smart-pointer target at +0x40)
  DestroyBody 0x00460920 (a jump, source only) / Destroy 0x00460AF0; `CObjectNode` (Lst::Node<CObject>)
  DestroyBody 0x00460B20 / Destroy 0x00460B40.
- vtable 0x006477E8: SetProperties (SetProps) 0x00461710, CallMemberFunction 0x004608F0, PassEventToScript
  0x00461140 (slot 5), MarkAsDead 0x00460A50 (slot 6).
- script: CreateScriptIfNeeded 0x00461290, SwitchScript 0x00461350, CallScript 0x00461500, SpawnScript
  0x00461410, SpawnAndRunScript 0x00461480, ClearEventHandler 0x004607A0.
- events: SelfEvent 0x004609F0, BroadcastEvent 0x00460A20.
- id / type / tags: SetID 0x00461930, SetType 0x00461970, CreateTagsIfNeeded 0x004611E0, SetIntegerTag
  0x004616B0, SetChecksumTag 0x004616D0, SetTagsFromStruct 0x004616F0, RemoveTags 0x00460C20, GetTag 0x00460CA0,
  GetTags 0x00460D90, RemoveTagFlag 0x004607B0, GetIntegerTag 0x004607C0, GetChecksumTag 0x004607E0,
  ContainsTagFlag 0x00460800, CopyTagsTo 0x00460820, GetFlagMask 0x00460960.
- the member functions scripts call (table 0x00683550, 0x18-byte entries {name, 0x57525413, member function
  pointer}, filled by the static initialiser 0x00642940): Obj_LeaveScript (0xB3C262EC, name unknown) 0x00460840,
  Obj_KillSpawnedScript 0x00460850, Obj_Visible 0x004608D0, Obj_Invisible 0x004608E0, Obj_GetId 0x00460940,
  GetTags 0x00460EB0, GetSingleTag 0x00460FB0, 0xEEEB7220 (name unknown: the tags named in an array)
  0x00460FC0, RemoveTags 0x00460FF0, Obj_FlagNotSet 0x00461040, Obj_ClearFlag 0x00461070, Obj_SetFlag
  0x004610C0, Obj_FlagSet 0x004610F0, SetTags 0x004617C0, Obj_SpawnScript 0x004617E0, Obj_SwitchScript 0x004618A0.
- free: FindObject 0x00460AD0 (cdecl), the walk callbacks Unlock 0x00460AC0 / DestroyIfUnlocked 0x00460AA0
  (cdecl), CCompositeObject::GetComponent 0x0045DB90.

`src/game/obj/objman.h/.cpp`:
- CBaseManager (0x28 bytes, vtable 0x00647818): Construct 0x00461BC0, DestroyBody 0x00461B50, Destroy 0x00461B80,
  RegisterObject 0x00461A00, UnregisterObject 0x00461A50, ReassignObjectId 0x004619A0, SetObjectPriority
  0x004619D0, GetObjectByID 0x004619F0, ForEach 0x00461CC0; CObjectListHead::Destroy 0x00461B20.
- CGeneralManager (0x40, vtable 0x00647834): Construct 0x00461BF0, DestroyBody 0x00461DF0, Destroy 0x00461EA0,
  KillObject 0x00461B00 (slot 5), FlushDeadObjects 0x00461AB0, its task's code FlushDeadObjectsTask 0x00461BB0.
- CObjectSearch FirstItemOfType 0x00461F30 / NextItemOfType 0x00461ED0.
- the tracker: ReleaseTracker 0x00461FA0 (its singleton's Release), CTrackerEventListeners::RegisterEventListener
  0x00461FE0 (CTracker::RegisterEventListener; event.cpp now calls it directly).

The functions 0x004607A0-0x00460840 came from wt/evt's range (left for wt/obj, see evt.md).

## Header / caller changes the merger must know
- New headers object.h, objman.h. `struct CEvent` is forward-declared in object.h (event.h defines it);
  objman.h includes event.h (CEventListener is evt's).
- After merging wt/trk: CTracker::RegisterEventListener 0x00461FE0 is declared in tracker.h and defined in
  objman.cpp; tracker.cpp's FindObjectById calls CBaseManager::GetObjectByID directly; object.cpp / objman.cpp call
  CTracker (LookUpObject, LaunchEvent, GetUniqueId, AddObject, RemoveObject) and CRefCounted (Construct, Destruct,
  ClearSmartPointers) directly. object.h's "no id yet" constant is `kUnassignedId` (-1) - trk's `kNoObjectId`
  (0x36B2EE74) is a different thing. CObject +0x3C is `m_aliasCount` (trk's finding).
- Thunks 0x00420060 (-> DestroyBody) and 0x004AAD30 (-> RegisterObject) are REPLACE_SOURCE_ONLY.
- cscript_exec.cpp: FindObject and GetComponent are direct calls now (Obj::FindObject,
  CCompositeObject::GetComponent). event.cpp: 0x00461FE0 is a direct call.
- singleton.cpp: `Singleton463730` became `ObjectTracker` (Obj::CreateTracker / Obj::ReleaseTracker); sys/main.cpp
  calls both directly. selftests_misc / gapsB / csB / evt
  stand in for ours next to the game addresses.
- Still through GameFunction: 0x0045A540 (Lst::Head insert by
  priority), 0x00406890 / 0x00406C90 (Tsk::Task).
- event.cpp's ObjectFlags() reads CObject +0x08 by offset; it could use `object->m_objectFlags` now (flag 0x10000:
  spawn the handlers - add it next to OBJECT_FLAG_* when someone finds what sets it).
- skater/core_physics.cpp still calls SelfEvent / BroadcastEvent (0x004609F0 / 0x00460A20) through GameFunction
  (its test table in selftests_skaterA.cpp stands in by address only); left as is.

## Not done (next to the range)
- 0x0045D850 (CObject vtable slot 2: a member function table entry - calls the entry's member function pointer,
  or slot 3 by name if it has none) and 0x004AAD40 (CGeneralManager slot 6: the list head) - outside the range.
- cscript.h's `CMemberFunctionEntry` is really 0x18 bytes: {+0 name, +4 component type, +8 a 16-byte member
  function pointer (+8 code, +0x0C this adjustment)}.

## Findings: how scripts see objects (for docs/script-runtime.md)
- **Name -> object** (code): `name:` and every CFunction that takes an object go through FindObject 0x00460AD0
  (cdecl) or CBaseManager::GetObjectByID 0x004619F0 (the same, thiscall, `this` unused) -> the tracker's
  0x00462120: the id is looked up in the tracker's object table (+0x08, hashed on the low bits); if missing (or
  its entry is empty) the second table (+0x0C) is tried with the same key - presumably aliases (wt/trk). Not found:
  null. An object's id is its name checksum; it's also kept as its tag `id` (and its type as tag `type`).
- **Ids** (code+test): an object registered with id -1 gets a unique one from the tracker (0x004621D0: a counter
  printed and checksummed, retried while taken). SetID on a registered object re-registers it under the new id
  (unregister, id, register - the tracker is told the new id while unregistering).
- **Object scripts are not spawned** (code): an object has at most one script of its own (+0x10, made on demand:
  SwitchScript / CallScript / SetProps's event_handlers; CreateScriptIfNeeded starts it on `empty_script`). It is
  updated by the object itself: CCompositeObject's update 0x0045D920 runs `m_script->Update()` first (unless its
  +0x104 bits 0/1 say paused / scripts off), then, if the object isn't dead, vtable +0x28, then each component's
  update. So an object's script runs inside its object's update, before its components (whose waits it may then
  see); where the objects' updates sit relative to UpdateSpawnedScripts (0x0054F380) is still open (who calls
  0x0045D920 per frame).
- **Spawning on an object** (code+test): CObject::SpawnScript 0x00461410 spawns with node -1, the given id and
  PauseWithObject, then sets the spawned script's m_object to the object (this answers "who sets m_object on a
  spawned script": the object's own spawn functions, after SpawnScript). Obj_SpawnScript (the member function):
  the first parameter must be a bare name (else nothing, true); id defaults to the calling script's spawn id;
  params= are its parameters; unless 0x01917DF2 is given the new script records the caller's .qb file
  (SetSpawnerFile). SpawnAndRunScript 0x00461480 also gives it one Update at once; with netEnabled the network
  message is sent with **no object** [quirk]. A C-function name returns no script and both crash [bug].
- **CallScript on an object** (code+test): the script runs as an Interrupt of the object's script (made if
  needed, its object set) - not a new script.
- **Events to an object's script** (code+test): vtable slot 5 0x00461140 hands the event to its script's
  ReceiveEvent holding a smart pointer to the object; it returns false if the object was deleted meanwhile (no
  script: true, nothing happens).
- **Dying** (code+test): MarkAsDead (Die, slot 6): the object's own script is **blocked for good** (Block(0)),
  every script using the object is stopped (StopAllScriptsUsingThisObject), it is unlocked, unregistered (the
  tracker forgets it: FindObject no longer finds it at once) and put at the front of the manager's dead list; the
  pointers to its +0x40 target are cleared. The object itself is deleted later by the manager's task (priority
  1000, on the main loop's system task list), which deletes the dead list front first - so a dead object stays in
  memory until that task runs that frame. Deleting (DestroyBody) frees tags, unregisters, drops its script's
  on-exit scripts (they don't run) and deletes the script, and stops every script using it.
- **Missing objects** (code): FindObject null -> `name:` doesn't advance (see section 4.2). The tag getters return
  false with no tags. GetSingleTag with no unnamed checksum uses the **parameter structure's address as the name**
  [quirk]; the 0xEEEB7220 member function with no unnamed array reads the parameter structure **as an array**
  [bug]. RemoveTags with no array at all removes **every** tag.
- **Flags** (code+test): Obj_SetFlag / ClearFlag / FlagSet / FlagNotSet use +0x24 (never initialised by the
  constructor [quirk]). The bits: an unnamed array of names, each one's global integer a bit number (mod 32), else
  1 << the unnamed integer; none -> the function returns false. ClearFlag `all` clears every bit; FlagSet `clear`
  clears the bits after testing them.
- **Managers' walks** (test): ForEach 0x00461CC0 visits every object once even if the callback registers or
  unregisters objects (any list change restarts the walk from the front, skipping visited objects; up to 32 nested
  walks, one bit each).
- **Lists** (code): a manager keeps its objects sorted by node priority, highest first (0x0045A540).

## Tests
`src/hook/selftests_obj.cpp` (TestObjects, Early): three runs (CObject tags/flags/member functions; CObject
scripts/events/death/destruction; managers, walks with restarts and nesting, searches, dead list, destruction,
the tracker release, listeners), original vs ours on the same arena with everything else stood in for, compared
call by call and then the whole arena.

Mutations: 72 deliberate bugs, 71 caught (66 at first; the tests then got: the id right after Construct, bit
numbers of 33+, a partly-set multi-bit FlagNotSet, m_reassigning seen by Register, a node taken out by hand during a
walk, the dead list's order), 1 equivalent (NextItemOfType's null-object return: both give null).
