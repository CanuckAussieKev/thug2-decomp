# Level editor phase 4: chains - rails, ladders, paths (NOT ORIGINAL - the user's plan)

Branch `worktree-agent-a5adf9f3165d66534` (off main c4fd5af: picking merged). **NOT run in the game yet** (the user tests
the editor by hand) - built and checked offline only.

## What

1. **Ladders and paths move like rail points.** A ladder's point (ClimbingNode) moves in the rail manager, as rail
   points do (drag, arrows, W A S D / Q E, G, the Position field; saved as the layout's existing `rail` records, Home and
   Reset level put it back); a path's waypoint (Waypoint) moves through its `pos=` (`set` records). **L** toggles whole
   chain / one point for rails, ladders and paths (the Inspector's "Whole rail / ladder / path (L) - n points"); a
   path's whole chain is its waypoints linked either way. Chain points don't turn (Z / X do nothing). L with anything
   else still locks (picking's L kept).
2. **New rails and paths** - the Place palette (N) has two more entries: **Rail** and **Path**. Arm one, then click in
   the view: each click is a point linked on from the last. The point is where the cursor's ray first meets the level's
   collision, **snapped to an edge** when the surface faces up (the nearest place within 32 units, in 8 directions, where
   the ground falls more than 8 - a ledge's lip, a bench's edge); **Alt+click** doesn't snap. Clicking within 24 units
   of a free end of a rail (path) takes that end: the first click draws on from it, a later click links onto it and
   finishes. **Enter** or a **right click** (pressed and let go without looking about) finishes; **Esc** stops. While
   drawing nothing is selected (the camera keeps flying). Each point is one undo step (Ctrl+Z takes the last away).
   - On a selected rail / path point (keys and the Inspector's Rail / Path section): **Insert** (a point halfway to
     the next), **Delete** (the point deleted, the points either side linked; a level's own point is hidden with phase
     3's marker - "dropped"), **End / Extend** (draw on from this end), **J / Link to...** (then click another chain's
     end: a free next is linked), **U / Unlink** (its link to the next removed: the chain splits).
   - New points copy the fields of the point they're drawn from (`like`: a ped's waypoint keeps its PedType /
     Priority / scripts, a rail its Type and TerrainType - not its TrickObject cluster). A new chain's points are a
     plain `RailNode` (CreatedAtStart, TerrainType TERRAIN_GRINDMETAL) / `Waypoint` (CreatedAtStart).
3. **A rail's terrain**: the Inspector's Terrain list (the game's 21 grind terrains, scripts\game\terrain.qb's
   Terrain_Types TERRAIN_GRIND..., each checked against the game's own table before use - an unknown one would crash
   0x004442C0) sets it for the whole rail, live (the rail manager node's terrain byte: the sound / sparks of grinding).
4. **Paths of peds and cars**: select a ped or a car (the object or its node): the Inspector's Path section shows where
   its path starts; **Assign a path...** then click a waypoint, or **No path**. The object's node's links= get that
   waypoint (its other links kept), and **the object is made again from its node** (marked dead; once the game has
   removed it - its object update, so after leaving the editor if the game doesn't purge dead objects while paused -
   0x005A2E50 CreateFromStructure makes it, as the level's load does). The path code reads its links when the object
   starts, so this is how the switch shows; on a reload of the edited copy the layout's `links` record does the same
   (the layout applies a second after the level is up: the object is made again then).
5. **Layout records** (still `thug2-decomp layout 2`; a phase 2 / 3 reader skips them - unknown records):
   ```
   point ed_rail01 RailNode pos=-120.5,8,330.125 like=8E1A2B3C links=12345678
   point ed_rail02 RailNode pos=10,8,330 terrain=TERRAIN_GRINDWOOD
   point ed_path01 Waypoint pos=0,0,0 links=AAAAAAAA
   links 00000010 DEADBEEF        a level node's links= as edited (a chain relinked, a ped's path)
   links 0000ABCD -               (none)
   drop 0BADF00D                  a level's rail / path point deleted (hidden, its neighbours relinked)
   terrain 00000003 TERRAIN_GRINDCONC
   ```
   Added chain points are **not** `node` records (an older reader would make them without their rail); dropped points
   are not `hide` records; an added rail point a sector move took along is saved where it is (its `point` pos), not as a
   `rail` offset. Malformed records refuse the file (no pos, a repeated key, a class other than RailNode / Waypoint, bad
   checksums, a node's links twice); unknown keys are skipped.
6. **Reset level** (and an autosave restore, the in-place switch back to shipped): the chains first - added points
   removed, links and terrains as the level has them, dropped points shown, the rail manager built again - then the rest
   as before. One undo step brings it all back (the layout text includes the chain records). Undo / redo: a new step
   kind (`kChains`) holding the chains' whole state before the change.
7. **Offline only**: nothing new reaches a network session - the chain records are layout records, applied only where
   layouts are (the 3a network guard is untouched).

## How the rail manager is rebuilt

The game makes the rail manager's nodes only at the level's load: `CRailManager::AddFromNodeArray` (0x0057E960) from
`CFunc_ParseNodeArray` (0x005A34E0, after `Cleanup` 0x0057C330, then `RemoveOverlapsIfNotPark`) on the node manager
owner's array. Its other caller (0x0042B880) is a moving object's own rail manager. There is no run-time add, and the park
editor's rails come through the same path (its node array regenerated, ParseNodeArray again). So `Chains::RebuildRails`
builds the level's rail manager again the same way, without reloading:

- a new node array (Mem::Malloc on the bottom-up heap, as AddFromNodeArray): **every node it had is copied as it is**
  (moved places - sector moves and rail moves -, terrain, on / off as RemoveOverlaps left it, climbing orientation); a
  node whose structure is gone (an added point removed) is left out; a dropped level point is kept but switched off
  (its on / off noted, put back when shown again) and takes no part in links;
- new rail points of the overlay (RailNode structures not in the old array) are added with the game's own
  `CRailManager::AddNode` (0x0057D090: flags, place, terrain from TerrainType);
- every node's next / previous is made again from the structures' links= exactly as AddFromNodeArray does (a node's
  next: its first link, or a DefaultLine one; the linked node's previous likewise), and the segment boxes;
- the editor's terrains put on; Scenery's rail tracks (the moved points' records) found again by name in the new array
  (`Scenery::RailsRebuilt`), the node layer re-read;
- **the old array isn't freed while the level is up** (a skater's grind state may still point into it): it is freed
  once the rail manager's array is no longer the one built here (the next level's ParseNodeArray made its own).

Links of the overlay's nodes are names. The node manager's `ResolveLinks` (0x0045D480) reads links= as indices on every
`AddNodeArray`, and the overlay is registered again on each change, so `NodeCreate::Sync` now takes links= off the
overlay's structures before registering and puts them back after. A level node's links= array is rewritten in place
(its own component; the level's kept to put back, and put back when its level goes if its array is still loaded).

## Files

- New: `src/game/front/level_editor_chains.{h,cpp}` (the model, records, topology, rebuild, drawing, picks, the
  Inspector's view), `src/game/front/level_editor_chains_ui.cpp` (the Inspector section, ImGui only),
  `src/hook/selftests_editor_chains.cpp`.
- Small hooks: `level_editor.cpp` (undo kind kChains, the palette's two entries, clicks / Enter / right click / Esc
  while drawing, armed picks, Insert / End / J / U, Delete on chain points, L and Z / X for chain points, the pause key
  held back while drawing, help / HUD lines, Chains::Tick), `level_editor_node_create.{h,cpp}` (Spec `like` / `terrain`
  / `links`, chain fields, links kept out of ResolveLinks, `SetLinks`), `level_editor_node_edit.{h,cpp}` (ladders and
  paths movable, `IsChain`, added points moved through Chains), `level_editor_scenery.{h,cpp}` (ladder points tracked as
  rail points, `RailsRebuilt`), `level_editor_layout.{h,cpp}` (the records, Capture / Apply / ResetToLevel /
  OnSceneLoaded), `level_editor_ui.{h,cpp}` (Model::chain, Request::kChainOp, `ChainSection`, Whole rail / ladder /
  path, help rows), `src/hook/selftests_editor_nodes.cpp` (paths are movable now).
- Shared: `CMakeLists.txt` (three lines).

## Checks (offline)

- Clean build in the worktree's own `out/build/chains` (own PDB endpoint), no warnings.
- `thug2_check.exe` on THUG2PM.exe and on C:\Games\THUG2\Game\THUG2.exe (stock, read only): 0 MISMATCHED, all 375 early
  tests pass; 5 new (`src/hook/selftests_editor_chains.cpp`): `TestEditorChainsGraph` (extend both ways, insert, delete
  with re-linking - middle, ends, a fork, a closing loop -, link both ways and refusals, unlink, connected),
  `TestEditorChainsRecords` (round trip exact, `-`, unknown keys, 12 malformed forms, sorting),
  `TestEditorChainsLayout` (a layout with chain records round trips, stays version 2, no `node` records for chain
  points, an older layout reads, a bad chain record refuses the file at its line), `TestEditorChainsSnap` (a ledge's
  edge, nothing in reach, walls, a small step vs a drop), `TestEditorChainsNodes` (a new rail point's fields, its own
  terrain, a waypoint's, specs compare links, the classes, the 21 grind terrains).
- Mutations (`node tools/mutate.ts out/scripts/mut_chains.ts`, tests EditorChains + EditorNode), **all 20 caught**:
  prepending the wrong way, insert losing the next point, delete linking a fork twice, delete making a self-link, a
  point linked to itself, IsLast inverted, connected one way only, a point without pos accepted, a repeated key
  accepted, `links ... -` refused, `like` not written, drops unsorted, any step down counted as an edge, the edge put a
  whole step out, walls snapped, any class accepted for a point, the layout reader skipping chain records, a new rail
  without a terrain, specs compared without links, paths not movable.
- `node tools/check_code_writes.ts`: No overlaps.

## In-game test checklist (for the user) - NOT run in the game yet

Use this branch's DLL (a workspace or the main install after the merge). Watch `thug2-decomp.log` (`level editor:`
lines; `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`). `[features] debug_editor=1` also logs "rail manager built again - n
nodes (k kept, m new)". Back up `thug2-decomp-layouts\` first.

1. **Ladder:** `node tools/game/edit_level.ts` a level with a ladder (Boston, New Orleans...). Click a ladder point
   (purple): arrows, the Inspector's "Whole ladder (L) - 2 points". Drag it: both points move. L, drag: one point.
   Leave the editor and climb it at the new place. Home puts it back. Ctrl+S, reload the EDITED copy: it's moved.
2. **Path move:** Boston, a ped path (green line, e.g. TRG_Ped_Sitter_Nav02). Drag a waypoint: the whole path moves
   (L: one waypoint). Leave the editor: peds walk the new path (one already heading for a moved waypoint may finish at
   the old place first). Ctrl+S writes `set` records for the waypoints.
3. **Draw a rail:** N, click "Rail (click points; Enter finishes)". Click on a ledge / bench top near its edge three
   times: blue points snap to the edge, a line joins them (Alt+click: exactly where clicked). Enter (or a right click)
   finishes. Log: `added ed_rail01 (RailNode ...)` etc. Leave the editor and grind it. **On flat open ground a rail
   isn't grindable** (the game's RailClear refuses rails with ground at the same height both sides) - draw on edges,
   or raise the points (Q / E, the Position field).
4. **Extend / snap to ends:** select the last point of the new rail, End (or the Inspector's Extend): click two more
   points, Enter: one rail of five. Draw a second rail and click within 24 units of the first rail's free end: it links
   onto it and drawing stops.
5. **Insert / Delete / Unlink / Link:** on a middle point: Insert (a point halfway to the next; selected), Delete (gone,
   the line joins its neighbours), U (the rail splits there - grind stops at the gap), then select the end before the
   gap, J and click the end after it: linked again. Each is one Ctrl+Z / Ctrl+Y step.
6. **A level's rail:** select a point of a level rail (e.g. TR's SkWare_Rail01 points): Delete hides it (grey) and the
   rail goes straight past it; grind it; Ctrl+Z brings it back. Extend from a level rail's end: the new points copy its
   terrain (Inspector).
7. **Terrain:** on any rail point, the Inspector's Terrain list: pick Wood: grind it - wooden sound. The whole rail
   changes. Ctrl+Z puts it back.
8. **Draw a path:** N > Path: click 3 waypoints on the ground, Enter. Select a ped (Boston's TRG_Ped_RollerBlader or
   any walking ped): Inspector > Path "Starts at ..." > Assign a path..., click the first new waypoint. The log says it
   is made again (`made again (its path from its links)`) - possibly only after leaving the editor. Watch it walk the
   new path. **No path**: it stands. Report any crash here (making a ped again from its node is new ground).
9. **Save / reload:** Ctrl+S: the layout has `point`, `links`, `drop`, `terrain` lines (step 5's file). Load the level's
   EDITED entry: the message / log ends "... N chain points, N relinked, N dropped"; rails grind as left, the ped walks
   its assigned path (made again a second after the level is up).
10. **Undo while drawing:** draw 4 points, Ctrl+Z twice: the last two go; the next click starts a new chain.
11. **Reset level:** Ctrl+Shift+Delete > Reset level: new rails / paths gone, level rails whole again, terrains back,
    peds' paths back (they're made again). Ctrl+Z: all back.
12. **Autosave:** draw a rail, wait over a minute (log `autosaved`), end the game from Task Manager, open the editor in
    that level: Restore brings the rail back.
13. **Leaving the level:** with drawn rails, load another level: none of them there; the log shows no errors. (The old
    rail node arrays are freed then.)
14. **F1:** both helps list the Chains rows / lines. Old HUD (`editor_ui=0`): Insert / Del / End / J / U work on a
    selected point (no Inspector, no palette - drawing needs the palette or End).
15. **Escape / Enter:** while drawing, Enter finishes (it doesn't leave the editor); Esc stops drawing, the next Esc
    closes the palette, etc.

## Known limits

- Ladders move (whole or a point) but aren't reshaped, made or deleted; a ladder keeps its facing.
- A rail laid on flat ground isn't grindable (the game's own RailClear rule); new rails aren't trick objects (no
  cluster: no gap / trick-object scoring on them).
- A dropped level point left alone (both its neighbours deleted) stays a one-point "rail" if it's still linked - as the
  game treats single nodes. Points deleted at the end of a level rail just shorten it.
- A ped / car made again starts at its node's place (an object move of it in the same layout isn't re-applied after).
- Links to a level node from an added point are by checksum; a level whose node array changes (a reconverted port)
  may leave dangling links (skipped when rebuilding).
- Waypoint moves take effect when a ped / car next heads for that waypoint; paths followed by scripts that cached
  positions won't change until restarted.
