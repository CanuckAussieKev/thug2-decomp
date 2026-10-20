# Level editor: scaling scenery (build plan step 3e)

Branch `worktree-agent-ad932b73392fe6706`. **NOT run in the game yet** (the user tests interactive editor behaviour
himself; checked offline only: build, thug2_check on THUG2PM.exe and the stock THUG2.exe - argument sizes 0 mismatched,
375 early self-tests passed on both -, 21 mutations, check_code_writes "No overlaps").

## What it does

A uniform scale of the selected scenery piece or group, **0.25x to 4x**, about the **middle of the selection's
bottom** (a bench scaled stays standing on the floor; a group scales as one). Keys **[ / ]** (10% a press; **Shift**: 1%),
the Inspector's **Scale** field (drag to scrub, Ctrl+click / double-click to type; one undo step until let go), F1 help,
the old HUD's key line and help panel. Combined with moves and quarter turns in one placement (offset + quarters +
scale); undo / redo; **Home** puts the scale back to 1x with the place; **Reset level** too. Objects and nodes don't scale
(a message says so). Non-uniform scale: out of scope (normals and the collision's face rejection would have to follow).

## How (new src/game/front/level_editor_scale.{h,cpp}; small hooks elsewhere)

- `Placement` (level_editor.h) gained `float scale = 1` (a default, so every `{q, offset}` stays 1x): a point x as the
  editor first touched it is at `QuarterTurn(x * scale, quarters) + offset`. `Apply`, `Compose` (scales multiply, the
  inner offset scaled), `PlacedBox` (its axes `scale` long - picking, outlines, the pivot and the bottom follow) and
  `EngineState` (comment) follow. `Scale::ScaleAbout(placement, pivot, factor)` is the new operation.
- **Render:** the engine has no scale for a level sector; it moves / turns one by rewriting the meshes' vertex copies
  (sMesh::MoveTo 0x004D6E00 / TurnY 0x004D6FB0) and unlocking each vertex buffer (0x004D40E0 uploads it). So the first
  time a sector is scaled, `Scale::Capture` keeps its meshes' positions (every vertex buffer's xyz), bounding spheres
  (centre, radius) and position lists (+0xAC / +0xB0), plus its collision, with the engine's quarters / position then.
  From then on `ApplyTrack` lets the engine turn / move it as before and then `Scale::Rebuild` writes everything again
  from the copy: `to + QuarterTurn((p - from) * scale, turns)` for vertices and sphere centres (radius * scale), the
  position lists scaled but not turned (as the engine treats them), each buffer uploaded. Always from the copy: no
  drift, and 1x gives the copy's exact numbers back. Normals untouched (uniform). Before writing, Rebuild checks the
  sector is still the one copied (mesh list, meshes, counts, strides, buffers and their data, collision arrays); if not
  (another level at the same addresses) it drops the copy and writes nothing.
- **Collision format found** (Nx::CCollObjTriData, src/game/nx/coll_tri.h, already decompiled): vertices are either
  floats (12 bytes) or **compressed**: three u16 offsets from the box's minimum in **sixteenths** (at most 4095.9 units
  across); face flags / terrain / indices separate; box at +0x10 / +0x20; BSP tree (+0x34) of 8-byte nodes whose split
  positions are in sixteenths (axis in the low 2 bits; a quarter turn swaps x / z and negates, flipping the sides' bit);
  +0x3C a float face-rejection scale (line tests skip faces by a per-face byte times it). The game itself has
  CCollObjTriData::Scale (0x00416730: vertices, box, tree; compressed through a temporary float array) and RotateY /
  Translate - CCollStatic's slots 22 / 23 forward to them. Rebuild puts the geometry back from its copy (vertex bytes,
  box, every split node's split and link, the rejection scale) and calls our decompiled Scale / RotateY / Translate on it
  about the copy's position - so it keeps its own format, and Scale / RotateY clear the rejection scale (no face skipped
  by distance: correct, a bit slower). The sector is flagged 0x20 as before and `Commit` runs the scene's
  UpdateSuperSectors (0x0049D780) after a key press / when the field is let go.
- **Limits:** `Scale::AllowedFactor` keeps every selected piece within 0.25x..4x, and a piece with compressed
  collision within `4000 / its unscaled largest side` (`Scale::CollisionLimit`); at a limit nothing more happens that way
  (message line).
