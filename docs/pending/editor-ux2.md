# Level editor UX2: transform tools, typed values, snapping, repeat duplicate

NOT run in the game yet (level editor / UI work: the user tests). Built, offline checks pass (thug2_check on THUG2PM.exe
and on the stock C:\Games\THUG2\Game\THUG2.exe: 454 early self-tests), check_code_writes clean, the new self-tests
mutation-proven.

## What

- **Tools: Move / Rotate / Scale** (`src/game/front/level_editor_transform.{h,cpp}`, toolbar `level_editor_transform_ui.cpp`).
  The gizmo is drawn through the ImGui draw list like the old arrows (it *is* the old arrows' code path, generalised):
  - Move: the three arrows (as before).
  - Rotate: a heading ring (48 segments, drawn 3 px) about the pivot, with a spoke towards the selection's local +z.
    Objects and point nodes turn freely (objects: `Objects::TurnAbout`; nodes: `angles.y`); scenery and invisible walls
    by whole quarter turns (the ring drags in 90-degree steps). Why scenery stays on quarters: the engine turns a
    sector with `CSector::SetYRotation` (quarters only), and the editor's `Placement` / `QuarterTurn` / collision
    bookkeeping (BSP, compressed vertices, shared collision) is exact only for quarters. A free turn would be possible
    in principle through the scale path (vertices and collision rewritten from a kept copy), but it would need an angle
    in `Placement`, rebuilt collision trees and rails turned by arbitrary angles - left for later.
    Chain points (rails, ladders, paths) don't turn: the gizmo shows the arrows for them.
  - Scale: a cube at each axis' end (scale along that axis) and one at the pivot (all three; the factor is the cursor's
    distance from the pivot's pixel over its distance when grabbed). Only scenery and objects with a model scale
    (nodes / walls: the arrows). Limits as [ / ] (0.25x - 4x, less for compressed collision).
  - Dragging the selection's body still moves it in every tool; the tool applies to its handles.
- **World / Local** (`` ` ``, the toolbar's World/Local button): Local puts the gizmo along the selection's own axes -
  the first object's matrix rows, a node's heading, scenery's quarter turns (first piece); walls stay World. Moving
  along a local arrow, scaling along a local axis (objects: their own axes directly; scenery: through `TurnedScale`).
- **Pivot** (the toolbar's Tools... menu): Base (as the editor always did - objects about the first one's position,
  scenery about its pivot, Scale about the middle of scenery's bottom), Centre (the middle of the selection's box;
  scaling objects spreads their places from it too; scenery is moved back so its box's middle stays), Own (each object
  about its own position; a scenery group keeps its group pivot - one group moves as one).
- **Typed values while dragging** (Blender): during any drag or the modal transform, X / Y / Z constrain to that axis
  (again: free; a Move puts the selection back where it started and drags along the axis), digits / `-` (turns the sign
  over) / `.` type an exact amount, Backspace erases (then the axis), Enter applies, Escape puts it back. Move: units
  along the axis (the typed one, else the arrow's, else X); Rotate: degrees of heading (scenery: rounded to quarters);
  Scale: a factor (on the axis, else all three). The status bar shows "Move along X: 12.5_" with Enter / Backspace /
  X Y Z / Esc. Keys come from the SDL key events (`HandleEvent`), only while dragging.
- **Modal start** (Blender's G / R / S): **Alt+G** starts the current tool's transform without holding the mouse (plain
  G stays Ground): the mouse moves / turns / scales it (Scale: from the pivot's pixel), a left click or Enter applies,
  Escape puts it back.
- **Snapping** (`level_editor_snap.{h,cpp}`): held while dragging, or always with the toolbar's Snap and its Snap to:
  - **Ctrl: grid, relative** (the lead's request: moves by whole grid steps from where the piece was - not onto the
    world's lines); **Ctrl+Shift: the world's grid** (absolute, as the old Ctrl). Grid sizes on the toolbar (1, 5, 10,
    25, 50, 100; UX1's preference `[editor] snap_move=`). Rotate drags: Ctrl snaps to the rotation snap (5 / 10 / 15 /
    30 / 45 / 90 degrees, default 15; UX1's `[editor] snap_turn=`), which Z / X now use for objects and nodes too. Scale drags: tenths.
  - **Alt+Shift: surface** - the selection's base slides along what the collision ray (the game's static feeler)
    meets first under the cursor, the selection's own sectors passed (re-cast past them, up to 8 times); objects stood
    square to the slope with the Tools menu's "Align objects to the slope" (rows' lengths kept; nodes and scenery keep
    their angles / quarter turns).
  - **Ctrl+Alt: vertex** - the selection's box corner nearest the cursor (chosen when snapping starts) goes onto the
    nearest vertex of what's under the cursor: an unselected scenery piece's drawn mesh vertices (where it is now), or
    an unselected object's model-box corner, whichever is nearer.
  - Shift alone still drags up / down (not with Ctrl or Alt now); Alt alone still leaves what scenery carries (not with
    Ctrl or Shift).
- **Repeat duplicate** (Blender's Shift+D / Shift+R): after Ctrl+D, move / turn the copy, then **Ctrl+Shift+D** makes
  the next copy from it as far again and turned as much (the offset turned by the turn: a turned row makes an arc) -
  rows of benches, lamps, ledges. Objects (turned about the first copy), scenery copies (whole quarter turns, the offset
  turned exactly with `QuarterTurn`) and nodes: **nodes the editor added** (spawns, team spawns, crowns, lights, sound
  emitters, proximity nodes) can now be copied with Ctrl+D too (their spec with a fresh name; limits checked). A level's
  own nodes, chain points, flag sets and trigger volumes still can't. One undo step per copy (the copies are placed
  and turned before their undo step is recorded).

## Keys - and the WASD / W E R clash

The editor flew (and nudged the selection) with W A S D / Q E whatever the mouse did, so Unreal's W / E / R couldn't
simply be added. The lead passed on the UX research (Unity, Unreal, Godot fly only while the right mouse button is held;
their letters pick tools; Unreal cycles with Space) and asked for RMB-held flying as the default with the old way as a
preference. Done:

- **`[editor] wasd_flies=0` (default)**: W A S D / Q E (Space up) fly only while the right button is held. With a
  selection, flying with the right button lets the camera leave the selection (it stops orbiting; the wheel dollies);
  **F** (UX1's frame) flies back to it and the camera follows it again (so does any click / Deselect). Without the right button: **W / E / R** pick Move /
  Rotate / Scale, **Space** cycles them, the **arrow keys** nudge the selection (Up / Down along the view, Left / Right
  across), **Page Up / Down** up / down (as before).
- **`[editor] wasd_flies=1`**: the editor as it was (W A S D / Q E / R fly or nudge; F frames - UX1); the tools are on **Space**
  (cycles - only with a selection; without one Space still flies up) and the toolbar's buttons. The Tools... menu has a
  checkbox for it (writes the ini). UX1's preferences window can take it over: `Transform::FlyAlways()` /
  `SetFlyAlways()`.

Every key this work binds:

| Key | When | Does |
|---|---|---|
| W / E / R | wasd_flies=0, right button up, no Ctrl / Alt / Shift | Move / Rotate / Scale tool |
| Space | right button up (wasd_flies=1: with a selection) | cycles the tools |
| RMB + W A S D / Q E / Space | wasd_flies=0 | fly (with a selection: the camera leaves it) |
| Arrow keys, Page Up / Down | wasd_flies=0, a selection, browser closed | nudge it |
| F (UX1's) | a selection | also: the camera follows the selection again |
| `` ` `` | not dragging | World / Local |
| Alt+G | a selection | modal transform of the current tool |
| X / Y / Z, 0-9 (and keypad), `-`, `.`, Backspace, Enter | while dragging | typed values (see above) |
| Ctrl / Ctrl+Shift / Alt+Shift / Ctrl+Alt | while dragging | grid (relative) / world grid / surface / vertex |
| Ctrl (Rotate / Scale drags) | while dragging | angle steps / tenths |
| Ctrl+Shift+D | a Ctrl+D copy selected | repeat the duplicate |
| Z / X | a selection | turn by the rotation snap's step (was a fixed 15 degrees) |

Toolbar: Move / Rotate / Scale, World/Local, Snap (was "Snap 25") with the grid's size beside it, and Tools... (pivot,
rotation snap, Snap to - Grid / World grid / Surface / Vertex -, Align to slope, the fly keys). The help (F1) and the
status bar's key hints list the new keys.

Merged with UX1 (main): its preferences hold the settings this work uses - `Prefs::Get().wasdFlies` (shown in its
window; the Tools... menu's checkbox writes the same), `snapMove` (the grid; the toolbar's list sets it), `snapTurn`
(the rotation snap; integers, so no 22.5). UX1's keys kept: F frames (it no longer nudges down - with wasd_flies=1 Q /
Page Down lower the selection), 1..9 / Ctrl+1..9 bookmarks (not while dragging now: digits are typed values then),
F5 / Ctrl+P, numpad views, RMB+wheel speed, Ctrl+, prefs. Alt+left-drag orbits (UX1): start a drag first, then hold
Alt+Shift / Ctrl+Alt for surface / vertex snapping. The Space / W / E / R / `` ` `` / arrow keys are in
`TransformKeys` (level_editor.cpp) - a keymap (UX3) can move them.

## Files

- New: `src/game/front/level_editor_transform.{h,cpp}`, `level_editor_transform_ui.cpp`, `level_editor_snap.{h,cpp}`,
  `src/hook/selftests_editor_transform.cpp`.
- Changed (hooks): `level_editor.cpp` - the drag code (StartDrag / Drag / EndDrag dispatch to Rotate / Scale, local
  axes, the snapping modes), the gizmo drawing and its hit test (`ArrowAt`), `HandleEvent` (typed keys), `Update`
  (TransformKeys, the fly / nudge scheme, the free camera, Enter / modal-click apply, Escape restoring a turn / scale
  from the drag's undo snapshot), `TurnSelection` (the rotation snap, the pivot), `Duplicate` (+ `DuplicateNode`,
  `RepeatDuplicate`), `EndChange` (an undo step for a turn / scale that left the position), `KeysNow` / `ModeName` /
  hover text; `level_editor_ui.cpp` - the toolbar's Snap and the two calls, help rows; `level_editor.h` - the controls
  comment; `CMakeLists.txt` - four sources added.
- Ini: UX1's preferences (level_editor_prefs.h) - `[editor] wasd_flies=` (0), `snap_move=` (25), `snap_turn=` (15);
  no new keys of this work's. The pivot, space, Snap to and
  Align are per session.

## Checks (offline)

- `thug2_check.exe` (THUG2PM.exe) and `thug2_check.exe C:\Games\THUG2\Game\THUG2.exe`: all 454 early self-tests pass.
- New self-tests (`selftests_editor_transform.cpp`): heading axes vs TurnAboutUp, HeadingOf, UnitAxes, AngleAbout;
  Wrap, angle snapping, quarters, relative grid snapping, scale factors (handle, pixels, snapped); the gizmo's segments
  (Move 3 arrows, Rotate ring, Scale cubes) and handle picking with stand-in projectors (arrows, ring, centre cube
  first); typed input (axis toggling, sign, second '.', Backspace, value parsing, the status text); the repeat (offset,
  any order, another selection / kind refused, turned offset = arc, scenery's exact quarter offset); snapping's pure
  parts (nearest point, nearest on screen, rows aligned to a slope, base onto a surface).
- Mutations caught (`node tools/mutate.ts out/scripts/mut_ux2.ts`, 11/11): heading x sign, angle floor instead of
  round, relative grid off, scale factor inverted, '-' appended, axis not toggled, repeat offset not turned, repeat for
  any selection, centre cube not preferred, aligned right row flipped, nearest picks farthest.
- `node tools/check_code_writes.ts`: no overlaps.

## In-game checklist (for the user) - NOT run

Start with `node tools/game/edit_level.ts <level>` (or the pause menu's LEVEL EDITOR).

1. **Fly scheme (default wasd_flies=0):** with nothing selected, W A S D do nothing; hold the right button: W A S D /
   Q E / Space fly, the mouse looks. Release: W / E / R switch the toolbar's Move / Rotate / Scale (the status line says
   "Tool: ..."), Space cycles.
2. Select an object: the right button + W flies the camera away from it (no orbit); F frames it again; the arrow keys
   nudge it (Up / Down along the view), Page Up / Down raise / lower it; Ctrl+Z undoes a nudge.
3. Tools... > tick "W A S D always fly": the old behaviour (W A S D nudge a selection, R / F up / down), Space cycles the
   tools with a selection; check `[editor] wasd_flies=1` was written; untick it again.
4. **Rotate:** E, select a bench (object): a ring around it. Drag the ring: it turns freely; hold Ctrl: 15-degree steps;
   change Tools... > Rotation snap to 45: Ctrl steps by 45, Z / X turn by 45. Let go: Ctrl+Z puts the turn back in one
   step. Escape during the drag puts it back.
5. Rotate a scenery piece (e.g. a kicker) with the ring: it turns only in quarter turns, its rails with it; what stands
   on it is carried (Carry objects).
6. Rotate a spawn (node): its heading arrow turns freely. A rail point shows the arrows instead of a ring.
7. **Scale:** R, select scenery: cubes at the axis ends and the centre. Drag the x cube: it stretches along x (Ctrl:
   tenths); the centre cube scales all three. Limits as [ / ] (0.25x - 4x). An object: the same; a node: the arrows only.
8. **Local space:** turn an object 30 degrees, press `` ` `` (or the World/Local button): the arrows follow the object's
   axes; dragging one moves it along its own axis; scaling along a local cube stretches along its own axis.
