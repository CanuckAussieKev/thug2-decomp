# Level editor UX8: context hints, selection sets, pie menus, controller mode, drill-through, follow / teleport / walk

Branch `worktree-agent-a9700f2a7e5c39d55` (main merged up to UX6, goals and UX7). **NOT run in the game yet** (editor UI
work: the user tests). Built and checked offline only: thug2_check on THUG2PM.exe and the stock
C:\Games\THUG2\Game\THUG2.exe (0 MISMATCHED, all 497 early self-tests pass), check_code_writes, mutations below.

The research's (docs/editor-ux-research.md) #6, #8, #10, #11, #15, #16.

## What was built

1. **Context hints** (`level_editor_hints.{h,cpp}`, pure): the status bar's key line is now built from the action
   registry (each action's keys *as bound now* - a rebinding shows at once; an unbound action is left out) and the
   editor's state: flying / right button held, the selection's kind (objects, scenery, a point node, another node, a
   wall), the tool, a drag (move / along an arrow / rotate / scale, typed values, carried objects, Alt+G modal), the
   browser, Place armed, chains drawing / picking, the ruler, Faces mode, box select armed, a pie up (held / tapped),
   walking, following, Simulate running, controller mode. **Holding Ctrl / Shift / Alt** lists what that modifier changes
   for the mouse and every registry action bound with exactly those modifiers (`ModifierHints`). Drawn Blender-style
   (`Ux8::HintRow`): key caps, mouse glyphs (left / right / middle button lit, the wheel, a double click), controller
   glyphs (A / B / X / Y in their colours, sticks / triggers / shoulders as rounded caps). UX6's Simulate / Trail and
   UX7's Arrange (F2), Adjust last (F4), linked duplicate (Ctrl+Alt+D), rail along an edge (Alt+R) are among them.
   `KeysNow()` in level_editor.cpp fills `Hints::Context`; the old HUD (editor_ui=0) keeps its own lines.
2. **Selection sets** = UX3's groups (named, saved in the layout as `group` records, the Outliner's Groups tree): they
   already were named selection sets. Added: **quick sets** "Set 1".."Set 9" - **Ctrl+Alt+1..9 stores** the selection
   (replacing the set, one undo step), **Alt+Shift+1..9 selects** it; the toolbar's **Sets** menu: each quick set
   (Store / Select / + add the selection / - take the selection out) and every other group (Select / + / -). No new
   record kind. (`Nav::StoreGroup`, `RemoveFromGroup` on `Organize::State`, applied with `Organize::Change`.)
