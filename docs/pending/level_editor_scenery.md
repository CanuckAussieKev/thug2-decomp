# Level editor: scenery (static scene sectors) picked, outlined and moved

Branch `worktree-agent-aba8ae8c772a2cee9`, based on main c4f11c5. **NOT run in the game yet.**

## What

The level editor (pause menu -> LEVEL EDITOR, `[features] level_editor=1`) now picks the level's static scenery as
well as composite objects:

- Hovering: the game's own collision ray (CFeeler, static collision only) finds the sector under the cursor; sectors
  with no collision are found by their bounding boxes. The nearer of that and the object box hit wins. The hovered
  sector is outlined yellow (orange when it can't be moved); the HUD's Hover line says
  `scenery <name> <checksum>  W x H x D  cluster <name>  (can't move: <why>)`.
- Left click on scenery selects the sector **and the rest of its trick-object cluster** (the LevelGeometry node's
  `TrickObject Cluster=`), outlined cyan, and starts a drag. Alt+click or a middle click adds / removes one sector.
  A click on nothing deselects; a click on an object selects the object (the two kinds don't mix).
- Moving: the existing controls - drag (horizontal at the selection's height; Shift: up / down), Page Up / Down
  (R / F) raise / lower, the wheel turns: **scenery by quarter turns (90 degrees a notch)** about the selection's
  centre (the engine can only turn level geometry by quarters, see below). HUD: Selected line
  `scenery <first> + N more at (x, y, z) turned <deg>`, and `Scenery moved: N sectors`.
- Nothing is saved; a moved sector stays moved until the level reloads (also after leaving the editor).

Files: `src/game/front/level_editor_scenery.{h,cpp}` (new), `level_editor.{h,cpp}` (picking / selection / drag /
HUD / drawing generalised to "an object or scenery"), `src/hook/selftests_editor.cpp` (TestEditorScenery),
CMakeLists.txt.

## The fire truck (LA)

It is **static LevelGeometry**, not an object: in LA's node array (`levels\LA\LA.qb`) nine LevelGeometry nodes share
`ncomp_LevelGeometry_542 = {Class=LevelGeometry CreatedAtStart TrickObject Cluster=Fire_Tender CollisionMode=Geometry}`:
`FE_CAB`, `Fire_Tender`, `FE_rearcab`, `dt_tire01`, `dt_tire03`..`dt_tire07` (around x -2600..-2030, z 6150..6600).
The ladder is separate: `Turntable_Ladder` (ncomp_LevelGeometry_1, no cluster), `FE_Ladder_Section02` and
`Object03` (cluster `FE_Ladder_Section02`). The truck also has grind rails: RailNodes `TRG_FireTruck01..25`
(cluster Fire_Tender, Chainlink). So a click on the body selects the 9 cluster sectors; Alt / middle click adds the
ladder's three. The rails are **not** moved (they live in the rail manager) - grinding stays where the truck was.
(The node Pos values are pivots only; level geometry vertices are in world space, drawn with no transform.)

## Findings (addresses)

- `Nx::CSector` (vtable 0x00649A40, 0x1C bytes): +0x08 flags (1 active?, 0x40 collision in super sectors, 0x20
  marked for a super-sector update, 0x08/0x10 delete/remove), +0x0C checksum default, +0x10 quarter turns, +0x14
  CGeom, +0x18 CCollStatic. Methods: 0x0049E660 SetGeom, 0x0049E680/0x0049E690 Set/GetVisibility (geom vfunc +0x2C /
  +0x30), 0x0049E6A0 GetBoundingBox, 0x0049E6C0 SetWorldPosition, 0x0049E700 SetOrientation, 0x0049E740
  GetWorldPosition, 0x0049E760 SetYRotation(rot90), 0x0049E7C0 SetScale, 0x0049E890 / 0x0049E8A0 flags set /
  clear, 0x0049E9B0 SetActive (geom vfunc +0x34, collision flag 4), 0x0049E9E0 IsActive, 0x0049EA30
  SetCollidable? (flag 1, collision flag 2), 0x0049EAA0 SetCollision, 0x0049E900 ctor.
- `CEngine::sGetSector(checksum)` 0x0048C510 (cdecl, walks the scenes); `CScene::GetSector` 0x0049CB00 (thiscall:
  hash table at scene +0xD0 = {bits, entries[1 << bits] of {checksum, sector, next}}); scene list head 0x0048C7F0.
- `CScene::UpdateSuperSectors(list)` 0x0049D780 (thiscall, RET 4): walks every sector, flag 4 -> add, 0x18 ->
  remove/delete, 0x20 -> update; then 0x00418F60 on the super-sector manager (scene +0xD4). The park editor calls it
  with 0 and the bottom-up heap pushed (0x00592090). Scene +0x274: "needs update" (0x0049E1B0, set by clones).
- PC geometry `CGeom` vtable 0x0064A368 (base CGeom stubs at 0x00648EC8, "STUB: Plat..." strings around 0x00648C24 "STUB: PlatSetWorldPosition", used by 0x0048DBB0):
  +0x14 / +0x24 box min / max (as loaded - not updated by moves), +0x3C meshes, +0x40 count, +0x44 visibility
  mask, +0x48 active, +0x4C instance (null for level geometry). Vfuncs: +0x2C SetVisibility 0x004B4870, +0x34
  SetActive 0x004B4630, +0x3C GetBoundingBox 0x0054CA80, +0x48 SetWorldPosition 0x004B55D0, +0x4C GetWorldPosition
  0x004B46A0, +0x50 SetOrientation 0x004B5650 (instances only), +0x58 RotateY 0x004B57C0.
- `sMesh::SetPosition` 0x004D6E00: moves the vertices by the change from the mesh's last position (+0x90 matrix,
  made on demand) in each vertex buffer's CPU copy (+0xCC[i]: {size, ?, D3D buffer, copy, ...}) and uploads it
  (0x004D40E0: Lock, memcpy, Unlock); also the sphere (+0x04). `sMesh::RotateY` 0x004D6FB0 (not a Ghidra
  function): quarter turns about the mesh's position: rot 1 (dx, dz) -> (dz, -dx), rot 3 -> (-dz, dx).
- `CCollStatic` vtable 0x00645C18 (0x18 bytes: +0x04 checksum, +0x08 flags, +0x0C tri data): +0x14
  SetWorldPosition 0x00410C20 translates by (position - vfunc +0x1C's constant origin 0x00691914) - i.e. it
  treats the position as a change (0x00416400 moves vertices / bbox); +0x18 SetOrientation is a no-op
  (0x005BAB90); +0x58 RotateY 0x0040AE80 -> 0x004164F0 (about a pivot).
- CFeeler collision data from +0x24: normal, point (+0x34), ..., distance fraction (+0x48), ignore flags
  (+0x4C/+0x4E), the hit collision object (+0x54) and its checksum (+0x58) - written by 0x004131F0.
- Node commands: 0x005A4DA0 dispatches Create(0)/.../Kill/Visible/Invisible; LevelGeometry Kill / Create
  (0x005A3B80) toggle the sector's visibility mask bit; Create with `CloneFrom`-style key 0xEDC063B9 (0x005A2E50)
  clones a sector (0x0049D510) and places it with SetOrientation (Angles), SetScale, SetWorldPosition (Pos).

## How moving works (and why not "promotion" to a composite object)

The engine already moves level geometry in place - the park editor does it for its pieces (0x00594730:
SetWorldPosition + SetYRotation on a CSector, then UpdateSuperSectors). That moves the drawn vertices and the
collision together, with no new objects, no hidden originals and no memory beyond the meshes' position matrices.
Promotion (a LevelObject cloned from the sector + hiding the original) was not needed. Limits: only quarter turns
(SetOrientation is instances-only for geometry and a no-op for static collision).

`CSector::SetWorldPosition` can't be used directly: the collision side takes the position as a change, so a second
move would double it. The editor keeps its own record per sector (box, turns and position when first touched, the
placement since) and applies: the turn through CSector::SetYRotation (both turned about the geometry's position),
then the geometry's SetWorldPosition (absolute) and the collision's SetWorldPosition handed origin + change, sector
flag 0x20 when 0x40 (as 0x0049E6C0), and `UpdateSuperSectors` (bottom-up heap pushed) once the drag / raise / turn
ends and on leaving the editor. The record is re-checked before each use (sector, geometry, turns, position) so a
reloaded level starts afresh.

Movable only when: PC geometry (vtable 0x0064A368), not an instance, a box no larger than 4000 in any direction
(ground / streets excluded), and every mesh's vertex buffers have a CPU copy holding exactly that mesh's vertices
(else "shared vertex buffer" / "no vertex copy" - moving would shift the wrong vertices).

## Tests

- `TestEditorScenery` (Early, offline): quarter turns match the engine's rotation code and TurnAboutUp; placements
  moved / turned about a pivot; the engine state (turns, position) follows a placement from any first state;
  placed boxes' corners, ray hits and bounds; size limits; box growth. Proved with tools/mutate.py (7 mutations:
  quarter-turn direction, turn sign, engine turns, engine position, box axis, size check, box growth - all caught).
- `tools/ghidra/audit_game_function_calls.py`: no mismatches (the three "?" are tail-jump thunks to vfuncs whose
  RETs were checked by hand: 0x004B55D0 RET 4, 0x004B46A0 RET, 0x00410C20 RET 4).
- thug2_check: all passed (178 early tests). Built with tools\verify_build.cmd.

## What to check in the game

Use `[features] debug_editor=1` (the log lists each scene's sector counts on entering the editor: "N movable").

1. LA: fly to the fire truck (around -2400, -350, 6400). Hover the body: HUD "scenery Fire_Tender ... cluster
   Fire_Tender", yellow box. Click: 9 cyan boxes (log lists the members). Drag it: the truck (cab, body, wheels)
   moves smoothly; the ladder stays (Alt+click / middle click its pieces to add them).
2. Wheel over it: turns 90 degrees a notch about its centre - check it turns in place, not around the world origin,
   and the boxes follow.
3. PgUp / PgDn: raises / lowers it.
4. Leave the editor, skate into the moved truck: you should collide with it where it now is, and pass through
   where it was. Grind rails (TRG_FireTruck*) are still at the old place (expected).
5. Hover the ground: "(can't move: too big)", orange box. Hover an invisible wall: the ray goes through it.
6. Things to watch: the truck disappearing at some angles (render culling), lighting looking wrong after a turn,
   a crash on the first move (vertex buffers), a freeze when committing (UpdateSuperSectors), skater falling
   through the moved truck (super sectors not updated).

## Risks

- Vertex buffers: moving writes the meshes' CPU vertex copies and re-uploads them (the park editor's path). Checked
  per mesh that each buffer's copy is exactly that mesh's vertices; if level meshes share buffers, sectors are
  reported "can't move: shared vertex buffer" instead (then nothing in a level would move - check the census).
- Collision: only updated in the super sectors when a move ends (UpdateSuperSectors) - during a drag collision
  lookups use the old cells (harmless: the skater is out of the world). The park editor's update runs with the
  bottom-up heap; in a normal level that heap is the level's - watch for heap errors in the log.
- Rails, trick-object clusters (gap / trick scoring geometry), particles, lights and shadows tied to the scenery are
  not moved. LevelObjects / Kill-Create of the same sectors by goal scripts keep the moved vertices.
- Normals are not turned with quarter turns (the engine's RotateY doesn't either) - prelit level geometry mostly
  doesn't care; dynamically lit parts may light oddly.
- Box picking (sectors without collision) uses the loaded box placed by our record; sectors that had been placed by a
  clone (CloneFrom) before the editor touched them may have boxes in their own space (rare in levels).
- Selecting by cluster can grab more than expected in levels where a cluster spans many sectors; Alt / middle click
  removes single sectors.
- Alt+click: Windows may treat a lone Alt press as a menu key (SDL normally swallows it); the middle click does the
  same thing.
