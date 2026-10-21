# Level editor UX7: adjust last action, placement ghost, live measurements, align / distribute, linked copies, replace, edge-to-edge snapping, auto-rail

**NOT run in the game yet** (level editor / UI work: the user tests). Built, offline checks pass (thug2_check on
THUG2PM.exe and on the stock C:\Games\THUG2\Game\THUG2.exe: 492 early self-tests, 8 new), check_code_writes clean,
the new self-tests mutation-proven (19/19), `npm run typecheck` / `lint` and the layout tests pass.

## What

- **Adjust last action** (`level_editor_adjust.{h,cpp}`, Blender's Adjust Last Operation). After a move (drag, arrow
  keys / nudge, typed values), a turn (ring, Z / X), a scale (cubes, [ / ]), a duplicate (Ctrl+D, Ctrl+Shift+D), an
  align / distribute or a place (Add palette, Place palette), a panel at the view's bottom left ("Adjust: Move (F4)")
  shows its numbers: offset (+ "On the grid"), angle (scenery: quarters), factor, copies / offset / turn of a
  duplicate, axis / edge / reference / local of an align, position / turn of a placed thing. Changing one makes the
  action again as the same undo step: the step it made is taken back without a redo (`Edit::TakeBack`) and the action
  made again from where it started (a duplicate from its source: N copies, each from the last - a row or an arc - folded
  into one step). A placed thing is just moved / turned (its undo step is its existence). Only while that step is still
  the newest (the panel greys out after another action, an undo or a redo). Duplicates are made again when a field is
  let go (dozens of copies a frame would be slow); the rest live while dragging the field.
- **Placement ghost** (`level_editor_arrange.cpp` AddGhost): with the Add palette (B) open, a see-through box of the
  highlighted piece / object follows the cursor (bottom-centred on the point a click / Enter places at); objects turned
  to the surface's slope with "Ghost / placed object turned to the slope" (and placed so). A Place palette (N) entry
  armed: a marker (post, ring, heading) in its colour. **Click places** (on by default): with the Add palette open a
  plain click in the view places its item there; Shift+click places more (the palette stays open). (No mesh silhouette:
  the off-screen preview's picture isn't a shape in the world - the box only.)
