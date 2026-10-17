# menu_backdrop: another level behind the main menu (branch wt/menulvl) - NOT run in the game yet

At the user's request: the front end shows a place in Los Angeles (level 15) instead of the skate shop, the camera
held at a view the user picked with /where. Setting: thug2-decomp.ini `[features] menu_backdrop = 0` (the original
skate shop, nothing changes), `1` (view 1, the default), `2` (view 2). The feature's code is clearly marked "NOT
ORIGINAL - the menu_backdrop feature" (src/game/front/menu_backdrop.*); the game functions it needed are rewritten
(Gfx::Camera, LoadScene) with the feature's calls marked "Not original".

## What level 15 is
`LevelNum_LA = 15` (scripts/game/Levels.qb): **Los Angeles**, structure `level_la`, load script `load_la`
(`load_level level_la`), scene `"LA"`, sky `"LA_Sky"`, collision LAcol, nodes levels\LA\LA.qb, scripts LA_scripts.qb,
startup LA_Startup, goals LA_goals, setup LA_setup, peds LAped.pre, ambient track la_bg.

## How the front end gets its level and camera (findings)
- The front end is the level `level_mainmenu` (`load_mainmenu`; start-up and every return go through
  CheckForInvalidLevels, which turns load_skateshop / load_cas into load_mainmenu). Its `level_number` is
  `LevelNum_Skateshop` = 0: load_level calls `CareerStartLevel level = <level_number>` (0x5AE990), which sets the
  career's level byte (skate module 0x7CE478 -> +0x20 -> +0x630) that Nx::CurrentLevel() reads - so "level 0" means
  "the front end" because the front end's structure says so, independent of which scene is loaded. Its scene is
  "mainmenu" (mainmenuscn / mainmenucol), its nodes mainmenu.qb (the skater's restart trg_playerrestart, lights,
  LevelGeometry / LevelObject sectors of the shop, the UI_bg / options_clouds ghosts), its scripts
  mainmenu_scripts.qb + 12 more QBs (the menus), `frontend_level = 1`, startup `mainmenu_startup`. Dozens of
  scripts test `LevelIs load_mainmenu`, so the front end has to stay that level.
- Loading another level as the front end isn't workable (the menus live in the mainmenu QBs, the skater spins at a
  mainmenu node, scripts test LevelIs load_mainmenu). Replacing only the scene name would leave mainmenu.qb's
  LevelGeometry / LevelObject nodes pointing at sectors that don't exist. So the backdrop is **added**: the
  mainmenu level loads exactly as before, then LA's sky and scene are loaded as extra scenes.
- Scenes: `LoadScene` (0x445130) -> Nx::CEngine::sLoadScene 0x48CCB0(name, texdict, add_super_sectors, is_sky,
  is_dictionary, is_net); a scene registers itself (0x48C560) in the scene list (head 0x6FA7E0, first node
  0x6FA7EC; node +4 = the CScene, +0xC = next) with **its name's checksum at CScene +0x84** and its texture
  dictionary at +0x88. UnloadAllLevelGeometry (0x48C810, from the level cleanup) unloads every scene in that list,
  so the extra ones go with the front end.
- The front end's camera: `skatercam0` (a composite object with CCameraComponent, vtable 0x645E38) plays skater
  camera animations from the skate module's movie manager (+0x368; `PlaySkaterCamAnim` 0x5AD830 ->
  0x57AB60; the playing one's name: 0x57B030 thiscall). The main menu uses `mainmenu_camera02` (targeting the
  spinning skater), the menus without the skater `play_no_skater_cam` = `mainmenu_camera03`, the network menu
  `SS_MenuCam`, the level select `level_select_cam` (skater hidden); Create-A-Skater uses virtual cameras about the
  skater's bones (name 0), the skater select `SS_SkaterChoosing` / skater_select_cam*, Create-A-Graphic `CAGR_cam1`.
  CCameraComponent::Update (0x41B810 / 0x41B8A0) copies the object's position and matrix into its Gfx::Camera with
  the camera's setters SetPos 0x485A70 / SetMatrix 0x485A90 - the only callers of those besides a debug camera at
  0x4A1910-0x4A22A7. The viewport's camera: 0x4A09C0(0). The renderer (RenderViewport) and Element3d read it back.
