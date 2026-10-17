# wt/cobj: CCompositeObject and CBaseComponent (0x0045D050-0x0045EFD0)

## Batches (all NOT run in the game yet)

- [x] Batch cobj-A (NOT run in the game yet; commit c8219a6): `src/game/obj/composite.cpp` - Obj::CCompositeObject
  (vtable 0x00647650) and the base of its components, Obj::CBaseComponent (vtable 0x00647618), 44 functions plus
  GetComponent 0x0045DB90 (moved from object.cpp):
  - CObject's vtable slot 2 0x0045D850 (a member function table entry: its member function pointer on `this` +
    the entry's adjustment, or slot 3 by the entry's name).
  - CBaseComponent: constructor 0x0045D730, destructor body 0x0045D740 (ComponentDestroyedAllScripts if a script
    waited on it), deleting destructor 0x0045D780, Suspend 0x0045D760 (+0x1C), CallMemberFunction(entry) 0x0045D720
    (+0x2C: a jump to +0x30 with the entry's name - components ignore the entry's member function pointer).
  - CCompositeObject: constructor 0x0045E510, destructor body 0x0045D890, deleting destructor 0x0045E620, Update
    0x0045D920, Hide 0x0045D990, Suspend 0x0045D9E0, SetTeleported 0x0045E4C0, ShouldUpdateScripts 0x0045E500 (slot
    7), MarkAsDead 0x0045D7C0 (slot 6), AddComponent 0x0045DA30, CreateComponentsFromArray 0x0045DA90,
    CreateComponentFromStructure 0x0045EE20 (+0x24), InitFromStructure 0x0045DAD0, Finalize 0x0045DB30 (+0x20),
    CallMemberFunction(name) 0x0045E450 (slot 3).
  - Script member functions (table 0x00683268, filled in by 0x006425D0 - 13 of its 30 entries point at code Ghidra
    never made functions of): Obj_GetPosition 0x0045DBC0, Obj_PrintDetails 0x0045D7F0, [0x406998A5] 0x0045D810 (a
    component from the parameters), [0x2BFFEAA3] 0x0045DBF0 (has a component of the type name=), Obj_SetPosition
    0x0045D830, Obj_ForceUpdate 0x0045DC40, Obj_GetVelocity 0x0045DC50, GetSpeed 0x0045DC80, Hide / Unhide / IsHidden
    0x0045DCD0 / 0x0045DD20 / 0x0045DD70, [0x10C1C887] 0x0045DD80 (moves along its own axes by x= y= z=), Suspend /
    Unsuspend 0x0045DED0 / 0x0045DF20, Obj_GetDistanceToObject 0x0045E0B0 (in feet), [0xF8CFD515] 0x0045E260
    (velocity against the at axis < 0), [0x7DCC5FB9] / [0xE5DF66D7] / [0xDD468509] 0x0045E2A0 / 0x0045E310 /
    0x0045E380 (speed within 0.1 of / above / below the unnamed float), Obj_GetType 0x0045E3F0, Obj_HasComponent
    0x0045E410, Pause / Unpause 0x0045E9B0 / 0x0045E9D0. (Names found by checksumming candidates; bracketed: not
    found, the C++ names say what they do.)
  - Quirks kept: CreateComponentFromStructure and Obj_HasComponent with no type use the parameter structure's
    address as the type; a type the manager can't make gives a null component that is added anyway (crash);
    AddComponent doesn't clear the component's next pointer.
  - Test: selftests_cobj.cpp TestCompositeObjects (life/update/components, structures, member functions). 72
    deliberate bugs: 62 caught at first, 70 after strengthening; 2 equivalent in practice (the order of the sums of
    squares in GetSpeed's length and in Obj_GetDistanceToObject: no float inputs found in 2M random tries where it
    changes a result).
- [x] Batch cobj-B (NOT run in the game yet; commit 24b3565): composite.cpp, 7 more: Obj_GetOrientation 0x0045DF70
  (display matrix at row, unit length), Obj_GetDistToNode 0x0045E020, Obj_GetOrientationToObject 0x0045E140
  (dotprod with the right axis and the angle in degrees - 90), Obj_SetOrientation 0x0045E650 (dir= / quat= / x y z
  angles; also sets the display matrix), Obj_GetQuat 0x0045E940, FindNearestObjectOfType 0x0045ED30, [0x37185D8A]
  0x0045EF30 (the nearest object of type 0xBAF9B341 lies more than the unnamed angle off its at axis). Precision: the
  original keeps FastACos's x87 result unrounded here (FastACosUnrounded in composite.cpp). **Mth::Vector::Normalize
  (vector.cpp) changed**: the original keeps the length and 1/length in double precision; ours rounded both to
  floats (1-ulp differences; the existing vector tests still pass). Test: RunMathTest; 42 deliberate bugs, 26
  caught at first, 41 after strengthening; 1 equivalent (FindNearestObjectOfType's null check after the first
  search: the loop returns null anyway).
