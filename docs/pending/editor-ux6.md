# Level editor UX 6: skater trails, gaps validated by skating, play from a restart, Simulate

Branch `worktree-agent-aba226e953b0369b2`. **NOT run in the game yet** (no game runs, as asked): built and checked
offline only - the checklist at the end is for the user. The UX research's #3 (trail), #19 (validate by skating), #20
(Simulate in Editor) and section 3's #6 (test from this restart) - docs/editor-ux-research.md.

## What

All in new files: `src/game/front/level_editor_playtest.{h,cpp}` (pure parts + the game side) and
`level_editor_playtest_ui.cpp` (ImGui only); self-tests in `src/hook/selftests_editor_ux6.cpp`.

### 1. Skater trail (Trackmania's build - drive - look)
- **Recorded** while the level is its edited copy (`Layout::EditingCopy`), the editor is closed and the game isn't paused
  - after any way out of the editor (Escape, F5's play from here, a restart's Play from here, the level select's
  EDITED entry). A run ends when the editor opens again or another level loads. Never in a network session or a refused
  mode (`Layout::Refused`, looked at once a second). Nothing of the game is hooked: it's read once a frame from
  `MouseMenus::Update` (beside the restart tester and UX1's play key).
- Each frame: the local skater's position (`m_pos`) and its **motion**, thinned (`Recorder::Feed`: a point kept when
  the motion changes - with the frame before, so each stretch keeps its colour -, after 150 units, or after 25 units and
  a turn over 8 degrees; a move over 600 units in a frame is a jump - a restart / teleport - not linked; 40000 points a
  run at most).
- **The state flags used** (THUG2PM.exe, found from the components' script member function tables - our reading, not
  decompiled into src/game yet):
  - **SkaterState** (component 0x43686585, vtable 0x0064C140, member functions table 0x00684A80 filled by the static
    initialiser at 0x00644340): **+0x18 the physics state** - OnGround (0x00525E90) `== 0`, InAir (0x00519480) `== 1`,
    OnWall (0x00519490) `== 2`, OnLip (0x005194A0) `== 3`, OnRail (0x005194B0) `== 4`, InWallplant (0x005194C0) `== 5`
    (core_physics.cpp's `state == 2 || state == 5` in DoAirPhysics agrees, and the component's reset 0x00519460 sets 1);
    **+0x70 InBailState** (0x005196D0); Walking (0x005194D0) = `+0x118 == 0 && +0x1C == 1`; Driving (0x00519510) =
    `+0x118 != 0`.
  - **SkaterBalanceTrick** (0x589E5FB3) **+0x18** the balance trick's type (manual.h): Manual 0xEF24413B / NoseManual
    0x0AC90769 make a manual (on the ground); cleared to 0 by its stop 0x004F8EE0 and reset 0x004F8DE0.
  - **SkaterScore** (0xD262802F) +0x18 the score: `+0x14 * +0x60` the combo's points pending (as trick_goals.cpp's
    0x00576520 reads it).
  - Motions: bail (first), drive, walk, then air / wall (wall ride or plant) / lip / grind (OnRail) / manual / ground.
- **Gaps**: the skate module's gap checklist (0x005656C0, gap_checklist.h) read each frame - a gap's `m_pending` going up
  is a gap got (GotGap 0x00578C70: a marker where the skater was), `m_count` going up is landed (AwardPendingGaps
  0x005796C0: the marker turns green, the gap is marked landed for the level), pending back to 0 with no landing is lost
  (ClearPending 0x00578CF0 - a bail: red).
- **Combos**: the pending points dropping to nothing is a combo's end - landed unless the skater bails within 12 frames
  (or is bailing then) - a "+points" label (lost ones "lost n"). **Bails**: a red cross where InBailState came on.