- **Live measurements while dragging**: next to the cursor - size (one object: its own box; else the selection's
  bounds), how far it moved, its height above the ground below (the ground marker's), the distance to the nearest grind
  rail or ledge segment (its own carried rails left out) with a thin line to that point.
- **Align / distribute** (Arrange window, F2; also in the command search: "Align X min" ... "Distribute along Z"):
  min / centre / max on X / Y / Z, world or Local (the first piece's axes), on the selection's bounds or on the first
  piece; distribute evenly (centres, or equal gaps). Pieces: each object; each scenery piece with its shadows (moved by
  their own amounts - nothing carried). One undo step; adjustable afterwards.
- **Linked copies** (`level_editor_links.{h,cpp}`): **Ctrl+Alt+D** copies as Ctrl+D but links the copies to the source
  (a copy of a linked copy joins its group). The source's scale (scenery and objects) is made on the copies (as a
  factor along their matching own axes - a copy made from a source turned an odd number of quarters has x / z swapped,
  remembered per copy); a level object's field edits (the Inspector: model swap, any field) make each linked copy
  again from the source's node where it stood. Not while dragging (after it's let go). Unlink (Arrange window / search)
  makes a copy independent; on the source it dissolves the group (an undo step). Saved: `link sector <src>
  <m>[@q],...` / `link object <src> <m>,...` (a clone written `c<n>`, its clone record's index); layout_file.ts reads
  them (`linkGroups`), apply_layout.ts bakes the copies as the plain copies their records already are.
- **Replace selection with** (Arrange window, the Add palette's "Replace selection" button, search): scenery selected +
  a scenery piece highlighted in the Add palette - each piece (with its shadows) hidden and a copy of the highlighted
  one put bottom-centred where it was, with its quarter turns and scale; objects selected + an object highlighted - each
  object copy replaced by a copy of it (place, turn, scale kept), a level's own object (it can't be removed) gets its
  model= (the Inspector's model swap). One undo step (`kGroup`).
- **Edge-to-edge snapping** (on by default; Arrange window): while moving scenery with no Ctrl / Alt snapping held,
  its box's faces snap flush with a neighbour's within 6..24 units (by the camera's distance): side by side on X / Z
  (at the same height), stacked on its top, or its top level with a neighbour's top beside it (coping heights). The
  faces met are drawn green. Arrow drags snap along that axis (World space only); Shift (vertical) drags on Y.
- **Auto-rail along this edge** (**Alt+R**, Arrange window): with a scenery piece selected its top outline is drawn
  cyan - edges of its upward triangles (meshes' strips, as picking reads them) that no other upward triangle shares
  and where the surface falls away (an edge with a wall rising from it - an inside corner - isn't offered), joined end
  to end where they turn under 30 degrees, straight stretches one segment, the longest first (at most 256, at least 16
  long). The one under the cursor is yellow with its length; a click lays a grind rail exactly on it through the chains
  (`Chains::AddRail`: ed_railNN points linked in order, a trick object of the piece - one undo step), Shift+click lays
  more. Alt+R again or deselecting stops.

## Keys (all in UX3's registry - level_editor_actions.h, rebindable, listed in F1 and the command search)

| Key | Action id | Does |
|---|---|---|
| F2 | arrange_window | the Arrange window |
| F4 | adjust_last | the adjust panel shown / hidden (it shows after each adjustable action) |
| Alt+R | edge_rail | rail along an edge (a scenery piece selected) |
| Ctrl+Alt+D | linked_duplicate | duplicate linked (more specific than Ctrl+D: only it fires) |
| (search) | align_<x/y/z>_<min/centre/max>, distribute_<x/y/z>, replace_selection, unlink | as named |
| (search: toggles) | edge_snap, drag_readout, placement_ghost, click_places | the settings |
| Click / Shift+click | Add palette open, Click places on | place its item at the ghost / place more |

Not taken: M (UX4's ruler), F6 / F7 (UX6), F9-F12 / Tab (debug mode). The asked "Blender F9" is F4 here (F9 is debug
mode's free camera).

## Files

- New: `src/game/front/level_editor_arrange.{h,cpp}` (align / distribute / replace / edge snap / readout / ghost /
  top edges / actions), `level_editor_arrange_ui.cpp` (the window, the Add palette's row, the adjust panel),
  `level_editor_adjust.{h,cpp}`, `level_editor_links.{h,cpp}`, `src/hook/selftests_editor_arrange.cpp`.
- Changed (hooks):
  - `level_editor_modules.h`: the `Edit` namespace - the selection and the editor's operations for these modules.
  - `level_editor.cpp`: undo steps get a serial and a `kGroup` kind (several steps folded into one: Restore last first,
    Current each); `NoteForAdjust` after a drag / nudge / Z X / [ ]; Duplicate records its source-copy pairs and notes
    the adjust panel; PlaceFromBrowser takes `keepOpen` and notes Place (PlaceNode / PlaceEntity too); Drag's last step
    calls `Arrange::EdgeSnapped`; the click chain: Alt+R's edge click and Click places; Update calls Arrange::Update,
    Adjust::Tick, Links::Tick; Draw calls Arrange::AddOverlay; the `Edit` bridge at the end.
  - `level_editor_chains.{h,cpp}`: `AddRail` (a rail through given points).
  - `level_editor_layout.{h,cpp}`: `File::links`, `link` records written / read, captured (clone order) / applied (the
    clones made, by order), reset / forgotten with the level.
  - `level_editor_ui.cpp`: the Add palette's extra row (3 button rows), the two windows in `Frame`.
  - `tools/game/layout_file.ts` (`readLinkRecord`, `linkGroups`), `apply_layout.ts` (a comment: baked as plain copies),
    `apply_layout.test.ts` (link records); `CMakeLists.txt` (4 sources, 1 self-test file).
- Ini: none (the settings are per session; keys through the registry's [editor_keys]).

## Checks (offline)

- `thug2_check.exe` (THUG2PM.exe) and `thug2_check.exe C:\Games\THUG2\Game\THUG2.exe`: 492 early self-tests pass; the
  8 new (`selftests_editor_arrange.cpp`): align (bounds / first / edges), distribute (centres, equal gaps with different
  sizes, fewer than three), edge snap (flush, out of reach, other height, axes, stacked, level tops, reach), the
  readout and box-segment distance, the top outline (a box: its 4 top edges not the bottom's; a split top merged; a
  step's inside corner left out; least length; the edge under the cursor), the adjust numbers (caps, grid, quarters,
  steps), linked groups (copy of a copy, unlink copy / source), link records (round trip, malformed refused).
- Mutations (`node tools/mutate.ts out/scripts/mut_ux7.ts`): 19/19 caught.
- `node tools/check_code_writes.ts`: no overlaps. `npm run typecheck`, `lint`, `node --test tools/game/apply_layout.test.ts` pass.

## In-game checklist (for the user) - NOT run

Start with `node tools/game/edit_level.ts <level>` (a workspace with this branch's DLL); watch `thug2-decomp.log`.

1. **Adjust a move**: drag a bench 200 units, let go: "Adjust: Move (F4)" bottom left shows the offset. Type 100 in X:
   the bench is at start + 100 (not +300); Ctrl+Z once puts it back where it was before the drag (one step); Ctrl+Y.
   Tick "On the grid". F4 hides / shows the panel.
2. **Adjust a duplicate**: select a bench, Ctrl+D; panel "Duplicate": Copies 1. Drag Copies to 8 and let go: 8 benches
   in a row; change Turn to 15 and let go: an arc; Ctrl+Z removes all 8 at once. Same with a scenery piece (turns in
   quarters) and after Ctrl+Shift+D.
3. **Adjust a turn / scale**: Z on an object: Angle = rotation snap; type 37: turned 37 from where it was. [ on scenery:
   Factor 0.909; type 2. Ring / cube drags too.
4. After any other action (or an undo) the panel greys: "Changed since".
5. **Place + adjust**: B, highlight a piece; the cyan see-through box follows the cursor on floors / roofs. Click: placed
   there (palette closes); Shift+click: places several. Panel "Place": edit Position / Turn - it moves; Ctrl+Z removes
   it. N, arm a spawn: a marker (post, ring, heading) follows; click places. Arrange (F2) > "turned to the slope", an
   object on a ramp: the ghost tilts, the placed object stands square to the ramp.
6. **Measurements**: drag a ledge piece near a rail: next to the cursor "size .. / moved .. / above ground .. / rail ..",
   a thin line to the rail's nearest point. Turn off in F2.
7. **Align / distribute**: Shift-select 4 benches at odd places; F2 > Y, Align Min: their bottoms level; X, Align Centre:
   lined up; "On the first": on the first one. Distribute (X): evenly spaced; Equal gaps. One Ctrl+Z each. Local with a
   turned first object: along its axes. Scenery pieces (Alt+click several) the same; shadows go with their pieces.
8. **Linked copies**: select an object, Ctrl+Alt+D twice (move each copy). Scale the source with ] or the cubes: on let
   go the copies scale the same. A level object as the source: Inspector model swap - the copies are made again with the
   new model where they stood (log: "its linked copies made again"). F2 shows "Linked: the source of 2 copies"; select a
   copy - Unlink: no longer follows; Ctrl+Z links it again. Scenery: Ctrl+Alt+D a piece, turn the copy a quarter, stretch
   the source along X with Alt+]: the copy stretches along its own matching axis. Ctrl+S, reload the edited copy: still
   linked (layout has `link` records); bake with apply_layout.ts: plain copies, no warning.
9. **Replace**: select 3 benches (copies) + B, highlight another object > "Replace selection": each replaced in place /
   turn / scale; a level's own bench gets the model instead. Scenery: select pieces, highlight a scenery piece >
   Replace: originals hidden, copies where they were (bottoms level, quarter turns, scale). One Ctrl+Z puts it all back.
10. **Edge snapping**: drag a kicker toward another piece: within a few units it jumps flush, the touching face shows
    green; along Y onto a box's top; next to a ledge its top meets the ledge's top. Ctrl / Alt+Shift / Ctrl+Alt
    snapping override it; off in F2.
11. **Auto-rail**: select a ledge / planter piece, Alt+R: its top edges in cyan (not inside corners against walls); hover
    one: yellow with its length; click: a rail along it (grind it with F5); Shift+click: more edges. Ctrl+Z removes the
    rail. Try a curved planter (one run round the curve) and a box (four edges).
12. Rebind one of the keys in the Keys section (Ctrl+Space search "Arrange") and check it works; F1 lists them.

## Known limits / left

- The ghost is a box, not the piece's silhouette; scenery placement doesn't tilt (quarter turns only).
- Adjusting a Scale re-applies about the [ / ] pivot (scenery: the middle of its bottom; objects their positions), not
  a drag's Centre pivot; a Move adjusts the first object's / pivot's offset.
- Linked copies: scale and (level objects) fields / model propagate; a copy's own fields can't be edited (copies aren't
  level nodes). Copies remade for a fields change keep their names (undo steps still find them).
- Edge snapping uses boxes (axis-aligned bounds of turned pieces), not the meshes; only movable-sized pieces within
  1500 units are neighbours.
- Auto-rail uses the drawn meshes (LOD 0), not collision; the rail lies exactly on the mesh edge.
- Replace for level objects is a model swap only (they can't be removed).
