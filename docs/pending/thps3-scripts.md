# thps3-scripts - THPS3 levels with their scripts, peds, vehicles and moving objects

Agent "thps3-scripts" (branch worktree-agent-a35caaabc2d08ff6a). Follows "thps3" (docs/pending/thps3.md: the levels'
geometry, collision, rails, kill zones) and its WIP commits a5044ab / e196d20 (tools/level_port/script_port.py,
vehicles as LevelObjects, peds as THUG2 ped AI, setup scripts).

## What a converted THPS3 level brings along now

`python -I tools/thps3_levels/thps3_levels.py convert` (then `tools/menu_level/make_menu_level.py`). Per level, in the
level's .qb:

- **Scripts**: every node's trigger script (sectors and rails: run by the skater; game objects, vehicles, proximity
  nodes: run by them) and the start scripts (Load_<lvl>'s <lvl>_Startup..., the Player1 restart's trigger script) with
  everything they call / spawn / hand out as exception, gap or callback scripts, translated to THUG2 tokens
  (script_port.py) - statements THUG2 can't run left out one by one (counts in the convert report). The start scripts
  run from `thps3_<lvl>_setup` (the level structure's setup_script).
- **Peds** (THPS3's skinned peds and scripts can't run): THUG2's ped AI at the same places - walking the waypoints they
  link to, or standing; women (THPS3 pedfem_* / girl / Steamer) as THUG2's levels write them (Ped_F_From_Profile,
  Ped_Female skeleton and animations - before, they had THPS6_human's); THPS3's `Pedestrian_Pro` class too. There at
  the start as in THPS3 (CreatedAtStart), the others when the level's scripts create them (Airport's areas create
  their peds - `Create prefix="TRG_Ped_StartArea_"`).
- **Crowds** (THPS3 AnimatingObjects Crowd_* / Crowd_Judge_*: Rio, Skater Island, Tokyo, Burnside): THUG2 standing
  peds, a mix of men and women (judges men).
