# thps4-scripts - THPS4 levels' scripts, peds, vehicles and moving objects in free skate

Agent "thps4-scripts". The user: "update the importers, such that THPS3, THPS4, THUG1, and THAW, all import scripts and
peds, vehicles, etc into free skate." This is THPS4's part (tools/thps4_levels).

## How

- `tools/thps4_levels/thps4_scripts.py` (new): the node array made THUG2's with its scripts, used by
  `thps4_levels.py convert` for every level (`--no-scripts` gives the old static levels).
  - LevelObject -> THUG2 LevelObject (THPS4's LevelObject sectors are in the object's own space, as THUG2's - before,
    they became static LevelGeometry, i.e. drawn at the world origin); CollisionMode=Geometry only when the collision
    has an object of its name.
  - GameObject Type=Ghost -> THUG2 ghost (model="none"); GameObjects with models (goal money, letters, icons, flags,
    tapes, combo letters) left out.
  - Vehicle: Car_Generic01 cars -> THUG2's own cars (peds_cars.py, as the skate shop's); the others (trams, coasters,
    ferris cars, teacups, trains, cranes, buses, floats...) -> LevelObjects of their THPS4 model
    (`VehicleModels`: Game\data\models\<model>mdl.dat / tex.dat / col.dat; the sectors merged into one sector of the
    node's name in the level's scene, materials / textures added, the collision objects merged into one of the
    node's name). Skinned models (cars / trams with a skeleton: shadow, body, 4 wheels) keep only the body, turned
    Z-up -> Y-up (x, z, -y): their wheels are missing (bones not converted) and the orientation is a guess.
  - Pedestrian: walkers (Obj_FollowPathLinked) -> THUG2 ped AI walking (peds_cars.py, now every level); the others
    -> standing THUG2 peds (Ped_InitAI stand, a profile from the THPS4 type: pros, guards, workers, women...);
    animals (Anl_*) left out (also kept out of peds_cars.py's walkers: Zoo / Carnival).
  - Waypoint kept (the objects' paths), except the paths peds_cars.py remakes as PedAI waypoints.
  - ProximNode left out (THUG2's are scene boxes - Shape=BoundingBox - not THPS4's radius spheres); their trigger
    scripts run once at the start (as the THPS3 port does).
  - ParticleEmitter left out (THPS4's script-driven emitters).
  - Scripts: script_port.Translator over THPS4's scripts (sources: the level's .qb and _scripts.qb, the designers'
    AJC / ALF / BDJ / CJR / CPF / JKU / JS / SK4_Scripts). Roots: sector / rail trigger scripts (run by the skater),
    LevelObject / ghost / waypoint trigger scripts (the object), the level structure's setup_script and
    startup_script and the proximity nodes' scripts (spawned from a `thps4_<lvl>_setup` the level structure names).
    Left out besides script_port's list: goal manager calls, panels, screen elements (goal arrows, minigame
    prompts), SetFlag / UnSetFlag, setup_ped_speech.
- `tools/thps4_levels/thps4_levels.py`: reads the level's scene / textures / collision first (read_geometry: the
  vehicles' models are added before the geometry is converted), PEDS_CARS = every level, `--no-scripts`,
  strip_missing_trick_objects on the node array (8 levels had TrickObject nodes whose sector convert_scene leaves out).

## Shared-code changes (small, additive, defaults unchanged)

- `tools/level_port/script_port.py`:
  - Translator(..., left_out=(), renames=None, objects=None, components=None, full=False, by_name=None): more
    LEFT_OUT / RENAMES / SAFE_COMPONENTS / BY_NAME per caller; `objects` = {node: (context, created at start)} turns
    on `object:command` statements (token 0x42: the command checked as run by that object; left out when the object
    isn't converted, or isn't there at the start and no kept Create makes it - THUG2 runs a missing object's command
    on the skater); `full` turns on `name = value` statements (kept; a script value brought along) and `not`
    conditions (0x39: the condition after it checked; left out -> the if is false). THPS3 could use `full=True` too
    (its `if not X` conditions go through unchecked today, and its `x = y` statements are left out).
  - switch / case / default / endswitch (THPS4's 0x3C / 0x3E / 0x3F / 0x3D carry no jumps): THUG2's `3E 49 <next
    case>`, `3F 49 <endswitch>`, `49 <endswitch>` before each later case - only taken when a script has a switch
    (THPS3 has none).
  - globals_ns skips values with 'local strings' (NeverScript can't compile them: THPS4's trick structures).
- `tools/menu_level/make_menu_level.py`: front_end_peds_cars takes the trailing run of peds / vehicles / waypoints
  (the shop's own waypoints now come earlier) whose Links stay inside it; front_end_rails drops the rails'
  TriggerScript (the shop's gap scripts aren't in the front end's .qb).

## Per level (convert report)

| level | level objects | vehicles (models) | THUG2 cars / walkers | standing peds | ghosts | waypoints | scripts brought |
|---|---|---|---|---|---|---|---|
| Alcatraz | 61 | 10 | 0 / 0 | 110 | 34 | 228 | 673 |
| Carnival | 50 | 29 (4 without a model) | 0 / 0 | 59 | 43 | 271 | 449 |
| Chicago | 9 | 10 | 2 / 0 | 38 | 7 | 70 | 331 |
| Shipyard | 104 | 38 | 0 / 0 | 57 | 37 | 271 | 495 |
| Kona | 30 | 0 | 0 / 0 | 41 | 10 | 39 | 346 |
| London | 131 | 45 (4 without) | 0 / 0 | 51 | 13 | 191 | 525 |
| San Francisco | 56 | 21 | 0 / 0 | 52 | 22 | 148 | 511 |
| College | 78 | 19 | 1 / 1 | 121 | 16 | 367 | 588 |
| Zoo | 38 | 11 | 0 / 1 | 58 | 28 | 351 | 419 |
| Skate shop | 9 | 0 | 2 / 5 (+ 4 pros skating, 8 standing) | 6 | 15 | 292 | 18 |
| Motocross | 0 | 0 | 0 / 0 | 1 | 1 | 0 | 4 |

(Standing peds include goal peds that are never created in free skate.) Most-left-out statements: LoadSound / streams
(THPS4's sounds aren't converted), Printf, panels, goal manager calls, MakeSkaterGoto, Obj_Var* (THPS4's object
variables: THUG2 has tags instead), Obj_LockToObject (THPS4's coaster / train cars locked to their heads: they stay
put), DuplicateTrigger.

Offline checks: every converted .qb's jumps land on tokens and NeverScript decompiles each; all four converters'
selfchecks pass (THPS4 139, THPS3 323, THUG1 333, THAW 735 files); THPS3 Foundry still converts.

## Game runs

NOT run in the game yet.

## For the user to try (per level, free skate)

- Every level: it loads; gaps fire (`if TriggerType`... - their names / scores show); no crash when skating around.
- Alcatraz: the trams (Veh_AlcTram + trailer) and the dozer move along their paths; the pool bar / cellblock doors.
- Carnival: ferris wheel cars, teacups, the gravitron, coasters (heads move; bodies don't follow: Obj_LockToObject).
- Shipyard: cranes, trains, the island bridge and the buoy.
- London: buses, taxis, the elephant truck; Chicago: the El train, the two THUG2 cars; San Francisco: the tram.
- College: the floats (parade), the garbage truck, the walker and the car.
- Zoo: the zoo tram; Kona: its level objects.
- Skate shop (and the front end after SKATE NOW): as before - pros on the halfpipe, walkers, cars.
- Look for: vehicles turned the wrong way or without wheels (skinned models: body only), objects at wrong places.
