# thug1-scripts - THUG1 levels' scripts, peds, vehicles and objects in free skate

Agent "thug1-scripts". Tools: `tools/thug1_levels/thug1_port.py` (new), `tools/thug1_levels/thug1_levels.py`,
shared `tools/level_port/script_port.py`; how to run: `tools/thug1_levels/README.md`. The earlier THUG1 work (formats,
geometry, kill zones): `docs/pending/thug1.md`.

## Approach

THUG1 is THUG2's direct predecessor: its node arrays use THUG2's classes and keys (Pedestrian with `Ped_InitAI`
trigger scripts and PedAI waypoints, Vehicle path followers, bouncy LevelObjects with `contacts=`, ProximNodes,
EmitterObjects, ClimbingNodes, ParticleObjects - THUG2's LA has the same node shapes), and its compiled scripts are
THUG2's tokens except for the long-form if / else / switch (no jumps). So instead of rebuilding objects (THPS3 / THPS4)
the node array is kept nearly whole and checked against what THUG2 has, and the scripts are translated by
script_port, which got the THUG1 forms (below).

`convert` does it by default; `--static` gives the old geometry / rails / restarts / kills only output.

## What's ported per level (offline counts, `convert` of all 16 default levels)

| Level | scripts | peds | vehicles | moving / bouncy objects | game objects | particles | ledges | emitters / proxim |
|---|---|---|---|---|---|---|---|---|
| NJ | 994 | 177 | 24 | 37 / 82 | 147 | 37 | 167 | 29 / 75 |
| NY | 747 | 85 | 32 | 6 / 55 | 110 | 24 | 55 | 25 / 34 |
| FL | 700 | 90 | 30 | 20 / 86 | 40 | 0 | 45 | 19 / 21 |
| SD | 595 | 133 | 11 | 18 / 65 | 33 | 21 | 48 | 28 / 35 |
| HI | 522 | 60 | 17 | 28 / 110 | 40 | 45 | 110 | 25 / 24 |
| VC | 803 | 67 | 20 | 45 / 41 | 186 | 156 | 66 | 24 / 40 |
| SJ | 612 | 111 | 0 | 245 / 30 | 55 | 0 | 67 | 15 / 27 |
| RU | 667 | 155 | 3 | 16 / 5 | 146 | 43 | 81 | 19 / 17 |
| SE | 515 | 33 | 5 | 25 / 78 | 25 | 73 | 238 | 8 / 5 |
| SC | 102 | 0 | 0 | 1 / 0 | 0 | 0 | 0 | 0 |
| DJ | 18 | 0 | 0 | 0 / 9 | 0 | 0 | 0 | 0 |
| PH | 197 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| VN | 166 | 8 | 0 | 0 | 16 | 0 | 28 | 0 |
| HN | 166 | 5 | 0 | 9 / 0 | 16 | 0 | 63 | 0 |
| SC2 | 241 | 18 | 0 | 1 / 0 | 16 | 0 | 53 | 0 |
| NJ_Skateshop | 2 | 0 | 0 | 0 | 1 | 0 | 0 | 0 |

"scripts" = scripts brought along (trigger scripts, the startup / setup scripts and everything they call). Every
node trigger script is either brought along or a THUG2 script; none had to be taken off. Peds whose THUG1 profile
THUG2 lacks get `random_male_profile` / `random_female_profile` (NJ 80, NY 51, FL 43, SD 49, HI 34, VC 22, SJ 91,
RU 74, SE 8, VN 5, HN 1, SC2 5).

## Left out, and why

- LevelLights (THUG2's levels have none; no gain, some risk), goal GenericNodes, CAS submesh nodes.
- Vehicles / game objects whose model isn't in THUG2's Data\models (NJ 22 game objects, FL 64 - goal pickups,
  flags of THUG1's own -, RU 35 + 4 vehicles, VC 2 vehicles...); particle objects whose texture isn't in THUG2's
  Data\images\particles (none so far).
- LevelObjects whose sector isn't in the converted scene (skinned): NJ 1, SE 1.
- Statements (counts over all levels): goal manager calls and conditions on them (`GoalManager_*`, `Goal_*`: in free
  skate no goal is active - the conditions read false), panels and messages, `printf`, cutscene cameras,
  `MakeSkaterGoto` / `KillSkater` (the skater's state), THUG1 commands THUG2 lacks (`TriggerType` 43 - THUG1's gap
  landing test: those gap conditions read false -, `Obj_LockToObject`, `Obj_ObjectInRadius`, `Obj_AdjustSound`,
  light members), animations THUG2 doesn't load (`Ped_Drunk_*`, `Ped_Bum_*`...: the statement), THUG1's ped
  animation lists (`standing_anims=drugdealer_goal_wait`...: the parameter - THUG2's Ped_InitAI uses its default),
  `LoadSound` of sounds THUG2 lacks (29; THUG2 ships THUG1's level sound folders, most load), streams (music / voice
  not converted).
- Music, goals, net games: not in scope.

## Changes to shared code (all additive; THPS3's output is token-identical before / after - Foun, Tok, LA checked)

- `tools/level_port/script_port.py`:
  - `Source(..., weights=False)`: THUG1 / THUG2 random tokens with their u16 weights (kept on output; THPS3's still
    written as 1).
  - switch / case / default / endswitch (THUG1's have no jumps): made THUG2's `3E 49 <next>` / `3F 49 <end>` and the
    body's `49 <past endswitch>`, as THUG2's compiler writes them; an empty case body gets a no-op statement (THUG2
    reads case labels right after a matched one as fall-through labels).
  - `Translator(objects=, left_out=, left_out_prefixes=, anim_check=, kept_in=)`: `object:command` calls (0x42) on the
    skater (checked as the skater) or on the level's objects (checked against that object's components) - wrapped in
    `if IsAlive name=<object>` (a missing object makes THUG2 run the rest of the statement on object 0, the skater);
    `<param>:command` likewise as a game object's. `if not <x>` checks <x>. `name = value` statements kept. New
    contexts PED / VEHICLE / BOUNCY with the components THUG2 gives those objects (from ped_add_components /
    bouncyobj_add_components and the member functions THUG2's own levels' ped / car / object scripts call on them).
  - `Thug2.anims`: the animations THUG2 loads (descChecksum= of its animload scripts).
- `tools/level_port/ported_levels.py`: optional `startup_script` in levels.json -> the level structure's
  startup_script (load_level runs it before the node array is parsed, as THUG1's did).

## Game runs

- NOT run in the game yet.

## What the user should try (per level)

- (to be filled after the load checks)