- [x] Batch cobj-C (NOT run in the game yet): `src/game/script/node_manager.h/.cpp`, SkateScript::CNodeManager
  (0x0045D050-0x0045D6E0, 19 functions): the singleton 0x006F0D84 / 0x006F0D88 (Create 0x0045D690, Release
  0x0045D6E0 - singleton.cpp's SingletonPtr instance and main.cpp now call ours; the selftests_misc / selftests_gapsB
  tables stand in for ours too), constructor 0x0045D370, AddNodeArray 0x0045D520 (a global array of nodes: hashed,
  position= turned into pos= with z negated, links= turned from indices into node names), RemoveNodeArray 0x0045D1C0,
  RemoveNodeArrayOfFile 0x0045D670, HasNodeArray 0x0045D270, HashNodeIn 0x0045D170, HashNodes 0x0045D3A0,
  ResolveLinks 0x0045D480, the last array / name 0x0045D120 / 0x0045D150, the walk over every node (StartWalk
  0x0045D050, EndWalk 0x0045D110, NextNode 0x0045D3F0), an owner pointer 0x0045D140 / 0x0045D470,
  MakeNodeArrayName 0x0045D2A0 (levels\nj\nj.qb -> nj_nodearray), NodeArrayChecksum 0x0045D310. The hash helpers just
  before the range (0x0045CE40 / 0x0045CE60 / 0x0045CEF0 / 0x0045CFC0, 0x0045C900, 0x0045BE40) stay GameFunction.
  Test: RunNodeTest; 39 deliberate bugs, all caught (5 by a crash, 1 after strengthening).

## Not done in this range

- **0x0045E9E0** (member function 0x0168B09C, probably Obj_GetCollision): 848 bytes of x87 math building a feeler
  (CFeeler 0x00417760, collision 0x004172F0, a smart pointer at feeler +0x90) from length= height= distance= and
  0x5AF4C4BA= (each * 12) along its matrix rows, aimed by the flags 0xDC7EE44A (side) / 0xE3006FC4; returns whether
  it hits. Left for a batch with the feeler code.

## Header changes (for the merger)