9. **Pivot:** select three objects; Tools... > Pivot Centre: the ring turns them about the group's middle; Own: each
   turns in place; Base: about the first one's position (as before). Scale with Centre spreads them from the middle.
10. **Typed values:** drag an object by its body, type `x 100` then Enter: it lands exactly 100 units along x from
    where the drag started (status bar "Move along X: 100_"). Drag the ring, type `90` Enter: a 90-degree turn. Scale's
    centre, type `2` Enter: doubles. `-` turns the sign, Backspace erases, Escape puts it back. Typing digits while
    dragging must not recall UX1's camera bookmarks (1..9).
11. **Alt+G:** with something selected press Alt+G: it follows the mouse without a button; type `z 50`, Enter. Again
    with Rotate: Alt+G turns it with the mouse; a left click applies. Escape cancels. Plain G still drops to the ground.
12. **Grid snapping:** grid 25: Ctrl-drag an object sitting at x = 13: it moves to 38, 63... (relative); Ctrl+Shift-drag:
    onto 25, 50... (world lines). Change the grid to 100 / 5 on the toolbar: the steps follow (and `[editor] snap_move=` is
    written). The Snap checkbox snaps without Ctrl (Tools... > Snap to picks which kind).
13. **Surface snapping:** hold Alt+Shift while dragging a bench over ramps / stairs: its base follows the surface under
    the cursor; with Tools... > Align to slope it tilts square to the ramp (Escape / Ctrl+Z put it upright). Dragging a
    scenery piece with Alt+Shift: it doesn't hit itself (it slides on what's under it).
