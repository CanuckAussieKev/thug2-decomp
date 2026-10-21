# Level editor: picking occlusion - what's nearest along the ray hides what's behind it

Branch `worktree-agent-ae22845585880fcc4`. **NOT run in the game yet** (the user tests interactive editor behaviour;
checked offline only: build, thug2_check on both exes, self-tests, mutations, check_code_writes).

## The user's report and its cause

Screenshot `thug2_2026-10-21_12-32-03` (TR, camera (-292.6, -137.3, -1667.7), mouse (834, 688)): hovering what looked
like a ramp in front, "under the cursor" said `scenery g_WareNew_Main_door01_reflect 120 x 295 x 360`, outlined further
back. Rebuilt offline from TR's own files (`tools/game/pick_census.ts TR --ray=... --ray=...`, the camera fitted to
the yellow outline: the view's tanX is about 1.0). Along the cursor's ray:

| t | piece | what it is |
|---|---|---|
| 545 | `g_ware_light_02` / `_25_noflick` (67 x 563 x 720, collision) | the hanging fluorescent light - the grey "beam" under the cursor |
| 2147 | `SkWare_Main_9a` (alpha-blended, flag 0x40, no collision) | TR's glossy floor layer |
| 2151 | `SkWare_Main_9a_water` (alpha-blended, collision) | the floor under it |
| 2705 | `g_WareNew_Main_door01_reflect` (diffuse, opaque, no collision, y -1060 .. -765) | the door mirrored **under** the floor |

Two faults together:
1. **Pieces with collision were only hit where their collision is.** The light's collision is its lamp body
   (y -320 .. -303); the ray crosses its drawn housing at y -299, above it. The triangle pass only looked at pieces
   *without* collision, so the light was invisible to picks.
2. **Passed pieces let the ray through.** TR's floor is alpha-blended (a glossy layer over mirrored `_reflect` pieces);
   as structure and see-through it was passed, and the ray went on under the floor to `door01_reflect` - an ordinary
   opaque, diffuse mesh (material flags 0x1024, blend mode 0), not structure, no collision: pickable.

`door01_reflect`'s material isn't see-through and never was: the reflection effect is the floor above it being
see-through. So the fix is occlusion, not a reflection test. The see-through test was also checked for additive
decals: in TR, BO, foun and z_ma every additive / subtractive / modulating piece already has flag 0x40 (the census:
see-through counts unchanged), but the test now also counts blend modes 1-4 and 7-10 without 0x40 as a safeguard
(not 5 / 6 blend / fixblend: BO's trash cans and NJ_Houses_North_85 are blend without 0x40 and solid).

## The rules now (level_editor_picking.h)

- **Every shown piece is tested by its triangles** (box first, pruned beyond the first occluding hit), as well as by
  the game's collision ray - pieces with collision too, so their drawn parts the collision doesn't cover count.
- **Occluding** (`Picking::Met::occludes`): the collision ray met the piece (it's solid - TR's glossy floor), or an
  opaque mesh of it (not see-through). See-through pieces met only by their meshes (webs, glows, shadows, grime,
  decals) let the ray on.
- **A plain click / the hover** (`Picking::Resolve`): walks the hits nearest first; takes the first pickable piece
  (not structure, not see-through, not locked); a structure piece, a locked piece or an occluding see-through piece
  first ends the search - nothing behind it (the click deselects; "Under the cursor" names the passed piece for Alt).
- **Objects** (by their boxes) against the scenery (`Choose` with `blocked`): an object behind the first occluding
  surface isn't taken; a kind the filter turns off still occludes (a filtered-out object hides the scenery behind it,
  filtered-out scenery hides objects behind it). Locked objects still don't occlude (their boxes are rough).
