# frontend-ui - the front-end UI range 0x4A4500-0x4B2280 (screen elements & text)

Agent "frontend-ui" (branch worktree-agent-ab742dae3358eabeb). The user's request: finish the front-end UI range
(docs/subsystems.md "Front-end UI: screen elements & text") start to finish. Batch records below, in the style of
decomp-progress.md (the lead numbers them when merging).

- [x] Batch FE-1 (NOT run in the game yet; early test passes offline): the animation controllers (new
      src/game/gfx/anim_controller.*), 0x4A4500-0x4A5730 - the range starts with Gfx code, not the front end: the
      controllers an object's animation blender stacks (made by the factory 0x00481FB0 from a structure's Type with
      its Priority; the base controller 0x00481710 / vtable 0x0064877C isn't rewritten). Vtable slots: 0 scalar
      deleting destructor, 1 GetPose(pose: 64 rotations, 64 translations at +0x400, more at +0x800), 2 0x005BAB90,
      3 CallMemberFunction(name, params, script) -> 0/1/2, 4 InitFromStructure, 5 Update. Rewritten:
      - the standard controller (type 0xCB4533FF, vtable 0x00649F50, 0x28 bytes, its own item pools 0x00701430):
        operator new 0x4A4F30 (`(size, int)`, cdecl) / delete 0x4A5600 (Mem::PoolPair), Construct 0x4A4500,
        DestroyBody 0x4A4590, Destroy 0x4A55E0, GetPose 0x4A4F70, CallMemberFunction 0x4A4670 (PlayAnim 0xAF2FAE19:
        AnimName / StartTime / EndTime / LoopType / BlendPeriod / Speed on the channel's timeline (0x00481430), the
        frame from the frame source by anim set + name; 0xC68365EB: a new frame source for Anim; 0x92267217;
        InvalidateCache: refetch, then 2 = "not mine"), Update 0x4A4620 (the timeline steps (0x004814B0), its events
        between the two times (0x00481E10); the frame refetched when the time went back);
      - the weighted controller (type 0x7E1940A1, vtable 0x00649F68, 0x24 bytes; name not recovered): Construct
        0x4A5010 (`(timeline, object)`: the channel's object, or `object` without a timeline), DestroyBody
        0x4A4820, Destroy 0x4A50B0, GetPose 0x4A5660 (nothing with no frame / source or a weight <= 0 - NaN goes on;
        the skeleton component read through null without one, as the original), CallMemberFunction 0x4A4890;
      - the wobble controller (wobble, vtable 0x00649F80, 0x38 bytes): Construct 0x4A49F0, DestroyBody 0x4A4A80,
        Destroy 0x4A50E0, GetPose 0x4A5110, CallMemberFunction 0x4A4AF0 (PlayAnim, 0xD0209498: the target as a
        0..1 fraction 0x4D747FA0 of the timeline, SetWobbleDetails: all five of wobbleAmpA/B, wobbleK1/K2,
        spazFactor or nothing), WobbleTime 0x4A5180 (amplitudes grow by spazFactor towards the timeline's ends,
        the target kept clear of the ends, two sines of the game clock + 0x00696D60 (x87 FSIN, the cycles' integer
        part through _ftol), clamped to the timeline), Update 0x4A52E0;
      - the pose controller (pose, vtable 0x00649F98, 0x854 bytes): Construct 0x4A5730 (64 identity rotations; the
        rest left), DestroyBody 0x4A4D20, Destroy 0x4A5320, GetPose 0x4A4D30 (0x840 bytes copied);
      - the partial animation controller (partialAnim, vtable 0x00649FB0, 0x24 bytes): Construct 0x4A5350 (a
        timeline of its own: Calloc + 0x00481650), DestroyBody 0x4A4D50, Destroy 0x4A5400, GetPose 0x4A5430,
        InitFromStructure 0x4A4DD0 (base's, AnimName's times / loop type (0x004877E0 / 0x00487770), Speed (1),
        Current through the timeline's PlayAnim slot), CallMemberFunction 0x4A54A0 (0x6AAEB76F: one step of
        0x06C9F278 frames, the speed held at 0 meanwhile; 0xBD4EDD44 Speed (per 1/60 s); GetPartialAnimParams;
        0xF5E2B871: direction 0 <-> -1; InvalidateCache), Update 0x4A4EF0;
      - 0x4A4610 (slot 4 of four of them): a jump to the base's InitFromStructure.
      Findings: a missing PlayAnim AnimName leaves the original's argument slot (the params' address) as the name -
      kept; other missing values were uninitialised locals in the original (0 here). 32 functions.
      Early test (src/hook/selftests_fe_anim.cpp, with the shared src/hook/fe_testkit.h: arena pair, stand-in
      structures, float-returning stand-ins): every function, original vs ours, on stand-in controllers, timelines,
      objects, skeletons, frame sources and parameter structures; every external call recorded (frame sources,
      timeline pieces, anim set, memory manager and pools, structure getters / adders); the member functions'
      every case incl. missing parameters, NaN / equal / backwards updates, the wobble's clamps with positive and
      negative amplitudes, the pool paths of new / delete. 41 deliberate bugs caught (2 after strengthening: the
      wobble's clamps).
- [x] Batch FE-2 (NOT run in the game yet; early test passes offline): CScreenElement, the base of every front-end
      element (src/game/front/screen_element.*, rewritten from the stub), 0x4A8610-0x4AAB30. The class now
      derives from Obj::CObject and has its whole layout: the four smart-pointer links (+0x4C parent, +0x58 first
      child, +0x64 next, +0x70 previous sibling), three 0x44-byte states (SElementState: alpha, pos3D, x / y, z,
      top left, scale, base scale, angle, rgba) - +0x7C the one shown, +0xC0 the one morphed to, +0x114 the summed
      one the layout pass leaves (the parents' included) -, the morph's start / length / last fractions
      (+0x104..+0x110), justification +0x158, size +0x160, z_priority +0x16C, remembered alpha +0x170, priority
      +0x174, and the flag bits (0x400 dirty, 0x800 children dirty, 0x1000 / 0x8000 z, 0x2000 dims locked, 0x4000
      the focus flag, 0x20000 / 0x40000 paused, 0x80000 pos3D, 0x100000 behind the camera, 0x200000
      block_events, 0x400000 hidden, 0x800000, 0x1000000, flip_h / flip_v, 0x10000000 priority). Rewritten:
      state copy 0x4A8730, Construct 0x4A8F90, DestroyBody 0x4AAB30 (out of the tree; id 0xDEADBEEF), Destroy
      0x4AAC90; GetJust 0x4A8610, GetPos 0x4A87C0, GetTopLeft 0x4A87E0, GetPriority 0x4A86C0 (source only, 7
      bytes), ManagerUsesPriorities 0x4A86B0, EventsBlocked / HasPriority / IsHidden 0x4A8F00 / 0x4A8F20 /
      0x4A8F40, SetPaused 0x4A8680, SetFocusSent 0x4A8940 (the element's 0x3056434F / 0x4A57316B event);
      PlaceTopLeft 0x4A8C30, SetPos 0x4A9100, SetPos3d 0x4A91F0, SetDims 0x4A92D0, SetJust 0x4A9390 (the position
      moves so the element stays put), SetScale 0x4A94A0 (relative: times the base scale; else it becomes the
      base), SetAlpha 0x4A8630, SetRotation 0x4A8800 (degrees -> radians plus 0.001, compared unrounded),
      SetRGBA 0x4A88C0 (the shown colour set without comparing), StartMorph 0x4A8870, SetZPriority 0x4AA390
      (+1 per generation; an element's own z_priority wins), SetChildrenPriority 0x4AA470; GetParent 0x4A8B80,
      GetWindow 0x4A8BB0, GetLastChild 0x4A95E0, CountChildren 0x4A9600, GetChild 0x4AA2A0 (by id, or by index
      when the id is 0), GetChildById 0x4AA690, GetChildByIndex 0x4AA6D0, SetParent 0x4AA8E0 (out of the old
      parent's list, last in the new one's; the second argument is unused); the layout pass UpdateLayout
      0x4A9D30 (pos3D projected through the first viewport's camera - behind it: -320, -224 -, the morph: four
      curves (linear, 0xF5B95CE4 ease out 2t - t^2, fast_in t^2, gentle (3 - 2t)t^2) - the position follows the
      curve, scale / angle / alpha / rgba the time, both as fractions of what was left last frame -, the summed
      state from the parent's (rotated by its angle: x87 FSIN / FCOS), slot 10, the children); ResolvePos 0x4A8C80,
      ResolveJust 0x4A8DC0, ResolveRGBA 0x4A8E90 (shifts past 31 bits wrap as SHL does); the vtable's
      SetProperties 0x4A96B0, CallMemberFunction 0x4A89B0 (SetProps, DoMorph - the script waits `time` -, Die,
      GetProps), GetProperties 0x4AA710, SetMorph 0x4A9AA0 (+ 0x4AEE50, a jump to it, source only); and
      Mth::Vector::operator!= 0x4A86D0 (any component differs or is NaN). 45 functions.
      Findings: the original's x87 products come in two operand orders for the top left (dims then scale, scale
      then dims) - kept, as they round differently; SetMorph's ResolvePos can return true without writing x / y
      (a Pos structure without relative / proportional): the original then passes its argument's slot (the
      params' address) as y - kept; CObject's +0x3C (m_aliasCount) is the element's use count during the layout
      pass. Element3d now calls CScreenElement's functions directly and uses the named fields.
      Early test (src/hook/selftests_fe_element.cpp on the shared world src/hook/fe_element_fixture.h: element
      trees in an arena with real smart-pointer links and patterned states, a recording vtable): every function,
      original vs ours, the trace of calls out (the object base and tags, the tracker's events, the manager, the
      camera's projection, the clock, structure getters / adders, arrays, memory) and the whole arena after each
      step: construction on dirty memory, destruction of inner / first / root elements, flags up parent chains,
      the focus flag's odd values, every placement setter (different / equal / NaN, now or not), the tree queries
      and SetParent moves (middle, first, last child, into an empty parent, out of the tree), the layout pass
      (each curve, time before / at / past the end, paused, pos3D in front / behind / keeping its scale, a
      changed parent, the root), the parameter helpers (each Pos form, each justification form, rgba arrays of
      2 / 4 / 6), properties / morph / GetProps / the member functions. 61 deliberate bugs caught (6 after
      strengthening: a NaN left in a position masked two; the object base's flags; an exactly finished morph;
      restore_alpha alone); 1 equivalent (DestroyBody's own release of the next-sibling link: SetParent already
      released it).
- [x] Batch FE-3 (NOT run in the game yet; early test passes offline): the menus and the container element (new
      src/game/front/menu_element.*), 0x4A74E0-0x4A8100 and 0x4AA530-0x4AACC0. CBaseMenu (vmenu / hmenu, vtable
      0x0064A048, 0x1A8 bytes; fields named: selected index / id, focus, controller, internal_just, vertical,
      regular_space_amount, padding_scale, spacing_between, pad handling, allow_wrap, grid index): Construct
      0x4A74E0, ConstructVMenu 0x4A75C0, ConstructHMenu 0x4A7630, DestroyBody 0x4A7620, Destroy 0x4A76C0,
      SetProperties 0x4A76F0, GetRegularSpace 0x4A7550, SetSelectionTags 0x4A7580, MoveSelection 0x4A7850 (twice
      the item count of tries; wrap or turn back at the ends; skips not_focusable / block_events items), Step
      0x4A7A40 (pad: unfocus, move, focus with controller and grid index), Arrange 0x4A7B80 (regular spacing or
      sizes x padding + spacing_between; locked dims only grow; the first item focused when nothing is
      selected), PassEventToScript 0x4A8100 (vtable slot 5: pad-event priorities, the focus flag event re-finds
      the selection and arranges, focus with grid_index / child_id, unfocus, the four pad directions).
      CContainerElement (ContainerElement, vtable 0x0064A0BC): Construct 0x4AAC20, DestroyBody 0x4AAC80,
      Destroy 0x4AACC0, SetProperties 0x4AA530 (focusable_child - a function Ghidra hadn't found),
      PassEventToScript 0x4AA560 (focus / unfocus passed to the focusable child with the controller). The
      screen elements' smart-pointer instances: ConstructFrom 0x4A7820, ConstructCopy 0x4A7690 (also not found
      by Ghidra), Assign 0x4A8F60 (CSmtPtr gained these three; Assign releases first, so self-assignment
      clears). 21 functions.
      Early test (src/hook/selftests_fe_menu.cpp on the element world): construction, properties, the moves over
      every end / wrap / skip case and empty menus, steps, arranging both orientations x regular / sized x
      locked / not, every event path, the container's events with and without priorities, the smart-pointer
      constructors and assignment. 47 deliberate bugs caught (4 after strengthening: refocusing without focus,
      the container's unfocus, self-assignment, a 1.1 padding scale); 2 equivalent / below float precision: the
      menu's dirty flag taken from its items (it is already set), and the unrounded vs rounded half item
      length (differences under a float's rounding of the offsets).
- [x] Batch FE-4 (NOT run in the game yet; early test passes offline): the screen element manager (new
      src/game/front/screen_element_manager.*), CScreenElementManager (0x00701440, 0x6A4 bytes, a CBaseManager with
      an event listener at +0x28, the root +0x44, 48 focus entries in two lists, 10 pad-event priorities, the
      pad_event_types). Construct 0x4AB990, DestroyBody 0x4ABB00, Destroy 0x4ACB40, the listener's Destroy
      0x4ABAF0, UnregisterObject 0x4AAFF0 (a removed root: the first other parentless element), GetObjectList
      0x4AAD40 (4 bytes: REPLACE_SOURCE_ONLY), GetElement 0x4AAE20, SetRoot 0x4AB730, SetParent 0x4ABBE0,
      DestroyTree 0x4AC400, DestroyElement 0x4AD100, FindByLocalId 0x4AB130, ResolveId 0x4AC120 (nested
      {id child = ...} structures, local ids, aliases, child indexes), ResolveElement 0x4AD1E0, Update 0x4ABD00
      (the layout pass with the tree locked), LockTree 0x4AAF90, SetPausedAll 0x4AAEE0, AddFocusEntry 0x4AB250
      (a new entry goes after the last entry looked at - list 1's tail when it has entries, whatever list it is
      for: as the original), RemoveFocusEntries 0x4AB330, ClearMarks 0x4AB4D0, ListenerReceive 0x4ABD40 (focus /
      unfocus / the focus flag's events, pad events to the focused elements with priorities and the device
      check), HasPriorities 0x4AAD80, IsPadEvent 0x4AAD50, IsTopPriority 0x4AB7C0, PushPriority 0x4AC570,
      UpdatePriorities 0x4AB5D0, CreateScreenElementManager 0x4ACAF0, DeleteScreenElementManager 0x4AACF0,
      SetScoreText 0x4AB910. 29 functions (CreateElement 0x4ACB70 is with the script commands, FE-6).
      The elements' own calls into the manager still go through the game's addresses (now ours when hooked).
      Early test (src/hook/selftests_fe_manager.cpp: a real manager over the element world): construction, the
      singleton's use counts, unregistering roots, SetParent with the root / no root, local ids, every ResolveId
      form, frames and the tree lock (counts checked between locks, a negative count), pausing, destruction with
      and without recursion / keeping the element, both focus lists (an element in the other list, entries from
      the parent on, marks, the entries full), every event path, priorities (pushes into free slots, the lowest
      replaced or refused, equal to the lowest, an inactive slot's priority), the score text. 46 deliberate bugs:
      44 caught (9 after strengthening); 2 equivalent: GetElement's assert argument (unused) in SetRoot, and
      ResolveId's own release of the element being resolved on the plain-id path (the caller releases it too).
      Note: thug2_check.exe crashed silently (exit -1) on 2 of 4 runs once, right after a build, in another
      area's tests (the last lines were the movie / script-trace tests); 12 later runs passed - not reproduced.
