# Level editor: a moved piece's collision goes with it (the TR rail bug)

Branch `worktree-agent-a33bcf63219045d0e`. **NOT run in the game yet** (editor work: the user tests it; checked offline
only - build, thug2_check on THUG2PM.exe and the stock THUG2.exe: argument sizes 0 mismatched, 392 early self-tests
passed on both; check_code_writes "No overlaps"; 18 mutations, 18 caught).

## The bug

User (main install, DLL 70387b5 / 24ad588): in TR, SkWare_Rail01 moved - mesh and rail nodes moved, "an invisible wall
where the rail used to be".

## Cause (evidence)

**The rail has no collision of its own.** TR's collision file (`Data\pre\TRcol.prx` -> `levels\TR\TR.col.xbx`, 605
objects) has no object named SkWare_Rail01 (checksum 0xBF49DBEE). The rail's render sector (TR.scn.xbx: box
(-646, -765, -3358)-(-504, -711, -2289), 2 meshes) gets its collision from **SkWare_Main_1**, a collision-only object
(no render sector: in the game a sector with geometry null - an "invisible wall"): 20 faces, flags 0x1000, of which 16
are the rail's box (x -640..-514, y -765..-717, z -2298..-3349, every corner within 3.5 units of the rail's triangles)
and 4 are SkWare_Rail02's (x = 1200, within 0.6 units of it). The editor moved the rail sector's meshes and its
`+0x18` collision - null - so the 16 faces stayed: the invisible box. Not specific to TR: 276 of TR's 790 render
sectors have no collision of their own (BO 556 / 1481, NO 1139 / 2431, ST 716 / 1463, SE2 574 / 934); most are
decoration, but some have their collision in collision-only objects like this (offline survey in the worktree's
out/collmove: survey.ts, proto.ts).

The engine path for a sector's **own** collision was checked and is right (Ghidra, our decompiled nx code):
- CCollStatic slot 5 (vfunc +0x14) = 0x00410C20: delta = position - GetWorldPosition (slot 7 / +0x1C, 0x004123A0: the
  shared origin 0x00691914) -> CCollObjTriData::Translate 0x00416400: BSP splits (0x00415D80), float vertices, box (a
  compressed geometry's vertices are offsets from the box: they move with it). So it really moves the faces.
- CSector::SetWorldPosition 0x0049E6C0 does exactly what ApplyTrack does (collision +0x14, flag 0x40 -> 0x20, geometry
  0x0048DEC0). CSector::SetYRotation 0x0049E760 turns the collision too (slot 22, +0x58, about the geometry's world
  position) - turns were affected by the bug the same way, scale too (Scale::Rebuild only rebuilds a sector's own).
- UpdateSuperSectors 0x0049D780 consumes the flags: 4 -> the "added" list (then 4 cleared, 0x40 set), 0x18 ->
  "removed", **0x20 -> "moved"** (cleared), then CCollGrid::Update 0x00418F60, which takes a moved object out of the
  cells its box no longer touches and puts it in the new ones.
- No cache of faces: CCollCache (0x0040ABA0) keeps object pointers and their box pointers, refilled per query box;
  faces are always read from the geometry.
- Ctrl+D (CScene::CloneSector 0x0049DF30) clones a sector's own collision only - so a copy of the rail had no
  collision at all; layouts (ApplyStates a second after the level is up) go through the same ApplyTrack.

**Drift (found by the walls agent, fixed here too):** ApplyTrack handed the sector's own collision fractional changes
every frame of a drag; each Translate rounds the BSP tree's splits (sixteenths) towards zero, so the tree drifted from
its faces by up to 1/16 unit a frame (faces missed near splits after long drags). Now the collision is moved by
`OnGrid(position - position0)` (whole sixteenths, level_editor_walls.h's OnGrid) minus what it has had: every step
exact, back home gives the level's tree again (self-test). The collision is within 1/32 unit of the meshes. Turns
(SetYRotation, discrete) are as before.

## Fix: shared collision (new src/game/front/level_editor_shared_coll.{h,cpp})

The first time the editor moves or hides a piece (ApplyTrack, before the engine moves it; ApplyStates before hiding),
the faces of the level's collision-only sectors that belong to it are copied into a collision object of our own (a
"companion": a CCollStatic with the game's vtable 0x00645C18, a float geometry, no BSP tree - the queries then test all
faces through the shared 0..2047 face list 0x0067C6B8 (SetUpAllFaces) -, rejection scale 0, a zero rejection buffer,
vertex bytes copied for the feelers' ground light), and the originals get face flag 0x10 (not collidable: every
feeler's default ignore mask, the camera's 0x90 too). The companion carries the piece's checksum (hits on it name the
piece: picking finds it). After each ApplyTrack its vertices are written from their copy by the piece's placement - the
same `Apply` the meshes follow (move, quarter turns, scale) - and Scenery::Commit, after UpdateSuperSectors, puts it in
the scene's collision grid (CCollGrid::Update: added once, then moved; top-down heap current as 0x0049D780 has it).

Which faces belong to a piece (offline-calibrated on TR / ST data):
- not triggers / not already non-collidable (flags 0x50 clear: gap and kill volumes stay), from collision-only sectors
  that are on (object flags 2 / 4 clear), all three corners inside the piece's box grown by 6 units;
- either the object is named after the piece (`<name>_col`, `_ic`, `_col01`, `_ic01` - TR's BlackBoard_Cut_Col,
  NJ_House_Column2_East02_IC: taken whole), or every corner is within **6 units** of the piece's triangles (meshes as
  drawn, billboards left out) and the piece is **strictly the nearest** of the level's other solid pieces whose grown
  boxes hold the face (see-through pieces - decals, shadows, glows - neither take nor compete; the editor's copies don't
  compete). Faces of an object named after another piece stay with that one. At most 2048 faces.