3. **Pie menus** (`level_editor_ux8_pie.*`, geometry `Nav::SliceAt`): **Q or Tab** - with a selection: Move, Rotate,
   Snap, Drop, Scale, Delete, Duplicate, Frame, Duplicate linked, Arrange; without: Add (browser), Place, Box select,
   Ruler, Walk, Simulate, Play, Trails. **Alt+Q or Shift+Tab** - view: Lit, Unlit, Wireframe, Collision only, Collision
   (C), Trigger volumes (T), Jump arcs (Y), Top view (in ortho: Perspective). Held: point and let go runs the slice
   (released in the middle: nothing); tapped (< 250 ms, nothing pointed at): it stays up - click a slice; Esc / right
   button closes. Each slice shows its key; toggles show a dot when on; slices run registry actions (`Actions::Run`, so
   the editor's own code does them) or set the tool / view mode directly; a slice that edits stops a running Simulate
   first. Q opens nothing while the right button is held (it flies down); with `wasd_flies=1` a pie key held together
   with a fly key flies (use Tab).
   **Controller mode** (Preferences > Controller mode, `[editor] pad=1`, or the search's "Controller mode"): the first
   attached game controller (the ones sio/sdl_input.cpp opened): left stick flies (with a selection the camera leaves
   it, as RMB flying; F / R3 frames it again), right stick looks / orbits, LT / RT down / up, L3 fast (as Shift),
   **A** = the left button at a crosshair in the viewport's middle (hold: drag - flying while holding carries the
   piece), **B** = Escape (never leaves the editor), **X** pie, **Y** view pie (left stick points; let go or A runs; B
   closes), D-pad = the nudge keys, LB / RB turn, R3 frame, Back undo; Start still leaves the editor (the game's pause
   key). Moving the mouse gives the cursor back; touching the controller takes it again. A / Back / LB / RB / D-pad stop
   a running Simulate (its key handler only sees typed keys).
4. **Wheel drill-through**: **Alt+wheel** over a spot makes click-to-cycle's list of everything under the cursor
   (without selecting) and steps the hover through it (down: deeper, up: nearer, round the ends), with "2 of 5 under
   the cursor"; the highlighted one stays the hover while Alt is held and after; a click takes it (with Alt still held:
   Alt+click's one-piece rule). Alt+wheel used to dolly the camera (as the plain wheel still does).
5. **Follow camera** (**Shift+F**, Unity's): frames the selection (F) and keeps the camera with it as it moves - nudges,
   Inspector values, undo, a moving object's preview - also after flying off it with the right button (the offset
   kept). Not during a mouse drag (the drag goes by the cursor's ray). Another selection starts afresh. Shift+F again
   stops. **Click-to-teleport**: a **double click on a surface** (where a click selects nothing - floors, the ground;
   the first click deselects) glides the camera there (0.35 s; 260 units back along the ray, at least 60 above the
   point - `Nav::TeleportTarget`); flying / looking ends the glide; "Teleport to the cursor" in the command search.
   **Walk mode** (**Shift+`**, Blender's walk navigation): the camera drops to a skater's eye height (62) with gravity
   (1400 u/s²) and the game's collision ray: W A S D (the fly keys) walk 220 u/s, Shift runs 560, Space / E (fly up)
   jumps (~39 high), the mouse looks (captured), walls stop it a radius (14) short and it slides along them, steps up to
   24 are walked up, a ceiling stops a jump, no ground below at all (a level's edge): it stays at its height.
   Shift+` or Esc stops (the camera stays). Controller: left stick walks, A jumps, B stops. The rest of the editor rests
   meanwhile (Update returns after the walk).

## Keys (all registered with `Actions::Register` in `Ux8::RegisterActions` - rebindable, in the search and F1)

| Key | Action id | Does |
|---|---|---|
| Q, Tab | `pie_context` | the actions pie |
| Alt+Q, Shift+Tab | `pie_view` | the view pie |
| Shift+` | `walk_mode` | walk mode on / off |
| Shift+F | `follow_selection` | follow the selection (toggle) |
| (none) | `teleport` | the camera to the surface under the cursor |
| (none, toggle) | `controller_mode` | controller mode |
| Ctrl+Alt+1..9 | `quick_set_store_n` | store the selection as "Set n" |
| Alt+Shift+1..9 | `quick_set_n` | select "Set n" |
| Alt+wheel, double-click, a controller | `hint_drill`, `hint_teleport`, `hint_pad` | (listed hints) |

Clash check (self-test `TestEditorUx8Keys`): none of these defaults is any other registered action's default, and each
press resolves to it alone: Shift+F not F (frame), Alt+Q / Q (Q is otherwise only fly-down, a Hold), Shift+Tab, Ctrl+Alt+1
not Ctrl+1 (bookmark store), Alt+Shift+1 not Alt+1 (view mode Lit), Shift+` not ` (world / local). Alt+wheel: was a
dolly; Alt+drag orbit's wheel (while orbiting) is unchanged.

## Files

New: `src/game/front/level_editor_hints.{h,cpp}` (pure hints), `level_editor_nav.{h,cpp}` (pure: walker, double click,
teleport target, follower, drill index, pie slices, stick response, quick-set group edits), `level_editor_ux8.{h,cpp}`
(actions, controller, pies' state, quick sets, follow / walk / glide state), `level_editor_ux8_pie.{h,cpp}` (the
slices), `level_editor_ux8_ui.cpp` (glyph hint row, pie and crosshair drawing, the Sets menu, the Preferences section),
`src/hook/selftests_editor_ux8.cpp` (5 Early tests).

Hooks in shared files (small, additive):
- `level_editor.cpp`: includes; `Ux8::Init` / `Start` / `Stop`; HandleEvent's Escape also cancels (not leaves) with a pie
  up or walking; in `Update`: the controller read after the keyboard (keys merged, A into the mouse, the crosshair as
  the cursor, B as cancel), the walk toggle + `WalkFrame` early return after `PlayTest::Tick`, `Ux8::Cancel()` first in
  the Escape chain, `Ux8::Update` after `Ux3::Update`, the right stick's look before `ApplyOrbit`, the left stick's fly
  and the teleport glide after the frame, `padCursor` in the ray's condition, Alt+wheel `DrillThrough` and the teleport
  action after `Camera::Keys`, the follow block before the browser's camera line, the drill's hover hold in the hover
  code, the double-click teleport after `boxCandidate`; new functions `DrillThrough`, `TeleportTo`, `WalkFrame` (before
  `TransformKeys`); `KeysNow()`'s body replaced (the context for `Hints::Build`).
- `level_editor_ui.cpp`: `Ux8Fonts()`, the status bar's key loop -> `Ux8::HintRow`, `Ux8::Toolbar` after
  `Ux3::Toolbar`, `Ux8::Windows` after `Ux3::Windows`. `level_editor_prefs.cpp`: `Ux8::PrefsSection()` before Keys.
- `level_editor.h`: the module list's line. `CMakeLists.txt`: 5 sources, 1 self-test file.
- Ini: `[editor] pad=` (0).

## Tests

`src/hook/selftests_editor_ux8.cpp` (Early): `TestEditorUx8Hints` (flying / RMB / wasd_flies, object selection's keys,
a rebound key shown, an unbound one left out, following, dragging and carried, walking, a pie held / tapped, controller
glyphs, Alt held + the registry's Alt list, the ruler; FirstKey, JoinKeys, Tokens incl. "Keypad+", GlyphOf,
ModifierHints exact / ordered / limited), `TestEditorUx8Walk` (a stand-in level: drop and stand, a wall a radius short,
sliding, a step walked up, walk / run speed, jump height and landing, no air jump, a ceiling, no ground, right =
the frame's right), `TestEditorUx8Nav` (double click, teleport stand-off / near / lift, follower, drill stepping and the
cycle index, pie slices incl. every slice's direction round trip, stick response), `TestEditorUx8Sets`,
`TestEditorUx8Keys` (registered, no clash with any registered default, each press resolves alone). The UX3 registry
test (built-ins don't clash) still passes.

Mutations (`node tools/mutate.ts out/scripts/mut_ux8.ts`, the file in this worktree's out/scripts): **18 of 18 caught** - RMB left off the fly hint, the whole keys text as the key, "Keypad+" split, a modifier list by subset, drags ignored, no axis radius at walls, no sliding, no step up, a jump in the air, no ceiling, the teleport's near case, the follower's first frame, the drill's start, the pie flipped, a linear stick, a set not replaced, the view pie on plain Q, quick-set store on Ctrl+n.

## Not done / limits

- The pie is drawn with ImGui: with the old HUD (`editor_ui=0`) there are no pies (the keys do nothing visible).
- Walk mode uses static collision only (the game's feeler: objects / cars aren't walls); it has no crouch / ladder; the
  mouse is captured while walking (no clicking in the view). Teleport is not available while walking.
- Controller mode reads the pad while the game is paused in the editor; whether the game's own input also reacts to the
  editor's A / B / X / Y while paused (screen elements, the paused front end) is untested - check (checklist 12).
- The follow camera doesn't follow during a mouse drag (by design) and isn't saved.
- Drill-through steps what click-to-cycle lists (gizmo, objects, scenery layers): trigger volumes and invisible walls
  aren't in that list.
- The quick sets are groups named "Set n": renaming one in the Outliner takes it off its key.

## In-game checklist (for the user - NOT run)

Start with `node tools/game/edit_level.ts TR` (a workspace with this branch's DLL); watch `thug2-decomp.log`.

1. **Hints**: with nothing selected the status bar shows a mouse glyph with the right button lit + "WASD fly", a left-lit
   mouse "select", a "2" mouse "go there", "Alt+wheel drill through", Q pie, B add... Hold the right button: the bar
   changes to flying (WASD, QE, Shift, wheel speed). Select an object: drag / W E R / nudge (arrows) / Z X / G / Ctrl+D /
   F / Shift+F / Q / Delete / Esc. Drag it: the drag's modifiers and Esc. Hold Ctrl, Shift, Alt in turn: the lists
   change (Ctrl: Ctrl+Z undo, Ctrl+S save...). Rebind Drop to K in the Keys window: the hint says K.
2. Open the ruler (M), Faces mode (K), box select (Ctrl+B), the browser (B), Place armed (N + a pick), a chain drawing:
   each shows its own hints.
3. **Quick sets**: select three objects, Ctrl+Alt+1; select scenery, Ctrl+Alt+2; Alt+Shift+1 selects the objects again,
   Alt+Shift+2 the scenery. The Outliner's Groups show "Set 1", "Set 2". Toolbar Sets: counts, Store / Select / + / -.
   Ctrl+Z undoes a store. Save, reload the EDITED level: the sets are back. Ctrl+1 / 1 (bookmarks) and Alt+1..4 (view
   modes) still work.
4. **Pie**: hold Q with nothing selected, move the mouse toward "Add (browser)", let go: the browser opens. Tap Q: the
   pie stays; click a slice. Esc / right click closes it. With a selection: Q > Rotate switches the tool; Q > Drop drops
   it; Q > Delete; Q > Duplicate linked / Arrange (UX7). Alt+Q (or Shift+Tab): view modes, collision, triggers, jump
   arcs, Top. Clicks while a pie is up don't select in the view. Right button + Q still flies down (no pie).
5. With `wasd_flies=1` (Preferences): Q flies down (no pie); Tab opens the pie.
6. **Drill-through**: over a stacked spot (TR's floor with pieces under it), hold Alt and roll the wheel: the hover
   highlight steps back and forth, the message says "n of m"; release Alt, click: that one is selected. Plain wheel still
   dollies; Alt+drag still orbits (and its wheel the radius).
7. **Follow**: select a bench, Shift+F: framed; fly away with the right button (W), then nudge it with the arrows: the
   camera moves along with it, offset kept. Inspector: type a new X: the camera follows. Ctrl+Z: follows back. Shift+F
   again: stops. Select something else while following: no jump.
8. **Teleport**: nothing selected, double-click a far floor / the ground: a short glide there, looking the same way.
   Double-click on an object: it's selected (no teleport). With something selected, a double click on the floor
   deselects then glides. Search "Teleport" runs it at the cursor.
9. **Walk**: Shift+`: the camera drops to skater height; W A S D walk, Shift runs, Space jumps, the mouse looks; walk into
   a wall (stops, slides along at an angle), up a kerb / stairs, off a ledge (falls), jump under a low ceiling. Shift+`
   or Esc: back to flying where you are (Esc doesn't leave the editor). The top of the view says WALKING. F5 while
   walking still plays from here.
10. **Controller mode**: Preferences > Controller mode (ini `[editor] pad=1`). Left stick flies, right stick looks, LT /
    RT down / up, L3 faster; a crosshair in the view's middle; A selects what's under it; hold A on an object and fly:
    it's carried; B cancels / deselects (never leaves); X pie / Y view pie with the left stick, let go of X or press A;
    D-pad nudges, LB / RB turn, R3 frames, Back undoes; the status bar shows the controller's glyphs. Move the mouse: the
    cursor is the mouse again. Start leaves the editor.
11. While walking with a controller: left stick walks, right stick looks, A jumps, B stops.
12. Controller: check nothing in the paused game reacts to A / B / X / Y while the editor is up (the log, the screen),
    and that Simulate (F6) stops when you press A / D-pad / LB / RB / Back.
13. Simulate (F6) running: a pie's Drop / Delete / Duplicate stops it first; a view-pie slice doesn't.
