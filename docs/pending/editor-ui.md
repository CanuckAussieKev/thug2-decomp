# Level editor UI: phases 0 and 1 (Dear ImGui, fonts, the node layer)

Branch `worktree-agent-a61260ddefe8e2d3b`. The plan is the artifact "Level editor entity map"
(https://claude.ai/artifact/B5EP6Qt7hqJ9RYvnZhkTHt). **NOT run in the game yet**: the user asked for no game runs
(checked offline only - see the checklist at the end for the in-game test).

## What

- **Phase 0: the UI layer and fonts.** The level editor's HUD (screen elements made by overlay scripts: hard to read,
  90 characters a line) is replaced by Dear ImGui panels drawn on the game's Direct3D 9 device, with SDL2 input:
  - a toolbar (mode indicator Fly / Select / Move / Add, Snap 25, Ground (G), the level, Undo, Redo, Save layout,
    Help (F1), Add (B));
  - the Outliner on the left and the Inspector on the right, fixed panels (the docking branch isn't needed: the
    layout is our own maths, `level_editor_ui_layout.cpp`);
  - the viewport in the middle (the game's view: panels lie over its edges);
  - a status bar (the message line, else the keys for what you're doing as key caps; changes, camera, speed);
  - the help (F1) as a table of every key, the piece browser (B) with tabs, filter, Place / Show me buttons, and the
    autosave offer (Restore / Discard / Later) as windows.
  - Every old key still works (the keyboard code is unchanged; the UI's buttons ask the editor to do the same
    things: `Ui::Request`).
  - The old HUD's elements are taken down (`level_editor_hud_destroy`) when the UI runs; the overlay scripts are
    unchanged (no `make_menu_level.ts` re-run needed).
- **Phase 1: see everything.** `level_editor_nodes.cpp` reads every node of the node manager's arrays
  (`SkateScript::NodeManagerInstance()->m_arrays`, the decompiled CNodeManager - no raw offsets), classifies them into
  13 families by class / type (Spawns, Crowns, Flags, Rails, Ladders, Paths, Objects, Scenery, Lights, Sounds,
  Particles, Triggers, Other), and keeps what the gizmos need. Rescanned when the arrays' names / sizes change.
  - Outliner: search, family chips (also hide the family's gizmos; remembered in `[editor] families=` of
    thug2-decomp.ini, a bit mask), a tree per family (ImGui list clipper - Foundry-size rail lists stay fast); click
    selects, double-click flies the camera there; a selection made in the view opens and scrolls the tree to it.
  - Inspector (read-only): name, class / type, node index and array, position, angles, heading, spawn types (badges)
    and order ("n of N restarts"), crown index ("#n of N - the order the network sends"), light colour and radii,
    terrain, links (clickable), and every field of the structure with the `ncomp_<class>_<n>` includes expanded
    (greyed). Names resolve through `Objects::NameOf` (KeptQbName, then the game's checksum names).
  - Online badge on spawns, crowns and flags when the level loaded is in one of the ported lists
    (`level_select_menu_<thps3|thps4|thug1|thaw>_level_info`, the load script being the skate module's +0x378) -
    i.e. shared with THUGPRO.
  - Gizmos (ImGui background draw list, projected with the editor's own `Project`): spawns as discs in their type's
    colour (team colour for CTF / team restarts, by `_Red/_Blue/_Green/_Yellow` in the name) with a heading arrow (the
    game's own angles-to-matrix, 0x0041A730, as the skater's restart uses it); crowns as gold diamonds with `#index`;
    flags as pennants in their team colour; lights as bulbs in their `Color` with inner / outer radius rings when
    hovered / selected; sounds (EmitterObject) as speakers; particles as sparks; triggers (ProximNode) as squares
    with a radius sphere (three rings) when near; objects as small open diamonds; rails as polylines coloured by
    type (metal / concrete / wood), ladders dashed, paths with direction arrowheads. Only what's in front of the
    camera and on screen, within 6000 units (points), 4000 (chains), 2500 (objects, particles) unless selected or
    hovered.
  - Picking by screen distance (8 px at 720 lines), points before segments, ties to the nearer; before the existing
    collision-ray picking of objects and scenery (which is unchanged). An object's / flag's node selects the object
    (drag works as before), a movable scenery node the scenery; anything else becomes a node selection (read-only:
    Esc / Backspace deselect; no arrows).
- **Setting:** `[features] editor_ui=0` falls back to the old HUD. Keeping both was cheap: the HUD is a few script
  calls the editor already made; the UI also falls back to it when ImGui can't start on the window or device.

## Dependencies and versions

| What | Version | How | Licence |
|---|---|---|---|
| Dear ImGui (core + `imgui_impl_sdl2` + `imgui_impl_dx9`) | 1.91.9b (`master`, not docking) | FetchContent of the tag's tarball, SHA-256 `8e1bbc76...54ffdb` | MIT |
| Inter (Regular, SemiBold) | 4.1 | FetchContent of `Inter-4.1.zip` (33 MB, once), SHA-256 `9883fdd4...756b11e`; `extras/ttf` | SIL OFL 1.1 |
| JetBrains Mono (Regular) | 2.304 | FetchContent of `JetBrainsMono-2.304.zip`, SHA-256 `6f6376c6...bf` | SIL OFL 1.1 |

The fonts are compressed into the DLL at build time by ImGui's own `misc/fonts/binary_to_compressed_c.cpp` (built as
a host tool, run by `cmake/compress_font.cmake`, output in `<build>/editor_fonts/*.inc`); nothing goes into the game
folder, nothing is committed. Credits in THIRD_PARTY_NOTICES.md (MIT text, OFL text). Font sizes: Inter 14 / 12,
JetBrains Mono 13, Inter SemiBold 15 at 1280x720; the scale is the smaller of width/1280 and height/720 in quarter
steps, 1 to 3 (4K = 3); the atlas is rebuilt when the window's scale changes. `io.IniFilename = nullptr` (no imgui.ini).

## Renderer-independence rules (the user's requirement)

- `src/game/front/level_editor_ui_backend.cpp` is the **only** file that includes `<d3d9.h>` or an ImGui backend
  header (`imgui_impl_dx9.h`, `imgui_impl_sdl2.h`). Its header has no Direct3D type.
- Every gizmo and line of the editor, with the UI running, goes through ImGui's background draw list
  (`Ui::Overlay`: lines, triangles, circles, texts in back-buffer pixels, scaled to the window). The old device path
  in `LevelEditor::Draw` stays only for the `editor_ui=0` fallback.
- A texture given to ImGui goes through `UiBackend::TextureId` (none yet; thumbnails would).
- ImGui's coordinates are window pixels (SDL's mouse events); `UiBackend::Render` scales the draw data to the back
  buffer when the two differ. The DX9 backend saves and restores the whole device state (a state block), so the
  renderer's cached state stays true.
- Swapping to OpenGL / Vulkan later: replace `level_editor_ui_backend.cpp` (and the imgui target's backend file).

## How (where things hook in)

- `LevelEditor_Start` -> `Ui::Start` (ImGui context once; `ImGui_ImplSDL2_InitForD3D(Sys::SdlWindow())`,
  `ImGui_ImplDX9_Init(device 0x007D69E0)`; controllers off), `LevelEditor_Stop` -> `Ui::Stop`.
- Events: the editor's SDL handler gives each event to `Ui::RouteEvent` first (`level_editor_ui_layout.h` Decide):
  a press over a panel / while ImGui wants the mouse is the UI's (its release too); a press in the viewport is the
  editor's (ImGui also sees it, to drop focus); a drag or right-button look started in the viewport keeps the mouse
  over panels; wheel over a panel is the UI's; keys go to both, but while ImGui wants the keyboard (a text field) the
  editor's keys are zeroed and Escape / the Pause key (Enter, `PassPauseKey`) don't reach it.
- Drawing: `Nx::RenderWorld` -> `LevelEditor::Draw` (after the 2D pass) -> `Ui::Frame`. Nothing runs while the editor
  is closed (only `Mlp::ResetDeviceIfLost` calls `Ui::DeviceLost`, which does nothing unless the backend started).
- Device lost / reset: `Mlp::ResetDeviceIfLost` -> `Ui::DeviceLost` -> `ImGui_ImplDX9_InvalidateDeviceObjects`
  before the game releases its own; `ImGui_ImplDX9_NewFrame` makes them again on the next frame.
- New small shared additions: `Sys::SdlWindow()` (sdl_window.h), `ConfigSetInt` (hook/log.h), the mlp.cpp call.

## Checks (offline)

- Clean build (`out/build/verify`, own PDB server), no warnings in our files.
- `thug2_check.exe` on THUG2PM.exe and on the stock `C:\Games\THUG2\Game\THUG2.exe`: all 345 early tests pass, with
  five new ones: `TestEditorUiLayout` (scale, font sizes, layout at 720p / 4K / narrow, panels tile the window,
  OnPanel), `TestEditorUiRouting` (every input combination against the rules), `TestEditorUiFonts` (the three fonts
  decompress and bake in a headless ImGui context, mono advances equal, Inter proportional, a frame draws),
  `TestEditorNodeFamilies` (22 real class / type pairs from THUG2 and port levels, shapes, bits, team / spawn colours),
  `TestEditorNodePicking` (points, segments, priority, ties, a projected node).
- Mutations caught: routing keys always to the editor, flags classified as objects, points no longer beating
  segments (and a scale-rounding case added after a mutation slipped through).
- `node tools/check_code_writes.ts`: no overlaps. No tools touched (no typecheck / lint needed).

## In-game test checklist (for the user)

Use a workspace or the main install with this branch's DLL. Watch `thug2-decomp.log` throughout (see the end).

1. **Start in a THUG2 level:** `node tools/game/edit_level.ts TR` (or `-edit TR`). Expect the log line
   `level editor UI: Dear ImGui 1.91.9b on SDL2 and Direct3D 9` and `level editor UI: scale 1.00 for a 1280x720
   window ...` (your size). The old HUD's grey text and dark bars must not appear.
2. **Panels and fonts:** toolbar at the top, Outliner left, Inspector right, status bar at the bottom; text crisp
   (Inter for labels, JetBrains Mono for numbers / names). Try 1280x720 and full screen at your desktop size (1440p /
   4K: everything larger, still crisp).
3. **Outliner:** each family with a count; type in the search box (W A S D must NOT fly the camera while typing; Enter
   must NOT leave the editor); click chips to hide / show families (gizmos follow), leave and re-enter the editor:
   the chips are remembered (`[editor] families=` in the ini). Click a Spawn row: the Inspector shows it;
   double-click: the camera flies there.
4. **Gizmos:** spawns (discs with heading arrows; the arrow should point the way the skater faces when F8's restart
   tester puts him there), crowns `#0..`, CTF flags in their colours (Airport / Barcelona), lights in their colour
   (rings when hovered), rails as lines by terrain, paths with arrows, ladders dashed. Nothing drawn behind the
   camera; far ones vanish beyond the cut-offs.
5. **Picking:** click a spawn gizmo in the view - the Outliner opens and scrolls to it, the Inspector shows its types
   and "n of N restarts". Click a flag / object gizmo - the object itself is selected and can be dragged. Esc and
   Backspace deselect.
6. **Inspector on a selected spawn:** position, angles, heading, types badges, All fields (greyed rows from the
   `ncomp_` include).
7. **A ported level:** `node tools/game/edit_level.ts foun` (or z_ma). A spawn / crown / flag shows the blue
   **Online** box naming the source game; a THUG2 level's spawns don't.
8. **Every old key:** fly (W A S D, Q / E, Shift, right mouse look, wheel, - / =), click-select objects and scenery
   (Shift / Alt / middle click), drag (Shift, wheel, Ctrl snap), the move arrows, nudge, Z / X, G, Ctrl+D, Delete,
   Home, Ctrl+Z / Ctrl+Y, B browser (Up / Down / Left / Right / F / Enter; also its buttons and a double-click), F1
   help, P (camera to the log), Ctrl+S; the toolbar's Snap 25 makes a drag snap without Ctrl; its Undo / Redo / Save
   buttons do as the keys. Escape order: drag put back, window closed, deselect, then leave. Leaving puts the skater
   below the camera as before; the pause menu comes back.
9. **Autosave prompt:** make a change, wait a minute (or leave), quit without saving, start the editor in the same
   level again: the "Unsaved changes from a previous session" window offers Restore / Discard / Later; Ctrl+R /
   Ctrl+Shift+R still work after Later.
10. **Device reset:** alt-tab out of full screen and back while the editor is open - the panels come back (no black
    or garbage text). Change the window size if you can (windowed): fonts re-size, the mouse still hits the panels.
11. **Fallback:** `[features] editor_ui=0`: the old HUD exactly as before, no panels or gizmos.
12. **The log:** look for `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT` (an ImGui assert would show here with a call
    stack), `SELFTEST FAIL`; and `level editor UI: not started - the old HUD instead` (the UI couldn't start - say
    what came before it).

## Known issues / limits

- Not run in the game at all (by request). The most likely trouble spots: the mouse mapping if the window and back
  buffer differ (handled by scaling, unverified), ImGui's SDL2 backend changing the cursor shape while the editor is
  open, and text input (SDL starts text input by default; if typing in the search box does nothing, that's why).
- Panels lie over the game's full-window view (the 3D view isn't narrowed to the viewport rectangle); picking under a
  panel is the UI's.
- Proximity nodes with `Shape=BoundingBox` (THUG2's) draw only their square: their box comes from their mesh - later
  (as the plan allowed). Trigger faces aren't drawn.
- Copies the editor made (layout `copy` records) aren't nodes, so they aren't in the Outliner (they're still pickable
  in the view as objects).
- The Outliner lists every node array loaded (the level's and any others the node manager holds), in their order.
- The modes in the toolbar are an indicator (they follow what you do), not tools to pick.

## Phase 2: move and turn point nodes

Branch `worktree-agent-a6b7d50ff850094b3`. **NOT run in the game yet**: checked offline only, as the lead asked.

### What

- **Which nodes:** a node selected by itself (its gizmo, or its Outliner row) moves and turns when it's a point node:
  spawns / restarts, crowns, CTF and team flags, lights, sound emitters, proximity nodes (triggers), and "Other"
  (cameras, generic nodes, unknown classes). Rails, ladders and paths (chains: phase 4), particles, objects and scenery
  don't: objects and scenery keep moving through their own layers, as before. `NodeEdit::Movable` decides.
- **Moving it:** the selected node joins the editor's selection, so everything that moves objects moves it:
  - hold and drag (on the ground plane; Shift: up / down; the wheel pushes / pulls); clicking a node's gizmo selects it
    and starts the drag, as clicking an object does;
  - the move arrows (one axis);
  - W A S D / Q E nudge;
  - Ctrl or the toolbar's Snap 25 snaps a drag to 25 units;
  - G drops it onto the ground (the ray from 50 units above its place, as for objects; the ground marker shows where);
  - Z / X turn it 15 degrees (its Y angle);
  - Escape puts a drag back;
  - Home (key or the Inspector's button) puts it back where the level has it.
  The camera orbits it as it orbits a selected object, so W A S D nudge it rather than fly; Esc / Backspace deselect.
  Ctrl+D and Delete say that nodes can't be copied or deleted yet.
- **A flag** is moved as its node. A click on its gizmo, or on its flag object when a CTF / team game has made one,
  selects the node; the object moves with it. Before, the object was moved as an object, and the place the game makes
  the flag at stayed put.
- **The Inspector:**
  - a movable node gets editable fields: Position (`DragFloat3`) and Heading (the Y angle in degrees). Drag to scrub,
    Ctrl+click or double-click to type. One undo step a field, from the first change until it's let go
    (`kNodeSet` / `kNodeSetDone`);
  - Angles stay read-only text;
  - "Shows in the game: ..." says when the change takes effect (`NodeEdit::WhenApplied`);
  - an orange line shows when it's away from the level's place, with Ground (G), Home and Go to buttons;
  - a crown's order is shown ("#n of N ... moving it keeps it"). Moving never changes it: the order is the node
    order, which doesn't change.
- **Undo / redo:** a new step kind `kNodes` (each node's pos / angles before) in the existing 64-step stack covers
  moves, turns, drops, field edits and Home. A drag or a field edit that changed nothing makes no step: position or
  angles are compared (`NodeEdit::Differs`), not only the position.
- **The online warning:** the first time in a game run that a spawn, crown or flag of a level shared with THUGPRO is
  moved or turned, a window "Online: a level shared with THUGPRO" explains the parity break (layouts aren't used in net
  games, a baked layout would be). The edit is made; "Understood" closes the window. The old HUD gets the same as a
  message. The Online box in the Inspector no longer says "read-only".

### What applies live, what needs a reload

Every edit writes the node's own `pos=` / `angles=` vector components in place (the last of each name in the node's
own list - what `CStruct::Search` reads; one is added if the node had none). Then whatever the game keeps of the node
is brought up to date, and the node layer is told (`Nodes::Reread`: its gizmo follows at once).

| Node | Read by the game | Live? |
|---|---|---|
| Restart (spawn, team / CTF / horse restarts) | each time: `Obj::GetRestartNodes` 0x0046B6C0 walks the nodes, the skate module's lookup 0x00566100, `CSkater::MoveToNode` 0x005815C0 reads pos / angles; the restart tester walks them too | **next respawn / F8** |
| Crown (GenericNode Type=Crown) | `CFunc_SpawnCrown` 0x00538170 walks the nodes each spawn, takes the n-th Crown's pos and makes the crown from the node (0x00578490) | **next crown spawned**; one already out stays |
| CTF / team flag (GameObject Flag_*) | the object is made from the node when a CTF / team game starts | node now; **its object moved and turned too** if it exists (`Objects::Restore` with the matrix from the angles, 0x0041A730) |
| Proximity node (ProximNode) | `Proxim_AddNode` 0x0046B1F0 copies pos into `ProximNode::m_pos` at the level's load | **live**: `m_pos` updated; a sphere (Radius) follows. A BoundingBox / sector shape is the level sector's box - the node moves, its area doesn't |
| Sound emitter (EmitterObject) | `Emitter_AddNode` 0x00468450 copies pos into `EmitterNode::m_pos` | **live**: `m_pos` updated (sphere); a box shape is the sector's |
| Light (LevelLight) | `0x00490570` (from `CFunc_ParseNodeArray`'s second loop, and the cutscenes') puts it in two lists by name: dynamic lights (pointers at 0x006FAAF0, count 0x006FAEF0, place +0x04, name +0x28) and level lights (0x24-byte entries at 0x006FAF80, count 0x006FAEF4, max 150, place +0x00, name +0x18) | **places updated live**. A level light also keeps a lazily built list of what it lights (entry +0x20, reset by 0x0048FC00); anything already lit into the scene's colours follows only **after reloading the level** |
| Camera, GenericNode, others | read by scripts when used | when next read |

Records apply on level load. That happens a second after the skater exists (`Layout::Tick`, as before), so **the
level's first spawn is at the level's own place**; later respawns use the moved one. They never apply in net games.

### Saved: layout format version 2

`thug2-decomp layout 2` on the first line. A new record:

```
set 8E1A2B3C pos=-1842,128.5,3306.125 angles=0,1.57079637,0  # TRG_MP_Restart04
```

- `set <node checksum> key=value...`: `pos=x,y,z` and `angles=x,y,z` (radians); either may be left out. Other keys
  are skipped, for later versions (`types=`, `order=`...). A malformed or repeated pos / angles refuses the file, as a
  bad line of any record does.
- Written for each node the editor changed this level that isn't back at the level's place (`NodeEdit::Changed`).
  Applied by `Layout::Apply` after the copies and objects: a node the level doesn't have is skipped. The message and
  log count them ("..., N nodes").
- The autosave and the crash save are the same text (`Snapshot` -> `Capture`), so they include them. Restoring an
  autosave puts every changed node back first (`NodeEdit::ResetAll` in `ResetToLevel`).
- A new level forgets the node changes (`NodeEdit::ForgetLevel`, from `Layout::OnSceneLoaded`). If the same
  structures are still loaded under those names, the level's places are written back first, so a node array that
  stays loaded doesn't keep stale edits as its "originals".
- **Reading:** the header is `thug2-decomp layout <n>`, n >= 1. Version 1 files load as before, and so do later
  versions (unknown records skipped). **The phase 1 reader (and every older DLL) accepted only the exact line
  `thug2-decomp layout 1`**, so an older DLL refuses a version 2 file as a whole ("Couldn't read ..."). It doesn't
  half-apply it. Its unknown-record skipping would have handled `set`, but the header check comes first. Going back to
  an older DLL means losing its layouts (they stay on disk).

### Checks (offline)

- Clean build (`out/build/verify`, own `_MSPDBSRV_ENDPOINT_`), no warnings.
- `thug2_check.exe` on THUG2PM.exe and the stock THUG2.exe: all 349 early tests pass, 4 new:
  - `TestEditorNodeAngles`: wrap, Z / X steps, a node turns the way objects turn (TurnAboutUp), heading in / out;
  - `TestEditorNodeSnapDrop`: `SnapToGrid`, `GroundRayStart`, `DroppedTo` (the editor's own drag snap and G use them
    now);
  - `TestEditorNodeUndo`: capture / put / differs as the editor's history uses them (move, turn, a no-op click, undo,
    redo, Home as a step, other nodes untouched), and which families are movable;
  - `TestEditorLayoutV2`: set records round trip with exact floats (pos or angles alone), other fields skipped,
    malformed ones refused, a v1 file still loads, a v3 header reads, bad headers refused.
- Mutations (node tools/mutate.ts), all 15 caught: turn direction flipped, a typed heading not wrapped, Differs
  inverted, Same ignoring the heading, Capture keeping missing nodes, Put writing nothing, rails movable, the half-turn
  wrap boundary, angles not written, an unknown field read as angles, only version 1 accepted, a repeated pos
  accepted, version 1 written, snapping by floor, G dropping the position (not the base) to the ground.
- `node tools/check_code_writes.ts`: no overlaps.

### In-game test checklist (for the user)

Use a workspace (or the main install) with this branch's DLL. Watch `thug2-decomp.log` (`level editor:` lines, and
`EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`, `SELFTEST FAIL`).

1. **A spawn, live:** `node tools/game/edit_level.ts TR`.
   - Click a spawn disc. It turns white, arrows appear, the Inspector shows editable Position / Heading and "Shows in
     the game: At the next respawn here, or F8".
   - Drag it (ground plane), Shift+drag (up / down), drag an arrow, nudge with W A S D / Q E, Ctrl+drag (snaps to 25)
     and the same with Snap 25 on. G drops it to the ground (the marker shows where). Z / X turn its heading arrow
     15 degrees a press. Escape during a drag puts it back.
   - Leave the editor (Esc until it closes). Press F8 until the log's `restart tester:` line names that restart: the
     skater stands at the new place, facing the new heading.
2. **The Inspector fields:** scrub Position x by dragging: the gizmo follows. Ctrl+click Heading, type 90, Enter: the
   arrow points along +x (east). Ctrl+Z undoes each field edit in one step; Ctrl+Y redoes it. Home (key or button)
   puts it back where the level has it; Ctrl+Z undoes the Home.
3. **A crown:** in a level with crowns (most THUG2 levels; Foundry), move a crown diamond. Its `#n` stays, and the
   Inspector shows the same order. King of the Hill is a net-game mode, and layouts aren't used there (the level
   reloads anyway), so the live crown spawn can't be checked offline. Check that the diamond moves, and that it's
   there after save + reload.
4. **A flag:** Airport or Barcelona: click a CTF flag pennant and move it. In free skate there's no flag object, so
   only the node moves. In a CTF game (net only) the object would follow.
5. **A light:** a level with LevelLight bulbs (a THUG1 port, or a THUG2 level that has them): move one near the
   skater. The bulb and its rings move; any effect on the skater's lighting is a bonus. No crash, no log errors. The
   level's baked lighting won't change until the level reloads, and then it reloads at the new place.
6. **A trigger / sound:** move a ProximNode square with a radius (its rings follow). Skate into its new place: its
   script runs there, not at the old place. A box-shaped one (no rings) keeps its old area: expected.
7. **Saving:** Ctrl+S. The message ends "..., N nodes". Open `thug2-decomp-layouts\<scene>.txt`: the first line is
   `thug2-decomp layout 2`, and there are `set <hex> pos=... angles=...  # <name>` lines. Quit to the menu, load the
   level again: after about a second the log says `Layout loaded: ..., N nodes`. The gizmos are at the moved places,
   and F8 reaches the moved restart there.
8. **Autosave / crash restore:** move a node, wait over a minute (log `autosaved`), end the game without saving (Task
   Manager). Start the editor in the same level again: the "Unsaved changes" window offers Restore. Restore puts the
   node back at the moved place.
9. **Online warning:** `node tools/game/edit_level.ts foun` (or z_ma). Move a spawn (or a crown / flag): the blue
   "Online: a level shared with THUGPRO" window appears once. "Understood" closes it, and later moves in the same run
   don't bring it back. Moving a light there doesn't warn; a THUG2 level's spawns don't warn.
10. **A version 1 layout:** a level with a layout saved by the phase 0 / 1 DLL (or edit one so its first line reads
    `thug2-decomp layout 1`) still loads its sectors / objects. After a Ctrl+S it's written as version 2.
11. **Unchanged:** objects and scenery drag, turn and save as before. Rails, ladders, paths and particles selected by
    their gizmo show no arrows and don't move (W A S D fly).

### Known issues / limits (phase 2)

- Nothing run in the game.
- The first spawn of a level uses the level's own place (the layout applies a second after the skater exists).
- Light edits: the dynamic and level light lists are updated; the lit scene colours and a light's cached list of what
  it lights (0x0048FC00's) aren't recomputed until the level reloads. Not re-adding the light was deliberate: the
  remove / add pair (0x0048FCA0 / 0x00490570, which the cutscenes use) also runs 0x0048FC00. Its effect on vertex
  colours wasn't decompiled, so it could light things twice.
- Box-shaped proximity nodes and sound emitters take their area from the level sector of their name: moving the node
  doesn't move that area (the Inspector says so).
- Particles aren't movable yet (ParticleObject: a composite object made from the node; it would want the object
  moved, as flags).
- A node's name appearing in two loaded node arrays: the one the node manager's hash finds is the one edited (the one
  the game reads).
- The camera orbits a selected node (as objects): W A S D nudge it. Phase 1's free flying with a node selected stays
  only for read-only nodes.

## Notes for phase 3 (creating and deleting nodes)

- **Crowns: at most 10.** `CFunc_SpawnCrown` collects the Crown nodes into two 10-entry stack arrays without a bound
  check. A level with more than 10 Crown nodes overflows its stack, so creating crowns must stop at 10 (count the
  level's own first). The crown sent is `net+0x68 % count`, in node-walk order (array order, then index): an overlay
  array added after the level's keeps the level's crowns' numbers.
- **Restarts: at most 256** per lookup (`GetRestartNodes` writes into a 256-name stack array, also unchecked).
- **Lights:** a new LevelLight needs `0x00490570(node)` (the game's own path, as the cutscenes use it); deleting one
  needs `0x0048FCA0(name)`. Capacity: 256 dynamic lights, 150 level lights (0x96, checked by 0x00490400).
- **Proximity nodes / emitters:** new ones need `Proxim_AddNode` / `Emitter_AddNode` (ParseNodeArray makes them at
  load). "Hiding" an original fits the game's own kill path (0x005A32C0 by class: `Proxim_SetActive(name, 0)`,
  `Emitter_SetActive(name, 0)`, `0x0048FCA0` for lights, objects killed).
- **The node manager** takes up to 20 node arrays (`AddNodeArray`, hashed by name, links resolved within an array).
  ParseNodeArray works on the last array added. An overlay array is the natural home for new nodes, as the plan says.
  Names must be unique; set records key by name and edit the node the hash finds.
- Set records already cover any node by name, so an overlay node's place can reuse them; `node` records would carry
  the class / type / defaults.
