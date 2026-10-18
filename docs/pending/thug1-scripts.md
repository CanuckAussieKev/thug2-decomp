# thug1-scripts - THUG1 levels' scripts, peds, vehicles and objects in free skate

Agent "thug1-scripts". Tools: `tools/thug1_levels/thug1_port.py` (new), `tools/thug1_levels/thug1_levels.py`,
shared `tools/level_port/script_port.py`; how to run: `tools/thug1_levels/README.md`. The earlier THUG1 work (formats,
geometry, kill zones): `docs/pending/thug1.md`.

## Approach

THUG1 is THUG2's direct predecessor: its node arrays use THUG2's classes and keys (Pedestrian with `Ped_InitAI`
trigger scripts and PedAI waypoints, Vehicle path followers with `Car_Generic01`, bouncy LevelObjects with
`contacts=`, ProximNodes, EmitterObjects, ClimbingNodes, ParticleObjects - THUG2's LA has the same node shapes), and its
compiled scripts are THUG2's tokens except for the long-form if / else / switch (no jumps). So instead of rebuilding
objects (as for THPS3 / THPS4) the node array is kept nearly whole and checked against what THUG2 has, and the scripts
are translated by script_port, which got the THUG1 forms (below).

`convert` does it by default; `--static` gives the old output (geometry / rails / restarts / kills only).

## What's ported per level (`convert` of the 16 default levels)

| Level | scripts | peds | vehicles | moving / bouncy objects | game objects | particles | ledges | emitters / proxim |
|---|---|---|---|---|---|---|---|---|
| NJ | 990 | 175 | 24 | 37 / 82 | 147 | 37 | 167 | 29 / 75 |
| NY | 734 | 72 | 32 | 6 / 55 | 110 | 24 | 55 | 25 / 34 |
| FL | 688 | 84 | 30 | 20 / 86 | 40 | 0 | 45 | 19 / 21 |
| SD | 595 | 133 | 11 | 18 / 65 | 33 | 21 | 48 | 28 / 35 |
| HI | 516 | 57 | 15 | 28 / 110 | 40 | 45 | 110 | 25 / 24 |
| VC | 796 | 67 | 14 | 45 / 41 | 186 | 156 | 66 | 24 / 40 |
| SJ | 538 | 40 | 0 | 245 / 30 | 55 | 0 | 67 | 15 / 27 |
| RU | 664 | 153 | 3 | 16 / 5 | 146 | 43 | 81 | 19 / 17 |
| SE | 515 | 33 | 5 | 25 / 78 | 25 | 73 | 238 | 8 / 5 |
| SC | 102 | 0 | 0 | 1 / 0 | 0 | 0 | 0 | 0 |
| DJ | 18 | 0 | 0 | 0 / 9 | 0 | 0 | 0 | 0 |
| PH | 197 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| VN | 166 | 8 | 0 | 0 | 16 | 0 | 28 | 0 |
| HN | 166 | 5 | 0 | 9 / 0 | 16 | 0 | 63 | 0 |
| SC2 | 241 | 18 | 0 | 1 / 0 | 16 | 0 | 53 | 0 |
| NJ_Skateshop | 2 | 0 | 0 | 0 | 1 | 0 | 0 | 0 |

"scripts" = scripts brought along (node trigger scripts, the level structure's startup / setup scripts and everything
they call). Every node trigger script is either brought along or THUG2's own; none had to be taken off. Peds whose THUG1
profile THUG2 lacks get `random_male_profile` / `random_female_profile`. The classic levels' `DuplicateTrigger` gap
sectors (SC 23, DJ 6, PH 62, VN 58, HN 15, SC2 29) get the trigger script of the node they link to.

## Left out, and why

- LevelLights, goal GenericNodes, CAS submesh nodes; animal peds (dogs, sharks, SJ's MonsterPed crowd: THUG2's ped AI
  plays human animations on them - an animation the skeleton's set hasn't crashes, see below).
- Vehicles / game objects whose model isn't in THUG2's Data\models, or whose skeleton THUG2 doesn't load
  (scripts\game\skeleton.qb: HI's bus and VC's 6 trucks use `car_6`); particle objects whose texture isn't in
  Data\images\particles; LevelObjects whose sector isn't in the converted scene (skinned).
- Statements: goal manager calls and conditions on them (`GoalManager_*`, `Goal_*` - in free skate no goal is active:
  the conditions read false), panels, `printf`, cutscene cameras, `MakeSkaterGoto` / `KillSkater` (the skater's state),
  THUG1 level / career flags (`GetFlag`, `SetFlag`... - THUG1's flag numbers aren't THUG2's), THUG1 commands THUG2
  lacks (`TriggerType` - THUG1's gap landing test: those conditions read false -, `Obj_LockToObject`,
  `Obj_ObjectInRadius`, `Obj_AdjustSound`, light members), all object animation commands (`Obj_PlayAnim`,
  `Obj_WaitAnimFinished`, ...) and anim parameters (`standing_anims=drugdealer_goal_wait`: THUG2's Ped_InitAI uses its
  defaults), `LoadSound` of sounds THUG2 lacks (THUG2 ships THUG1's level sound folders: most load), streams.
- Music, goals, net games: not in scope.

## Crashes found in the game runs, and fixed (all in the data we write)

- **Script component pool** (NJ, HI: write to 0x4 in ParseQB's CreateScriptSymbol, later a null in the loading
  screen): the THUG1 node arrays had been expanded (`ncomp_*` includes put into every node) - with the objects kept
  that was ~53-58k script components, more than THUG2 has room for (its own levels: 30-43k). The shared parts are
  made global structures again (`thug1_port.compress_includes`: NJ 58k -> 40k).
- **Animation not loaded** (0x00482BA0 on a null, from the anim blender's PlayAnim 0x0047F260): peds playing THUG1
  animation names / animal peds. Anim commands left out (see above), animal peds left out.
- **Skeleton not loaded** (0x004A3D30 reading 0x594 of a null skeleton): vehicles with `skeletonName=car_6`.
- **GetFlag of a THUG1 flag** (0x005844C0 reading far outside the flag bits): NY's `GetFlag flag=FLAG_NY_...`.
- **Endless loop without a wait** (hang while loading, SE): `while { random { Obj_PlayAnim } Obj_WaitAnimFinished }`
  with the anims left out spun for ever. script_port now puts `Wait 1 frame` before the `repeat` of an endless loop
  that lost statements.
- **Gap scan of the trigger scripts** (SkipToEndOfStatement on a null under ForEachScriptCall 0x0045C950 at its
  EndGap gapscript= branch, NY; found by symbolising the minidump's stack with main's verify pdb): THUG2's own
  vehicle model qbs (Data\models\veh\ny\veh_cab_ny.qb - THUG2 ships THUG1's cars with their rails and car-hop
  triggers) hand out `gapscript=NY_Goal_BurningTaxi_Got1`, a THUG1 Manhattan goal script; the loader follows every
  gap script and reads through a null for one that isn't defined. The level now defines the kept models' gap scripts
  as empty scripts; `GetGap` / `Gap_CarHop` calls also get their gap script parameter explicitly.
  (Possibly worth a note in the decomp: the original's gap scan has no check there either - ours is faithful.)

## Changes to shared code (additive)

- `tools/level_port/script_port.py`:
  - `Source(..., weights=False)`: THUG1 / THUG2 random tokens with their u16 weights (kept on output; THPS3's still
    written as 1).
  - switch / case / default / endswitch (THUG1's have no jumps): made THUG2's `3E 49 <next>` / `3F 49 <end>` and the
    body's `49 <past endswitch>`, as THUG2's compiler writes them; an empty case body gets a no-op statement.
  - `Translator(objects=, left_out=, left_out_prefixes=, anim_check=, kept_in=)` (after main's `inherit_params`,
    `undrawn`): `object:command` calls on the skater (checked as the skater) or on the level's objects (checked
    against that object's components) wrapped in `if IsAlive name=<object>`; `<param>:command` likewise as a game
    object's; `if not <x>` checks <x>; `name = value` statements kept. New contexts PED / VEHICLE / BOUNCY with the
    components THUG2 gives those objects.
  - `Thug2.anims` (descChecksum= of the animload scripts).
  - endless loops that lost statements get `Wait 1 frame` (applies to THPS3 too: its converted scripts change only
    where a loop lost statements - a hang fix there as well).
  - merge with main: Thug2's `bodies / needs` and my `anims` both kept; THUG1 passes `undrawn` (geometry nodes
    without a scene sector) and trusts THUG2's own `ped_*` / `car_*` / `ShatterAndDie` scripts for its peds / cars
    (Thug2.script_needs follows every branch, so it would refuse THUG2's ped AI on a ped).
- `tools/level_port/ported_levels.py`: optional `startup_script` in levels.json -> the level structure's
  startup_script (load_level runs it before the node array is parsed, as THUG1's did).

Selfchecks pass: thug1 333, thps4 139, thps3 323, thaw 735 files.

## Game runs (2026-10-19, workspace C:\Games\THUG2-1, under its lock)

All 16 levels load into free skate (SKATE NOW patched to load each one; window pictures after ~60 s): NJ, NY, FL, SD,
HI, VC, SJ, RU, SE, SC, DJ, PH, VN, HN, SC2, NJ_Skateshop - game responding, no exception in the log. A ped is seen in
Vancouver's hotel lift at the start. Nothing beyond loading was checked (nobody skated). The workspace keeps this
branch's THUG1 levels installed (SKATE NOW restored with make_menu_level.py; the lock put the DLL / ini back).

## What the user should try (per level)

- Every level: peds walking / standing (THUG2's ped AI), cars driving their THUG1 paths (Car_Generic01 - they stop
  for the skater, can be skitched), bouncy cones / trash cans / chairs knocked over, ledges and ladders (hang /
  climb), level sounds (ambient emitters), particles (smoke, fire).
- NJ: the train-station doors (StationDoorOpen / Close gap scripts swing LO_TrainDoorSouth_*), NJ's gaps, the
  flies near the junk (FO_Flies_01), the drivable car spot.
- NY: the cars on the street and the high road, the taxi car-hop gaps, the escalators.
- FL / SD / HI / VC / RU / SE: their moving objects (doors, gates, rides), VC's 156 particle emitters, SE's ledges.
- SJ: 245 moving objects (crowd / stage pieces) - do they sit right; MonsterPed crowd is left out.
- Classic levels (SC, DJ, PH, VN, HN, SC2): gap messages for the gaps whose second sector used DuplicateTrigger.
- Kill zones as before (they still name a restart).
