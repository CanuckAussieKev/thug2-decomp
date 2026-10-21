# editor-ux4 (agent record): view modes, overlays menu, palette thumbnails, jump arcs, grind preview, ruler - NOT run in the game yet

NOT ORIGINAL (the level editor is ours). Built and checked offline only; the user tests the interactive behaviour.

## Files
New: `src/game/gfx/view_mode.h/.cpp` (the renderer's side of the view modes), `src/game/front/level_editor_overlays.h/.cpp`
+ `level_editor_overlays_ui.cpp` (View / Overlays menus, the Painter the overlays draw with, grid, carried-object hints),
`level_editor_arcs.h/.cpp` (jump arcs), `level_editor_grind_preview.h/.cpp`, `level_editor_ruler.h/.cpp`,
`level_editor_thumbs.h/.cpp` (the Add palette's grid, Recent, Favourites), `src/hook/selftests_editor_ux4.cpp`.

Small hooks in shared files (all additive):
- `game/gfx/render.cpp` (RenderWorld: `ViewMode::BeginWorld / EndWorld` beside Wireframe's), `render_state.cpp`
  (SetPixelShader / SetPixelShaderWithConstants: the device gets `ViewMode::DeviceShader(shader)`), `scene_render.cpp`
  (RenderScene returns while `ViewMode::WorldHidden()`), `pixel_shaders.cpp` (each assembled shader's source noted:
  `ViewMode::NoteShaderSource`). Each marked "Not original" with the original behaviour.
- `level_editor.cpp`: includes; `Overlays::Start()` beside `View::Start()`; Stop: `Overlays::Stop()`, `Thumbs::Stop()`,
  `Ruler::SetArmed(false)`; the keys (Alt+1..4, Y, M / Shift+M) after `View::Tick()`; the ruler's hover after
  `Faces::Hover` and its click as the first branch of the click chain (after the trigger handle); PlaceFromBrowser:
  `Thumbs::NoteUsed`; Draw: the feature map behind `Overlays::Shown(kFeatureMap)`, labels off clears the texts, then
  the Painter's overlays (grid / carry, arcs, grind preview, ruler) and `Thumbs::Frame` before `Ui::Frame`; the
  screenshot sidecar's `view mode` and `measure` lines.
- `level_editor_ui.cpp`: ViewToolbar's Collision (C) / Faces (K) checkboxes replaced by `Overlays::ToolbarMenus` (which
  keeps Faces (K)); CollisionLegend behind `kLegend`; the Outliner's mark letters behind `kOutlinerMarks` (Changed only
  still filters); F1 appends `Overlays::HelpRows()`; Browser's item list replaced by `Thumbs::ItemsPanel` (grid / list).
- `level_editor_ui_backend.h/.cpp`: the thumbnail atlas (`BeginAtlasCell / EndAtlasCell / AtlasTexture /
  AtlasGeneration / FreeAtlas`; freed with the device objects).
- `level_editor_preview.h/.cpp`: the drawing split out as `Preview::DrawView(kind, id, orbit)` (Render uses it).
- `level_editor_carry.h/.cpp`: `CarriedObjects()` / `CarriedNodes()` (read only).
- `level_editor.h`: the module list's comment. `CMakeLists.txt`: 8 lines.

## What it does
1. **View modes** (toolbar "View: Lit" menu; **Alt+1 Lit, Alt+2 Unlit, Alt+3 Wireframe, Alt+4 Collision only**; back
   to Lit when the editor closes, not remembered):
   - Unlit: every pixel shader multiplies by the vertex colour v0 (the level's baked light, or a model's fixed-function
     lit colour; pixel_shaders.cpp). Each shader's source is kept when assembled and an unlit twin made from it on first
     use (`UnlitShaderSource`: `def c7, 0.5, 0.5, 0.5, 0.5` after the version line, every whole-operand `v0` -> `c7`,
     `v0.a` kept - fades and decals keep their alpha); during the 3D passes only, SetPixelShader hands the device the
     twin (the cache keeps the original, and is reset at BeginWorld / EndWorld so the 2D gets the originals). Without
     pixel shaders (the stand-in texture stages) Unlit draws as Lit.
   - Wireframe: the game's render mode 0x006FA734 set to 1 (game/gfx/wireframe.h draws it), 0 again when the mode goes
     (only if the editor set it).
   - Collision only: RenderScene draws no scene (level and sky) in the 3D passes, the target cleared to dark grey at
     BeginWorld; the collision view is turned on while it lasts (and off again after, if C had it off). Objects, peds
     and the skater are still drawn (their render path is not RenderScene's - not confirmed which way they go).
2. **Overlays menu** (toolbar "Overlays"): checkboxes for Collision (C), Trigger volumes (T), and the bits of
   `[editor] overlays=` (default 0x1B7: all but Grid and Jump arcs): Labels, Feature map, Grind preview, Jump arcs (Y),
   Ruler (M), Grid, Carried objects, Collision legend, Outliner markers; then every gizmo family (the Outliner's chips,
   `[editor] families=`, through the existing kFamilies request) with All / No gizmos. Every old key still works (C, T,
   K, H, I, Alt+H). Faces (K) stays a toolbar checkbox (a click mode, not an overlay).
   - Labels off drops the texts the other overlays drew (gizmo / trigger names, the feature map's numbers); the arcs',
     grind preview's and ruler's readings are added after and stay.
   - Grid: 100-unit lines (every 500 brighter) on the ground below the selection, else below the camera, 1500 round.
   - Carried objects: while scenery moves with Carry objects on, what it carries - object boxes and node rings, green.
3. **Add palette (B)**: Grid / List toggle (`[editor] palette_grid=`, default grid); chips All / Recent / Favourites;
   the filter box searches names in both. Grid cells are 92 px (scaled) pictures drawn by the preview's own render
   (`Preview::DrawView`, the default angle) into a 2048-px atlas of 128-px cells (256 kept; the least recently shown
   given up first, never one on screen), 3 a frame, only the cells the grid shows (one frame late). Star on a cell's
   corner / a row's end: Favourites. Recent: the last 20 placed (Enter, Place, double click), newest first. Both per
   level in `[editor_palette] <scene>_recent= / <scene>_favourites=` (`s:<checksum>` / `o:<id>` hex, comma separated).
   Clicking a cell highlights it (the big preview above shows it), double click places; the keys (Up / Down, Enter, F)
   work as before.
4. **Jump arcs (Y)**: with scenery selected, its collision's lips (`FindLips`: skatable sloped faces grouped by uphill
   heading; the piece's winding convention from its sloped area so a kicker's back wall isn't a vert lip; near-vertical
   faces join the group they face; the lip at the group's top vertices, its tangent the top faces'; vert when the top is
   flagged 0x08 or steeper than 0.97; at most 4 lips, rising at least 6). Each lip: a white edge, its tangent, "lip H
   high, A deg"; three flights at push / top / max speed (green / yellow / red) with a full ollie, frame by frame (1/60
   s, 6 s) as DoAirPhysics integrates, each frame's segment cast with the game's collision ray (CFeeler 0x00417760 /
   0x00417710); the landing crossed and labelled "top: 812 (67.7 ft), 40 lower [ - hits a wall][ on <piece>]". A vert
   lip shows how high each speed rises instead (v^2 / 2|g|). Made again when the selection or its boxes change (at most
   4 times a second while it moves).
   **Physics constants used** (read from the game's script structures each time the selection changes - skater_physics
   or the ridden vehicle's via `Obj::GetPhysicsStructure` / `GetPhysicsFloat`; the pairs scaled by the skater's own
   stats, `CSkater::GetStat` 3 speed / 2 ollie, `Skater_Default_Stats` 5 without a skater):
   - gravity: `Physics_Air_Gravity` (-1350) / `Physics_Air_hang_Stat` (1.0) = -1350 off kickers; / `Physics_Vert_hang_Stat`
     (1.1) = -1227 for vert lips - as 0x004FF470 (the air gravity getter, read in Ghidra) picks them; its moon cheat
     (`Moon_gravity` 0.5) and an unnamed falling factor (0xBE33A41E) left out; air friction (1e-5) left out.
   - ollie: `Physics_Jump_Speed_Stat` (414 - 450 by the ollie stat; 432 at 5) - 0x00509450 (the jump) clamps a falling
     velocity.y to 0 and adds the jump speed to velocity.y (the full charge; a tap gives `Physics_Jump_Speed_min_Stat`
     350 - not drawn).
   - speeds: push `Skater_Max_Crouched_Kick_Speed_Stat` (532 - 675, limit 900; 603.5 at 5), top `Skater_Max_Speed_Stat`
     (757 - 900; 828.5), max `Skater_Max_Max_Speed_Stat` (957 - 1100; 1028.5) - scaled as GetScaledValue 0x005805B0
     (low + (high - low) stat / 10, its limit=), without its switch / difficulty factors.
   The launch velocity is speed x the lip's tangent (up the surface), then the ollie. No skater physics decompiled for
   this (the air step was already ours: core_physics.cpp; the gravity getter and the jump were read, not rewritten).
5. **Grind preview** (hover a rail gizmo; else the selected rail point): the whole rail from the rail manager (the
   points where skaters grind them, a point to its m_next): segments thick white (grey when the rail is off), chevrons
   in the link direction, points (the hovered one yellow), ends ringed, branches ringed orange "branch", speed-ups
   (Chains::SpeedSegments on its links: the level's RailAccelerate and the editor's) green "speed-up 5.0% [(level)]",
   other rails' ends within 120 units of its ends dashed magenta "transfer 34"; a label "<name>: 12 points, 11 segments,
   840 long, TERRAIN_GRINDMETAL[, off][, speed-ups]".
6. **Ruler (M)**: armed, the next clicks put its ends on collision (the collision ray through the cursor), snapped to
   the nearest collision corner of the piece hit within 12 px (Alt: no snap; a ring shows a snapped end); a third click
   starts again; drawn: the line with its distance, the horizontal leg ("across") and the vertical leg ("up / down"),
   units and feet. **Shift+M** clears; M again disarms (the measurement stays). While armed, clicks measure, not select.

Keys bound (all documented in F1's new rows): **Alt+1 / Alt+2 / Alt+3 / Alt+4** (view modes), **Y** (jump arcs),
**M** (ruler), **Shift+M** (clear the ruler). In the palette: the stars, Grid / List, All / Recent / Favourites (mouse).

## Checks (offline)
- Built (own build dir out\build\ux4, own PDB endpoint); `thug2_check.exe` on THUG2PM.exe and on the stock
  C:\Games\THUG2\Game\THUG2.exe (read only): 0 MISMATCHED, all early self-tests pass (446, five of them new:
  TestEditorUnlitShaders, TestEditorJumpLips, TestEditorJumpPhysics, TestEditorOverlaysRulerGrind,
  TestEditorPaletteThumbs).
- `node tools/check_code_writes.ts`: No overlaps. No new REPLACE (nothing of the game's replaced).
- Mutations (`node tools/mutate.ts`, a spec of 18 in the worktree's out\scripts\mut_ux4.ts): all caught - the unlit twin
  (alpha replaced too, the def before the version), lips (no winding convention, the back wall joining, no minimum
  height, maxLips ignored), the launch's clamp, the flight's half-gravity step and its skip, a stat's limit, the ruler
  (horizontal from y, the farthest corner), the grind walk (one way only, wrong ends), the grid's majors, the cells
  (evicting one on screen), Recent's and the ini list's duplicates.

## In-game checklist (NOT run in the game yet)
1. `-edit TR`. Toolbar: "View: Lit" and "Overlays" buttons where "Collision (C)" was; "Faces (K)" still there.
2. Alt+2 (or View > Unlit): the level's textures at full brightness, no baked shadows / light; decals, fences and fades
   keep their transparency; the HUD / editor panels unchanged. Alt+1: lit again. Look at a car / ped: unlit too.
3. Alt+3: wireframe (the game's render mode). Alt+1: solid again. Debug menu's Wireframe Toggle while not in the editor
   still works as before.
4. Alt+4: the level and sky gone, dark grey background, the collision view shown (coloured faces, legend); objects
   still drawn. Alt+1: the level back; if C was off before, the collision view is off again (and `[editor]
   collision=` as it was).
5. Leave the editor in Unlit / Wireframe / Collision only: the game draws normally (Lit). Re-enter: View: Lit.
6. Overlays menu: switch each checkbox - Collision (same as C), Trigger volumes (same as T), Labels (gizmo / trigger
   names vanish), Feature map (speed-up chevrons, boosts vanish), Collision legend, Outliner markers (the letters at the
   Outliner rows' ends vanish; Changed only still lists the changed rows), each gizmo family (as the Outliner's chips -
   the chips follow), All gizmos / No gizmos. Restart the game: the choices are kept (`[editor] overlays=`,
   `families=`, `collision=`).
7. Overlays > Grid: a grid on the ground below the camera, following it; select something: under the selection.
8. Overlays > Carried objects: move a piece with objects on it (Carry objects on): green boxes / rings on what moves
   with it while dragging.
9. B: the Add palette in Grid mode - pictures fill in a few a frame; scroll: new ones appear, old ones stay. The
   preview above follows the hovered cell. Click a cell: highlighted (orange border); double click: placed. Up / Down,
   Enter, F still work and the highlighted cell scrolls into view.
10. List: the old list with a star at each row's end. Filter: both modes search the names.
11. Star a few entries (cell corner / row end): Favourites lists them; un-star: gone. Place a few: Recent lists them,
    newest first, at most 20. Restart / reload the level: Recent and Favourites are kept for that level only (another
    level has its own).
12. Alt-tab out and back with the palette open in Grid: the pictures come back (drawn again); no crash.
13. Select a kicker (TR's or a THPS level's), press Y: a white lip edge, "lip H high, A deg", three arcs (green, yellow,
    red) and landing crosses with distances; a long gap shows which speed clears it ("on <piece>" when it lands on
    another piece). Move the kicker: the arcs follow (a few times a second). Y again: gone.
14. Select a quarter pipe: "(vert)" lip, three vertical bars with "rises N" - compare with the skater's real air height
    off it at full speed (Skate: hit the QP straight at top speed with an ollie).
15. Compare one kicker's top-speed arc with play: skate off it at top speed with a full ollie - the landing should be
    near the yellow cross (the skater's switch / difficulty factors and the hang stat's moon cheat aren't modelled).
16. Hover a rail's gizmo: the whole rail highlighted - chevrons show its direction, its ends ringed, the label with
    points / segments / length / terrain; a rail with a speed-up (a THAW port, or one given in the Inspector) shows it
    green; rails ending near its ends show "transfer N". Select a rail point: the same while not hovering.
17. M: the ruler. Click two points on the ground / a ledge: the line, its distance, across and up/down legs in units
    and feet; near a corner the end snaps (ring), Alt+click doesn't snap. Third click: a new measurement. M: the tool
    off, the measurement stays; Shift+M: cleared. While armed, clicks don't select.
18. F1: the new rows (View modes, Jump arcs, Grind preview, Ruler, Add palette) at the end.
19. Screenshot (Print Screen) in the editor: the sidecar has `view mode` (and `measure` lines with arcs / ruler / a rail
    shown).

If something is wrong:
- Unlit looks lit: the log says once "view mode: an unlit twin of a pixel shader couldn't be made" with the text (an
  assembler refusal) - or the device has no pixel shaders.
- Collision only shows a smeared old frame: the clear at BeginWorld missed the target the world draws into (note the
  level and settings: screen blur / bloom on?).
- Thumbnails stay "...": the log says "no thumbnail atlas of 2048 pixels (HRESULT)" (the card refused the size).

## Left / ideas
- Unlit without pixel shaders (the stand-in texture stages); objects in Collision only (hiding them needs their render
  path read).
- The arcs: the tap ollie (min jump), no-ollie rolls off the lip, switch / difficulty factors, the moon cheat; lips of
  pieces whose ramp is in another sector's collision (shared collision) aren't found from the selection.
- The ruler snaps to the hit piece's corners only (not edges' midpoints, not other pieces').
- The grind preview reads the rail manager's links (one next per point): a level's branching links collapse to the
  rail manager's choice.
- The palette's atlas is kept while the editor runs (about 32 MB of video memory with its depth surface).