- Gfx::Camera (vtable 0x6487E0, derived from Obj::CObject, 0xC4 bytes): +0x4C old position, +0x5C position, +0x6C
  matrix (row 2 backward, row 1 up), +0xAC horizontal angle in degrees at 4:3 (the cameras' default is
  Nx::GetScreenAngle = camera_fov), +0xB0 the angle as drawn (widened by GetScreenAngleFactor), +0xB4, +0xB8 12
  and +0xBC 12000 (near / far by their values), +0xC0 1.0. The captured "fov 88.180" is +0xB0 for a 72-degree
  camera on a 16:9 window (2 atan(tan 36 x 4/3) = 88.18), so the view uses the default angle.

## What was changed
- **Gfx::Camera** (src/game/gfx/camera.*, rewritten): constructor 0x485BA0, destructor body 0x4859F0, scalar
  deleting destructor 0x485A20, GetPos 0x485A00 / GetMatrix 0x485A10 (source only, 4 bytes), StoreOldPos 0x485A50,
  SetPos 0x485A70, SetMatrix 0x485A90, UpdateAdjustedHFOV 0x485AA0 (x87), GetAdjustedHFOV 0x485B10, SetHFOV
  0x485B20, CallMemberFunction 0x485B40. SetPos / SetMatrix / SetHFOV end with the feature's HoldCamera.
- **LoadScene** (src/game/gfx/scene.*, rewritten, 0x445130): as the original (including its never-used description
  structure); at the end the feature's OnSceneLoaded.
- **render.cpp** (RenderViewport): the scene struct's name field (+0x84); the three scene loops skip
  `Front::MenuBackdrop::HideScene(name)`; HoldCamera on the viewport's camera before it's read (in case nothing
  set it this frame, e.g. while objects are paused).
- **menu_backdrop** (src/game/front/menu_backdrop.*, the feature): after "mainmenu" (any case) is loaded with the
  setting on, LoadScene is called for "LA_Sky" then "LA" (is_net passed on). "Holding" = setting on, career level
  0, the "LA" scene in the scene list, and the playing skater camera animation is mainmenu_camera,
  mainmenu_camera02, mainmenu_camera03, SS_MenuCam or level_select_cam. While holding, the viewport-0 camera gets
  the view (position w 1; matrix row 2 = back, row 1 = up, row 0 = up x back, normalised; row 3 kept; the
  default field of view) whenever its setters run and before each render, and the "mainmenu" scene isn't drawn.
  In the front end with the backdrop loaded but another camera (CAS, skater select, CAG...), "LA" and "LA_Sky"
  aren't drawn and the camera isn't touched: those screens look as before, in the skate shop.

## What of level 15 runs / is suppressed
Only its geometry and textures: no LA.qb nodes (so no peds, cars, goals, lights, trigger scripts, restarts),
no LA_scripts / startup / setup / goals scripts, no collision, no level sounds or ambient track, no TOD (the
front end turns it off), the front end's own music. Nothing had to be suppressed - none of it is loaded.

