# Level editor UX 1: play from here, camera tools, preferences, first-run tips

Branch `worktree-agent-a147bf896eb9883cd`. **NOT run in the game yet** (no game runs, as asked): checked offline only -
the checklist at the end is for the user.

## What

### 1. Play from here (`level_editor_play.cpp`) - Unreal's Play-In-Editor / Unity's Play
- **F5 or Ctrl+P in the editor** closes it straight into play: `LevelEditor_Stop` already puts the skater on the ground
  below the camera, facing the camera's heading; leaving brings back the pause menu (`level_editor_exit`), so two frames
  later the pause menu is closed as its CONTINUE does (`exit_pause_menu`, gamemenu_pause.qb) - the game runs at once.
- **F5 or Ctrl+P in play** opens the editor again: Start is sent as a player's Start (`handle_start_pressed` pauses and
  makes the pause menu), then after 10 frames of the pause menu `level_editor_enter` runs - the same path as `-edit`
  (start_level.cpp). If the editor was left by play from here in the same level load (`Layout::LevelLoads`), it comes
  back with **the camera exactly where it was** (place, yaw, pitch, fly speed) and **the selection it had** (objects by
  id, scenery sectors, or a node by name). The Outliner's tree / search state lives in the ImGui context and the UI's
  statics, which survive Stop / Start already.
- Guards: the key in play does nothing unless the editor is offered (`LevelEditor_Available` - [features]
  level_editor, in a level - plus `Layout::Refused`: no network session, no career-type mode; the overlay's
  `level_editor_enter` must be loaded), the skater exists and the game isn't paused (the pause menu has LEVEL EDITOR).
  The key that opened the editor must be let go before it counts in the editor (no instant bounce).
