# Level editor: stretching scenery, scaling objects (and the collision component decompiled)

Branch `worktree-agent-af733fcd291489c45`. **NOT run in the game yet** (editor work: the user tests it; checked offline
only - build, thug2_check on THUG2PM.exe and the stock THUG2.exe: argument sizes 0 mismatched, 411 early self-tests
passed on both - 420 after merging main -; check_code_writes "No overlaps"; 75 mutations, 75 caught - 6 after strengthening the tests).

## 1. Non-uniform stretch of scenery

- `Placement.scale` (level_editor.h) is now a `V3`, the scale along the piece's own axes as first touched: a point x is
  at `QuarterTurn(PerAxis(x, scale), quarters) + offset`. New helpers there: `PerAxis`, `TurnedScale` (odd quarter turns
  swap a per-axis scale's x and z: `Q S = S' Q`), `IsUnitScale`, `IsUniformScale`, `BoxSize`. `Apply`, `Compose`
  (`S = TurnedScale(S_outer, q_inner) * S_inner`, offset `Q_o (S_o o_i) + o_o`), `PlacedBox` (each axis its own length),
  `Scale::ScaleAbout(placement, pivot, V3 factor)` (a factor along the *world's* axes: `S' = TurnedScale(F, q) * S`) follow.
  Every `scale != 1.0f` became `!IsUnitScale(...)` (scenery, layout, walls).
- **Keys:** `[ / ]` uniform as before (scenery and now objects); **Alt+[ / ]** stretches along the world axis the camera
  faces most (`Scale::MainAxis` of the view direction: looking down at a piece stretches its height; looking along x
  stretches along x); a message names the axis. Shift: 1% steps for both.
- **Inspector:** Scale **X / Y / Z** fields (the piece's own axes; drag to scrub, Ctrl+click to type; one undo step a
  field) and a **Uniform** checkbox (on by default: a field changes all three in proportion, clamped so none leaves
  0.25..4). For scenery the axes are the first selected piece's as first touched; its quarter turns map them to the
  world's for the group.
- **Limits** per axis (`AllowedFactor(V3, scales, limits, uniform)`): each axis within 0.25x..4x; compressed collision
  per axis (`CollisionLimit` -> `4000 / unscaled side along that axis`); a uniform request takes the most limited axis.
- **Render (the normal fix):** `Scale::Capture` now also keeps each vertex buffer's normals when the mesh's vertex
  format has them: `sMesh +0x38` is the vertex format (the loader 0x004D6100 / 0x004D5CC0 stores the file's FVF, or a
  built-in shader's handle for 1..6; the 0x004D5940 path makes `table | 0x52`, XYZ | NORMAL | DIFFUSE); the renderer
  itself tests `+0x38 & 0x10` (0x004D1099). `NormalOffset`: an FVF (< 0x10000) with XYZ positions and NORMAL, stride
  >= 24 -> the normal's three floats at +12 (D3D's fixed FVF order; the file's own normal-offset byte, +0x1D, isn't what
  D3D reads). `Rebuild` writes them as `QuarterTurn(normalise(n / scale), placementQuarters)` - the inverse transpose of a
  per-axis scale, normalised, and turned with the piece (the engine's `sMesh::TurnY` never turns normals, so the copy's
  are the piece's as first touched; a scaled piece now gets turned normals right, an unscaled turned piece keeps the
  engine's unturned ones, as before). Unscaled: only turned, not normalised again (1x and no turn gives the copy back
  exactly). Bounding spheres: radius times the largest axis.
- **The copy's frame** (`Scale::FrameFor`, pure and self-tested): the copy is taken after `copied` quarter turns of the
  piece, so the placement's scale is applied along the copy's axes as `TurnedScale(scale, copied)`, positions turned by
  the turns since the copy. (Before, with a uniform scale, this didn't matter.)
- **Collision:** the game's `CCollObjTriData::Scale` (0x00416730) already takes a vector: vertices, box and the BSP
  splits per axis (`CCollBSPNode::Scale` scales each split by its own axis's factor). `RebuildCollision` passes the copy-
  frame vector. Shared-collision companions (level_editor_shared_coll.h) follow `Apply`, so they stretch too. Rails in the
  selection get the same `ScaleAbout` (their points stretch with the piece).
- **Layout:** `scale=<s>` when uniform (as before), `scale=<x>,<y>,<z>` when not, on sector / rail / clone records;
  both read. A DLL from before this change refuses a file with a per-axis `scale=x,y,z` on a sector / rail / clone record
  (its reader wants one number); uniform files are unchanged. **tools/game/layout_file.ts / apply_layout.ts** (another
  agent's) read only one number: they refuse such a record cleanly ("scale=" not a number) - for the bake they'd need:
  per-axis vertices, normals through `n / s` normalised then turned, sphere radius times the largest axis, collision
  `Scale` with the vector.
- **Known look:** textures stretch with the faces (a stretched brick wall has stretched bricks).

## 2. Objects scale (uniform and stretch)

### The collision component, decompiled (src/game/obj/collision_component.{h,cpp}; batch record below)

An object's collision is its **collision component** (`Obj::CCollisionComponent`, type 0xA432DC41 "Collision", vtable
0x00645EEC, 0x1C bytes, made by 0x0041C920): `+0x18` a movable collision object made by `Nx::CreateMovableCollision`
(0x00414990) - type 1 a `CCollMovBBox` (a box around the geometries), type 4 a `CCollMovTri` (one geometry) or a
`CCollMulti` of them - from the node's `CollisionMode` (0x2D7E583B -> `ShapeTypeFromChecksum`). Its triangles are the
model's collision: the asset manager's data for the model (`model +0x30` checksum -> data +0x88 count, +0x8C geometries)
**shared by every object of that model**, in the object's own space; a `LevelObject` takes the geometry of the level
sector named as it (`GetSector(Name)` -> its `+0x18` collision's geometry). `Update` (slot 1) places it at the object's
position (plus a single-bone model's bone translation, not turned) turned by the display matrix (times the bone matrix).
`DestroyBody` unregisters it from the movables' list and deletes it.

### How objects scale (level_editor_scale.cpp, `Scale::SetObjectScale` / `ObjectScale` / `ObjectLimit`)

- The model: `CModel::SetScale` (0x00492EE0, thiscall, the vector at +0x1C, flag 8 of +0x44 unless 1, 1, 1) - the model
  draw (0x00493090) multiplies the matrix rows by it; both model paths of the model component (0x004219F0) go through it.
- The collision: the first time an object is scaled, each of its `CCollMovTri`s gets **a geometry of its own**
  (`CCollObjTriData::Clone`, set as the triangle object's with `kOwnsGeometry` - the game's own `CCollMovTri::Clone`
  pattern - so the base destructor deletes it with the object), kept as copied (`CopyCollision`); every scale is then
  written from that copy (`RebuildCollision`, about the object's origin, along its own axes), the radius made again
  (`SetGeometry`), the triangle object's copy of the rejection scale (+0xD0) cleared while scaled and put back at 1. A
  `CCollMovBBox` is scaled from its first box. A `CCollMulti`'s children each so (its box marked dirty). Then
  `SetTeleported(true)`. 1, 1, 1 puts the copy's numbers back exactly.
- A record per object (id, object, its collision object, the copies) is checked each time (the object alive, the same
  collision object, the same geometry): anything else - the object made again - starts afresh. Forgotten with the level
  (`Objects::ForgetLevel`).
- Objects scale **about their own position** (their origin - usually their base), along their own axes; a group of
  objects each about its own. Stretch (Alt+[ / ]): each object along its own axis nearest the world axis chosen.
- `Objects::State` gained `scale`: undo / redo, Ctrl+D copies (the copy takes the source's scale), Home and Reset level
  (back to the first state: 1x) all go through `Save` / `Restore`.
- **Layout:** `object` / `copy` records get a trailing `scale=` (uniform or x,y,z); older DLLs ignore it (the object
  then unscaled); older files read as 1x.
- Limits: 0.25x..4x per axis; compressed collision per axis (`ObjectLimit`).

## Shared-file edits (small, additive / type change)

CMakeLists.txt (3 files), level_editor.h (Placement.scale V3 + helpers, BoxSize, comments), level_editor_scenery
(.h: SelectionScale V3, SelectionQuarters, ScaleSelection(V3, uniform); .cpp: Apply / Compose / PlacedBox, comparisons,
DescribeSelection / hover size, Rebuild call), level_editor_layout (.h records' scale, docs; .cpp scale fields on object /
copy, StateOf), level_editor_objects (.h State.scale; .cpp Save / Restore / ForgetLevel), level_editor_walls.cpp (3 x
`scale = {1,1,1}`), level_editor.cpp (ScaleSelectionBy / ScaleObjects / ScaleSelectionTo, keys, Inspector requests,
model, help, HUD line), level_editor_ui (.h model V3 + request doc, .cpp Inspector X / Y / Z + Uniform, F1 rows),
selftests_editor_walls.cpp (2 comparisons).

Merged with main (the carry / modes / polish / chains / bake work): `level_editor_carry.cpp`'s `Inverse` made per-axis
(`S^-1 Q^-1 = Q^-1 T`, T = 1 / S seen through Q) and `IsNone` tolerant (a per-axis reciprocal isn't exact: Between a
placement and itself can be 1.0000001 - within 1e-6 of 1 and 1e-4 units counts as no step); its self-test placements
stretched; the Outliner's moved mark (`ChangeMarks`) compares the V3; [ / ] keeps main's "invisible walls don't scale"
message and FollowScenery (scenery only). 4 more mutations there (out\scripts\mut_carry.ts), 4 caught (1 after
strengthening: IsNone's tolerance).

## Self-tests

- src/hook/selftests_editor_scale.cpp (Early, 6 tests): placements (ScaleAbout per axis after every turn, the world-x
  stretch of a turned piece is its z, Compose for all turn pairs with stretches, EngineState, PlacedBox corners / size /
  rays), the copy's frame (every q0 / copied / now turn and five stretches: rebuilt == placed) and normals (inverse
  transpose vs the stretched triangle's own normal, unit length, unscaled exact, zero), normals written at their offset,
  NormalOffset (FVFs, a handle, stride), limits (uniform and per axis, the most limited axis, NaN, compressed per axis,
  MainAxis / AlongAxis / NearestAxis, Describe), vertices (stretched rewrite, 200 changes then exact), records (tokens
  both forms, 13 bad forms, sector / rail / clone / object / copy write and read, old files, later fields, bad object
  scale), collision (stretched, turned, moved: vertices, box per axis, tree; 100 rebuilds then exact; limits per axis),
  objects (a fake object with model + collision components: unscaled copies nothing; stretched - model scale and flag,
  own geometry with kOwnsGeometry, shared one untouched, vertices / box / radius / rejection / teleported; clamped; 30
  changes then 1x exact; a changed collision starts afresh; a box per axis and back; compressed limits; no model / dead).
- src/hook/selftests_collision_component.cpp (Early, 2 tests): see the batch record.
- Mutations: 41 (out\scripts\mut_scale2.ts in the worktree) - placements, frame, normals, FVF, limits, tokens, layout,
  collision vector, objects - 41 caught (3 after strengthening: normal offset, FVF handle, the box's y).

Not testable offline: Capture / Rebuild on live meshes (D3D upload), normals in a real vertex buffer, compressed
collision scaled (the top-down heap), a live object's collision in the movables' list / its drawing - the checklist.

## Decomp batch record (for decomp-progress.md)

- [x] Batch N (NOT run in the game yet): **the collision component and the matrix component** (src/game/obj/
  collision_component.{h,cpp}, matrix_component.{h,cpp}; 12 functions).
  - `Obj::CCollisionComponent` (type 0xA432DC41, vtable 0x00645EEC, 0x1C bytes): 0x0041C920 Create (Calloc 0x1C, the
    base's constructor, vtable, type), 0x0041C710 DestroyBody (UnregisterMovable 0x00417800, the collision's slot 0
    with 1, the base's), 0x0041C780 Destroy, 0x0041C7B0 InitFromStructure (CollisionMode -> ShapeTypeFromChecksum, none:
    return; Class LevelObject: Name asserted, GetSector 0x0048C510, its collision 0x005480F0's geometry; then slot 13
    virtually), 0x0041C890 InitCollision (slot 13, RET 8: CreateMovableCollision from the geometry (count 1) or the
    model's asset data (+0x88 count, +0x8C geometries; the model component not null-checked - a crash without one, as
    the original) or none), 0x0041C980 Update (slot 1: the bone matrix 0x00493040 of the model component's model -
    position = its translation + the object's position (not turned, w the object's), orientation = D3DX multiply
    (bone, display matrix); else the object's position and display matrix; slots 5 / 6 of the collision object).
    Quirk kept: InitFromStructure passes the Name slot's old value when Name is missing (ours: 0).
  - `Obj::CMatrixComponent` (type 0x208353AC, vtable 0x00645F24, 0x58 bytes - cutscene_update.cpp's kSoundComponent):
    0x0041CB20 Create, 0x0041CAD0 Construct (identity matrix), 0x0041CA60 DestroyBody, 0x0041CA70 Destroy, 0x0041CAA0
    Update (object's display matrix = its matrix), 0x0041CAC0 SetMatrix. (Closes the gap find_gaps showed between
    0x0041C980 and 0x0041CB80.)
  - Tests: TestCollisionComponent (16 seeds: made / out of memory, InitFromStructure every branch, InitCollision
    geometry / asset / none, Update none / no model / no bone / bone, DestroyBody / Destroy with and without collision,
    flags 0 / 1 / 2 / 3) and TestMatrixComponent (4 seeds), original vs ours with recording stand-ins. 30 mutations
    (out\scripts\mut_component.ts, mut_matrix.ts), 30 caught (2 after strengthening: free flags 2, the vtable before
    the destructor).
  - Scans: scan_register_assumptions (only the known 0x4EFF92 and the existing InitEditedGoalStub lines), audit (no
    MISMATCH), find_gaps (0x0041CA60-0x0041CB20 were listed: now done), check_code_writes "No overlaps".
  - Switch off: `[replacements] disable=collision_component.cpp,matrix_component.cpp`.

## Risks / not done

- **Objects' rails / nodes:** an object's grind rails (rail nodes linked to a level object, cars' rails) don't scale with
  it; nor do proximity / trigger volumes on it.
- **Object culling / shadows:** the model's bounding sphere for culling may not include the scale (a 4x object may pop
  at the screen's edge); the shadow follows the model's own code (untested). Stretched objects are drawn with a non-
  uniform world matrix: their lighting may look a little off (D3D transforms their normals with that matrix).
- **Objects whose collision is a level sector** (LevelObject with a sector of its name): the geometry is the sector's
  own (shared with the sector) - it is cloned too, so the sector itself is untouched.
- A script that sets an object's model scale itself (ModelApplyObjectScale, the skater appearance) is overwritten if the
  editor scales that object (objects only; the skater isn't pickable).
- Scenery: a turned piece that has never been scaled keeps the engine's unturned normals (as before); after being scaled
  (even back to 1x) its normals are turned correctly - a lit piece may light differently once touched by [ / ].
- Meshes with one of the built-in vertex shaders (a handle at +0x38) keep their normals (can't be told apart); the
  per-vertex data at +0x94 (0x4C bytes a vertex: position, normal, tangent of the 0x004D5940 path) isn't rewritten (as
  moves leave it).
- Super-sector grid / compressed limits: as editor-scale.md (stretched pieces reach further on one axis).
- Memory: each scaled object keeps a copy of its collision vertices (DLL heap) and owns a cloned geometry (game heap,
  freed with the object).

## In-game checklist (NOT run yet)

1. TR, `-edit tr`: select a free-standing piece (a funbox). Look at it horizontally along the world's x (the status bar /
   the compass), press **Alt+]** three times: it stretches along x only (the message "Stretched along x"), standing on
   the floor; the Inspector shows Scale x 1.331, y 1.000, z 1.000.
2. Look down at it from above, **Alt+]**: it grows in height only (y). **Alt+[**: back down. Shift+Alt+]: 1% steps.
3. Skate it: the collision matches the stretched shape (grind / land on the stretched top and edges; no old-size box,
   no falling through the new parts).
4. Turn it a quarter (X), stretch along x again: the stretch follows the world's x (the piece's own z); skate it.
5. Inspector: untick **Uniform**, type X 2, Y 1, Z 0.5 (Ctrl+click each): exactly that; one Ctrl+Z each. Tick Uniform,
   drag X: all three change in proportion (stopping at 0.25 / 4 for the first axis to reach it).
6. Lighting: a piece with lit / shiny surfaces (TR's metal ramps, a car-park pillar) stretched 3x along one axis - its
   shading looks like an unstretched piece's (no odd dark or bright faces). Compare with the same piece unscaled.
7. Textures stretch with the faces (expected - note how it looks).
8. A group with rails (LA's fire truck / TR's SkWare_Rail01): stretch it - the rails stretch with it (their lines on the
   edges); grind them; the rail's shared collision (editor-collision-move) stretches too (no invisible box).
9. Ctrl+S, leave, load the (EDITED) entry: stretched pieces come back stretched; the layout has `scale=x,y,z`.
10. Ctrl+D a stretched piece, move the copy: same shape; save / reload.
11. Home / Reset level: back to 1x (exactly: no seams with neighbouring pieces after many presses).
12. **Objects:** select a level object (a bench / bin / barrier - a LevelObject or GameObject with collision) and press
    **]** five times: it grows about its position (its base stays put), the outline and Inspector follow (Scale 1.611
    on each axis).
13. Skate the scaled object: its collision matches the drawn size (bump into it, grind / land on it if it's grindable).
    Other objects of the same model (another identical bench) keep their own size and collision.
14. Alt+] on an object: it stretches along its own axis nearest the camera's main axis; skate it.
15. Inspector fields on an object (X / Y / Z, Uniform); undo / redo; Home (back to 1x); Reset level.
16. A car (a parked Vehicle with collision): scale it 2x - bigger drawn and solid at that size; does it still drive /
    follow its path if it's a moving car (watch for oddities).
17. Ctrl+D a scaled object: the copy has the same scale; Ctrl+S, reload: objects / copies come back scaled (`object` /
    `copy` records with `scale=`).
18. An object at 4x near the camera's edge: does it pop out of view early (culling)? Its shadow?
19. Old HUD (`editor_ui=0`): [ / ] and Alt+[ / ] work; F1 lists them.
20. thug2-decomp.log after the session: no errors ("collision couldn't be copied", "unknown kind").

Switching it off: the whole editor `[features] level_editor=0`; the decompiled components
`[replacements] disable=collision_component.cpp,matrix_component.cpp`. Scaling has no separate switch (nothing scales
unless [ / ], Alt+[ / ], the fields or a layout's scale= do it).