## Tests (src/hook/selftests_menulvl.cpp, all Early; offline all 152 pass, 0 MISMATCHED)
- TestCameraMethods: original vs ours, 12 seeds, the setting on but not in play: constructor (distance clipping on /
  off, negative / huge settings), getters, SetPos / StoreOldPos / SetMatrix (its result) / SetHFOV, the x87
  UpdateAdjustedHFOV over 6 angles and 3 divisors and factors (bit-exact), GetAdjustedHFOV, the member function
  (radians, missing - the params pointer's bits -, another name), destructor body, scalar delete 0 / 1; every
  callee stood in (CObject, Mem::Manager::Free, GetFloat, the angle factor / default angle as float stand-ins), the
  camera compared word by word after each step.
- TestLoadScene: original vs ours on a model CStruct (7 names incl. sky ones in odd case x 16 flag combinations x
  setting 0/1/2, the front end's name only with 0), every call recorded (flags, the description structure, the
  CRC, the texture path, the engine load's arguments); with the setting on and "MainMenu": the original's calls,
  then LA_Sky (levels\LA_Sky\LA_Sky.tex, sky, no super sectors) and LA (levels\LA\LA.tex, super sectors),
  is_net passed on, every structure freed.
- TestMenuBackdropViews: worked-out matrices (view 1 row 0 = (-0.777805, 0.000184, -0.628506), row 1 = up, row 2 =
  back; view 2 row 0 = (0.719350, -0.000148, 0.694647)), determinant +1, normalisation of unnormalised input.
- TestMenuBackdropDecisions: the scene name checksums equal Crc("LA" / "LA_Sky" / "mainmenu"), the menu cameras by
  name (and the skater / CAS / CAG ones not), the hold / hide truth tables.
- TestMenuBackdropState: on stand-in game state (skate module, career level byte, scene list, movie manager,
  viewport camera): Holding / HideScene / the setters holding the camera over setting x level 0/15 x backdrop
  loaded or not x camera animation x viewport or other camera; the field of view only updated when it differs.
- Mutations: 54 + 28 deliberate bugs; all caught except `upd_halfangle_double` (computing hFov x pi/360 as a float
  multiply instead of x87 - equivalent: the product of two floats is exact in 53 bits, rounded once either way);
  3 caught after strengthening (destructor's vtable, the divisor, SetHFOV's update). One unrelated flake seen once:
  "net app: queued and streamed messages" failed in one mutation run and passed in 3 clean runs after.
- Register scan clean (Construct: 4 EAX readers, returns this), call audit no mismatch, check_code_writes no
  overlaps, find_gaps: nothing skipped around 0x4859F0-0x485BA0.

## What to check in the game (menu_backdrop=1, then 2, then 0)
- thug2-decomp.log: "menu_backdrop: loading the scenes LA_Sky and LA behind the front end (view 1)" each time the
  front end loads; no crash / "pool" dump (memory: the front end now also holds LA's ~5.5 MB of scene and textures).
- Main menu: behind the spin menu, Los Angeles from the captured view (view 1 looks along (-0.62, -0.18, 0.77), view 2
  along (0.68, -0.18, -0.71)), with LA's sky; no skate shop. The spinning skater is NOT visible (it stays at the shop's
  node; the camera no longer follows it) - that's the design of a held view. Check the field of view looks like it
  did in the level (the view uses the default 72 degrees, widened for the window as in a level).
- Options, network (ACTIONS, LAN/Internet select), memory card, level select: also LA (their backdrop panels and 3D
  signs / level icons in front of it as before).
- Create-A-Skater, the pro skater select, Create-A-Graphic, face mapping: the skate shop as before (LA not drawn).
  Going back to the main menu returns to LA.
- Starting a level (incl. Los Angeles itself), going back to the main menu, net games: levels look as before;
  the front end loads LA again each time.
- menu_backdrop=0: the original skate shop everywhere, nothing loaded extra (no log line).

## What might go wrong
- LA's geometry is shown with **every sector** on: sectors its node array would hide or create later (alternate /
  broken states, goal objects) may show, and moving objects (LevelObject sectors) sit at their rest positions; no
  peds, cars, lights or particles.
- A front-end screen using another camera animation than the five listed shows the skate shop (safe), and the main
  menu's first ~10 frames after loading (before play_no_skater_cam) too. Screens whose camera isn't one of those
  but should be LA: add its name to kMenuCameras.
- If something writes the camera other than through SetPos / SetMatrix (inlined code), Element3d models could be
  placed for the shop camera in that frame; the renderer re-holds the camera before drawing, so the picture is
  right.
- The skater (and the shop's ghost objects near the origin) could appear tiny in the distance in view 2 if LA's
  geometry doesn't hide the origin (~6,300 units away).
- Memory: the front end's heap now also holds LA (out-of-memory would show as the pool dump crash at 0x448386).
- Disable: `[features] menu_backdrop=0`; the rewritten functions: `[replacements] disable=camera.cpp` /
  `scene.cpp` (the feature's hooks live in them and in render.cpp).
