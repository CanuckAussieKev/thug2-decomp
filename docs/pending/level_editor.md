# Level editor (phases 1-3) - NOT run in the game yet

Not original: our own feature, added at the user's request. Branch `worktree-agent-ae56b46d5b1633ebf`.

## What was built

- **Pause menu item** `LEVEL EDITOR` (after YOUR OPTIONS, inside `if ! InNetGame`; the park editor's pause menu
  returns before it) shown while the script command `LevelEditor_Available` is true (`[features] level_editor`, default
  1, and `Nx::CurrentLevel() != 0`). It lives in the data overlay: `tools/menu_level/make_menu_level.py` now also writes
  `thug2-decomp-data\scripts\game\menu\gamemenu_pause.qb` (the game's file from `qb_scripts.prx`, round-tripped through
  NeverScript) with the item and two new scripts:
  - `level_editor_enter`: `LevelEditor_Start` (returns false -> nothing happens), then the goal editor's way of taking
    the pause menu down (`current_menu_anchor` and `helper_text_anchor` destroyed, `pause_menu_gradient off`,
    `menu3d_hide`) and of taking the skater out (`PauseSkaters`, `HideSkaterAndMiscSkaterEffects`); the game stays
    paused (no `UnpauseGame`). root_window's pad_start -> `level_editor_exit`, pad_select -> `null_script`.
  - `level_editor_exit`: `LevelEditor_Stop`, `Skater:AddSkaterToWorld`, `create_pause_menu` (its CONTINUE unpauses
    the skaters and restores the start key binding as ever).
  Checked offline: the overlay generated into the scratchpad and decompiled back (`ns -d`) reads as intended.
- **Script commands** `LevelEditor_Available` / `LevelEditor_Start` / `LevelEditor_Stop`
  (`src/game/front/level_editor.cpp`), registered by `Front::LevelEditor::RegisterScriptFunctions` from `Sys::Main`
  right after 0x005A5E70 (which registers the game's 1254-entry table 0x0067F3A0 through our
  `Script::RegisterCFunctions` 0x0046EFD0).
