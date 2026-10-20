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

## Notes for phase 2

- A node selection exists (`g_nodeSelected`, read-only): moving point nodes means writing `pos` / `angles` in the
  node's structure (`Nodes::StructOf`) and the gizmo follows on the next rescan - the scan is keyed to the arrays'
  names and sizes, so a moved node needs `Nodes` told to refresh (add a `Touch` / per-node update).
- Restart lookups read the node when used (so moved restarts apply live, as the plan expected); crowns are found by
  index among Crown nodes (`Node::order`), already computed.
- The Inspector's typed fields are where editing goes (ImGui `DragFloat3` etc.); the Online badge is ready to become
  a warning on edit.