- TR with that rule: SkWare_Rail01 takes SkWare_Main_1's 16 rail faces, SkWare_Rail02 its 4; the folding chairs take
  nothing of the barrel-fire collision; SkWare_Fence02 takes 45 of SkWare_Fence03's 63; Wh_TB_Keg takes 7 of each of
  WH_TB_Barrelfire_03_Col / _04_Col's 10 (props stacked in one spot - a doubtful case); BlackBoard_Cut takes its _Col.

Copies (Ctrl+D, layouts' clones): the copy gets a copy of its source's companion as it is now, or of the faces that
would be the source's (gathered before the copy exists, the originals left on for the source). Hiding (Delete,
layouts' hidden) switches the companion off with the piece (flag 4); Home / Reset level put it back with the piece.
An entry whose piece / scene / grid is gone (another level) is forgotten without touching anything.

Switch: `[editor] shared_collision=0` in thug2-decomp.ini turns the taking off (the bug comes back; nothing else
changes). The drift fix has no switch.

Shared-file edits (small, additive): CMakeLists.txt (2 files), level_editor_scenery.h (`SceneLive`),
level_editor_scenery.cpp (Track.checksum / collisionMoved, ApplyTrack: Ensure / on-sixteenths collision move / Place,
TrackOf: Rebase, ApplyStates: hide, Duplicate: PrepareCopy / FinishCopy, Commit: per-scene SharedCollision::Commit).

## Self-tests (src/hook/selftests_editor_shared_coll.cpp, Early, 4 tests)

Point-to-triangle (inside / above / below / past corners and edges / degenerate), strip distances (best kept, joins of
repeated indices not triangles), Belongs (within 6, strictly nearest, NaN), Takeable; faces read from float 10-byte and
compressed 8-byte geometries; the companion built (shared corners, vtable, no tree, rejection 0, box), placed with turns
/ moves / scales (vertex = Apply(placement, base), box, raw vertex read), 50 placements then home exact, rebase, flag 4,
the 2048 cap; a BSP split moved along a 120-step drag on sixteenths comes back exactly (and fractional steps drift).
**18 mutations, 18 caught** (spec: out\scripts\mut_shared.ts in the worktree).
Not testable offline: Gather on a live level, the grid update, hits on the companion - the checklist below.

## Risks / not done

- The rule is a heuristic: a collision-only face close to a piece's surface (6 units) and nearer to it than to any
  other solid piece goes with it. Invisible helper walls hugging a piece (ST's g_invis_*) may partly go with it; a
  shared object spanning several pieces is split between them. `shared_collision=0` turns it off.
- A level script turning a collision-only sector off after its faces were taken doesn't turn the companion off; nor
  does the walls module's switch / delete of that wall (level_editor_walls.h) - the companion follows the piece only.
  The walls module's "collision off" for a drawn piece doesn't reach a companion (a piece without collision of its own,
  like the rail, can't be switched there at all).
- The collision view (level_editor_view.h) draws sectors' collision: companions aren't drawn; the faces taken show as
  their original sector's (now non-collidable).
- A companion outside the level's grid box isn't found (as any moved piece: docs/pending/editor-scale.md).
- Memory: a companion per moved piece with shared collision, kept for the level (DLL heap).

## In-game checklist (NOT run yet)

1. TR, `-edit tr`, `debug_editor=1`: select SkWare_Rail01 (the rail by the warehouse ramps), drag it ~300 units sideways.
   The log says "shared collision of BF49DBEE: 16 faces taken along".
2. Leave the editor and skate: no invisible wall where the rail was (ride / ollie through the old place); the rail in its
   new place is solid (bump into its side, land / manual on its top) and grindable.
3. Turn it (Z / X) and scale it (]) in the editor, skate again: the collision matches the turned / scaled box.
4. Home (back where the level has it): skate - the collision as originally (no doubled or missing box).
5. Ctrl+D the rail, move the copy away: both the source and the copy are solid, nothing at in-between places.
6. Delete (hide) a moved rail: nothing solid there any more; Ctrl+Z: solid again where it was put.
7. Ctrl+S, leave, load the level's (EDITED) entry: the rail in its moved place, solid there, nothing at the old place.
8. SkWare_Rail02 (x = 1200): move it - its own 4 faces go with it; SkWare_Rail01 still solid where it is.
9. A piece with its own collision (a funbox): drag it around for a long time (many seconds, back and forth), then Home:
   skate its edges - no gaps or missed faces (the drift fix).
10. Camera: with the moved rail between camera and skater, the camera behaves as with any wall (no snapping at the old
    place).
11. Click the moved rail in the editor where its collision is: it is picked (hits on the companion name the rail).
12. Another level (ST or NO): move a few houses / props; log lines show what was taken; skate around them - report any
    piece that dragged collision it shouldn't have (an invisible wall moving with it) or left some behind.
13. `[editor] shared_collision=0`: step 1 takes nothing (the old behaviour: the invisible box stays).
14. thug2-decomp.log after the session: no errors from the editor.

Switching it off: `[editor] shared_collision=0` (the taking); the whole editor `[features] level_editor=0`.