- **Animals**: THPS3's resting birds (Bird_A_Resting: Canada, Suburbia; Bird_B_Resting: Skater Island) as THUG2's
  pigeons / seagulls (Data\models\animals, placed as THUG2's AU places them), flying up when the skater comes within 15
  feet and back down once he's 20 away (THUG2 AU's AU_Pigeon without its sounds); Skater Island's sharks as THUG2's
  (SE2's SE2_Shark: swimming their linked waypoints). THPS3's flying birds (made by the resting ones' scripts) are
  left out. Every level has a `load_level_anims` (THUG2's load_level runs it, as every THUG2 level has one):
  THPS6_human, Ped_Female and the animals' animations.
- **Cars**: THPS3's traffic (vehicles whose trigger script calls Car_Generic01: Rio, LA, Burnside) as THUG2's level
  cars (Class=Vehicle, a THUG2 car model like THPS3's - police -> veh_cop, vans / buses -> veh_van, pickups ->
  veh_rust, the rest one of sedan / galaxie / chevy_impala / custom - skeletonName=car, THUG2's Car_Generic01 NoSound
  along the same waypoints). Only the ones there at the start (THPS3's spare cars are made by its car scripts).
  **Fixed a crash**: before, they were LevelObjects running THPS3's trigger script, which calls THUG2's Car_Generic01
  - its member functions on an object without those components crashed THPS3 Burnside on load (null component in
  CallMemberFunction, in the game).
- **Other vehicles** (Tokyo's bullet trains and planes, Cruise Ship's boats, Canada's snowmobiles, Airport's karts and
  planes, Suburbia's ice cream truck / plane...): LevelObjects drawn with their THPS3 model (Data\Models .dff),
  moved by their translated scripts.
- **Moving game objects**: GameObjects / BouncyObjects with a clump of their name in the level's .dff (fans, presses,
  valves, doors, cones...), or with `model=` another clump of the level (Cruise Ship's sailboats, deck chairs: 60) or a
  rigid Data\Models model (LA's police cars: 14) - LevelObjects with a copy of the geometry, moved by their scripts.
  Script-only GameObjects (controllers) are ghosts. THPS3's goal pickups (letters, stat points, tapes, deck icons)
  are left out.
- **Proximity nodes** (THPS3's ProximNode: a radius round a node; its script runs with `inside` when the camera comes
  within it and without when it leaves - Airport's escalators and LOD swaps, Suburbia's / Tokyo's area visibility,
  Canada's / Skater Island's birds, Cruise Ship's exhaust): a ghost GameObject of the same name whose own scripts
  (thps3_proxim_<checksum>) watch the radius (THUG2's ObjectInRadius / ObjectOutOfRadius, inches -> feet) and spawn
  the THPS3 script (`params={inside}` on the way in). Only those whose translated script still does something.
- THPS3's scripts relied on a called script getting its caller's parameters (ALF_SUB_Hide_Const_Pool tests `inside`
  passed to the proximity script that calls it); THUG2's don't: calls of brought scripts with no arguments get `<...>`
  (script_port `inherit_params`).

Per level (offline, `convert`): scripts brought along / proximity nodes watching / what's made:

| Level | Scripts | Peds (walk + stand) | Crowds | Animals | THUG2 cars | Vehicles (THPS3 models) | Moving objects | Proximity |
|---|---|---|---|---|---|---|---|---|
| Foundry | 655 | 5 + 18 | - | - | - | - | 44 | - |
| Canada | 299 | 4 + 17 | - | 29 pigeons | - | 5 | 21 | 41 |
| Rio | 735 | 32 | 11 | - | 8 | - | 17 | - |
| Suburbia | 503 | 9 + 7 | - | 6 pigeons | - | 2 | 136 | 34 |
| Airport | 487 | 55 + 5 | - | - | - | 7 | 31 | 13 |
| Skater Island | 307 | 18 + 33 | 10 | 22 seagulls, 4 sharks | - | - | 32 | 32 |
| Los Angeles | 850 | 10 + 5 | - | - | 6 | 34 | 376 | 1 |
| Tokyo | 770 | 62 | 34 | - | - | 23 | 15 | 5 |
| Cruise Ship | 1282 | 18 + 14 | - | - | - | 4 | 310 | 12 |
| Warehouse | 52 | - | - | - | - | - | 3 | - |
| Burnside | 33 | 15 | 8 | - | 4 | - | - | - |
| Roswell | 38 | - | - | - | - | - | 4 | - |
| Tutorials | 58 | 1 | - | - | - | - | - | - |
| Skate shop | 8 | 2 + 1 | - | - | - | 4 | 24 | - |

(Peds / vehicles / objects counted are the nodes made; many are created later by the level's scripts, as in THPS3.)

## Left out, and why

- Statements (most common, all levels): SetParticleSysVisibility (743 - THPS3's particle systems aren't converted),
  LoadSound / PlayStream / Obj_PlayStream / Obj_PlaySound (THPS3's sounds and music aren't converted),
  BouncyObj_Go (191 - THPS3's knock-about physics: the bouncy objects are static LevelObjects, no collision),
  TriggerType (112 - THPS3's `if TriggerType TRIGGER_LIP_ON` before lip gaps: THUG2's TriggerType takes a
  GetTriggerArray array with other codes; left false, so those lip gaps don't score - Roswell, Cruise Ship, SI),
  DuplicateTrigger (71), MakeSkaterGoto / SpawnSkaterScript / PlaySkaterCamAnim (the skater's state machine and
  cutscenes), Obj_ApplyScaling / Obj_PlayAnim (THPS3 clump animations), Obj_VarEQ / VarSet / VarInc (object
  variables: not in THUG2), Obj_SetGroundOffset, LaunchPanelMessage, goals and the tutorials' lesson scripts.
- THPS3's spare cars (Burnside 44, LA 1): made by THPS3's car scripts, not brought along.
- THPS3's flying birds (Canada 55, Suburbia 8, SI 23), skinned game objects (Suburbia 2, LA 1) and the goal pickups.
- No collision on moving objects / vehicles (the skater passes through them), no sounds.

## Shared code changes (tools/level_port/script_port.py - interface kept, additions only)

- `Translator(..., inherit_params=False)`: new keyword; True adds `<...>` to argument-less calls of brought scripts
  (THPS3's semantics). Default off: other games unchanged.
- `Thug2.script_needs(name)`: the components a THUG2 script's member function calls need (transitively through the
  THUG2 scripts it names, not `obj:` calls). The Translator now checks THUG2 scripts like member functions: a call /
  script value of a THUG2 script whose needs the runner's context hasn't is left out ("THUG2 script needing
  components the runner hasn't" in the report). Over-approximates (Car_Generic01 names skater scripts), but only
  Car_Generic01 (on LevelObjects) and VibrateOff (skater context) are affected in THPS3's levels.
- `Translator(..., undrawn=())`: new keyword - geometry nodes with no scene sector; Visible / Invisible / Create /
  Kill / Shatter of them are left out or (prefix=) expanded per drawn node (`without_undrawn`, `SECTOR_COMMANDS`).
  The other converters should pass theirs too (any collision-only sector a script toggles crashes THUG2).
- `does_something(code)`: whether a translated script has a statement left besides if / else.
- (tools/thps3_levels only otherwise.)

## Run in the game (2026-10-18, under the game lock; overlay from this branch, the deployed DLL as found)

Test method (not committed): the installed overlay's SKATE NOW patched to `level_select_change_level level=Load_<lvl>`
(scratch boot_level.py), keys posted to the window; for views of peds / cars the Player1 restart moved next to a node
(scratch test_start.py, single-level converts - the staging was put back with full converts).

- Note: on the first run keys.ps1 was used without KEYS_POST=1 (SendInput) while the game's window wasn't found yet:
  one Enter may have gone to whatever window was in front at the time (that game run then quit). Later runs posted
  the keys to the game's window only.
- Burnside **crashed loading** (Car_Generic01 on LevelObjects, above) - fixed; loads now.
- Airport **crashed loading** in its start scripts: `visible prefix="StartArea"` (CFunc_Visible 0x005A5050 ->
  0x005A3B80 -> null sector at 0x0049E690) reached a collision-only geometry node (no scene sector). Fixed: Visible /
  Invisible / Create / Kill / Shatter of such nodes left out; a prefix= matching one becomes one statement per
  matching node that has a sector (script_port `undrawn`). Loads now.
- All 14 levels loaded and ran ~1 min in the level with no exception in the log (the last build: Airport, Foundry
  rerun; the others with the build before the `undrawn` change, which only removes statements).
- Seen: Skater Island's crowd in the bleachers (THUG2 peds, men and women); a THUG2 car driving Rio's road.
- The game folder was put back as main has it (main's converter + make_menu_level) - this branch's levels are NOT
  installed: after merging, run the full convert and make_menu_level.

## What the user should try (per level)

- Every level: load from the level select (THPS3 section); free skate a minute; peds standing / walking where THPS3
  has them, no crashes.
- Foundry: the press / fans / buckets moving; peds (workers, guards) at their posts.
- Canada: pigeons on the roofs / fences fly up when you skate close and come back; chimney / steam proximity nodes
  (nothing visible - particles aren't converted).
- Rio / LA / Burnside: THUG2 cars driving THPS3's roads (stop for the skater, as THUG2's).
- Suburbia: the areas THPS3 hides / shows by proximity (construction pool, nude bowl, rich side) appear when you get
  close (watch for holes); pigeons.
- Airport: peds in the terminal areas; escalators switch to the moving steps when you get close.
- Skater Island: crowd in the stands, seagulls, sharks swimming in the water.
- Tokyo: the competition crowd; bullet trains and planes moving.
- Cruise Ship: sailboats / ambient boats moving, deck objects, peds (mostly women) walking.
