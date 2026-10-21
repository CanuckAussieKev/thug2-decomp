# Editor UI polish: menu bar, tool strip, copy edit, UI scale

Branch `worktree-agent-a5c2285900d3ef910`. NOT run in the game yet (UI work: the user tests). Built in the worktree
(`out\scripts\build.cmd`, Debug, own PDB endpoint); `thug2_check.exe` passes on THUG2PM.exe and on the stock
`C:\Games\THUG2\Game\THUG2.exe` (521 early self-tests); `node tools/check_code_writes.ts`: no overlaps.

## What changed

1. **Top of the screen** (user report: the toolbar overflowed and items overlapped at 2560x1440 / 200 %):
   - A **menu bar** (`src/game/front/level_editor_ui_menus.cpp`): File, Edit, View, Tools, Help. Every item is an
     action of the registry (`level_editor_actions.h`) with its key as bound now, right-aligned; clicking runs it as
     its key would (`Actions::Run`). Everything the old toolbar had moved here: undo / redo / history / changes,
     save / snapshots / autosave / reset, view modes, Overlays (incl. Gizmos), Face Select, occlusion, hide / isolate /
     show all, camera views and bookmarks, windows (Problems, Trails, Arrange, Adjust Last), Snap and the transform
     options (pivot, rotation snap, snap to, align to slope, WASD), carry, pick, Add Piece / Place Node, ruler, gap
     wizard, ground tool, rail along edge, Arrange's align / distribute, rail and path operations, controller mode,
     Selection Sets (Edit > Select), keyboard shortcuts / command search / keymap / tips.
   - One **tool strip** (`level_editor_ui_strip.h/.cpp`): Fly / Select, Move / Rotate / Scale, World / Local, Snap +
     grid size, Pick filter, Show All (n) when something is hidden, Stop Simulation while simulating; at the right
     the edited level's name (`<Level> (Edited)`, `*` when unsaved), the budget meter and Problems. Each control's
     width is measured with the fonts and style in use, `UiStrip::Fit` places them; what doesn't fit goes into a
     `»` overflow menu, least important first (name, Show All, Fly / Select, Pick, budget, World / Local, Problems,
     Snap, the tools; Stop Simulation last).
   - **Status bar**: the right-hand text (unsaved, changes, moved, camera, speed) is elided to at most 45 % of the bar;
     the hints that don't fit are left out with "…"; a long message is elided (full text in its tooltip).
   - Removed (moved into the menus / strip): `Transform::ToolbarTools/ToolbarSnap` (now `OptionsMenuItems`),
     `Overlays::ToolbarMenus` (now `ViewModeItems` / `OverlayItems`), `Ux3::Toolbar`, `Ux8::Toolbar` (now `SetsMenu`),
     `Checks::ToolbarButton`, `PlayTest::ToolbarButtons`, `Budget::ToolbarItem` (now `StripMeter` + `Tooltip`).
   - Fonts and style moved to `level_editor_ui_theme.cpp` (shared with the self-test).
