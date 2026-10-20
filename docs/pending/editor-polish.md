# editor-polish (agent record): carrying, Outliner markers, wall hover, shared collision in the collision view, trigger handles and exit triggers, undoable locks - NOT run in the game yet

The remaining items of the editor's "left" lists (editor-view, editor-walls, editor-triggers, editor-picking,
editor-collision-move, editor-shadows). Editor-only code (no REPLACE lines); nothing changes play unless an exit trigger
is made (below).

## What it does

### 1. Objects and point nodes follow their scenery (new src/game/front/level_editor_carry.{h,cpp})

When a scenery selection moves, turns, scales, goes Home or drops (G), what stands on it or is attached to it comes
along. Gathered when the change starts (the undo step's Snapshot: a drag, a key move, Z / X, [ / ], the Inspector's
Scale, Home, G):

- **What can be carried**: composite objects with a model (level objects, game objects, cars - the editor's
  `Objects::ModelBox`: not skaters or cameras), but not peds (class Pedestrian: they walk their paths) and not a flag's
  object (its node is carried and moves it); point nodes the editor moves (`NodeEdit::Movable`: spawns, crowns, flags,
  lights, sounds, proximity nodes, the editor's trigger volumes, cameras, other nodes), not hidden ones, not chain
  points (rails / ladders / paths: rails already go with their scenery).
- **The carry rules** (in this order, `Carry::Attached`):
  1. **Cluster**: its node's `Cluster=` names a member of the selection or a member's own cluster;
  2. **Link**: one of its node's links names a member;
  3. **Standing**: it is over a member's footprint (`OverFootprint`: inside the member's placed box along its own x / z
     axes grown by 10 units, from 10 below its bottom to 400 over its top - shadows' boxes left out) and the game's
     collision ray straight down from 50 units above it (400 units long, static collision, hits on a moved piece's
     shared collision name the piece) meets a member first - or meets a collision-only sector within 6 units of a
     member's box (a piece whose collision isn't taken along yet: SkWare_Rail01's is in SkWare_Main_1 until its first
     move). The ground beside the group, a floor under a table, or a ceiling over a lamp is not the group.
- **How it follows**: the selection's first piece is the reference; after each change its placement's step since the
  last (`Carry::Between(from, to) = Compose(to, Inverse(from))`) is applied to each carried thing - its place, and for
  quarter turns its heading (objects' matrices turned about themselves, nodes' angles.y + quarters x 90 degrees). A
  scale moves their places only (they keep their size).
- **Toggle**: the toolbar's **Carry objects** (default on; `[editor] carry=0/1` in thug2-decomp.ini). **Alt** held while
  dragging or moving with the keys puts the carried things back where they were when the move started and leaves them
  there for that move (the status bar shows "Alt: leave the n carried"). The request asked for Shift, but Shift is
  already up / down while dragging (the vertical drag), so it is Alt.
- **Undo / Home / layouts**: the scenery's undo step (kScenery) carries the carried things' places before the change
  (restored with it; redo captures them after); objects carried are the editor's moved objects (`Objects::NoteOrigin` /
  `Touch` on their first carry - their layout records, Home on them), nodes move through `NodeEdit::Set` (their `set`
  records, origins noted, the THUGPRO online warning for spawns / crowns / flags). Home on the scenery takes them back
  with it (the step back to the level's placement). Undo / redo, leaving the editor and switching it off disarm it (the
  steps restore places themselves).
- Logged with `debug_editor=1`: "carrying n objects and m nodes with the selection".

### 2. Outliner markers and "Changed only"

