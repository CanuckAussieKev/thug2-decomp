# thaw-scripts - THAW levels' scripts, moving objects, peds, cars and particles in free skate

Agent "thaw-scripts" (branch worktree-agent-af48b31bffadfa1da). Builds on docs/pending/thaw.md (the static levels).
New: `tools/thaw_levels/thaw_scripts.py`; `thaw_levels.py convert` runs it for every level (`--static`: without).

## How

- **Scripts.** THAW's compiled scripts are THUG2's token stream (same if/else 0x47/0x48, randoms with weights,
  switch with 0x49 short breaks) plus 0x4A, an inline structure in THAW's item format (a call's arguments, mostly).
  `ThawSource` reads a THAW qb into script_port's form (if/else as its 0x25/0x26, every jump as its target token,
  the randoms' own weights kept, the switch short breaks with their "one before the next case" quirk, inline
  structures as THUG2 `{ }` tokens - a call's own one as plain arguments) and script_port's Translator brings the
  scripts along: from the trigger scripts of the kept geometry / rails (run by the skater - with the skater's own
  components allowed, context `thaw_skater`) and of the made objects, down through what they call / spawn. Left out
  besides script_port's list: GoalManager_* / goal_* (THAW's goals aren't THUG2's), screen elements and panels,
  flags / Change / ResetScore / SetSlomo, IGC cameras, `object:member` calls (THUG2 runs the rest on the skater when
  the object isn't there, and crashes on a member it hasn't). Renamed: SpawnScriptLater/Now -> SpawnScript,
  Obj_SpawnScriptLater/Now -> Obj_SpawnScript, ShatterFromSkater -> Shatter. Every converted script's jumps land on
  tokens of its own script (offline check, all 15 levels); NeverScript decompiles them.
- **Breakables**: ShatterFromSkater counts as a shatter too (thaw_levels.breakable_scripts): Hollywood's fences and
  roof panes, Minneapolis' glass... (breakables now 0-98 per level).