- Esc in play is unchanged (the game's pause).

### 2. Camera tools (`level_editor_camera.cpp`) - Unreal / Blender / TrenchBroom
- **F frames the selection**: a smooth flight (a third of a second, smoothstep, yaw the short way) to see all of it - a
  sphere round its box (object model box, scenery pieces' bounds), from the side the camera looks from now. F used to
  be a nudge-down alias (with Q / Page Down) - it no longer is; the browser's F (show the item) is unchanged.
  `[editor] speed_by_selection=1` also sets the fly speed from the selection's size (4 x radius a second; the lead's
  point 3).
- **Alt+left drag orbits**: round the selection, or round **the surface point under the cursor where the drag
  started** (the game's collision ray; TrenchBroom's) - a rigid turn, so that point stays where it is on screen. **The
  wheel while orbiting changes the radius.** An Alt press is held back until the mouse moves 4 px (an orbit); let go
  unmoved, it is given back as the Alt+click it always was (one scenery piece).
- **Right button + wheel changes the fly speed** (x1.25 a notch, 50..20000; Unreal's), with a message; the status bar
  already shows `speed`.
- **Camera bookmarks**: Ctrl+1..9 stores (place, heading, and the ortho view if one is on), 1..9 flies back (the
  selection dropped first: the camera orbits a selection). Per level in
  `thug2-decomp-layouts\<scene>.cameras.txt` (a side file - the layout format is untouched): header
  `thug2-decomp camera bookmarks 1`, lines `<n> x y z yaw pitch view equivalent` (%.9g: exact floats).
- **Ortho views** (Blender's numpad): numpad 7 top, 1 front (looking along +z), 3 right (looking along -x); Ctrl+7 / 1 /
  3 bottom / back / left; numpad 5 toggles perspective / ortho for the view as it is. The wheel and W / S zoom, A / D /
  Q / E pan; looking about (right button, Alt+drag) goes back to perspective (Blender's auto-perspective). The status
  bar shows e.g. `Top ortho (1500)`.
  - **How (renderer independence kept)**: near-orthographic with the renderer's own perspective - the camera far back
    along the view (`EyeDistance` = 15 x the zoom's equivalent distance, 15000..60000), the field of view narrowed to
    keep the picture's size (`ViewAngle`), the near plane brought up to 40% of the eye's distance (`NearPlane`: depth
    precision). So the game's culling, the editor's picking rays, the gizmos' projection and the outlines all stay
    right with no projection matrix of our own and nothing Direct3D outside the backend. Distortion: about +-8% scale
    for +-1000 units of depth at the default zoom (it is a long lens, not a true ortho).
  - Hooks: `Nx::RenderViewport` (render.cpp) asks `LevelEditor::ViewAngle(camera, CameraAngle(camera))` for its three
    SetCamera calls and `LevelEditor::NearPlane(camera)` for the two world passes (`s_nearZ`, SetCamera's `nearZ` -
    2 as the original otherwise; the sky's pass keeps 2). Both return the game's values unless the editor runs, the
    camera is the editor's and an ortho view is on.
  - The move arrows' length and the gizmos' distance cut-offs use the distance as the picture shows it
    (`Camera::Apparent`), so they don't vanish or grow in an ortho view.
- **Flying's acceleration** (`[editor] fly_accel_ms`, default 0 = as before), **mouse sensitivity / invert Y** apply
  to the right-button look and to the orbit.

### 3. Preferences (`level_editor_prefs.cpp`) - the toolbar's **Prefs** button, **Ctrl+,**, or the help's button
Saved in `[editor]` of thug2-decomp.ini (integers; written when a slider is let go or a box clicked):

| Key | Default | What |
|---|---|---|
| `fly_speed` | 800 | the fly speed when the editor opens (units/s) |
| `fly_accel_ms` | 0 | time to reach full fly speed |
| `look_pct` | 100 | mouse look / orbit sensitivity |
| `invert_y` | 0 | mouse look's Y inverted |
| `gizmo_pct` | 100 | move arrows' length and the gizmos' pixel size (and their pick radius) |
| `ui_scale_pct` | 0 | UI scale, 0 = automatic (window size) |
| `autosave_s` | 60 | autosave interval |
| `snap_move` | 25 | Ctrl / toolbar snap grid (the toolbar's checkbox reads "Snap <n>") |
| `snap_turn` | 15 | Z / X turn for **objects** (nodes keep NodeEdit's 15 degrees) |
| `accent` | 0 | accent colour: Orange (as before), Blue, Green, Purple, Red, Teal |
| `wasd_flies` | 0 | **for UX2** (RMB-held flying default): W A S D always fly - shown in this window; UX2's code reads `Prefs::Get().wasdFlies` |
| `speed_by_selection` | 0 | F sets the fly speed from the selection's size |
| `tips_seen` | 0 | the first-run tips dismissed |

The UI's scale and accent rebuild the fonts / style at once (`Prefs::StyleGeneration`); the toolbar's and Outliner's
orange highlights now follow the accent (`Accent(alpha)` in level_editor_ui.cpp replaces the nine orange literals).

### 4. First-run tips
A small window the first time the editor opens ("The five keys you need": right mouse + W A S D, click / drag, F,
F5, F1); "Got it" (or its close box) sets `tips_seen=1`. The help window (F1) has **Show tips** and **Preferences**
buttons at its top; the Prefs window has Show tips too.

## Keys bound (for the lead's clash check)

| Key | Where | Does |
|---|---|---|
| F5, Ctrl+P | editor | play from here |
| F5, Ctrl+P | play (not paused, editor offered) | back to the editor |
| F | editor, something selected (not with the browser open) | frame the selection (**removed F from the nudge-down keys**) |
| Alt+left drag | editor | orbit (Alt+click unmoved: as before) |
| wheel while Alt-orbiting | editor | orbit radius |
| right button + wheel | editor | fly speed (was: forward / back or orbit distance while the button was held) |
| Ctrl+1..9 / 1..9 (top row) | editor | store / recall a camera bookmark |
| numpad 7 / 1 / 3, Ctrl+numpad 7 / 1 / 3, numpad 5 | editor | ortho views / toggle |
| Ctrl+, | editor | preferences |

Not taken: W / E / R (UX2's transform modes - note R still nudges up and E / Q still nudge, UX2 rebinds those), B,
Ctrl+Space, Shift+G (UX3). P alone stays the camera log.

## Shared files touched (small, additive)
- `src/game/front/level_editor.cpp`: includes; the prefs replacing `kSnap`, `kTurnPerNotch`, `kAutosaveMs`;
  `ArrowLength` / `PixelScale` / the two gizmo cut-offs; Start / Stop calls; `InputLogic` (play request); `PlaceCamera`
  (resume); in `Update`: `Camera::FilterMouse` + the F5 / Ctrl+P / Ctrl+, keys after the modifiers, the look line
  (sensitivity, invert, `ApplyOrbit`, relative mouse while orbiting), `Camera::Keys` after `ApplyUiRequests`, the fly
  ramp, F out of the nudge keys, `Camera::AfterUpdate` at the end; a helper block before `ApplyUiRequests` (`RigNow`,
  `SelectionRadius`, `ResumeNow`, `ApplyResume`); `ViewAngle` / `NearPlane`.
- `src/game/front/level_editor.h`: the module list; `ViewAngle` / `NearPlane`.
- `src/game/front/level_editor_ui.cpp`: includes, `Accent()`, the scale override in `UpdateScale`, the toolbar's Prefs
  button and Snap label, the status bar's camera state, the help's buttons and rows, the two windows in `Frame`.
- `src/game/gfx/render.cpp`: `s_nearZ` and the three angle calls (the gfx world-render trace test still matches).
- `src/game/front/mouse_menus.cpp`: `Play::Update(frontEnd)` beside the restart tester.
- `CMakeLists.txt`: three sources, one self-test file.

## Checks (offline)
- Clean build (`out/build/verify`, own `_MSPDBSRV_ENDPOINT_` thug2ux1), no warnings in our files.
- `thug2_check.exe` on THUG2PM.exe and the stock `C:\Games\THUG2\Game\THUG2.exe`: all 448 early tests pass, six new
  (`src/hook/selftests_editor_ux1.cpp`): `TestEditorCameraFlight` (ease, shorter yaw, blend), `TestEditorCameraFrame`
  (a framed sphere's edges inside the lens, minimum, speed from size, fly ramp), `TestEditorCameraOrbit` (pivot stays
  at the same NDC, distance kept), `TestEditorCameraOrtho` (picture size kept, near plane before the pivot, eye limits,
  the six axis views' directions), `TestEditorCameraBookmarks` (exact round trip, refused header, malformed lines
  skipped, file name), `TestEditorPlay` (key rules, F5 / Ctrl+P, resume once and only in the same load),
  `TestEditorPrefs` (limits, defaults = old behaviour, accents, Set / style generation).
- Mutations (`node tools/mutate.ts`, 17): all caught - nearest angle, blend's yaw, orbit frame, ortho narrowing, right
  view's direction, bookmark precision (after adding a value needing 8 digits), view range, framing margin, linear ramp,
  near plane at the pivot, play while paused, P without Ctrl, resume in any load, resume twice, UI scale floor, accent
  without rebuild, the default orange.
- `node tools/check_code_writes.ts`: no overlaps.

## In-game checklist (for the user - NOT run)
Start with `node tools/game/edit_level.ts TR` (a workspace with this branch's DLL). Watch `thug2-decomp.log`
(`level editor:` lines; `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`).
1. **Tips**: the first time, "Welcome to the level editor" shows five keys; Got it closes it; reopen the editor - it
   doesn't come back; `[editor] tips_seen=1` in the ini. F1 > Show tips brings it back.
2. **Play from here**: fly somewhere, press F5. The editor closes, the skater stands on the ground below where the
   camera was, facing its heading, and **the game runs at once** (no pause menu). Log: `play from here`, `the skater put
   at ...`, `play from here - the pause menu closed`. Skate a little, press F5: the pause menu flashes ~10 frames, the
   editor opens **with the camera exactly where it was** and the same selection (try with an object, a scenery group,
   a spawn node selected). Ctrl+P does the same both ways. P alone still writes the camera to the log.
3. **Play key guards**: in a career / net session or with level_editor=0 F5 does nothing in play; F5 while paused does
   nothing; opening the editor with F5 and holding it doesn't bounce straight back to play.
4. **F**: select a big scenery group, fly away, press F: the camera glides (not jumps) to see all of it. Select a small
   object, F: nearer. With "Speed from the selection's size" on, the status bar's speed changes. F with nothing
   selected: a message. Q / Page Down still nudge down (F no longer does).
5. **Alt+drag orbit**: nothing selected - Alt+drag on a wall: the view turns round that point (it stays under the
   cursor); wheel during the drag goes nearer / further. With a selection: orbits round it. Alt+click (no movement)
   still takes one scenery piece.
6. **Right button + wheel**: fly speed changes (message, status bar's speed), the camera doesn't move.
7. **Bookmarks**: Ctrl+1 somewhere, fly away, Ctrl+2 elsewhere, press 1 / 2: smooth flights back. Restart the game:
   they're kept (`thug2-decomp-layouts\<scene>.cameras.txt`). Pressing an unset digit: a message.
8. **Ortho**: numpad 7 (NumLock on): top view, the status bar says "Top ortho"; wheel zooms, W / S zoom, A / D pan,
   Q / E pan in front/side views; clicking still selects the right thing under the cursor; the move arrows keep a
   sensible size; spawns / rails gizmos still show. Numpad 1 / 3 front / right, Ctrl+numpad opposite. Numpad 5 toggles
   back to perspective (and to ortho of the current direction). Right-drag look returns to perspective. Look for
   z-fighting, missing peds / objects at the far eye (LOD), and the sky (front views show it zoomed or not at all).
   **NumLock off**: SDL may report the keypad keys as Home / End / Page Down - then numpad 7 would be Home (Reset)!
   Check which happens.
9. **Prefs** (toolbar Prefs, or Ctrl+,): every slider takes effect at once (fly speed at the next editor start; snap:
   the toolbar's checkbox label changes and Ctrl+drag snaps to it; turn step: Z / X on an object; gizmo size: arrows
   and gizmo dots; UI scale: everything resizes, Automatic goes back; accent: the toolbar's highlighted buttons and
   headers change colour; invert Y / sensitivity: right-drag look and Alt-orbit; acceleration: flying eases in;
   autosave: the log's `autosaved` cadence). Close and reopen the game: all kept in `[editor]`. Defaults resets them.
10. **Old HUD** (`editor_ui=0`): F5, F, Alt+drag, bookmarks and the numpad still work (no Prefs window / tips).

## Known limits / left
- Ortho is a long-lens perspective (see above), not a true orthographic projection; the trigger volumes' own distance
  cut-offs (triggers.cpp) don't use `Apparent` yet, so far-eye triggers may hide in ortho views.
- Camera bookmarks don't store the selection; recalling one deselects.
- `wasd_flies` is only stored and shown here: UX2 implements it. `snap_turn` applies to objects only (nodes:
  NodeEdit's own 15 degrees) - UX2's snapping work may adopt both.
- The back-to-editor pause menu flashes for ~10 frames (the -edit path's way in).
- Flights and orbit pivots aren't undo steps (camera only).
