# editor-walls (agent record): deleting, restoring and moving invisible walls; scenery collision off - NOT run in the game yet

NOT ORIGINAL (the level editor is ours) - the user's question "how can I delete a collision wall?". Branch
`worktree-agent-aefb44516b0bc4014`. Checked offline only (build, thug2_check, mutations, check_code_writes); **NOT run
in the game yet** - the user tests the editor by hand.

## The switch the game itself uses

**CSector::SetActive (0x0049E9B0**, thiscall, RET 4): `coll = sector+0x18; if (coll) coll[+8] = on ? &~4 : |4;
if (geom = sector+0x14) geom->SetActive(on) (0x0048DE60)`. It is what a LevelGeometry node's Kill runs (0x005A32C0 ->
SetActive(false)) and what the editor's Delete of scenery already used. The world's collision query (0x004188C0's grid
lists) skips collision objects with flag 2 or 4, so every feeler (skater, camera, peds) passes a sector switched off.
With a null geometry (an invisible wall) SetActive touches only that flag - read back by CSector::IsActive
(0x0049E9E0: with no geometry it returns !(coll flag & 4)).

- Invisible walls: `SetActive(sector, !off)` - the game's own call, nothing else.
- A drawn piece's collision by itself (the Inspector's Collision checkbox): the same flag 4 set / cleared directly (the
  collision half of SetActive), the geometry left active so it stays drawn. SetActive(true) (the editor's Home / undo of a
  hidden piece) clears the flag with the geometry, so `Walls::Tick` sets it again each frame while editing, and after a
  layout is applied.
- Known engine caveat (docs: src/game/skater/debug_collision.cpp): the collision *cache* (0x0040ABA0) isn't re-checked
  for flag 4 when queried, so a sector switched off after a cache was filled stays collidable through that cache until
  it's filled again. The editor takes the skater out of the world and back, so this shouldn't show - checklist item 4.

## What it does

Source: `src/game/front/level_editor_walls.h/.cpp` (new), small hooks in level_editor.cpp (selection helpers, Delete,
Home, undo kind `kWalls`, the click, the model, two requests, Tick), level_editor_view.cpp/.h (off collision drawn,
dashes, Stats.off), level_editor_layout.cpp/.h (the `wall` record, Capture / Apply / ResetToLevel), level_editor_ui.cpp/.h
(Inspector buttons and checkbox, legend line, F1 rows, two requests, model fields), level_editor_scenery.cpp/.h
(`NoteCollisionMoved`; a copy of a piece gets its collision on), CMakeLists.txt (two lines).

1. **Delete a wall:** collision shown (C), Shift+click a magenta face, **Delete** (or the Inspector's **Delete wall
   (Del)**): its collision is off - the skater goes through. It stays drawn faded with **dashed** edges (the legend says
   "Faded / dashed: collision switched off (n pieces)"), still Shift+clickable. **Delete again**, **Home**, or the
   Inspector's **Restore (Del / Home)** switches it back on. Undo / redo each step.