- **Gizmos** (node icons, rail segments and points, spawns...): a plain click / hover takes one only if its depth along
  the view (a segment's, perspective-correct, where it passes nearest the cursor) is no further than the first
  occluding surface under the cursor + `kGizmoSlack` (12 units) + what the 8-pixel pick radius spans at its depth.
  **Trigger volumes** on a plain click: only those the ray enters before that surface + 12 units.
- **Alt+click / middle click**: unchanged - the nearest non-locked piece whatever is in front (occlusion aside), gizmos
  too. **Click again (cycle)**: unchanged - every layer, past everything. A click that takes nothing starts no cycle
  (as before).
- **Structure's low-and-flat rule** (from 6f902ab): <= 80 high and >= 350 across both ways **and** at least 90% of the
  piece's upward-facing surface level (faces with |n.y| > 0.25 count as upward; >= 0.996, about 5 degrees, as level;
  none at all - a piece of side walls only - counts as level). Measured from the meshes' strips once a piece while the
  editor runs (`Scenery::LevelShare`, cleared when the editor starts); used by the click rules and by `GroupOf`'s
  structure checks alike. Wide (>= 1500 in two dimensions) and slab rules unchanged.

### Pieces that change class (structure -> pickable), from `node tools/game/pick_census.ts <level>`

(No piece's see-through class changes.)

| level | piece | size | level top |
|---|---|---|---|
| TR | NJ_Floors_FarEast22 | 693 x 71 x 1636 | 66% |
| TR | NJ_bFloors_FarEast01 | 609 x 76 x 577 | 0% |
| BO | BO_YMCA_HighRail | 396 x 23 x 1271 | 0% |
| BO | NORMAL_Girder16 | 495 x 71 x 387 | 27% |
| BO | JG_CO_WallShdw01 (see-through anyway) | 586 x 74 x 528 | 59% |
| BO | BO_Metals | 408 x 74 x 423 | 48% |
| BO | NightOn_09_JG_CY_Lights03 / NightOff_09_JG_CY_Lights02 (see-through anyway) | 477 x 76 x 702 | 5% |
| BO | BO_HospitalPlanter | 554 x 37 x 374 | 44% |
| BO | BO_Wire04 | 530 x 42 x 390 | 48% |
| foun | 3812E505, 98BCAD79, 5DA856C2 (kickers) | 360 x 48 x 680 | 43% |
| foun | 00292F1B | 396 x 36 x 368 | 28% |
| foun | AA21967D | 523 x 60 x 626 | 79% |
| z_ma | 61529B45 (bank) | 353 x 48 x 915 | 0% |
| z_ma | 03B31895 | 2047 x 70 x 699 | 33% |
| z_ma | 10E6E909 | 425 x 43 x 429 | 0% |
| z_ma | 16738DDF, 40292A59, E695823C, 6687E2C7 (see-through anyway) | ~390 x 60 x 390-560 | 0% |
| z_ma | 8A2D8133 | 363 x 39 x 357 | 31% |

Counts: TR 98 -> 96 structure, BO 126 -> 118, foun 56 -> 51, z_ma 268 -> 260. TR's kickers / funboxes were never low
and flat (194 x 54 x 184, 615 x 183 x 964); TR's SkWare_Main_12 (360 x 60 x 924, side walls only) stays structure.
(Ported levels' sector names aren't in their .qb name tables: checksums.)

## Files

- `src/game/front/level_editor_picking.{h,cpp}`: `IsStructure(..., levelShare)`, `TopArea` / `AddTopTriangle` /
  `AddTopStrip` / `LevelShare`, `MaterialSeeThrough`, `Met` / `Resolved` / `Resolve`, `GizmoInFront` / `GizmoSlack`,
  `Candidates::blocked` / `blockT` and `Choose`'s occlusion.
- `src/game/front/level_editor_scenery.{h,cpp}`: `Pick` rewritten (all pieces by triangles, collision hits occlude,
  `Hit::blocked` / `blockT` / `nearest`), `GeomSeeThrough` via `MaterialSeeThrough` (material +0x20 pass flags, +0x80
  blend mode - checked in the scene loader 0x004D3390), `LevelShare` / `GeomLevelShare` cache, `GroupOf` uses it.
- `src/game/front/level_editor_nodes.{h,cpp}` (shared, additive): `Pickable::depthA` / `depthB`, `Occluder`,
  `PickNearest(..., occluder = {})`.
- `src/game/front/level_editor_triggers.{h,cpp}` (shared, additive): `Pick(..., maxT = 1e30f)`.
- `src/game/front/level_editor.cpp`: the hover (Scenery::Pick before the gizmos; Alt uses `Hit::nearest`), segment end
  depths, `GizmoOccluder`, `TriggerReach`. `level_editor.h` / `level_editor_ui.cpp`: help and tooltip text.
- `tools/game/pick_census.ts` (new): the offline census / ray tool above.
- `src/hook/selftests_editor_picking.cpp`: `TestEditorPickOcclusion` (the screenshot's ray with and without the light,
  webs, walls, locks, a piece met twice, Choose against a surface / filters, gizmo slack, PickNearest with an occluder,
  a rail running behind a wall), `TestEditorPickSeeThrough`, `TestEditorPickLevelTop` (triangles, strips, real sizes);
  one `TestEditorPickChoose` expectation changed (a filtered-out nearer object now hides the scenery behind it).

## Checks (offline)

- Build (own dir out/build/occl, out\scripts\build_occl.cmd, own PDB endpoint): clean.
- thug2_check.exe on THUG2PM.exe and on C:\Games\THUG2\Game\THUG2.exe (read-only): 0 MISMATCHED, all 441 early
  self-tests pass.
- Mutations (out\scripts\mut_occl.ts, worktree-local): 18 of 18 caught - low-flat share ignored, level normal /
  upward thresholds, the "no up faces" share, each see-through blend rule, passed / locked occluders ignored, the sort,
  locked pieces given to Alt, Choose's block / filtered object / surface tie, the gizmo slack (both parts), segment
  depth at the middle, the occluder ignored in PickNearest.
- `node tools/check_code_writes.ts`: No overlaps. `npm run typecheck`, biome on the new tool: clean.

## In-game checklist (for the user) - NOT run in the game yet

`debug_editor=1` helps (the "hovering" / "click - selected" log lines).
1. `-edit TR`, put the camera where the screenshot was (-292, -137, -1668, yaw 205, pitch -8) and hover the hanging
   light under (834, 688): "under the cursor" names `g_ware_light_02` (or `_25_noflick`), its outline round the light -
   not `door01_reflect`.
2. Hover / click TR's glossy floor anywhere: nothing under the cursor except "(passing scenery SkWare_Main_9... -
   Alt+click takes it)"; no `_reflect` piece is ever hovered or taken through the floor.