Each Outliner row ends with coloured letters for what the editor has done to its node, object or scenery
(`ChangeMarks` in level_editor.cpp, read from each module's own change records; the letters, colours and names in
level_editor_ui_layout.cpp - pure, self-tested): **M** moved / turned / scaled (scenery, rails, walls, objects,
nodes), **+** added, **D** hidden in the level (scenery's Delete, a level node hidden - saved), **H** hidden while
editing only (H / Isolate), **L** locked, **C** collision switched off. Hovering a letter names it. The checkbox
**Changed only** under the search shows only marked rows (families open); its tooltip is the legend; "n changed" beside
it.

### 3. Walls

- **Hover**: with the collision shown (C) and Shift held (not Alt, not dragging), the invisible wall a Shift+click would
  take is drawn yellow with thicker yellow edges, and the status bar says "invisible wall <name> - Shift+click selects
  it" (`View::SetHoverWall`, from the last frame's drawn faces as Shift+click uses).
- **Scale / copy**: not done - [ / ] and Ctrl+D on a wall now say so instead of doing nothing silently. Why: the walls
  module moves a wall's collision with the geometry's own Translate / RotateY on the BSP tree's sixteenths (exact, no
  drift); a scale needs the collision kept and rebuilt from a copy as level_editor_scale.h does scenery
  (`Scale::CopyCollision` / `RebuildCollision`, being reworked by the scaling agent now) plus a `scale=` on the `wall`
  record (apply_layout.ts is being worked on too). A copy needs a sector of its own: `CScene::CloneSector` with a null
  geometry is unverified offline; a shared-collision Companion (level_editor_shared_coll.h) holding the wall's faces
  would do, with a new layout record. Both left for a follow-up.