14. **Vertex snapping:** hold Ctrl+Alt while dragging a ledge piece near another: its nearest corner snaps onto the
    other piece's vertex under the cursor. Over an object: its box corners.
15. Alt alone during a scenery move still leaves the carried objects; Shift alone still drags up / down.
16. **Repeat duplicate:** select a bench, Ctrl+D, drag the copy 200 units along the street, then Ctrl+Shift+D x3: a
    straight row 200 apart. Ctrl+Z removes one copy per press. Turn the copy 15 degrees before repeating: the row curves.
    Scenery copy (a rail piece): Ctrl+D, move, Ctrl+Shift+D. A spawn the editor added: Ctrl+D copies it (a fresh
    ed_restartNN), move, Ctrl+Shift+D repeats; a level's own spawn says it can't be copied yet.
17. Ctrl+S after these, reload the edited copy: turned / scaled / copied pieces come back (no new record kinds - the
    usual records).

## Known limits / left

- Scenery turns stay quarter turns (see above). Objects turn only about up (no pitch / roll ring); nodes only heading
  (their X / Z angles: the Inspector).
- Own pivot doesn't split a scenery group (each piece about its own middle would break clusters).
- Scaling with Centre spreads objects' places by the asked ratio even where a scale limit stopped the scale itself.
- Surface snapping uses static collision (objects' collision isn't hit); vertex snapping's own corner is a box corner
  (not a mesh vertex) and the target an object's box corner rather than its mesh's vertices.
- The pivot, the space, Snap to and Align aren't saved in the ini (the grid, the rotation snap and wasd_flies are -
  UX1's preferences).
- The old HUD (editor_ui=0) has no toolbar: the keys work, the settings stay at their defaults / ini values.