2. **Move a wall:** with the wall selected, a plain click on its face drags it (Shift: up / down, Ctrl / Snap 25 snaps,
   wheel pushes / pulls), the arrows drag along an axis, W A S D / Q E nudge, **Z / X** quarter turns about its middle,
   **G** drops it onto the ground (the Inspector's Ground too). One undo step a drag / nudge / turn; Home puts it back
   where the level has it (and on). The camera orbits the selected wall as with scenery (W A S D move it, not the camera,
   while it's selected - Esc deselects).
   - How: the collision geometry's own methods - CCollObjTriData::RotateY (0x004164F0, about a centre) then Translate
     (0x00416400) - what CCollStatic's slot 22 and SetWorldPosition (0x00410C20) hand on to; the sector flagged 0x20 and
     the scene's UpdateSuperSectors (0x0049D780) after the move (Scenery::NoteCollisionMoved + Commit), as scenery moves
     do. CSector::SetYRotation (0x0049E760) isn't used for walls: with no geometry it turns the collision about the
     shared origin (0x00691914), not about the wall.
   - **No drift:** the BSP tree's splits are whole sixteenths and Translate rounds each towards zero (0x00415D80), so a
     fractional move per frame would let the tree drift from the vertices. A wall's placement is kept on sixteenths
     (`Walls::OnGrid`; at most 1/32 unit from where the mouse put it): every step is exact, 300 drags and turns keep the
     tree on the vertices, and moving it home gives the level's bytes back (self-tested). **Finding:** the editor's
     normal scenery moves (ApplyTrack -> CCollStatic::SetWorldPosition with each frame's fractional change) don't do
     this, so their BSP splits can drift by up to 1/16 a frame of dragging - not changed here (Scenery's file); worth a
     look if long drags of scenery ever give holes in its collision.
3. **Layouts:** a new record `wall <checksum> <quarters> <x> <y> <z> <off>` (after the phase 4 records; layout version
   stays 2). An older reader skips it (unknown kinds are skipped: the walls then as shipped); a bad switch value or a
   record cut short refuses the file. Written by Ctrl+S / the autosave (Walls::Changed: walls moved or switched,
   drawn pieces switched off; copies of scenery left out); applied when the edited copy loads (Layout::Apply - never in a
   network session, the existing guard); Reset level puts every wall and switch back (Walls::ResetAll from
   ResetToLevel) and its undo brings them back.
4. **Scenery collision off by itself:** select a drawn piece (or group): the Inspector has a **Collision** checkbox
   ("Collision (some off)" for a mixed group). Unticked: the piece stays drawn, the skater passes through it; with the
   collision view its faces are faded (not in the way of Shift+click). Same switch flag, same `wall` record (no move
   - its `sector` record moves it). One undo step; Home on the piece doesn't change it (it's not a place); Reset level
   switches it back on. A copy (Ctrl+D) of such a piece gets its collision on (the engine copies the collision's flags;
   a layout makes copies again with collision, so the copy now matches what a reload gives).
5. **Inspector / F1:** the wall section shows its state ("Collision OFF - moved by (x, y, z), n quarter turns" /
   "Collision on - where the level has it"), Delete wall / Restore, Put back (Home), Ground (G), Close and a line on
   moving it. F1's View rows and Delete row describe all of it.

## Checks (offline)

- Built (own build dir `out/build/walls`, own PDB endpoint `thug2walls`), no warnings.
- `thug2_check.exe` on THUG2PM.exe (default) and on C:\Games\THUG2\Game\THUG2.exe (read only): argument sizes 3287
  replacements, 0 MISMATCHED; **388 early self-tests all passed** on both.
- New self-tests `src/hook/selftests_editor_walls.cpp` (Early, 4): `TestEditorWallsSteps` (sixteenths, the step between
  placements against Apply for every pair, moving / turning about the middle, home, the combined checkbox),
  `TestEditorWallsCollision` (a geometry with a BSP split through the game's CCollObjTriData::RotateY / Translate:
  vertices / box / split agree after a move + turn, 300 fractional drags and turns keep them agreeing, back home is
  byte-exact), `TestEditorWallsRecords` (write / read, old files, bad / short records refused), `TestEditorWallsDashes`
  (dashes, last dash cut, cap, fade).
- Mutations (`node tools/mutate.ts out/scripts/mut_walls.ts`, spec in the worktree's out\scripts): **13 / 13 caught** -
  no sixteenths grid, moves off the grid (caught by the 300-drag drift and byte-exact-home tests), the turn direction,
  the turn's centre, RotateY the wrong way, moves backwards, the combined checkbox, home ignoring turns, the record's
  switch written / read, the last dash not cut, no dash cap, the fade.
- `node tools/check_code_writes.ts`: No overlaps.
- Not testable offline: the game side (TrackOf on live sectors, SetActive, the flag on drawn pieces, the super sectors'
  update, the view's drawing) - the checklist.

## In-game checklist (NOT run in the game yet)

Watch `thug2-decomp.log` (`level editor:` lines with `[features] debug_editor=1`: "collision of XXXXXXXX switched off
(an invisible wall)"; and `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`).

1. Open the editor in a level with invisible walls (THAW The Mall's entrance blocker, a level's out-of-bounds
   blockers), press **C**, **Shift+click** a magenta face: the Inspector shows the wall, "Collision on - where the level
   has it", a red **Delete wall (Del)** button, the move arrows appear on the wall, the camera orbits it.
2. Press **Delete**: message "Wall deleted: its collision is off ..."; the wall turns faded with dashed edges, the
   Inspector says "Collision OFF" (orange) and shows **Restore (Del / Home)**; the legend says "Faded / dashed:
   collision switched off (1 pieces)".
3. Leave the editor (Esc) and skate / ollie / wallride through where the wall was: nothing stops the skater; the camera
   isn't pushed by it either.
4. Same, but skate *into the wall right after leaving the editor* at full speed - does it ever still block for a moment
   (the collision cache caveat)? Note it if so.
5. Back in the editor, Shift+click the dashed wall, **Delete** again: on again (solid magenta); skate: it blocks again.
   Then Delete, **Home**: on again. Then Delete, the Inspector's **Restore**: on again.
6. **Ctrl+Z / Ctrl+Y** over steps 2-5: each switch undone / redone, the wall selected again each time.
7. **Moving:** with the wall selected, click (no Shift) on its face and drag: it follows the mouse (Shift: up / down);
   drag an arrow; W A S D / Q E nudge; **X / Z** quarter turns about its middle (a long blocker turns 90 degrees in place);
   **G** drops it to the ground. Ctrl+Z undoes each; the Inspector says "moved by (...), n quarter turns".
8. Skate the moved wall: it blocks where it is now (no invisible blocker left at the old place, no gap in the new one -
   run along its whole length and try the ends; turned too).
9. **Home** on the moved wall: back where the level had it, on; skate: as shipped.
10. **Saving:** move one wall, delete another, Ctrl+S; the layout file (`thug2-decomp-layouts\<scene>.txt`) has
    `wall <checksum> ...` lines. Leave, load the level's **(EDITED)** entry: the moved wall is where you left it, the
    deleted one lets you through. Load the level as shipped (its own entry): both as shipped.
11. **Reset level** (Ctrl+Shift+Delete) in the edited copy: both walls as shipped; Ctrl+Z brings them back as they were.
12. **Scenery collision:** click a drawn piece (a bench, a ramp), untick the Inspector's **Collision**: message
    "Collision off for 1 piece ..."; with C on its faces are faded. Leave and skate through it: you pass through, it's
    still drawn. Tick it again: solid. Ctrl+Z / Ctrl+Y.
13. With its collision off: Delete (hide) the piece, then Home (show): it comes back drawn and *still* not collidable
    (the switch kept). Ctrl+S, reload the EDITED entry: still drawn, not collidable. Reset level: collidable again.
14. A group (Shift+click two pieces), untick one with the checkbox... (the checkbox reads "Collision (some off)" when
    only some are off); ticking turns all on.
15. Ctrl+D a piece whose collision is off: the copy is collidable (as a reload makes it).
16. Network (by hand, if at all): in a network game no layout applies - the walls are as shipped.
17. F1: the View rows describe Delete / move / Restore for walls and the Collision checkbox.

## Left / ideas

- Walls in the old HUD (`editor_ui=0`): no collision view there, so no Shift+click - not reachable.
- A wall's quarter turns keep the sector's own turn count (+0x10) as loaded (the engine's SetYRotation isn't used); only
  the editor reads it for walls, so nothing should care.
- A scaled wall: not offered (`[ / ]` says only scenery scales).
- A copy (duplicate) of an invisible wall: not offered (Ctrl+D does nothing for a wall). A copy of scenery whose
  collision is switched off with the checkbox works in the session but isn't saved (copies' checksums are new each
  session; their clone records carry no switch).
- Normal scenery's collision moves don't keep to sixteenths (see the finding in item 2).
- `tools/.../apply_layout.ts` (phase 6, another agent): if it bakes layouts into level files, it should learn the
  `wall` record (switch: drop the sector's collision / a flag in the .col; move: its vertices) or skip it.
