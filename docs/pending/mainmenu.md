# mainmenu - the THPS4 skate shop as the front end, with a standard main menu

Agent "mainmenu" (branch worktree-agent-a732fe33b1470e35d). The user's request: "I have pressed 'p' in the THPS4
skateshop level. Can you make that level the mainmenu level - we don't need a custom level anymore, since we won't
be reusing the wheel of loogies. Make a standard menu for the mainmenu, which should appear above the skateshop desk."

**NOT run in the game yet**: copying the DLL into the game folder (and so regenerating the overlay for it) was
refused by the session's permission classifier; the lock was released untouched (the game folder still has the old
DLL, ini and overlay). Built, offline self-tests all pass (197), mutations caught.

## What it is now
- `[features] menu_backdrop`: 0 = the original front end (THUG2's skate shop, the wheel). Any other value (default
  1; the user's 3 and the old 2 included) = THPS4's skate shop behind the front end's menus, with a standard
  vertical main menu - once the THPS4 skate shop is installed in the overlay
  (`thug2-decomp-data\levels\thps4_skateshop\thps4_skateshop.scn.xbx`; MenuBackdrop::Installed, logged once).
  Without it nothing changes. `menu_zoom` and `debug_backdrop` are gone (the user's `menu_zoom=79` is ignored).
- The backdrop (src/game/front/menu_backdrop.*, the batch 95 mechanism, now one view): when "mainmenu" loads in the
  front end, LoadScene also loads `thps4_skateshop_sky` and `thps4_skateshop`. While a menu camera plays
  (mainmenu_camera / 02 / 03, SS_MenuCam, level_select_cam: main menu, Game Options and the other no-skater menus,
  network select, level select) the viewport camera is held at the user's P-key view
  {564.790, 74.075, -1114.151} back {0.007642, 0.023744, 0.999689} up {-0.000181, 0.999718, -0.023743} with the
  default field of view, and THUG2's shop scene isn't drawn. Other cameras (Create-A-Skater, skater select, the story
  / create-a-modes menus' camera turning round the skater, CAGR...) show THUG2's shop with the skater as the game
  always did, and THPS4's scenes aren't drawn. THUG2's shop and its objects (skater, wheel, boards) sit ~1,100 units
  behind the held view (camera at z -1114 looking along -z; THUG2's shop is round the origin) - out of sight, so the
  skater and the camera scripts run exactly as the game's.
- The main menu (tools/menu_level/make_menu_level.py MAIN_MENU_SCRIPTS -> overlay
  `front_end\levels\mainmenu\mainmenu_scripts.qb`, read first while MenuBackdrop::Active): create_main_menu as the
  game's but `make_new_roundbar_menu` (menu_id main_menu, vmenu_id main_vmenu, title MAIN MENU, icons) instead of
  make_spin_menu. Items as add_main_menu_items (same conditions, ids, new_menu_script / change_gamemode /
  hold_camera / do_loading_logo parameters, mark_first_input): STORY MODE, CLASSIC MODE, MULTIPLAYER (OnXbox, as
  the game), HIGH SCORE / FREE SKATE, 2 PLAYER, CREATE-A-MODES, GAME OPTIONS, QUIT. Chosen through
  `main_menu_choose` = main_menu_exit without the wheel's spin (skater spin animation, click, turning the menu and
  the wheel): the same flags / second-controller stop / attract-timer kill, a 0.2 s fade (main_menu_loading_screen),
  then the item's script. The last chosen item is focused when the menu comes back (main_menu_list_index, as the
  game's spin_menu_selected_index). Inactive items (active=0) are shown not focusable; the 2 PLAYER item keeps the
  `active` tag the game's enable / disable_two_player_option switch, and main_menu_choose ignores an inactive item.
  The skater, camera02, the wheel's objects (spin_menu_visible), attract mode, second controller check, the CAGR
  layer reset are as the game's. Mouse menus: a roundbar menu, as Game Options (nothing special).
- Placement: `main_menu_list_pos = (195, 100)` (items' top left in the 640 x 480 layout, 250 wide: centred). Worked
  out from THPS4 shop node positions projected through the view (72 degree fov): the glass counter's top (wheels,
  decks, Box01-03 at y ~5-21) lands at screen y ~300-335, the wall racks behind it at y ~170-270 - so the menu fills
  the space above the counter. **To check in the game** and adjust (one number in make_menu_level.py).

## Removed
- src/game/front/menu_level.{h,cpp} (the LA-built front end: the camera move, Obj_Rotate's turn, the zoom keys and
  ScaleMainMenu, hiding the shop scene) and its hooks in render.cpp and motion.cpp (RotateTo is the original again).
- src/game/front/frontend_objects.{h,cpp} + src/hook/selftests_feobj.cpp (moving the wheel and the skater into the
  LA backdrop) and its hooks in CCompositeObject::Update, CCompositeObjectManager::Update, the script's C function
  calls, LoadScene and RenderWorld; the camera setters' pose notes.
- make_menu_level.py: the menu_level folder (LA Levels.qb, moved node array, LA stand-ins, mainmenu_cas.qb), the
  move maths, --view / --lift. The tool keeps its name and still makes everything else.
- The data overlay itself moved to src/game/front/data_overlay.{h,cpp} (Front::DataOverlay::OverlayPath, used by
  pip / file / async); its preferred subfolder is now `front_end` (while MenuBackdrop::Active) instead of
  `menu_level`.

## Tests
- selftests_menulvl.cpp: LoadScene original vs ours at settings 0-2 with the backdrop installed or not (and the
  front end's scene only when off / not installed); with it on (1, 2, 3) the two THPS4 scenes are loaded after it
  (is_net passed on); the view as captured and its matrix (worked out in double); the decisions with "on" = any
  setting but 0; TestDataOverlay (file names; Active = setting != 0 and installed). Motion's RotateTo test no longer
  loops over settings.
- Mutations (tools/mutate.py): Active ignoring the install, LoadScene loading without it, setting 2 off, the scene
  order, the view's z - 5 of 5 caught.
- The overlay was generated into the scratchpad (not the game folder) and the new mainmenu_scripts.qb decompiled
  back: the scripts as written. The game's mainmenu_scripts.qb round-trips byte-identical through NeverScript.

## For the game run (when allowed)
1. Lock; copy out\build\verify\dinput8.dll; `python -I tools\menu_level\make_menu_level.py` (empties and remakes
   thug2-decomp-data: the old `menu_level` folder goes, `front_end` appears); the ini's `menu_backdrop=3` works as is
   (`menu_zoom=79` can be deleted).
2. Check: the main menu over the counter (move main_menu_list_pos if needed); every item (Story -> options menu with
   the skater camera in THUG2's shop; Classic; Free Skate -> skater select; Create-A-Modes -> CAS; Game Options;
   Multiplayer -> network select held view; Quit dialog and No); back from each to the main menu with the focus on
   the item chosen; level select (held view), SKATE SHOP / TEST LEVEL / THPS4 levels load; mouse hover and click;
   menu_backdrop=0 (the wheel back); no exceptions in the log.

## Skate Now, the skater in THPS4's shop, the menu's layout (lead, 2026-10-18 - tested in the game)
- The front end's skater stands in THPS4's shop: the overlay's `front_end\levels\mainmenu\mainmenu.qb` moves
  TRG_Playerrestart / SS_player_restart to (449, 0, -1340) (the floor left of the held view, by the counter). Every
  front-end camera that follows the skater (Create-A-Skater, skater select...) now looks at him there, so the DLL
  never draws THUG2's shop in the front end any more (ShouldHideScene: the front end's scene only, whatever the
  camera) - those menus NOT checked in the game yet.
- The main menu: SKATE NOW first; the list centred over the glass counter, above it (main_menu_list_pos (229, 98),
  220 wide, spacing 2, text scale 0.85); the dark backing sized from the item count with 16 / 9 padding (white2 is
  8 units). The skater's main-menu animation is Idle (no wheel to reach for).
- SKATE NOW (main_menu_skate_now): the menu slides out (generic_menu_animate_out), the camera animation is killed
  (the skater camera follows him), the skater gets main_menu_skater_skate (SkaterInit, then **SwitchToSkatingPhysics
  when walking** - the front end's skater is in walking physics mode, so UnpausePhysics alone resumed the walking
  physics and he pushed on the spot - UnpausePhysics, default friction, input on, OnGroundAI), and root_window's
  pad_start (Esc) runs main_menu_skate_stop: create_main_menu (the skater back at his restart, idle, physics paused).
- mainmenu_startup also loads the shop's collision (`LoadCollision scene="thps4_skateshop"`) and
  `do_load_unloadable` (the skater's gameplay animations: load_level skips them for front-end levels - without them
  he skated in his bind pose).
- Then (the user's request): SKATE NOW puts him off his board, walking (main_menu_skater_skate: a real switch to skating
  physics, then SwitchToWalkingPhysics - the front end's 'walking' mode is an uninitialised default that doesn't move -,
  then JustStoppedSkatingState Restart: the stand pose and the walk camera); he faces the camera (restart yaw 0.474:
  he faces (sin yaw, 0, cos yaw)). Movement is the arrow keys (the user's bindings), not WSAD.
- Tested: the menu over the shop with the skater on the left, facing the camera; SKATE NOW, walking round the shop
  with the board in hand, Esc back. Not yet: the other front-end menus with the skater in THPS4's shop, tricks / rails in the shop.