- **Drawn** in the editor (UI only - the old HUD doesn't draw them) through the overlay: lines coloured by motion (ground
  grey, air blue, grind orange, lip pink, wall violet, manual green, bail red, walk olive, drive cyan), the newest run
  thicker and opaque, older ones fainter; within 20000 units; "Run n" at each run's start; gap markers with their names
  (within 5000), combo labels nearer.
- **The Trail window** (F7, the toolbar's **Trail (F7)**): record on / off, show / hide all, the colour legend, the runs
  (newest first: show / hide each, run number, time, gaps landed, bails, best combo - a tooltip with seconds per motion;
  x deletes), Clear, **Save** (`thug2-decomp-layouts\<scene>.trails.txt`), Load. Kept per level (its main scene's
  checksum) for the session, the last 12 runs; the file is read when the editor first opens in that level this run and
  has no runs in memory. `[editor] trail_record=0` starts with recording off.
- File format (`Format` / `Parse`, self-tested): header `thug2-decomp trails 1`, `gap <name>` (landed), per run
  `run <n> <seconds> <visible>`, `s x y z <motion> <jump>`, `e <gap|bail|combo> x y z <value> <pending|landed|lost>
  <name>`.

### 2. Gaps validated by skating
- The Trail window lists the level's gaps (the gap list's: the level's own and the editor's), "landed" (green) or
  "untested" (orange), with **Untested only** and a "landed k / n" count.
- The **Triggers window** (T) marks the editor's gap triggers `- landed` / `- untested` in its list; a gap trigger's
  Inspector shows "Landed in a test run" or "Untested: not landed in a test run yet". (A Changes panel / problem checker
  is UX5's; it can call `PlayTest::GapLanded(name)`.)
- "Landed" means landed during a recorded run of this session (or one read back from the trails file) - not the career's
  landings.

### 3. Play from this restart
- A restart (Spawn family) node's Inspector has **Play from here**: `Play::Request()` (UX1's F5 path - the editor closes,
  the pause menu it brings back closed), then once the game runs (4+ frames unpaused) `ResetSkaters node_name=<restart>`
  as F8's restart tester does, and a panel message "Playing from <restart> - F5: back to the editor". F5 / Ctrl+P comes
  back with UX1's camera and selection. Gives up (logged) if the game isn't back within 300 frames, or the editor didn't
  close within 15 s.

### 4. Simulate (Unreal's Simulate in Editor) - F6, the toolbar's **Simulate (F6)** / **Stop (F6)**, the Trail window
- Start: every composite object but skaters and cameras noted (`Objects::Save`: place, matrices, scale; and its
  velocity), then `FrontEnd::PauseGame(false)` (what CONTINUE does: scripts, objects, devices, the logic tasks) and the
  skaters paused again through their own vtable +0x2C (`CSkater::Pause`: COMPOSITE_FLAG_PAUSED - frozen; still hidden
  by the editor). The editor's camera, picking and panels go on; a banner "SIMULATING - F6 stops" shows over the view.
- Stop (F6 again, the toolbar, or automatically): `PauseGame(true)` - the game paused as the editor keeps it - and
  **reset by default**: every object noted put back (`Objects::Restore` + velocity); objects gone meanwhile are skipped;
  objects the editor moved during the simulation keep the editor's change. **Why reset**: Unreal's default, and the
  editor's layout records / undo steps hold objects' positions - leaving peds and moving objects where the simulation
  took them would make "moved" positions out of nothing. **Limit**: only place / orientation / velocity come back - a
  ped's or car's path progress and object scripts' waits carry on (a ped walks from its old place towards the waypoint it
  was heading to). The Trail window's **Keep where they got to** (Unreal's K) leaves them instead.
- Stops by itself (the edit is then made on the world as it was): a left click in the view; any key that isn't the
  camera's / view's / a window's (`SimulateKeyKeeps`: Ctrl+S / Z / Y / D, Delete, G, the arrow keys' nudges, Escape, R,
  Alt+G ...; W A S D Q E Space keep it running only when they fly - the right button held, or the default
  wasd_flies=0 scheme); leaving the editor (F5, Escape, Start); a network session. The keys are watched by an SDL event
  handler registered on the first Simulate (`Platform::RegisterEventHandler`). Panel edits (Inspector fields) don't stop
  it.