- `object.h`: CBaseComponent (full), COMPONENT_FLAG_*, COMPOSITE_FLAG_* (compositemgr.h's duplicate PAUSED removed),
  the full CCompositeObject layout (m_pos +0x4C, m_oldPos +0x5C, m_matrix +0x6C, m_vel +0xAC, +0xBC byte,
  m_displayMatrix +0xC0, mp_components +0x100, m_compositeFlags +0x104) and its methods; includes mth/matrix.h and
  vector.h; a `CObject::CallMemberFunction(CMemberFunctionEntry*, ...)` overload (0x0045D850), so
  `&CObject::CallMemberFunction` needs a static_cast (object.cpp and selftests_obj.cpp fixed).
- `cscript.h`: CMemberFunctionEntry as wt/left made it (mName / mCode / mThisAdjustment) - nothing of mine left.
- compositemgr.cpp calls composite.cpp directly and uses the real fields; composite.cpp calls the manager's
  AddComponentByType / RemoveComponentByType / CreateComponent directly; selftests_coim.cpp stands in for ours too.
- tracker.cpp's Position() reads `CCompositeObject::m_pos`.
- vector.cpp: Normalize's precision (above).

## Findings for others

- front_end.cpp's comment (and script-runtime.md section 2) say that while paused "only the 0xC kinds of logic task
  run"; the task list runner 0x00406AC0 does the opposite: a task runs when `(list mask & task+0x14) == 0`, so with
  the mask 0xC the tasks whose kind has bit 2 or 3 are *skipped*. Worth a look by whoever owns front_end.cpp.
- Mth::FastACos (0x00403EF0) returns a float in ours; the original leaves ST0 unrounded, and callers that keep using
  it in x87 (0x0045E140) see the extra precision. Other callers of FastACos / Vector::Length may differ by an ulp.

## For script-runtime.md

### Section 12 additions (Objects and their scripts)

- **CCompositeObject** (code: `src/game/obj/composite.cpp`; vtable 0x00647650, 11 slots; CObject's fields then
  position +0x4C, orientation matrix +0x6C, velocity +0xAC, display matrix +0xC0, components +0x100, flags +0x104).
  Its constructor (0x0045E510) sets CObject flag **0x20** - the "has a position" flag the tracker's broadcast radius
  check reads (answers half of the 0x20 open question). Its flags (+0x104): 1 paused, 2 suspended, 4 hidden, 8
  teleported (cleared at the end of each update), 0x20/0x40 finalized, 0x80 being destroyed.
- **One object update** (0x0045D920, code + test): paused -> nothing at all. Otherwise, unless suspended: the
  object's own script gets **one Update** (if it has one), then if that killed the object (MarkAsDead) the update
  stops there; then its own update (vtable +0x28: subclasses' - the skater's etc.; empty in the base). Then each
  component's Update in list order (the order they were added), skipping suspended components, **stopping as soon
  as a component kills the object**; then position w = 1 and TELEPORTED cleared. A suspended object's components
  still update; its script doesn't.
- **PAUSE_WITH_OBJECT** (section 2): the object's vtable +0x1C is 0x0045E500 for composite objects: "not paused"
  (flag 1). (CObject's own is 0x005B9760: always true.) So a spawned script with PAUSE_WITH_OBJECT waits exactly
  while its object is paused (Pause member function, or PauseGame through the composite object manager 0x0045F5D0,
  which sets/clears flag 1 on its objects - wt/coim's range).
- **Member functions on composite objects** (code): `obj:Name` first asks each component in turn (vtable +0x30:
  0 = false, 1 = true, 2 = not mine), then CObject's names (SetProps, Die). A member function table entry with a
  member function pointer is called directly (0x0045D850, `this` moved by the entry's adjustment); with none, by
  name as above. A component given a table entry (0x0045D720) always goes by the entry's name.
  The table is 0x18-byte entries `{name, component type, 16-byte member function pointer}`: CObject's at
  0x00683550 (type 0x57525413), CCompositeObject's at 0x00683268 (type 0x0ADA4E74, 30 entries).
- **Obj_ForceUpdate** (0x0045DC40, test): a whole object update (its script included) runs at once, nested inside
  the calling script's statement - when called by the object's own script, that script's Update runs re-entrantly.

### Section 13 answers

- **Who updates object scripts, and when** (code, not run): the composite object manager (0x006F0FB8) makes a task
  in its constructor (0x0045F6E0, wt/coim's range): a Tsk::Task (vtable 0x006476F0) of **priority -900** whose code
  0x0045F6D0 calls the object update walk 0x0045F4A0 on the manager, added straight to Mlp::Manager's **logic task
  list** (+0x04, 0x00406C90). Task lists are kept sorted highest priority first (0x0045A540 inserts a node before
  the first one whose priority is <= its own) and run from the head (0x00406AC0). The front end's logic task
  (UpdateSpawnedScripts, section 2) is priority -2000 on the same list and its keyboard task -1000. So **every frame
  the composite objects update (their own scripts, then their components) before UpdateSpawnedScripts runs the
  spawned scripts.** Consequences: an event a spawned script sends to an object is handled by the object's script
  (ReceiveEvent, synchronous) during the spawned walk, but the object's next ordinary Update is the next frame; an
  object script's one-per-frame wait sees "one-per-frame released" (0x006F352C) as the previous frame's spawned walk
  left it (UpdateSpawnedScripts clears it at its start, after the objects have run). Other tasks on the logic list
  between -900 and -2000 (if any) aren't identified yet.
  - The task's kind mask (+0x14) is left 0 by the task constructor 0x00406890, so the pause mask (the list's +0x04
    word = 0xC while paused; 0x00406AC0 runs a task only when `list mask & task+0x14` is 0) never stops it; pausing
    works through each object's own paused flag (0x0045F5D0 sets it on every object; an object update then returns
    at once). (wt/coim found the same order: the -900 task runs the object walk before the front end's -2000.)
  - Updates outside the walk (code): Obj_ForceUpdate 0x0045DC40 (script); 0x00501600 (a skater component's member
    function 0x8819DD8B: after moving the skater it calls SetTeleported(true) and, when the object's +0xBC byte is
    set and 0x00580880 returns non-null, updates the object at once); 0x00583DA0 (vtable 0x0064EF48's class, on a
    restart: the object named 0x967C138C or 0xE17B231A gets SetTeleported(false) and an immediate update).
- **What an object does with a targeted event** (code): CObject's vtable +0x14 (0x00461140, PassEventToScript) is
  **not overridden** by CCompositeObject (0x00647650) nor by any of its three subclasses (vtables 0x0064EC44,
  0x0064EF48, 0x0064F138: constructors around 0x0057826D, 0x00582DC3, 0x0058987F). Components never see targeted
  events through it: an event to a skater/ped/level object goes to that object's own script only. Of the 21
  vtables with CObject's slot 2 (0x0045D850), the only ones with a different +0x14 are in the screen element range:
  0x004A8100 (vtable 0x0064A048), 0x004AA560 (0x0064A0BC), 0x004AE6A0 (0x0064A154 / 0x0064A188 / 0x0064A1BC) -
  screen elements handle targeted events themselves (not decompiled; likely where the menus' pad events become
  actions, section 11.5).
- **Unidentified checksums** found: 0x406998A5, 0x2BFFEAA3, 0x10C1C887, 0xF8CFD515, 0x7DCC5FB9, 0xE5DF66D7,
  0xDD468509, 0x37185D8A (CCompositeObject member functions, by what they do: component from params, component
  exists (name=), move along own axes, moving backwards, speed near / above / below, nearest-of-type outside an
  angle), 0x976B04A3 (Obj_GetDistanceToObject's result), 0x24495505 (Obj_GetType's result), 0xC97F3AA9
  (Obj_GetOrientationToObject's angle result).