- **LevelObjects** (moving and bouncy objects): THUG2 has the class with the same keys (BE's bouncy trash cans), so
  they're kept as THUG2's (THAW-only keys dropped; CollisionMode only with a collision object). THAW keeps their
  sectors and collision in their own space, centred on the origin - the static conversion drew them all at the
  origin (and their collision there). Now drawn by THUG2 at their nodes; not moved by the recentring. LevelObjects
  without a sector in the net scene (story-only bouncy props: CH 79, SZ 40, SV 78, SM 82) are left out with their
  collision. Their trigger scripts run (fans: Obj_RotY...).
- **GameObjects**: the script objects (model="none", e.g. HO's bees) with their scripts; the ones with THAW models
  or profiles (goal letters, chairs, barricades) left out.
- **Waypoints** kept (car paths, Obj_FollowPathLinked); the ones the peds walk as THUG2 ped AI waypoints.
- **Peds**: THUG2's ped AI at THAW's peds' places (walking their linked waypoints, else standing), a THUG2 profile by
  words in THAW's profile / type (female, skater, guard, graffiti, bum...; else random_male_profile). Only the peds
  there in free skate: CreatedAtStart or NODEFLAG_PERMANENT_PEDS; goal / story peds and animals left out.
  **LifeNodes** (THAW's ambient ped spots): a standing ped each, at most 24 per level.
- **Cars**: CreatedAtStart vehicles whose script drives (Car_Generic01 / Obj_FollowPathLinked, 2 calls deep) and that
  link to waypoints: THUG2's Car_Generic01 on the same path; the others parked. THUG2's model of the same name
  (veh_liberty, veh_cop, veh_wrangler, veh_van...), else Veh_Sedan.
- **Particles**: THAW's particle boxes there at the start as THUG2 ParticleObjects (THAW's BoxDims / BoxPositions / Radii /
  RadiusSpread / BoxRGBAs arrays -> THUG2's Start/Mid/End keys; mid / end positions recentred), with THUG2's particle
  texture of the same name (apm_spark_0x, BA_APM_smokePuff01s, dt_nj_waterdrop05...) or a stand-in
  (apm_smokepuff_rot_01 -> BA_APM_smokePuff01, apm_flame_rot01 -> apm_flame_01), loaded by a LoadAllParticleTextures
  script in the level's qb (THUG2's load_level runs it).

## Per level (offline, `convert`)

| level | scripts | moving / bouncy | script objs | peds (life) | cars drive/park | particles | breakables |
|---|---|---|---|---|---|---|---|
| CH Chicago | 159 | 2 / 0 | 0 | 10 | - | - | 10 |
| MA The Mall | 137 | 1 / 12 | 1 | 5 | 0/1 | 5 | 14 |
| DN Minneapolis | 249 | - | 0 | 24 (life) | 2/0 | - | 68 |
| JA Kyoto | 342 | - | 5 | 4 | - | 0 | 8 |
| SZ Santa Cruz | 369 | - | 10 | 12 | 0/1 | 0 | 18 |
| SV2 The Ruins | 196 | 10 / 0 | 8 | 10 | - | 34 | 5 |
| SV Vans Park | 361 | 12 / 22 | 14 | 15 | - | 0 | 1 |
| HO Hollywood | 276 | 15 / 12 | 16 | 17 (2 walk) | 4/0 | - | 31 |
| BH Beverly Hills | 270 | 3 / 0 | 51 | 29 | 3/0 | 0 | 5 |
| DT Downtown | 435 | 4 / 1 | 46 | 23 | 8/0 | 13 | 9 |
| EL East LA | 382 | 5 / 0 | 64 | 8 | - | 7 | 38 |
| SM Santa Monica | 364 | 4 / 0 | 32 | 10 | - | - | 1 |
| SR Skate Ranch | 331 | 12 / 0 | 18 | 2 | 0/1 | - | 0 |
| LV Casino | 421 | 8 / 0 | 17 | 6 | - | - | 98 |
| OI Oil Rig | 356 | 4 / 0 | 3 | 1 | - | - | 0 |

(scripts: brought along, most of them gap scripts - StartGap / EndGap on the skater. Left out per level: 11-431
statements, mostly printf, RailAccelerate (THAW's rail speed-ups), Triggertype / IsCreated conditions, object member
calls, goal commands.)

## Collision-only geometry (lead's Airport crash)

Geometry nodes with no scene sector (2-89 per level) are never named by a converted statement: script_port's
`undrawn` (Visible / Invisible / Create / Kill / Shatter, prefix= expanded) plus, in THAW's Translator, any statement
naming one anywhere (so `<x>` passed on to a Kill name=<x> can't reach one) - 1-74 statements per level left out.
convert checks every brought script's tokens and fails if one still names such a node (none in all 15 levels).
Breakables only for nodes with a sector (after: CH 10, MA 14, DN 68, JA 8, SZ 18, SV2 5, SV 1, HO 31, BH 5, DT 9,
EL 38, SM 1, SR 0, LV 98, OI 0) - the others' glass is collision-only in the net scene.

## Left out / not done

- THAW's own ped AI and models, goal peds, animals (pigeons), THAW-model game objects (chairs, tables, barricades,
  goal letters), skitching, the story's node flags (CreatedFromVariable NODEFLAG_*: those nodes stay absent unless a
  converted script creates them), ladders (ClimbingNode), EmitterObjects (LV), proximity nodes, sounds THAW plays by
  name (PlaySound of THAW sounds: THUG2 hasn't them loaded - silent), music.
- Literal world positions inside scripts aren't recentred (only node positions are) - none seen in the kept ones.
- `if NOT <command THUG2 hasn't>` is false (as `if <command THUG2 hasn't>`).

## Shared code changes (tools/level_port/script_port.py, small and additive)

- `Tok` has optional `weights` (a random's own weights; else 1 each, as before) and `adjust` (0x49 short jumps).
- 0x49 short jumps (switch) are re-made from their targets like the other jumps (`short` items).
- `if NOT <command>`: the command is checked like any statement (left out -> the condition is false). THPS3 had these
  kept unchecked.
- `name = value` statements (a script parameter set) are kept (were left out as "not in THUG2: name").
- `x:member ...` statements are left out ("object member call").
- (thaw_scripts adds its own context `thaw_skater` to script_port.SAFE_COMPONENTS / BY_NAME at import - THAW's process
  only.) THPS3's convert was re-run with these changes: 14 levels, every jump of its 6157 scripts lands on a token.

## Game runs (2026-10-19, workspace C:\Games\THUG2-2, this branch's levels installed)

Load check, free skate through the main menu's SKATE NOW (patched to load the level): **all 15 levels load and the
skater skates** (CH, MA, DN, JA, SZ, SV2, SV, HO, BH, DT, EL, SM, SR, LV, OI; no exception in the log). Nothing
checked beyond the start point (peds / cars / objects not seen from there in the pictures) - see the list below.
- Kyoto crashed loading first (EXCEPTION writing 0x4 under LoadQBData 0x0046ED90, after thaw_ja.qb was read):
  bisected to its 9 particle boxes, all not CreatedAtStart (neon sparks its scripts make). Particle boxes not there
  at the start are now left out (JA 9, SZ 1, SV 1, BH 14); the CreatedAtStart ones (SV2, DT, EL, MA) load. Cause in
  the engine not looked into.

## For the user to try (per level, free skate)

- All: the level loads; gaps announce (StartGap / EndGap from THAW's scripts); glass / fences break when skated into.
- MA The Mall: the bouncy boxes by the skate area (Obj_Bouncy_Box_Low01-12) knock over; smoke at the 5 particle
  boxes; the skidmark grates are at their places (they were at the origin).
- HO Hollywood: the fans on the building (ho_fan01-03) turn; 4 cars drive their streets; 2 peds walk; bouncy objects.
- DT Downtown: 8 cars drive; water drips / sparks (particles); peds.
- SV2 The Ruins: fires and smoke (34 particle boxes); moving objects.
- SV Vans Park: 22 bouncy objects, 12 moving (bathroom doors...).
- BH / DN: the life-node peds stand around (Beverly Hills' old ladies / models, Minneapolis').
- LV Casino: 98 breakables (glass).

## THUGPRO-compatibility pass (agent thaw-thugpro, 2026-10-20)

See thaw.md "THUGPRO compatibility". Script changes: the thaw_skater context has the skater's animblender / input /
vibration too (THUG2's TeleportSkaterToNode: THAW's teleports, 5 in The Mall); RailAccelerate written for THUG2 from
THAW's skutils.qb (provided in every level); trigger_jump_boost (THUG2's = THAW's) allowed for the skater; the made
LevelObjects / script objects are `objects` for script_port (name:member kept, checked as that object's, IsAlive
guard: The Mall's Rigidbody_Kick box sets); CTF / team flag game objects made (game-mode flags: 12 per level).
Still left out (largest): Triggertype conditions (THAW-only C function; THUG2 records the trigger events at
CTriggerComponent +0x28 but has no script function to read them), THAW goal / story calls, iscreated, printf.