- Refused when the game isn't paused (it always is in the editor) or `Layout::Refused`.

## Keys bound (for the lead's clash check)

| Key | Where | Does |
|---|---|---|
| F6 | editor (not dragging) | Simulate on / off |
| F7 | editor (the UI) | the Trail window |

(In play nothing new: F5 / Ctrl+P are UX1's, F8 the restart tester's.) Both are registered in UX3's keymap
(`level_editor_actions.h`: `kSimulate` "simulate" F6, `kTrailWindow` "trail_window" F7 - appended to the built-ins,
rebindable in [editor_keys], in the command search; UX3's clash self-test passes). Help (F1) has both rows. Simulate's
"keys that keep it running" (`SimulateKeyKeeps`) are by scan code, not through the keymap: a rebound camera key may stop it.

## Shared files touched (small, additive)
- `src/game/front/level_editor.cpp`: include; `PlayTest::OnEditorStart()` after `Camera::Start` in CFunc_Start;
  `PlayTest::OnEditorStop()` first in CFunc_Stop; `PlayTest::Tick(g_dragging)` after the play / preferences actions in Update; `PlayTest::AddOverlay(...)` after `Entities::AddOverlay` in Draw.
- `src/game/front/level_editor_ui.cpp`: include; `PlayTest::ToolbarButtons()` after Prefs; `PlayTest::RestartSection`
  before `SpawnSection`; `PlayTest::Window` after the tips window; two help rows (F7, F6).
- `src/game/front/level_editor_triggers_ui.cpp`: include; a gap trigger's row note and its Inspector line.
- `src/game/front/mouse_menus.cpp`: include; `PlayTest::Update(frontEnd)` after `Play::Update`.
- `src/game/front/level_editor.h`: the module list's line.
- `src/game/front/level_editor_actions.{h,cpp}`: `kSimulate`, `kTrailWindow` appended (Id and the built-in table).
- `CMakeLists.txt`: two sources, one self-test file.

## Checks (offline)
- Clean build (`out/build/verify`, own `_MSPDBSRV_ENDPOINT_` thug2ux6), no warnings in the new files.
- `thug2_check.exe` on THUG2PM.exe and on the stock `C:\Games\THUG2\Game\THUG2.exe` (read-only): 0 MISMATCHED, all early
  self-tests pass (480 after merging main 1b3384b), six new: `TestEditorTrailClassify` (the six physics states, manual only on the ground with a
  manual's trick, bail / drive / walk order, names), `TestEditorTrailRecorder` (150-unit thinning, a motion change keeps
  its stretch's last frame, jumps unlinked, turns kept, the cap), `TestEditorTrailGaps` (learn, got, landed, lost, got +
  landed between reads, a new gap isn't a landing, Forget), `TestEditorTrailCombo` (settled landing, bail while settling,
  bail at the drop), `TestEditorTrailFile` (round trip, another file refused, bad lines skipped, run cap, file name,
  counts), `TestEditorSimulateKeys` (what keeps it running / stops it).
- Mutations (`node tools/mutate.ts out/scripts/mut_ux6.ts`, 20): all caught (wall plant, nose manual, bail order, a
  stretch's last frame, jump mark, turn, step, cap, got+landed, lost, a new gap's landings - after strengthening its test -,
  settle, bail at the drop, visible, names with spaces, run cap, fly keys, Ctrl keys, file name, header).
- `node tools/check_code_writes.ts`: no overlaps (nothing written into the exe).

## In-game checklist (for the user - NOT run)
Start with `node tools/game/edit_level.ts TR` (a workspace with this branch's DLL). Watch `thug2-decomp.log`
(`level editor:` lines - `trail`, `simulate`, `play from the restart`; `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`).
1. **A trail**: in the editor press F5, skate a line - ollie, grind a rail, manual, bail once - then F5 back. The log has
   `trail - recording a run` then `trail run 1 recorded (the editor opened): ...`. Over the view: a line along where you
   went - grey on the ground, blue in the air, orange on the rail, green in the manual, red at the bail with a red cross,
   "Run 1" at its start. Check the colours change where they should (takeoff, rail on / off).
2. **F7 / Trail (F7)**: the window lists Run 1 (time, gaps, bails 1, best combo); hover the run number - seconds per
   motion. Untick it: its line goes. Do two more runs: three listed, the newest thickest; x deletes one; Show unticked
   hides them all.
3. **Gaps**: in a level with gaps (or add one with T > Gap), skate through and land it: a green dot with the gap's name
   where it was got; the log `gap "..." landed for the first time in a test run`. The Trail window's gap list shows it
   "landed", the rest "untested" (Untested only filters). Get a gap and bail before landing: a red dot "(lost)". The
   Triggers window (T): the editor's gap triggers say `- landed` / `- untested`; a gap trigger's Inspector says which.
4. **Combos**: after a landed combo a "+points" label near its end (fly close); a bailed combo "lost n".
5. **Save / Load**: Save, close the game, start again, open the editor in that level: the runs and landed gaps are back
   (`trails read: n runs, k gaps landed`). Clear empties them (the file stays until Save).
6. **Play from this restart**: select a restart (a spawn gizmo or the Outliner's Spawns), Inspector > **Play from here**:
   the editor closes, the skater appears at that restart ("Playing from <name> - F5: back to the editor"); F5 returns
   with the camera where it was. Try an added (editor-made) spawn and a team / CTF one.
7. **Simulate**: in a level with peds / cars (e.g. a level with traffic), press F6: "SIMULATING - F6 stops" banner, peds
   walk, cars drive, moving objects move, particles animate; the skater stays hidden and doesn't move; fly about with
   the right button + W A S D (it keeps running); numpad / bookmarks / F keep it running. F6: everything jumps back to
   where it was when you started (log `simulate off (F6) after n s: put back N objects`). Tick "Keep where they got to"
   in the Trail window, F6 twice: they stay where they got to.
8. **Simulate stops by itself**: start it, click an object in the view - it stops (objects back) and the click selects.
   Start it, press Delete / G / an arrow key / Ctrl+S / Escape - each stops it first. Start it and press F5 - play
   starts normally (objects back first), and the game isn't left unpaused in the editor afterwards.
9. **Simulate edge cases**: during Simulate, does any goal / timer start (career-only things shouldn't - free skate)?
   Does the game's music / sound behave? Does the skater's camera fight the editor's camera (it shouldn't - the editor
   places it each frame)? After stopping, does F5 / Escape leave the editor normally? Check `[selftests]`-free logs for
   `EXCEPTION` while simulating in a ported level (THPS3 / THPS4) with moving objects.
10. **Old HUD** (`editor_ui=0`): F6 works (no banner, a message), F7 says it needs the UI; trails still record.

## Known limits / left
- The trail isn't drawn in the old HUD (lines go through the UI's overlay).
- Simulate's reset is place / orientation / velocity only (see above); objects created during it (projectiles) stay;
  objects killed during it (a breakable shattered) aren't brought back by the reset (the editor's own Break triggers
  come back when the editor opens, as before).
- "Landed" is per session (and the file): no career data is read or written. A gap's name is the key - two gaps of the
  same name count as one.
- Combo points are the pending points as the score has them (base x multiplier) at the frame before they drop - an
  approximation of the score shown on landing.
- The skater state offsets are read from the components' member functions (above) and not yet rewritten into src/game
  (the SkaterState component isn't decompiled); if the user reports wrong colours, check those offsets first.
- No Changes-panel / checker entry (UX5's): `PlayTest::GapLanded` / `GapNote` are there for it.