- **Collision view** (level_editor_view.cpp - its face styles in a pure `View::StyleOf`, self-tested):
  - a moved piece's shared collision (its Companion, where it is now) is drawn with its faces' surface colours and
    **solid cyan edges** (`SharedCollision::ForEachCompanion`; hidden pieces' off companions not drawn);
  - the faces it took (turned off with 0x10 in their collision-only sector) are drawn **where they were, faded, with
    cyan dashed edges**, classified as before they were turned off (`SharedCollision::TakenFaces`; the view's cache is
    made again whenever faces are taken or given back - `SharedCollision::Generation`); they're no longer Shift+click
    targets;
  - **level triggers** (faces with 0x40 of a sector the triggers module found - `Triggers::LevelTriggers()`) are filled
    in their **kind's colour** (kill, teleport, script, gap start / end, other - as the trigger view draws them), also
    for collision-only trigger sectors (before: magenta as walls; their magenta edges stay - they're still walls to
    Shift+click); other faces of those sectors as before;
  - the legend (the collision panel) lists the cyan edge / cyan dash / hover swatches, counts, and a "Triggers (0x40) by
    kind" tree with the kinds' colours.

### 4. Trigger volumes

- **Resize handles**: an editor trigger volume selected by itself shows a dot on the middle of each of its six faces
  (red x, green y, blue z - as the move arrows; yellow under the cursor or while dragged), drawn through the overlay's
  circles (`Triggers::AddHandles`). Pressing one (within 8 pixels, after the arrows) drags that face along its own axis
  (`ClosestOnAxis`, as the arrows): the opposite face stays where it is, the size grows by the drag (10 .. 50000), the
  node's centre moves by half (`Triggers::ResizeFace` - pure, self-tested); Ctrl or Snap 25 puts the size on 25s. One
  undo step a drag (the triggers' own undo: NoteUndo at the press). The camera holds still while resizing; the mouse
  stays the editor's over panels (as a drag). The status bar names the face under the cursor.
- **Fire on exit**: the Inspector's **Also on leaving it** (the spec's `exit`, layout record `exit=1` - older readers
  skip the key; a bad or repeated one makes the record unreadable as other keys do). How the game reports leaving: it
  doesn't - the trigger component (0x00434D90) casts its feeler from the object's last place to its place now, and a
  CCollMovBBox is met only where that line enters it (editor-triggers.md). So leaving is emulated: `Triggers::Play`,
  every frame from LevelEditor::Update (editor running or not), tests the local skater's position against each alive
  exit trigger's box (`Inside`); inside last frame and outside now (`Left` - never on the first frame seen) runs the
  node's TriggerScript on the skater as `CTriggerComponent::TriggerNode` (0x00434A60) does: not when the skate module
  skips the node (0x00565BB0), with its NetEnabled / Permanent flags and the node's name (`SpawnAndRunScript`). Not
  while editing (its memory is cleared then, so a box moved in the editor doesn't fire on leaving it), not in a network
  session, local skater only.
- **Trigger colours in the collision view**: item 3.

### 5. Locks undoable

L (lock / unlock the selection, unlock all with nothing selected) and the Inspector's Unlock push an undo step of the
whole lock list before the change (`UndoStep::kLocks`: `Picking::Locks()` / `SetLocks`); Ctrl+Z / Ctrl+Y put it back.
Selection changes are **not** made undo steps: every click changes the selection, so they would flood the 64-step
history and push real edits out; each undo step already reselects what it puts back. Left as is.

## Checks (offline)

- Build: out\build\polish and out\build\verify (own scripts out\scripts\build_polish.cmd / build_verify.cmd, own PDB
  endpoints) - no errors, no warnings.
- thug2_check.exe on THUG2PM.exe (C:\Games\THUG2_PM_BAK, read only) and on C:\Games\THUG2\Game\THUG2.exe (read only):
  argument sizes 0 MISMATCHED, all early self-tests pass (412 with the 5 new ones).
- `node tools/check_code_writes.ts`: No overlaps.
- New self-tests (src/hook/selftests_editor_carry.cpp, Early): TestEditorCarrySteps (Inverse / Between over moves,
  quarter turns, scales; a move's and a turn's steps; IsNone; headings), TestEditorCarryRules (footprints of unturned
  and turned / scaled boxes, margins in world units, the attachment order, 0 never matching), TestEditorOutlinerMarks,
  TestEditorTriggerHandles (handle points / directions turned, each on its face, the opposite face kept, the least
  size, NaN, Inside, Left, exit=1 records), TestEditorCarryCollisionStyle (walls, hover / selected, taken faces,
  companions, triggers by kind).
- Mutations: out\scripts\mut_polish.ts in the worktree (tools/mutate.ts, TESTS EditorCarry / EditorOutliner / EditorTriggerHandles) - **31 mutations, 31 caught** (Inverse / Between / IsNone, footprints, attachments, headings, markers, handles, resizing, Inside, exit records, the collision view's styles).

## In-game checklist (NOT run in the game yet)

Use `-edit tr` (TR, the warehouse) unless said; `debug_editor=1` logs the carrying.

1. **Carry, standing**: find a funbox / platform piece with a prop or a parked car on it (or add one: B > Objects,
   place a copy on top of a box). Select the box's group, drag it sideways: the prop moves with it, staying on top. Let
   go, Ctrl+Z: both back; Ctrl+Y: both moved again.
2. **Carry, nodes**: put a crown and a spawn (N) on top of a box; drag / Z / X the box: they ride along, the spawn's
   heading arrow turns with the quarter turn. A crown on the floor next to the box stays.
3. **Not carried**: a prop on the floor beside the box (within a few units of it), a light high above it (over 400),
   something under a table piece: none of them move.
4. **Alt**: start dragging the box, hold Alt: the carried things jump back to where they were and stay; the status bar
   says so. Release, drag again without Alt: they come along again (from where they are).
5. **Toggle**: toolbar Carry objects off - a drag moves the box alone; check thug2-decomp.ini [editor] carry=0; on
   again.
6. **Scale / Home / G**: with things on top, [ / ] the box: they keep standing on its top (heights follow the scale);
   Home: the box and its riders back; G on a raised box: riders drop with it.
7. **Cluster / links**: a node whose Cluster= names the box's cluster (e.g. a level's TrickObject helpers) moves with it
   even when not on top.
8. **Layouts**: Ctrl+S after carrying, load the (EDITED) entry: the props and nodes where they were carried; the
   Outliner marks them M.
9. **Outliner markers**: move a piece (M), hide one with Delete (D), H another (H), lock one (L), switch a piece's
   collision off (C), add a crown (+): the rows show the letters in colour; hovering names them. Tick Changed only:
   just those rows; untick: all.
10. **Locks undo**: L on a piece (locked, deselected), Ctrl+Z: unlocked again; Ctrl+Y: locked. Unlock in the
    Inspector, Ctrl+Z: locked again. L with nothing selected (unlock all), Ctrl+Z: all locks back.
11. **Wall hover**: C on, hold Shift over magenta walls: the one under the cursor turns yellow with yellow edges, the
    status bar names it; Shift+click selects that one.
12. **Wall scale / copy**: a wall selected, [ / ] and Ctrl+D: a message each, nothing changes.
13. **Shared collision in the view**: C on; move SkWare_Rail01 (editor-collision-move.md item 1): at its new place cyan
    edges round its collision; at the old place, SkWare_Main_1's faces it took faded with cyan dashes. Home: the cyan
    goes, the old faces are plain again.
14. **Triggers by kind**: C on near a level's kill zone / water (BO, NJ): its trigger faces in the kill colour, a gap's
    in the gap colours, as T's outlines; the legend's "Triggers by kind" lists them.
15. **Resize handles**: T, add a kill zone (Triggers window), select it: six dots on its faces. Drag the +x dot out:
    that face follows, the -x face stays; Ctrl while dragging: sizes in 25s. Ctrl+Z: one step back to the old size and
    place. Turn it (X) and resize again: the handles follow its heading. Dragging over a panel keeps resizing.
16. **Exit triggers**: make a Script trigger running a visible script (or a teleport) with "Also on leaving it" ticked;
    leave the editor and skate into and out of it: it runs on the way in and on the way out. Unticked: in only. Ctrl+S,
    reload the edited level: the tick kept (exit=1 in the layout).
17. **Exit safety**: with the skater standing inside an exit trigger, open the editor, move the box away, leave: nothing
    fires. In a network game nothing of this runs.
18. thug2-decomp.log after the session: no errors from the editor.

## Left / limits

- Carrying: one reference piece's step for the whole selection - pieces that were moved by themselves before being
  grouped follow exactly for moves / turns / scales (one rigid step), but Home on such a group takes the riders back by
  the reference's step only. Objects carried only when they have a model box (`ModelBox`); peds never; a level's
  trigger volumes (collision sectors) on top aren't carried (they're walls to the walls module - move them there); the
  ground ray is static collision only (an object standing on another object on the box isn't found by the ray; its
  Cluster / links still work).
- Walls: no scale, no copy (above).
- Exit triggers: emulated for the local skater only (other objects with trigger components fire on the way in only);
  tested once a frame on the skater's position (a skater passing right through a thin box in one frame never was
  inside: no exit fires).