3. Click a ramp / funbox / kicker: it's taken; click a wall with a ramp behind it: nothing (deselects).
4. Click again on the same spot after a ramp click: the cycle still reaches what's behind (status bar "n of m").
5. Alt+click on the floor: the floor piece; on a wall: the wall.
6. Gizmos: with a wall between the camera and a spawn / crown / light icon, hover the icon: not highlighted, a click
   doesn't take it. A spawn on the floor in view, a rail along a ledge, a sound icon in the open: still highlight and
   pick, including clicking a few pixels off the rail line. Watch for icons you'd expect to pick that don't (report
   where: the slack is 12 units + the 8-pixel radius).
7. With the trigger volumes shown: a volume behind a wall isn't taken by a click; one on the floor in view is.
8. Lock a ramp (L) in front of another piece: a plain click on it takes nothing (it still hides what's behind);
   click again cycles past it; Alt+click takes what's behind it.
9. Pick filter: turn off Objects, click a car in front of a ramp: nothing (the car still hides the ramp); turn Objects
   back on: the car.
10. foun (THPS3) and z_ma (THAW): the low kickers / banks (360 x 48 x 680 in foun, 353 x 48 x 915 in z_ma) hover and
    select; their floors still don't. BO: the hospital planter, the YMCA high rail and the girders select.
11. TR: SkWare_Main_12 (the low edge by the water, 360 x 60 x 924) still isn't taken by a click on it.
12. Frame rate while hovering in big levels (BO, z_ma): every shown piece's box is now tested and pieces in front of the
    first solid hit by their triangles (with collision too).

## Left / notes

- Occluded gizmos aren't drawn dimmer (that would need a ray per gizmo per frame) - only their picking changed.
- Locked objects don't occlude (objects are picked by boxes); locked scenery does.
- A non-uniform scale doesn't re-measure a piece's level top until the editor is restarted.
