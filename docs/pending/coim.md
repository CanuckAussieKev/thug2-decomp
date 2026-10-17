# wt/coim: the composite object manager and the COIM heap (0x0045EFD0-0x0045F7A0)

NOT run in the game yet.

## What was rewritten (src/game/obj/compositemgr.h/.cpp; tests src/hook/selftests_coim.cpp)

All 18 functions of the range:

- `Obj::CCompositeObjectManager` (CGeneralManager + 0x408 bytes, vtable 0x006476D4, the singleton 0x006F0FB8 with
  references 0x006F0FBC; 0x0045F7A0 - not in this range - makes it 0x448 bytes **zeroed**, which is why the
  constructor never sets the component count):
  - 0x0045F6E0 Construct: CGeneralManager, the logic task (Tsk::Task vtable 0x006476F0, code 0x0045F6D0, data the
    manager, priority **-900**) added to the main loop's **logic** list (Mlp::Manager +0x04), then the member function
    tables 0x00683550 (CObject's, type 0x57525413) and 0x00683268 (CCompositeObject's, type 0x0ADA4E74) registered.
  - 0x0045F200 DestroyBody (task removed 0x004068F0 and deleted through its vtable slot 2, then CGeneralManager's),
    0x0045F300 the scalar deleting destructor.
  - 0x0045F270 RegisterComponent(type, create, registerFn) - 81 calls, all from 0x004E4860 (Sys::Main right after
    the manager is made); `registerFn` is called at once; no bound check on the 128 slots.
    0x0045F2A0 CreateComponent(type) (first matching entry's maker; none: null).
  - Lists of all components of a type: the static array 0x006F0DB8[128] (index = the type's entry), linked through
    the component's +0x14, type at +0x04. 0x0045F2D0 GetFirstComponentByType, 0x0045F3E0 AddComponentByType (front;
    unregistered type: nothing), 0x0045F420 RemoveComponentByType (clears +0x14; searches every entry with the type).
  - 0x0045F330 CreateCompositeObject (Calloc 0x108, CCompositeObject ctor 0x0045E510, SetType 0xCDEB9229, register
    through vtable slot 1; out of memory -> SetType on null [bug: crash]).
    0x0045F3A0 CreateCompositeObjectFromNode(CArray* components, CStruct* nodeData, bool finalize): 0x0045DAD0(nodeData),
    0x0045DA90(components, nodeData), then vtable +0x20 if `finalize` (CFunc_CreateCompositeObject passes true).
  - 0x0045F4A0 Update (the object walk) and 0x0045F5D0 Pause(bool) - see below; 0x0045F6D0 the task code.
  - 0x0045F1E0 ReleaseCompositeObjectManager: the last reference **forgets the instance without deleting it** [quirk].
- The COIM heap (the remains of a "CCompositeObjectInstanceMgr"): 0x0045EFD0 DestroyCoimHeap ("Destroying COIM heap";
  empties an instance list at 0x006F0DB0 that nothing in this build fills, deleting each entry's object through
  vtable slot 1, then `std::list::erase` 0x004ECC30 on the (empty) rest; heap removed from the memory manager, region
  deleted), 0x0045F0B0 CreateCoimHeap(size) (destroy, then "COIMHeap" bottom-up over an AllocRegion of `size`; heap
  +0x64 = true, +0x68 = 0x006F0D90 - 16; reads script integer 0xBA2C7CCB and ignores it; returns false),
  0x0045F160 InitCoim (the script command InitCOIM's work; the values it stores, 0x006F0D8C/0x006F0D90/0x006F0D98 and
  0x0067D804/0x0067D808, are read by nothing else), 0x0045F1C0 ResizeCoimHeap (ResizeCOIMHeap's).

## Changes outside the range

- object.h untouched. New header compositemgr.h. CBaseComponent is still only declared: compositemgr.cpp reads a
  component's +0x04 (type) and +0x14 (next of type), and a composite object's +0x104 flags, through local accessors
  (TypeOf / NextOfType / CompositeFlags) - switch them to real fields when wt/cobj's CCompositeObject /
  CBaseComponent layouts land (`COMPOSITE_FLAG_PAUSED` = +0x104 bit 0 is defined in compositemgr.h).
- Calls into wt/cobj's range through GameFunction, marked `// -> 0x... (wt/cobj)`: 0x0045E510 (constructor),
  0x0045D920 (Update), 0x0045DAD0, 0x0045DA90.
- front_end.cpp PauseGame calls `Obj::CompositeObjectManager()->Pause(pause)` directly; its test
  (selftests_menu.cpp) stands in for ours too. **Its comment about the pause mask was wrong and is fixed** (see below).
- sys/main.cpp calls `Obj::ReleaseCompositeObjectManager()`; singleton.cpp's SingletonPtr instance for 0x0064AFD4 is
  now `Spt::CompositeObjectManager` (Create still the game's 0x0045F7A0); selftests_gapsB / selftests_misc stand in
  for our Release too.

## Tests

TestCompositeObjectManager (Early, three original-vs-ours runs on stand-in managers/objects/components, everything
called outside stood in and recorded, arena + globals compared): construction (dirty memory, no memory for the task),
the task code, component types (with/without register function, a type twice, unknown), the component lists
(add/remove head/middle/tail/absent/unregistered/second entry), making objects (from a node with/without finalize, no
memory), destruction (task / no task, free flags), the reference count; the walks (plain, walks in progress, an object
unregistered and another registered mid-walk -> restart, nested Update, nested Pause, next object unlinked by hand,
a node without an object ending the walk, empty list); the COIM heap (init, resize with an instance list, resize to 0,
destroy with nothing / a count but no list, create 0 / a size, a heap without region).
Mutations: see the batch record at the end of this file.

---

## For docs/script-runtime.md (paste-ready)

### The frame: who updates objects, and when (answers section 13's first question)

**The main loop** (Mlp::Manager::MainLoop 0x0044EEF0, code) runs, every frame, the **system** task list
(Mlp::Manager +0x7C) and then the **logic** task list (+0x04), then renders.

**A task list** (code: add 0x00406C90 -> 0x00406A80 -> Lst::Head insert 0x0045A540; run 0x004070A0 -> 0x00406AC0):
- Tasks are kept by priority, **highest first**; a task added with the same priority as others goes **before** them
  (newest first).
- A run goes from the front, each task at most once per run (a run counter is stamped into the task, +0x10). If the
  list changes during the run (a task added: +0x1C set), the walk starts again from the front, skipping tasks
  already stamped.
- A task is skipped when its +0x04 is non-zero, or when the list's mask (+0x04 of the list; the logic list's is
  Mlp::Manager +0x08) shares a bit with the task's kinds (+0x14). **The test is `mask & kinds` != 0 -> skip**
  (0x00406B61-0x00406B6D). So PauseGame's mask 0xC *skips* the logic tasks of kinds 4 and 8 while paused; it does
  not restrict the list to them (section 2 said the opposite). Tasks constructed by 0x00406890 have kinds 0 (never
  skipped by the mask); neither the composite object manager's task nor the front end's logic task is given kinds
  anywhere I looked, so both run while paused.

**The composite object manager's logic task** (0x0045F6D0, priority **-900**, made by its constructor 0x0045F6E0,
on the logic list) (code) calls its Update (0x0045F4A0). The front end's logic task (0x0054FCC0) has priority
**-2000** on the same list. So **every frame, every composite object is updated - its own script included - before
the front end's Update runs UpdateSpawnedScripts** (section 2). In frame order:

1. System tasks, among them each CGeneralManager's FlushDeadObjectsTask (priority 1000) - the composite object
   manager is one, so objects marked dead last frame are deleted here, before anything of this frame's logic.
2. Logic tasks, highest priority first: the input manager's task (0x0044E7F0's singleton, made at 0x0044E76A with priority
   2000, added by Sys::Main) ... **the object walk (-900)** ... **the front end (-2000): FrontEnd::Update -> UpdateSpawnedScripts**.
   (Other logic tasks - e.g. the skate module's two tasks of priority -1000 made at 0x0056E2B0 - were not placed;
   any between -900 and -2000 run between the two.)
3. Render.

Consequences (code):
- A script spawned by an object's own script (or by a component) during the walk is at the head of the spawned list
  before UpdateSpawnedScripts starts, so **its first Update is in the same frame** (unlike scripts spawned during
  UpdateSpawnedScripts, section 2).
- Whatever an object's script does in a frame (events it launches, globals it sets) happens before that frame's
  UpdateSpawnedScripts; whatever spawned scripts do reaches objects' scripts' own statements only next frame (an event
  handled at once, section 11, is the exception).
- Spawned scripts with `PAUSE_WITH_OBJECT` are checked against their object's vtable +0x1C in UpdateSpawnedScripts,
  after the object has updated this frame.

**Other updates outside the walk** (code, not followed in detail): CCompositeObject's update 0x0045D920 is also
called from 0x0045DC40 (wt/cobj's range: a `bool (CStruct*, CScript*)`-shaped member function that updates its
object at once and returns true - presumably a script member function), and from component code at 0x00501600 and
0x00583DA0 (the latter updates the object with id 0x967C138C or 0xE17B231A - a camera, by the look of it - once more
on the spot). An object updated there runs its script again that frame.

### The object walk (0x0045F4A0) (code + test)

- Every object of the composite object manager's list, **in list order: node priority highest first, equal
  priorities newest first** (CBaseManager::RegisterObject -> 0x0045A540). A new object's node priority is 0
  (CObject's constructor); CBaseManager::SetObjectPriority moves it.
- It is CBaseManager::ForEach's walk (section 12) with the work inline: one bit of the manager's +0x18 per walk in
  progress (nested walks get the next free bit), every object's bit cleared first, the next object looked up
  **before** the current one updates; when the list changes during an update (an object registered or unregistered:
  the manager's +0x1C set), the walk starts again from the front, skipping objects already updated. So an object made
  during the walk (its visited bits are 0) **is updated in the same frame**; an object unregistered (e.g. marked dead)
  before the walk reaches it is not; a node with no object ends the walk (test).
- Each object: CCompositeObject::Update (0x0045D920, wt/cobj): +0x104 bit 0 (paused) -> nothing at all; bit 1 ->
  no script, no vtable +0x28, components still updated; otherwise the object's own script (`m_script->Update()`, the
  result ignored), then - unless the object is now dead - its vtable +0x28, then each component not suspended
  (component +0x08 bit 0) in list order (+0x10), stopping if the object dies; finally +0x58 = 1.0 and +0x104 bit 3
  cleared.

### Pausing objects (code + test)

- `CCompositeObjectManager::Pause(bool)` (0x0045F5D0) sets or clears +0x104 bit 0 on **every composite object that
  exists at that moment** (same walk). Callers: PauseGame (0x0054F960, the front end) and the script commands
  PauseObjects / UnPauseObjects (0x005A0620 / 0x005A0630).
- While paused, the object walk still runs every frame (the task isn't masked); each paused object's update returns
  at once - **its script doesn't run, its components don't update**. Spawned scripts on the object with
  `PAUSE_WITH_OBJECT` are held by UpdateSpawnedScripts through the object's vtable +0x1C (section 2).
- [quirk] An object created while paused is not paused (Pause only touches the objects it walks). Unpausing clears
  the bit on every object - also on objects paused by PauseObjects before the game was paused.

### Creating and destroying composite objects (code + test)

- Component types: Sys::Main calls 0x004E4860 right after making the manager, which registers 81 component types
  (RegisterComponent: type checksum, maker, optional register function called at once). CreateComponent(type) calls
  the first matching maker.
- Every component is also on the list of its type (static, 0x006F0DB8[]), newest first, through its +0x14 -
  GetFirstComponentByType walks it (e.g. 0x00565DA0 after the render: all components of type 0x1CDFB2A3).
- CreateCompositeObject (0x0045F330): 0x108 bytes, CCompositeObject's constructor, type 0xCDEB9229, registered with
  the manager (so with the tracker: an id - unique `autoid` if none - and findable by FindObject at once).
  CreateCompositeObjectFromNode (0x0045F3A0; CFunc_CreateCompositeObject 0x00449540 with `components=` and
  `params=`, finalize true): registered **before** its id/position are read from the node data (0x0045DAD0: the
  `name` -> SetID, re-registering it), then its components made from the array, then Finalize (vtable +0x20).
- Destroying: as for every CGeneralManager object (section 12): MarkAsDead -> the manager's dead list, deleted by
  FlushDeadObjectsTask at the start of the next frame's system tasks.

### COIM heap (code)

The script commands InitCOIM (0x004492E0) and ResizeCOIMHeap (0x00449380) make a heap "COIMHeap" of the given size
(bottom-up, its own AllocRegion); nothing else in this build uses the "CCompositeObjectInstanceMgr" values it keeps.
CFunc_Cleanup (0x0059FE50) destroys it.

---

## Mutation record

47 deliberate bugs (constants, vtables, task set-up, each branch of the component lists, the walk's bit / clear /
visited / restart / end / next-before-update, pause set/clear, the reference count, every COIM step): 45 caught, 6
of them only after strengthening the test (a second 0x30 entry's list recorded, a removed component's link recorded,
the task code run on a non-empty list, objects with +0x104 bit 1 set, the globals recorded after a resize to 0).
2 equivalent: DestroyCoimHeap's `node != head` re-check inside the loop (the loop never reaches the head unless a
deleted object changes the list head) and erase(head->next, head) vs erase(head, head) after the loop (both empty).

Checks: thug2_check 0 MISMATCHED, all early tests pass (the unrelated "PC async handle (synchronous I/O)" test
failed in 2 of 4 runs while other worktrees' checkers were running - it touches no code of this batch); register
scan: no new RISK, the five value-returning functions listed; call audit: no MISMATCH (0x00406C90 "?" as everywhere -
a tail jump); check_code_writes: no overlaps; find_gaps: 0 thunks, and **0x0045F7A0** (the singleton's Create, 76
bytes, right after this range) is the one gap left next to it - not taken (outside the range given).