- Selection changes are not undo steps (above).
- apply_layout.ts (another agent): should learn the trigger record's `exit=1` (it skips unknown keys now; a baked level
  gets no exit behaviour - it's the DLL's).

## Files

New: src/game/front/level_editor_carry.{h,cpp}, src/hook/selftests_editor_carry.cpp, docs/pending/editor-polish.md.
Changed: level_editor.cpp (undo kinds kLocks / carried places, FollowScenery calls, Alt, toolbar request, markers,
hover wall, trigger handles' input, Play), level_editor_ui.{h,cpp} (Carry objects, markers, Changed only, legend),
level_editor_ui_layout.cpp (the markers' pure parts), level_editor_view.{h,cpp} (StyleOf, hover wall, companions,
taken faces, trigger kinds, HiddenIds), level_editor_shared_coll.{h,cpp} (ForEachCompanion, TakenFaces, Generation),
level_editor_triggers.{h,cpp} (exit, handles, Play, Inside / Left / ResizeFace), level_editor_triggers_ui.cpp (the
checkbox). Shared-file edits (small, additive): CMakeLists.txt (2 lines).

Switching it off: `[editor] carry=0` (carrying); the rest is editor UI (`[features] level_editor=0` turns the editor
off; an exit trigger is only made by ticking it).