- **Fly camera**: `LevelEditor::PlaceCamera` in `Nx::RenderViewport` (next to MenuLevel::PlaceCamera) - the first
  viewport camera seen after Start has its pose noted (Gfx::Camera m_pos +0x5C, m_matrix +0x6C) and is then given the
  editor's pose every frame; Stop writes the noted pose back. `LevelEditor::Update` at the top of `Nx::RenderWorld`
  (runs while paused; frame time from SDL's performance counter, clamped to 0.1 s): W/A/S/D along the view / its right,
  Q/E (or Ctrl/Space) world down/up, Shift x4; right mouse button held = mouse look (SDL relative mode); the wheel
  changes the speed (x1.25 a notch, 50-20000 units/s, default 800) when it isn't turning an object. No collision.
- **Picking**: walks `Obj::CompositeObjectManager()` (0x006F0FB8) with our `CBaseManager::ForEach`. Candidates: not
  hidden / being destroyed, no skater physics component (0xD9151D4D - skips the skater), a model component
  (0x286A8D26) whose model (+0x208) has flags +0x44 bit 2 on and bit 4 off (the condition the model component's
  Teleport 0x004219F0 places a model under - others wouldn't follow a move), its geometry 0 (0x00493CD0) and that
  geometry's bounding box (0x0048DE80 -> geom vtable +0x3C), scaled by the model's +0x1C..+0x24 when +0x44 bit 8 is on
  (0x00493090 draws it so). The box is placed with the object's display matrix rows (+0xC0) and position (+0x4C) - the
  model's world matrix as 0x004215F0 builds it. Mouse ray: window pixel -> back buffer (MouseMenus::CurrentMapping) ->
  the 3D viewport's normalised coordinates -> `-back + x tanX right + y tanY up`, tanX = tan(angle / 2),
  tanY = tanX / (aspect * 0.90909094) - the same angle (Camera::GetAdjustedHFOV) and aspect (0x004A0D20) RenderViewport
  passes to Nx::SetCamera, noted by `LevelEditor::NoteView` after the first SetCamera there.
- **Highlight**: `LevelEditor::Draw` after `Draw2DWithBlackBars` in RenderWorld: the box's 12 edges, clipped to the near
  plane and projected with the same lens, as a pre-transformed line list (FVF 0x144 through the renderer's cached
  SetTexture 0x004DA730 / SetVertexFormat 0x004D9DB0 / SetVertexShader 0x004DAA80; texture stage 0 colour/alpha op set
  to "select diffuse" and put back). Hovered: yellow; selected: cyan. No on-screen label (debug_editor logs the name).
- **Moving**: left click on the hovered object selects it and starts a drag on the horizontal plane through it (the
  grab offset kept); Shift (also toggled mid-drag) = up/down only, on an upright plane facing the camera. Wheel over the
  selected object (or while dragging) turns it 15 degrees a notch about the world's up (m_matrix +0x6C and
  m_displayMatrix +0xC0 rows). A move writes m_pos and m_oldPos then `CCompositeObject::SetTeleported(true)`
  (0x0045E4C0: every component's Teleport - the model and collision follow at once, as FrontEndObjects does). Click on
  nothing deselects. Nothing is saved.
- **Leaving**: Escape (the editor's SDL handler; level_editor_exit is run with `Script::RunScript` from the front end's
  input logic, not from the event pump or the render), Enter (the keyboard's Pause binding) or a controller's Start
  (root_window pad_start -> level_editor_exit).
- **Stand-downs while active** (each a "Not original" hook with its original behaviour noted):
  `Sio::PollKeyboard` gives the game only the Pause key, and only once it has been up since Start (the Enter that chose
  the item would otherwise leave at once); `MouseMenus::Update` drops the mouse's clicks / wheel and runs
  `LevelEditor::InputLogic` instead (FrontEnd::InputLogic now calls MouseMenus::Update whatever the mouse_menus setting
  - Update checks it itself). The editor shows the OS cursor on Start and restores its state on Stop.
- Settings: `[features] level_editor` (default 1), `debug_editor=1` (logs the camera taken over, the view, hover changes
  with each object's id / name / type / position / flags / box, selections, moves from -> to, turns, speed changes).
  CLAUDE.md's features list updated.
- Scenery later: picking is one function (`Pick` in level_editor.cpp) over composite objects; scene sectors would be
  another candidate list tested the same way (their world boxes), a hit remembered as a sector.

## Tests (`src/hook/selftests_editor.cpp`, Early)

- TestEditorFly: the frame for yaw/pitch (right-handed: right x up = back; right level; up upwards), yaw/pitch back
  from a frame, W/S/A/D/Q/E directions and diagonal speed, mouse look directions and the +-89 degree clamp, turning
  about up.
- TestEditorUnproject: for 4 poses x 4 angles x 3 aspects x 5 screen points x 3 depths, a point on our mouse ray put
  through D3DX's look-at (0x005C0985) and perspective (0x005C0ACD) built exactly as Nx::SetCamera builds them lands on
  the same normalised coordinates (so the ray matches the game's projection, handedness and widescreen aspect
  included); our Project agrees; pixels <-> normalised coordinates round trip.
- TestEditorRays: ray vs sphere / box (hit, miss, behind, inside, a turned and scaled box), box corners, the drag plane
  (hit, parallel, behind), world- vs model-space box test, near-plane clipping, the Pause key gate.
- TestMain (selftests_misc.cpp) stands in for RegisterScriptFunctions (ours only, not in the original's trace).
- tools/mutate.py: 14 mutations of level_editor.cpp's maths, all caught (see below).

## What to check in the game

1. Run `python -I tools/menu_level/make_menu_level.py` (writes `C:\Games\THUG2\thug2-decomp-data`), set
   `debug_editor=1`. In a level, pause: LEVEL EDITOR after YOUR OPTIONS. Choose it with Enter and with a mouse click:
   the menu goes, the skater disappears, the view stays where the game camera was and does not jump out at once.
2. Fly: W/A/S/D/Q/E/Shift, right mouse look (left/right not mirrored, up looks up). Wheel without an object: speed.
3. Hover objects (benches, cars, peds, level objects): the yellow box should sit on the object. If the boxes are offset
   or huge, check the log's "box" values: the geometry's box may be in world space for some objects (a heuristic picks
   world space when the box holds the object's position but not the origin) or in another unit.
4. Click-drag: the object follows the cursor on the ground plane; Shift moves it up/down; wheel turns it. Its collision
   should move with it (skate into it after leaving). Click empty space: deselects.
5. Leave with Escape, with Enter and with a pad's Start: the pause menu returns; CONTINUE: skater back where he was,
   game runs, moved objects stay moved.
6. `level_editor=0`: no item.

## Known risks

- Not run in the game. The geometry box (geom vtable +0x3C) is assumed to be in the model's space (element3d uses it
  so for front-end models); the model flags test (+0x44 bits 2/4) is inferred from 0x004219F0 and may hide objects
  that should be pickable (debug_editor's hover log shows what is found).
- Objects whose scripts / components put them back when the game unpauses (peds on paths, cars on rails, the wheel of
  the front end) will move back; suspended objects (far away when paused) stay as they were - the suspend component's
  distance pass doesn't run while paused.
- The 2D state the lines draw with: Z test / blend states are left as the 2D pass leaves them (the black bars draw the
  same way); if the lines don't show, the render states need setting.
- Mouse look uses SDL relative mode; the cursor's position after it is SDL's choice.
- Escape runs level_editor_exit from MouseMenus::Update (the front end's input logic). If that isn't called while
  paused in some mode, Escape does nothing (Enter / Start still work through root_window's pad_start).
- The HUD is already hidden by the pause menu; goal panels / messages that appear while paused might show.
- The overlay's gamemenu_pause.qb replaces the game's for every mode; a changed file in a future game data version
  would make the tool stop (it checks the YOUR OPTIONS line is there once).