2. **UI scale** (`UiLayout::ScaleFor`): 1 at 1920x1080 (Blender's / Unreal's 100 %), quarter steps of the smaller of
   width / 1920 and height / 1080 - 1.25 at 2560x1440 (Windows' suggested 125 % there), 2 at 4K, 3 at most, never below
   0.875 (1280x720: Inter at 12 px). Was 1.5 at 1080p and 2 at 1440p. Font sizes stay whole pixels. Preferences' UI
   scale (`[editor] ui_scale_pct`) still overrides. Layout: menu bar 22, tool strip 28, status bar 24 (x scale).
3. **Copy edit** of the editor's user-facing strings: noun labels, verb buttons, Title Case menu and window names,
   one-line tooltips with the key in parentheses, no internals (addresses, ini keys, how the engine stores it), no
   chatty phrasing. Keys moved out of button labels into tooltips. The registry's names are the menu items' names.
   The F1 window lists the registry (every command with its keys) and a short Reference table of the mouse gestures.
   Not edited: the old screen-element HUD fallback (`editor_ui=0`, level_editor.cpp's key list), log lines.
4. **Self-tests** (`src/hook/selftests_editor_ui.cpp`): `TestEditorUiStripFit` (the placement: room for all, the
   least important into the overflow, nothing overlaps the overflow button, all hidden when nothing fits);
   `TestEditorUiBarsFit` lays out the menu bar, tool strip and status bar headless with the editor's fonts and style
   at 1280x720, 1920x1080 and 2560x1440, each at the automatic scale and at 100 / 150 / 200 %, with a crowded state
   (every strip item, a long level name, the screenshot's 16 hints, a long status text), and checks no two controls
   overlap, every one is inside the window and its bar, Move and Problems are always on the strip, and the overflow is
   used in some cases but not all. Proven by mutation: Fit given 3x the room -> "outside" failures; the hint row
   without its width limit -> "StatusRight overlaps Hint" failures. Layout / scale tests updated to the new values.

## Renamed strings (examples, before -> after)

| Before | After |
|---|---|
| Inspector: "Shows in the game: Now: skaters grind it where it is (its node keeps the level's place; the layout's rail record has it)." | "Applies: Live" (+ "Modified" when moved) |
| Toolbar "Save layout", "Reset level", "Help (F1)", "Add (B)", "Place (N)", "Prefs" | File > Save Layout (Ctrl+S), File > Reset Level..., Help > Keyboard Shortcuts (F1), Tools > Add Piece (B), Tools > Place Node (N), Edit > Preferences... |
| "Carry objects" checkbox + 3-line tooltip | Tools > Carry Objects ("Move objects standing on scenery with it") |
| "Help: the keys" / "Add: the piece browser" / "Place palette" | Keyboard Shortcuts / Add Piece / Place Node |
| "Home: as the level has it" / Inspector "Home" | Revert to Original / "Revert" |
| "Ground (G)" button | "Drop to Ground" (tooltip "Drop to Ground  (G)") |
| "Whole rail (L) - 2 points" + 2-line tooltip | "Whole rail (2 points)" ("Move all points together  (L)") |
| "Nothing selected - click something in the view (a gizmo, an object, scenery), or pick it in the Outliner." | "Nothing selected" |
| Window "Level editor - keys (F1 or Esc closes this)" | "Keyboard Shortcuts" |
| Window "Unsaved changes from a previous session" / "Restore it (Ctrl+R)" | "Recover Autosave" / "Restore" |
| Window "Reset the level?" + two paragraphs | "Reset Level": "Revert all changes to X and delete its saved layout and autosave?" |
| HUD "Duplicated - move / turn the copy, then Ctrl+Shift+D repeats that" | "Duplicated" |
| HUD "Collision shown (C hides it): invisible walls magenta, Shift+click one" | "Collision shown" |
| HUD "Wall deleted: its collision is off - the skater passes it (shown dashed; Delete / Home brings it back, Ctrl+Z undoes)" | "Wall collision off" |
| "Faces mode (K)" | "Face Select Mode" |
| Face flag "Skatable (0x01)": "Skatable whatever its slope (the skater's feeler check 0x005003A0)" | "Skatable": "Skatable at any slope" |
| Outliner search hint "Search (rail: spawn: object: group:<name> label: locked:)…" | "Search…" (prefixes in its tooltip) |
| "Changed only" + marker legend sentences | "Changed Only"; markers "Moved", "Added", "Hidden in game", "Hidden in editor", "Locked", "Collision off" |
| Prefs "Automatic UI scale": "From the window's size (1280 x 720: 100%, 4K: 300%)." | "Automatic UI Scale": "Based on window size (1080p: 100%, 1440p: 125%, 4K: 200%)" |

Action registry names now Title Case (menu items): e.g. Undo History, Save Layout, Restore Autosave, Revert to
Original, Reset Level..., Lock / Unlock, Box Select, Select Similar..., Drop to Ground, Show Collision, Face Select
Mode, Hide Selection, Isolate Selection, Show All, Insert Point, Extend from End, Link to Point, Play from Here, Frame
Selection, Go to Bookmark n / Set Bookmark n, Top View..., Move Tool, Rotate Tool, Scale Tool, Toggle World / Local,
Lit / Unlit / Wireframe / Collision Only, Jump Arcs, Problems, Trails, Pie Menu, Walk Mode, Follow Selection, Go to
Cursor, Controller Mode, Store Set n / Select Set n, Gap Wizard, Skateability, Ground Tool, Arrange, Adjust Last
Operation, Rail Along Edge, Duplicate Linked, Replace Selection, Unlink Copies, Edge Snapping, Snap, Carry Objects, Pick
Nodes / Objects / Scenery, Add Note. Ids (and so `[editor_keys]` bindings) are unchanged.

## Checklist for the user (in the game, `C:\Games\THUG2` once merged)

1. Open the level editor at your usual size (2560x1440): the UI is smaller than before (125 % automatic). Preferences
   (Edit > Preferences..., Ctrl+,) > Automatic UI Scale off and the slider at 100 / 150 / 200 % changes it at once.
2. The top is a menu bar (File, Edit, View, Tools, Help) over one tool strip; nothing overlaps or is cut off.
   Open each menu: every item shows its key on the right; hovering shows a one-line tooltip.
3. Try a few menu items: File > Save Layout, Edit > Undo / Redo, View > Wireframe, View > Overlays > Grid,
   View > Camera > Top View, Tools > Ruler, Tools > Pick > Pick Scenery (off / on), Help > Keyboard Shortcuts.
4. Tool strip: Move / Rotate / Scale switch the gizmo (W / E / R still work); World / Local; Snap toggles and the
   grid button lists 1..100; Pick lists Nodes / Objects / Scenery; the level name's tooltip; the budget meter's
   tooltip and click (Problems > Budget tab); Problems opens the Problems window.
5. Narrow the window (or set UI scale 200 % at 1280x720): controls drop into the "»" button at the right in order
   (level name first); "»" opens them; nothing draws over anything.
6. Hide something (H): "Show All (n)" appears on the strip. Simulate (F6): "Stop Simulation" appears and stops it.
7. Status bar: with something selected, the key hints end in "…" instead of running into the camera text at right.
8. Inspector on a rail point: "Applies: Live" instead of the long note; buttons Drop to Ground / Revert / Hide or
   Delete / Focus; tooltips carry the keys.
9. Read through the windows (Problems, Trails, Arrange, Triggers, Command Search, Undo History, Changes, Snapshots,
   Keymap, Preferences, Place Node, Add Piece): wording is short and neutral - report anything that reads oddly or
   lost information you need.
10. Rebound keys (Keymap) still show in the menus and tooltips as bound.

To switch the work off: there is no game-code replacement here (UI only); `[features] editor_ui=0` brings back the old
HUD, or revert the branch's commits.
