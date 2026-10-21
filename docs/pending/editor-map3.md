# editor-map3 (agent record): the feature map's sky, per-face collision flags, scenery clusters, camera nodes - NOT run in the game yet

NOT ORIGINAL (the level editor is ours). Branch `worktree-agent-ae45f24c3a2a52fa1`. Checked offline only (build, thug2_check
on THUG2PM.exe and the stock C:\Games\THUG2\Game\THUG2.exe, check_code_writes, npm typecheck / lint / test, mutations);
**NOT run in the game** - the user tests the editor by hand. All four rows: saved in the edited copy's layout, undo /
redo, Home, Reset level, offline only (layouts never apply in a network session or a career-type mode - the existing
guards).

## 1. Sky (Level properties: Sky)

- **How a level's sky is chosen and loaded:** the level structure's `sky=` (Levels.qb: `Level_TR ... sky="TR_Sky"`;
  21 of THUG2's own, the ported levels' in the overlay's Levels.qb). `load_level` runs `if gotparam sky { LoadScene
  scene=<sky> }` before the level's own scene; Nx::CFunc_LoadScene (0x00445130) takes a scene whose name holds "_sky"
  as the sky (is_sky, no super sectors). The sky scenes are **loose files** (`Data\levels\<X>_Sky\<X>_Sky.scn/.tex/.col.xbx`,
  the ported ones in `thug2-decomp-data\levels`), not in the levels' pre archives - so any level's sky loads with any
  level.
- **The skies offered:** every level structure's `sky=` the game has (a structure with a `load_script`), each once,
  sorted (`LevelProps::Skies()`): THUG2's AP / AU / BA / BE / BO / CA / Default / DJ / LA / NO / PH / SC / SE / SE2 / ST /
  TestLevel / TR / sk5ed..sk5ed5 skies, plus the ported levels' (alc_sky, burn_sky, ... zoo_sky, thps3_*, thug1_*).
- **Applied:** as the other level properties - written into the level structure by LevelEditor_PickLevel when the
  level select loads the edited copy, put back once it's up. **On reload only** (opened with -edit / the pause menu, the
  level was already loaded: the sky changes the next time the EDITED entry is loaded). Not live: loading / unloading a
  scene mid-level wasn't attempted (heaps, the renderer's sky pass).
- Record: `level ... sky=<scene>` (%-escaped; `LevelProps::ValidSky`: letters, digits, _, holding "_sky"). Older DLLs
  skip the key. Baked: Levels.qb's `sky=` (apply_layout.ts `levelStructureValues`).

## 2. Per-face collision flags (Faces mode, K)

- **K** (or the toolbar's **Faces (K)**) turns Faces mode on (and the collision view with it). A click takes the
  collision face under the cursor (the nearest face drawn - `View::PickFace`, the view's own triangles, so what you see
  is what you pick), **Shift+click** adds / drops one; the face under the cursor is outlined yellow, the selected ones
  cyan. The Inspector's **Collision faces** section: the selection (face n of <piece>: 0x.... or n faces in m pieces),
  a checkbox per flag - Skatable 0x01, Not skatable 0x02, Wallride 0x04, Vert 0x08, Not collidable 0x10, Trigger
  0x40, Camera passes 0x80, Not taggable 0x2000 ("(some)" when mixed; ticking a mixed one sets it on all) - each with
  what the game does with it (tooltip), **Home** (the level's flags), **Drop selection**, **All faces of <piece>**
  (the piece's whole collision and the faces its shared collision took along) and **All faces of the selected
  scenery**. Esc drops the faces, then leaves Faces mode; Home with faces selected puts them back.
- **Written live:** the face's u16 flags in the sector's collision (CCollObjTriData face +0) - the feelers read them
  at every query (the collision cache keeps objects, not faces). Kept through moves / turns (Translate / RotateY touch
  vertices and the tree), scale (Scale's rebuild copies vertices only) and shared collision: a face a moved piece's
  companion took along is edited in the companion and in the flags given back when the companion goes; the original
  keeps 0x10 while taken (`SharedCollision::SetTakenFlags`). A copy's (Ctrl+D) faces are refused with a message (a
  copy's checksum is new each session); a copy copies its source's faces as they are.
- Undo: one step a change (module step, below). Reset level: every face back. Record: `faces <checksum>
  <face>:<flags>,...` (index in the collision as loaded = the .col's order; 4 hex digits). Applied with the layout,
  after the sectors (a moved piece's companion exists by then). Baked: the .col object's face flags; a taken face's in
  its `ed_coll_*` companion, the original `| 0x10`; a copy's companion keeps its loaded flags (as the DLL).
- Not offered: the trigger *node* a 0x40 face needs (a face flagged trigger without a node whose TriggerScript the
  feeler finds does nothing - the tooltip says so; trigger volumes are the Triggers window's).

## 3. Scenery cluster membership (Inspector: Trick object)

- **What the game does:** CFunc_ParseNodeArray (0x005A3732-0x005A37CE) registers each node with TrickObject (not a
  rail: 0x00565BB0's gate) with the trick object manager (0x007CE500): AddCluster 0x005896D0 (Cluster=, else its own
  name), then 0x00589760(name, cluster) - the cluster's object list (+0x44; 0x00588630 also readies the sector for
  graffiti colours via 0x0049E670) and the name -> cluster map (+0x20) when the name isn't the cluster's.
- **Live:** "Put in this cluster" (a typed name - another piece's cluster, or a new one), "Each its own" (its own name),
  "Not a trick object", "Home" - for the selected scenery pieces. Put in: AddCluster + 0x00589760, as the load. Taken
  out: unlinked from its cluster's list (the 24-byte node kept, not freed, until the level goes) and flushed from the
  name map. An emptied cluster stays in the manager's table. The editor's selection groups follow at once
  (Scenery's ClusterOf asks `Clusters::Override`).
- The level's node isn't changed in the session; on reload the layout's `cluster <sector> <cluster>|-` records are put
  in place after the level's own registration (live calls again). Baked: the node's own `TrickObject` + `Cluster=`, or
  both taken out; a TrickObject from a shared structure (ncomp_LevelGeometry_n) can't be taken out of one node: warned
  (the DLL still takes it out of the manager when the edited copy loads). A piece without a node: warned, not baked.

## 4. Camera nodes (V; Inspector: Camera / Cameras)

- **What THUG2's cameras are (studied):** no level has camera nodes - the node arrays' `Type=Camera` nodes are
  ProximNode sound-reverb boxes. Intro / goal / view-goals cameras are script structures: `{virtual_cam targetid=world
  targetOffset=(point looked at) positionOffset=(camera relative to it) frames=...}` (BO_scripts.qb's
  BO_Goal_Intro_Cameras, every goal's view_goals_cameras); cutscenes are .ska camera animations.
- **Ours:** `{Pos Angles Name=ed_cameraNN Class=GenericNode Type=ed_camera targetOffset=(...) positionOffset=(...)}` in
  the editor's overlay (owned by this module: its own record). Angles as every node's: (-pitch, heading, 0) - the
  game's angles-to-matrix 0x0041A730 (RotationX * Y * Z) turns its at row to where the view looked (self-tested
  against the real function). targetOffset / positionOffset as a virtual_cam takes them (target `dist` units ahead,
  default 1000, 50..20000), kept in step with the node's place every frame - a script can build a virtual_cam from it.
- **V** (or the Inspector's Cameras > New camera from the view) makes one where the view is; the frustum (orange, the
  selected one yellow; its up marked; a line and a cross at its target) is drawn for every camera within 12000. Select
  it (Outliner / gizmo): drag / arrows / W A S D move it, Z / X turn its heading, the Inspector's **Camera** section:
  Pitch, Target distance, **Look through** (the editor's view put there - fly to leave), **Put at the view**, Delete.
  Delete key on it works too (an added node). Undo: create / delete / pitch / target / put-at-view one step each;
  moves as any node's. Reset level removes them.
- Record: `camera <name> pos=x,y,z angles=x,y,z dist=d`. Baked: the node appended to the node array.
- **The game honours:** nothing reads a camera node by itself (GenericNode makes nothing) - it's for scripts / goals
  that use its fields.

## Files

- New: `src/game/front/level_editor_faces.{h,cpp}`, `level_editor_clusters.{h,cpp}`, `level_editor_cameras.{h,cpp}`,
  `level_editor_modules.h` (what the editor offers modules: a generic undo step `ModuleStep` / `PushModuleUndo`,
  `CurrentView`, `LookFrom`), `src/hook/selftests_editor_map3.cpp`.
- Small hooks: `level_editor.cpp` (UndoStep::kModule; K / V keys; module Updates; the Faces-mode click and hover; Esc /
  Home / Deselect; the overlays; the three module functions), `level_editor_ui.cpp` (toolbar Faces (K), the sections,
  F1 rows), `level_editor_fields_ui.cpp` (Sky row), `level_editor_level_props.{h,cpp}` (sky, ValidSky, Skies),
  `level_editor_layout.{h,cpp}` (records, Capture / Apply / ResetToLevel / OnSceneLoaded), `level_editor_view.{h,cpp}`
  (PickFace, InvalidateCache; the drawn faces keep their index; companions' faces drawn are pickable - PickWall still
  skips them), `level_editor_shared_coll.{h,cpp}` (Companion::SetFaceFlags, TakenFace, OriginOf, SetTakenFlags,
  CompanionOf), `level_editor_scenery.cpp` (ClusterOf asks Clusters::Override), `CMakeLists.txt` (4 lines).
- Tools: `tools/game/layout_file.ts` (faces / cluster / camera records, level sky=, validSky), `tools/game/apply_layout.ts`
  (their bake; FaceCopy.origin), `tools/game/apply_layout.test.ts` (4 tests).

## Checks (offline)

- Built (own build dir out\build\map3, PDB endpoint thug2map3), no warnings. thug2_check on THUG2PM.exe and on the stock
  THUG2.exe (read only): argument sizes 0 MISMATCHED; **425 early self-tests all passed** on both (5 new:
  TestEditorFaceFlags, TestEditorClusters, TestEditorCameras - including the game's own 0x0041A730 -, TestEditorSky,
  TestEditorMap3Layout).
- `node tools/check_code_writes.ts`: No overlaps. `npm run typecheck`, `lint`, `test`: 131 pass.
- Mutations (`node tools/mutate.ts out/scripts/mut_map3.ts` in the worktree, own verify build): **28 / 28 caught** -
  flag bits, set / clear, the mixed checkbox, click / Shift+click, face records (twice, 5 hex digits, unsorted,
  trailing junk, decimal flags), unchanged faces grouped, a cluster's own name / compare / zero / trailing, the pitch
  sign (also caught against the game's 0x0041A730), the roll, the offset sign, the target clamp, the frustum's sides,
  camera records (pos twice, no pos, distance unclamped), the sky's _sky test / record / case / structure value, the
  layout's camera reading.

## In-game checklist (NOT run in the game yet)

Use a workspace or the main install with this branch's DLL; `[features] debug_editor=1` helps. Watch thug2-decomp.log for
`level editor:` lines and `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`. Back up `thug2-decomp-layouts\` first.

Sky
1. `-edit TR`, Inspector foot > Level properties > **Sky**: the list has every level's sky (BO_Sky, ..., zoo_sky); pick
   BO_Sky. Ctrl+S. The layout's `level` line has `sky=BO_Sky`.
2. Leave, main menu > level select > EDITED LEVELS > TRAINING (EDITED): Boston's sky over Training. Load Training as
   shipped: its own sky. Try a ported level's sky (e.g. zoo_sky) on a THUG2 level, and a THUG2 sky on a ported level.
3. Ctrl+Z / Ctrl+Y over the Sky choice; Reset level puts it back (the level's).

Faces
4. Press **K**: collision view on, "Faces mode" message, the toolbar's Faces (K) ticked. Hover: the face under the cursor
   outlined yellow. Click a floor face: cyan outline, the Inspector's Collision faces shows "face n of <piece>: 0x...."
   and its flags ticked.
5. Shift+click two more faces (on a quarter pipe): "3 faces in ..."; Vert shows "(some)" if mixed. Shift+click one
   again: dropped.
6. Select a flat floor face patch, tick **Wallride (0x04)** and **Not skatable (0x02)**: the collision view recolours
   them (yellow). Leave the editor and skate onto them: you can't skate there / wallride as on a wall. Back in: **Home**
   puts them back (colour back).
7. On a wall face tick **Not collidable (0x10)**: skate through it. Tick **Camera passes (0x80)** on a wall between the
   camera and the skater: the camera goes through it (doesn't get pushed).
8. **All faces of <piece>** on a bench: every face of it selected (and its shared collision's). Ctrl+Z / Ctrl+Y over
   each flag change.
9. Move that piece (drag; K off first), turn it, scale it: its edited faces keep their flags (the view's colours stay).
10. A piece without its own collision (TR's SkWare_Rail01): move it (its shared collision is taken along), then in Faces
    mode click its moved faces: they're selectable (named after the original object's faces); set Vert, skate: it
    behaves vert where it is now; nothing at the old place.
11. Ctrl+D a piece, click the copy's faces: refused with "a copy ... edit its source's faces".
12. Ctrl+S; the layout has `faces <checksum> n:xxxx,...` lines. Load the EDITED entry: the flags are back (log: "the
    layout's n collision faces ... put in place"). The level as shipped: original flags.
13. Reset level: every face back; Ctrl+Z brings them back.

Clusters
14. Click a scenery piece that's a trick object (a bench in BO with graffiti). Inspector > **Trick object**: "cluster
    <name>". Click a piece that isn't one: "not a trick object". Type the bench's cluster name (or "Use its name" on the
    bench first, then select the other piece) > **Put in this cluster**: clicking either now selects both as one group.
15. In a Graffiti game (2 players offline if possible) or a trick-object goal: tricks on the added piece tag / count for
    that cluster (it colours with the bench). **Not a trick object** on the bench: tricks on it no longer tag it.
16. **Each its own** on a group: each piece its own cluster (groups split). **Home**: back as the level has them.
17. Ctrl+S, reload the EDITED entry: memberships as set (the log lists n trick-object clusters put in place).

Cameras
18. Fly somewhere, press **V**: "Camera ed_camera01 made..."; an orange frustum where the view was, a line to a cross
    1000 units ahead. Inspector foot > **Cameras** lists it with Look through.
19. Select it (Outliner, family Other, or its gizmo): the **Camera** section: Pitch, Target, targetOffset /
    positionOffset. Drag it / the arrows; Z / X turn it (the frustum follows); change Pitch and Target (the cross moves).
20. **Look through**: the editor's view is exactly the frustum's (same place and direction). **Put at the view** after
    flying elsewhere: the camera jumps there.
21. Delete (key or button): gone; Ctrl+Z: back with its pitch and target. Ctrl+S; the layout has a `camera` line; reload
    the EDITED entry: the camera is there; the level as shipped: none.
22. Leave the editor and play: nothing changes (no script uses the node yet).

General
23. Old HUD (`editor_ui=0`): K says it needs the UI; V still makes a camera (no frustum shown there).
24. Bake: `node tools/game/apply_layout.ts --level TR --game C:\Games\THUG2-n --dry-run` with such a layout: report lines
    "collision faces: n faces' flags set", "scenery X: a trick object of cluster Y" (or the shared-structure warning),
    "camera ed_camera01: a GenericNode ...", and `Levels.qb: Level_TR ... sky=BO_Sky`.

## Left / ideas

- Sky live (load the new sky scene and unload the old in the editor) - not attempted.
- A trigger node for faces flagged 0x40 (only the Triggers window makes working triggers).
- Faces of copies (their checksums are new each session); a shared-structure TrickObject taken out in the bake (would
  need the node's include inlined).
- A script command to play a camera node as a virtual_cam (its fields are ready for it).
- Switching off: `[features] level_editor=0` (the whole editor); nothing here is a REPLACE.