- **Rails** in the selection (SelectRails: the cluster's and those inside the boxes) get the same ScaleAbout, so their
  points scale about the same pivot and their segment boxes are made again (ApplyRails). A rail point moved by itself
  afterwards is a plain move again (scale 1), as before.
- **Layouts:** `sector`, `rail` and `clone` records get a trailing ` scale=<s>` (9 significant digits) when not 1;
  the reader takes `scale=` and skips other key=value fields; a bad scale refuses the file. Older layout files (none)
  read as 1x; version stays 2. An older DLL reads a new file (it stops after `hidden`) but places scaled pieces unscaled.
  Copies of scaled pieces compose correctly (Compose multiplies scales).
- Shared-file edits (small, additive): CMakeLists.txt (two files), level_editor.h (Placement.scale), level_editor_scenery
  (.h: SelectionScale / ScaleSelection; .cpp: Apply / Compose / PlacedBox, Track.original, ApplyTrack's capture and
  rebuild, comparisons that now include the scale, DescribeSelection "scaled 1.50x", hover size), level_editor_layout
  (.h doc, .cpp scale= write / read), level_editor.cpp (keys, ScaleSceneryBy, kScaleSet / kScaleDone, model, key hints,
  HUD help), level_editor_ui (.h model fields + 2 requests, .cpp Inspector field + F1 row).

## Self-tests (src/hook/selftests_editor_scale.cpp, Early, 5 tests)

Placements (ScaleAbout with turns / moves and its inverse; Compose with scales; EngineState; PlacedBox corners and a
ray hit), limits (0.25x..4x, at-limit, groups, compressed, NaN / negative), vertex rewrite (turned / scaled / moved, other
bytes kept, 200 changes then 1x exactly the copy), layout records (Token / ReadToken, write / read of sector / rail /
clone, old files, later fields skipped, bad scale refused), collision (an uncompressed geometry with a one-split BSP tree:
scaled / turned / moved - vertices, box and tree agree; 100 rebuilds then 1x exact; another array not "same"; limits).
Not testable offline: Capture / Rebuild on live meshes (D3D upload) and compressed collision (it needs the game's heap) -
both in the checklist below. **21 mutations, 21 caught** (spec: out\scripts\mut_scale.ts in the worktree - ScaleAbout,
Apply / Compose / PlacedBox, the limits, the rewrite's turn, scale= written / refused, the collision's box / tree /
rejection restore, its turn, its limit and identity check, the field's clamp).

## Risks / not done

- **Super-sector grid:** a piece scaled up past the level's bounds - UpdateSuperSectors (0x0049D780, not decompiled)
  builds the grid the collision lookups use; collision outside the grid may not be found (the piece shows but you fall
  through the part outside). Watch big pieces at the level's edge at 4x.
- **Compressed collision** limits some large pieces below 4x (e.g. a 2000-unit piece: 2x). Converting a geometry to float
  vertices would lift that but means allocating arrays the level's own free code would release - not done.
- **Billboard meshes** (flag 0x10: their quad's vertices are offsets about a billboard pivot) are rewritten as positions,
  as the engine's own moves treat them: a scaled piece with billboards may show them displaced.
- The geometry's box (+0x14 / +0x24) is left as loaded (moves leave it too); per-vertex data (+0x94, lighting?) untouched.
- Memory: a scaled piece keeps a copy of its vertex positions (12 bytes a vertex a buffer) and collision for the session.
- If something else moves a sector the editor scaled (TrackOf starts afresh), the editor forgets its scale while the
  vertices stay scaled (as with moves; nothing in the game does this to level sectors).
- **Objects: not done.** The model code has a scale: `CModel::SetScale` (0x00492EE0: the scale vector at +0x1C..+0x28,
  flag 8 of +0x44 set unless 1,1,1) - what the script member `ModelApplyObjectScale` uses (handler 0x0048A3B0, case
  0x50F1285B, on the model at 0x006FA2F8 - the skater appearance's scale), and the model draw (0x00493090) and our
  ModelBox honour it. But it is render-only: an object's collision (its collision component - not decompiled; a
  CCollStaticInstance has a SetScale, a CCollMovTri doesn't) keeps its size, so a scaled car would draw big and collide
  small. Left until the collision component is decompiled (then: SetScale on the model + the instance's collision, an
  `object` record scale=).
- Non-uniform scale: out of scope (normals, face rejection, the BSP's per-axis scale is supported by the game's code).

## In-game checklist (NOT run yet)

1. TR, `-edit tr`: select a free-standing piece (a bench / funbox), press **]** five times: it grows 10% a press about
   the middle of its bottom (stays on the floor), status bar / Inspector show the scale ("scaled 1.61x"), the outline
   and arrows match the new size.
2. Skate it (leave the editor): the collision matches what you see - grind / land on the enlarged top, no invisible
   old-size box, no falling through the new parts.
3. **[** down to 0.25x: stops at 0.25x with the limit message; skate it again (small collision matches).
4. **Shift+]**: 1% steps.
5. Inspector **Scale** field: drag left / right scrubs smoothly; Ctrl+click, type 2, Enter: exactly 2.000x. One Ctrl+Z
   undoes the whole drag / the typed value; Ctrl+Y redoes.
6. Scale, then move (drag, arrows, WASDQE), turn (Z / X) and Ground (G): the piece keeps its scale, G puts its scaled
   bottom on the ground; turning keeps it in place.
7. A group with rails (LA's fire truck, or TR's SkWare_Rail01): scale it 1.5x - the rails scale with it (their lines
   on the piece's edges); grind them in the game.
8. **Home**: back where the level has it at 1x. **Ctrl+Shift+Delete** (Reset level): every scaled piece back at 1x.
9. Ctrl+S, leave, load the level's (EDITED) entry: the pieces come back scaled, in place, with their rails; the layout
   file has `scale=` on those records.
10. Ctrl+D a scaled piece: the copy has the same size; save and reload: the copy still the same size where it was.
11. Many presses back and forth (]]]][[[[ x10), then Home: the piece exactly as before (no gaps / seams where it meets
    other pieces).
12. A big piece (a building wall ~2000 units): ] stops early (compressed collision limit message) - note the scale
    reached.
13. A large piece scaled near the level's edge: can you still collide with the part outside the original bounds?
    (super-sector risk)
14. A piece with see-through / billboard parts (trees, signs, glows): do they stay in place when scaled?
15. Select an object (a car) and press ]: the message "Only scenery scales"; nothing changes.
16. Old HUD (`editor_ui=0`): [ / ] work; the key line shows "[ ] scale"; F1 lists it.
17. thug2-decomp.log after the session: no errors from the editor (with `debug_editor=1`, the actions logged).

Switching it off: the whole editor `[features] level_editor=0`; scaling has no separate switch (nothing is scaled
unless [ / ] or the field is used, or a layout has scale=).
