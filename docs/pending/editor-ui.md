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
  (Phase 3 found that ParseNodeArray parses the node manager owner's array - the level's - not the last one added:
  see below.)

## Phase 3: create and delete point nodes

Branch `worktree-agent-ac73f9426e90a064c`. **NOT run in the game yet**: checked offline only, as the lead asked.

### What

- **Place palette (N, or the toolbar's Place (N)):** a window "Place: what to add" with a search box. Its entries:
  Multiplayer spawn; Team spawn (Red / Blue / Green / Yellow); Crown; CTF flag set (Red / Blue / Green / Yellow);
  Level light; Sound emitter (silent for now); Proximity node (sphere). An entry that can't be placed now is greyed and
  its tooltip says why (a limit, or the level already has that team's node). Click an entry to arm it, then click in
  the view: the node is placed where the cursor points, dropped to the ground as G drops a node (a light 150 units above
  it), facing the camera's heading (angles (0, yaw, 0)). It is selected, and moves and turns like phase 2's nodes.
  Shift+click keeps placing; Esc disarms, then closes the palette. A double-click on an entry places one where the view
  looks. Without the UI (`editor_ui=0`) N only says the palette needs it.
- **Typed defaults, in THUG2's own levels' form** (the converters' forms):
  - multiplayer spawn: `Restart`, Type MultiPlayer, CreatedAtStart, RestartName "Multi: Restart", restart_types
    [MultiPlayer Horse];
  - team spawn: `TRG_CTF_Restart_<Colour>` (found by name, 0x00566100), Type CTF, RestartName "Team: CTF <Colour>",
    restart_types [CTF MultiPlayer Horse];
  - crown: `GenericNode` Type Crown, CreatedAtStart;
  - CTF flag set (Airport's form): `TRG_CTF_<Colour>` (GameObject Type Flag_<Colour>, NeverSuspend, model
    gameobjects\flags\flag_<colour>, lod_dist 800 / 801, TriggerScript TRG_CTF_<Colour>Script) and
    `TRG_CTF_<Colour>_Base` (Flag_<Colour>_Base, its model, no script), both at the same place;
  - level light: `LevelLight`, Color 255 214 170, Brightness 100, InnerRadius 150, OuterRadius 400;
  - sound emitter / proximity node: `EmitterObject` / `ProximNode`, Shape Sphere, Radius 300 / 200, CreatedAtStart.
- **Names:** fresh `ed_restart01`, `ed_crown01`, `ed_light01`, `ed_sound01`, `ed_proxim01`... (the first not used by any
  loaded node or object). Team spawns and flags keep the exact names the game looks for; placing one is refused when
  the level already has that name ("move it").
- **Delete (Del, or the Inspector's button):**
  - a node the editor added is removed;
  - a level's own spawn, crown, flag, light, sound or proximity node is hidden after a confirm window ("Delete an
    original node?"). For a crown it says: "It will be hidden, not removed, so the crowns after it keep their order -
    each moves up one number while it's hidden". Its gizmo turns grey (a crown shows "hidden"), the Outliner row greys
    with the tag "hidden", and the Inspector says so. **Home** (or "Show again (Home)") brings it back. Other families
    (rails, paths, objects, scenery, other nodes) can't be hidden yet; the disabled button's tooltip says why.
  - Refused: hiding the level's last counted crown (CFunc_SpawnCrown would divide by zero), and the last Player1 /
    Multiplayer restart.
- **Crown order:** the Inspector shows "#n of N" (N: the crowns the game counts). An added crown has Move up / Move
  down: it changes places with the added crown before / after it in the overlay array. **Only added crowns reorder**
  (the choice): they always come after the level's own crowns, so the level's crowns keep the level's numbers.
  Reordering the level's own would mean rewriting the level's array, which this design avoids.
- **Undo / redo:** new step kinds in the 64-step history: added nodes (created / removed, with where they were in the
  overlay and their place when removed - a moved node comes back where it was), hidden (hidden / shown), order (the
  overlay's order).
- **Online:** adding, deleting, hiding or reordering a spawn, crown or flag in a level shared with THUGPRO brings up the
  same once-a-run window as moving one (its text now covers all four).

### How: the overlay node array

- New nodes are structures (Script::NewStruct, built from the kind's fields) in a global array symbol
  `thug2_decomp_editor_nodes` (made once with CreateSymbol, reused), registered with the node manager **after the
  level's array** (`CNodeManager::AddNodeArray(name, false)` - not owned, so 0x004696C0 doesn't run). Each change takes
  it out (`RemoveNodeArray`), sets its elements again and adds it again: the hash and the prefix tables stay right.
- Walks go array by array (`Obj::GetRestartNodes` 0x0046B6C0, `CFunc_SpawnCrown` 0x00538170): the level's restarts and
  crowns keep their numbers; added ones come after.
- **The game never parses the overlay:** `CFunc_ParseNodeArray` (0x005A34E0) parses the node manager owner's array
  (0x0045C220: the level's CNodeArrayAsset +0x58), not the last one. So the editor makes what it would: a proximity node
  (`Proxim_AddNode(name, true)`), a sound emitter (`Emitter_AddNode`), a level light (0x00490570, as the cutscenes do).
  Neither manager can free a single node, so a removed proximity node / emitter is made inactive and reused (place,
  radius) if the same name is added again (undo).
- **NOT ORIGINAL changes in src/game/script/node_manager.cpp** (marked): `GetLastNodeArray` / `GetLastNodeArrayName`
  pass over the editor's array (`SetEditorNodeArray`; 0 = the original behaviour). Their callers - the park editor
  (0x00594FD0, and 0x004E8F50, which hashes park nodes with `HashNodeIn(GetLastNodeArrayName(), ...)`), LoadNodeArray,
  ReLoadNodeArray - mean the level's. And `RemoveNodeArray` tells a hook after an array is taken out
  (`SetNodeArrayRemovedHook`; null = none): when the level's array goes (CNodeArrayAsset::Unload), the overlay is taken
  out too and its structures freed. Otherwise the next level's array would be added after it, and every walk would meet
  the stale added nodes first.
- **Hidden** (src/game/front/level_editor_node_create.h): the game has no kill path for restarts or crowns (0x005A32C0
  has no case for Restart or GenericNode). So the node keeps its place in its array, but its class and type become the
  marker `ed_hidden`, its own kept on it as `ed_hidden_class` / `ed_hidden_type`. The restart walks skip it (class),
  SpawnCrown skips it (it counts type Crown, whatever the class), Create makes nothing of it (0x005A2E50: an unknown
  class returns 0). And by class, the game's own kill path: a proximity node and an emitter are made inactive (their
  state kept for Home), a light goes out of both light lists (0x0048FCA0; Home adds it again with 0x00490570), a live
  flag object (a CTF game's) is killed. The node layer reads hidden nodes through the marker (family and colour stay;
  drawn grey).

### What applies live, what after a reload

| Kind | Added | Hidden |
|---|---|---|
| Spawn (MP / team) | **live**: the next respawn / F8 (the restart tester walks every array) | **live**: the walks skip it; the restarts after it count one lower |
| Crown | **live** for the next crown spawned (King of the Hill is net-only, where layouts don't apply) | **live**: not counted; the crowns after it move up one |
| CTF flag set | node now; flag objects are made only when a CTF game starts (net only) - it matters for a later bake | node now; a live flag object killed |
| Level light | **live**: in the light lists at once (as cutscenes add lights) | **live**: out of the lists (0x0048FCA0, the cutscenes' path); Home adds it again |
| Sound emitter | node + emitter now, **silent** (nothing plays on it until a script does - choosing its sound is for later) | **live**: inactive |
| Proximity node | **live** (a sphere; no script by default) | **live**: inactive |

Layouts apply a second after the skater exists (as in phase 2), never in net games.

### Limits (enforced before adding, with a message)

- **10 crowns:** CFunc_SpawnCrown's two 10-entry stack arrays (unchecked). Counted as the game counts: every node of
  type Crown in every array; hidden ones not (hiding really takes them out of the walk).
- **256 restarts:** GetRestartNode's 256-name stack array (unchecked). Class Restart, hidden ones not.
- **Lights:** 256 dynamic (0x0048FB80, count 0x006FAEF0) and 150 level (0x00490400, count 0x006FAEF4): the lists'
  counts now. The game only refuses silently when they're full (no corruption); the editor says so instead.
- The node manager's 20 arrays: if all are in use, the overlay can't be registered (logged; the add is undone).

### Saved: layout records (still version 2)

```
node ed_restart01 Restart type=MultiPlayer pos=-120.5,8,330.125 angles=0,1.57079637,0 types=MultiPlayer,Horse
node ed_light01 LevelLight pos=0,250,0 angles=0,0,0 color=255,214,170 brightness=100 inner=150 outer=400
node ed_proxim01 ProximNode pos=1,2,3 angles=0,-0.25,0 radius=200
node TRG_CTF_Red GameObject type=Flag_Red pos=7,8,9 angles=0,0,0
order crown ed_crown02 ed_crown01
hide 8E1A2B3C  # TRG_KOTH03
```

- `node <name> <Class> key=value...`: the added nodes in the overlay's order. `pos` is required; `type`, `angles`,
  `types`, `color`, `brightness`, `inner`, `outer`, `radius` are optional; unknown keys are skipped; a repeated key or a
  bad value refuses the file. The rest of the node (RestartName, model, script, Shape...) follows from class / type /
  name, so the record stays short. An added node that was moved is written with its place now (not as a `set`).
- `order crown <names>`: the added crowns' order (written when there are two or more). Applied after the node records
  (it fills the crowns' slots in that order).
- `hide <checksum>`: a level's node hidden.
- The header stays `thug2-decomp layout 2`: the phase 2 reader skips unknown records, so a phase 2 DLL loads these files
  without the added / hidden nodes. Phase 2 and version 1 files load as before.
- Apply order: copies, objects, then node records (one re-registration), order, hide, then set records. The message and
  log end "..., N nodes, N added, N hidden". The autosave and the crash save are the same text (Capture), so they
  include them; restoring an autosave first removes every added node and shows every hidden one (`ResetAll`).

### Checks (offline)

- Clean build (`out/build/verify`, own PDB endpoint), no warnings.
- `thug2_check.exe` on THUG2PM.exe and on the stock THUG2.exe: all 355 early tests pass, 6 new
  (src/hook/selftests_editor_create.cpp): `TestEditorCreateNames`, `TestEditorCreateKinds` (each kind's fields, the
  heading, the kind read back), `TestEditorCreateLimits` (the four limits at their edges, the last-restart rule),
  `TestEditorCreateUndo` (create / delete / undo / redo through the overlay list; crowns moved and reordered by an order
  record), `TestEditorCreateLayout` (node / order / hide round trip exact, malformed records refused, phase 2 and v1
  files), `TestEditorCreateNodeManager` (GetLastNodeArray / Name pass over the overlay only when it's named and last).
- Mutations (node tools/mutate.ts), all 22 caught: a taken name used, one-digit names, the crown / restart limits off
  by one, the level-light limit ignored, the last-restart check inverted, the heading not wrapped, team spawns without
  CTF, the flag base given the script, PutBack not clamped, Take not removing, MoveCrown swapping with any node, unnamed
  crowns dropped by an order, names compared with case, restart types not written, a repeated key accepted, pos
  optional, the colour range unchecked, hide records misread, other orders read as crowns, GetLastNodeArray not
  skipping, GetLastNodeArrayName skipping when it shouldn't.
- `node tools/check_code_writes.ts`: no overlaps.

### In-game test checklist (for the user)

Use a workspace (or the main install) with this branch's DLL. Watch `thug2-decomp.log` (`level editor:` lines; also
`EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`, `SELFTEST FAIL`).

1. **Add a spawn and F8 to it:** `node tools/game/edit_level.ts TR`. Press N; the palette opens. Click "Multiplayer
   spawn", then click on the ground in the view. A blue disc appears there facing the way the camera looks; it's
   selected (Inspector: `ed_restart01`, "Added by the editor", node 0 in thug2_decomp_editor_nodes, "n of N restarts"
   with N one more than before). Log: `level editor: added ed_restart01 (Restart MultiPlayer) at (...)`. Drag it; Z / X
   turn it. Leave the editor and press F8 until the log's `restart tester:` line names `ed_restart01`: the skater
   stands there facing its arrow.
2. **Add a crown and see its #:** in a level with crowns (Foundry: `edit_level.ts foun`, or a THUG2 level), place a
   Crown. Its diamond shows `#N` (one past the level's last), the Inspector "#N of N+1". Place a second; select it and
   press Move up: the two added crowns swap numbers. The first added crown's Move up is disabled (tooltip: the level's
   own come first). A level crown's Move buttons are disabled.
3. **Hide an original crown:** select a level crown (say #2), press Del. The window "Delete an original node?" names it
   and says the crowns after it move up a number. Hide: the diamond turns grey and says "hidden"; the old #3 is now #2,
   and so on; the Inspector says "not counted while hidden". Home (or "Show again (Home)") brings it back and the
   numbers return. Hide crowns until one is left: the last is refused (message).
4. **Add a light:** near the skater, place a Level light: a bulb 150 units above the ground with rings; no crash, no
   log errors. Any lighting change on the skater is a bonus.
5. **Delete an added node:** select `ed_restart01`, Del: it's gone at once (no window). Ctrl+Z brings it back where it
   was (also after moving it first); Ctrl+Y deletes it again.
6. **Hide and Home:** hide a level spawn (window, Hide). Its disc is grey, the Outliner row grey with "hidden"; F8 never
   reaches it. Home shows it again. Hiding the level's only Player1 restart is refused.
7. **Undo / redo:** add, move, hide, reorder, delete - then Ctrl+Z back through all of them and Ctrl+Y forward again.
   Everything comes back with the same places and numbers.
8. **Save and reload:** Ctrl+S. The message ends "..., N added, N hidden". In `thug2-decomp-layouts\<scene>.txt`:
   `node ed_... Restart ...`, `order crown ...` (with two added crowns), `hide <hex>  # <name>`. Quit to the menu and
   load the level again: after about a second the log says `Layout loaded: ..., N added, N hidden`; the added nodes are
   back (same names, places, crown order), the hidden ones grey; F8 reaches the added spawn.
9. **Autosave / crash restore:** add and hide something, wait over a minute (log `autosaved`), end the game from Task
   Manager. Start the editor in the same level: the "Unsaved changes" window, Restore: the added and hidden nodes come
   back.
10. **Limit messages:** with 10 crowns (add up to 10), the palette's Crown entry is greyed ("This level has 10
    crowns...") and placing is refused. Team spawn (Red) / CTF flag set (Red) in Airport or Barcelona (they have them)
    are greyed: "This level has that team's ... already". (256 restarts and the light lists are hard to reach by hand;
    the self-tests cover them.)
11. **Online warning:** `edit_level.ts foun` (or z_ma). Place a spawn: the blue "Online: a level shared with THUGPRO"
    window appears once (its text: moving, adding, hiding or reordering). "Understood"; later adds / hides don't bring
    it back. A light there doesn't warn; a THUG2 level doesn't.
12. **A sound emitter and a proximity node:** place each; their gizmos (speaker; square with sphere rings). The emitter
    is silent (expected). Delete and Ctrl+Z each: no errors in the log.
13. **Leaving the level:** with added nodes, quit to the menu and load another level: the log says `the level's node
    array left - its N added nodes go with it`; the new level has none of them (no thug2_decomp_editor_nodes in its
    Inspector); F8 there never names an `ed_` restart.
14. **Old HUD:** `[features] editor_ui=0`: Del on a level spawn hides it at once (message); N says the palette needs the
    UI.

### Known issues / limits (phase 3)

- Nothing run in the game.
- A hidden crown / restart changes the numbers of those after it (the game counts only what it finds); keeping "#2 stays
  #2" would need CFunc_SpawnCrown rewritten.
- CTF flag sets reference the level script TRG_CTF_<Colour>Script (Team_Flag <colour>); a level without it would need
  the script - which matters only in net games (flags are made only there) or for a future bake.
- An added sound emitter plays nothing until a script plays a sound on it (choosing its sound: later). Added proximity
  nodes have no script yet (phase 5's script picker).
- Removed proximity nodes / emitters stay in the game's lists, inactive, until the level is loaded again.
- Hiding a flag kills its object if a CTF game made one; showing it again doesn't make it again (the game does at the
  next CTF start).
- A script that refers to a hidden node by name (Create / Kill) finds the marker class: it does nothing.
- Hiding works for spawns, crowns, flags, lights, sounds and proximity nodes only.

### What changes phase 4 (chains: rails, ladders, paths)

- Chains go in the same overlay array. `AddNodeArray`'s `ResolveLinks` turns links= **indices within the array** into
  names, so a chain's links must be built as indices into the overlay (or as name arrays, which ResolveLinks would then
  misread as indices - write indices). Links to the level's nodes can only be by name (the plan's rule), added after
  the array is registered.
- Rails are made by the rail manager from the owner's array at ParseNodeArray (0x0057E960, 0x0057C330, 0x0057D7A0):
  like proximity nodes, added rails must be made by the editor (or the rail manager rebuilt as the park editor does).
- The owned-array chain numbering (0x004696C0 when AddNodeArray's flag is true: component 0x9C91C3CA on linked nodes)
  doesn't run for the overlay; check whether the rail / path code needs it.
- Hiding rails maps to the game's own kill path (0x005A32C0: RailNode / ClimbingNode -> rail SetActive); the marker
  class would also stop Create.
