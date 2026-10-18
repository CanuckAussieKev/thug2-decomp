# Level ports: state and compromises

What the THPS3, THPS4, THUG1 and THAW level ports in THUG2 bring over, what they don't bring over exactly as the
original, and what an authentic copy still needs. State of main as of 2026-10-19 (all four "scripts, peds, vehicles"
branches merged; all 56 levels installed in C:\Games\THUG2).

The goal ("exact assets"): the original game's own models, particles, sounds and animations, with THUG2's behaviour
only where the original's logic is engine code that can't run.

**Sources:** the converters (`tools/<game>_levels/`, shared `tools/level_port/`) and their records in docs/pending:
thps3.md, thps3-scripts.md, thps4-scripts.md, thps4-lighting.md, pedscars.md, thug1.md, thug1-scripts.md, thaw.md,
thaw-scripts.md.

How to read the tables:
- **Original**: the original's own data, converted.
- **Stand-in**: a THUG2 asset or behaviour in its place.
- **Static**: there, but doesn't move or react.
- **Missing**: not brought over.

## 1. Status per game

| | THPS3 | THPS4 | THUG1 | THAW |
|---|---|---|---|---|
| Levels | 14: Foundry, Canada, Rio, Suburbia, Airport, Skater Island, Los Angeles, Tokyo, Cruise Ship, Warehouse, Burnside, Roswell, Tutorials, Skate shop | 11: Alcatraz, Carnival, Chicago, Shipyard, Kona, London, San Francisco, College, Zoo, Skate shop, Motocross | 16: NJ, NY, FL, SD, HI, VC, SJ, RU, SE, SC, DJ, PH, VN, HN, SC2, NJ skate shop | 15: Chicago, The Mall, Minneapolis, Kyoto, Santa Cruz, The Ruins, Vans Park, Hollywood, Beverly Hills, Downtown, East LA, Santa Monica, Skate Ranch, Casino, Oil Rig |
| Converter | `tools/thps3_levels` | `tools/thps4_levels` (+ `thps4_scripts.py`) | `tools/thug1_levels` (+ `thug1_port.py`) | `tools/thaw_levels` (+ `thaw_scripts.py`) |
| Static-only switch | (none) | `--no-scripts` | `--static` | `--static` |
| Every level loads in free skate | Yes | Yes | Yes | Yes |
| Played by a person | Geometry, glass, grinds; scripts / peds not yet | Not yet (scripts, peds, vehicles) | Not yet | Glass; scripts / peds not yet |

The agents' game runs only loaded each level and let it run (about 10-60 s each, nobody skating), so gaps, peds, cars
and moving objects have mostly not been seen working. The "what to try" lists are in the pending records above.

## 2. Across all four games

| What | THPS3 | THPS4 | THUG1 | THAW |
|---|---|---|---|---|
| Level geometry, textures | Original | Original (vertex colours halved to THUG2's scale) | Original | Original, from the online (`_net`) version (see 6) |
| Collision, rails, restarts, kill zones | Original | Original | Original | Original (3 face flags dropped) |
| Level scripts (gaps, triggers, moving things) | Original, translated | Original, translated | Original, translated (node array nearly whole) | Original, translated |
| Gap checks that test how you landed (TriggerType) | Missing: those lip gaps never score | Translated to THUG2's form | Missing: those conditions read false | Missing: those conditions read false |
| Peds: model | Stand-in (THUG2 people) | Stand-in (THUG2 people) | Original profile where THUG2 has it, else a random THUG2 one | Stand-in (THUG2 people, chosen by keyword) |
| Peds: where they stand or walk | Original | Original (walkers only; others stand) | Original | Original; ambient "life node" spots get standing peds (max 24) |
| Peds: AI and animations | Stand-in (THUG2 ped AI) | Stand-in | THUG2's ped AI (THUG1's is its predecessor); THUG1-only animations dropped | Stand-in |
| Story and goal peds | Missing | Missing | Missing | Missing |
| Crowds | Stand-in (THUG2 standing peds) | n/a | Missing (SJ's MonsterPed crowd) | n/a |
| Animals | Stand-in (THUG2 pigeons, seagulls, sharks); flying birds missing | Missing (Zoo's among them) | Missing (dogs, sharks crash THUG2's ped AI) | Missing (pigeons) |
| Traffic cars: model | Stand-in (THUG2 look-alike) | THUG2's car of the same name, else a sedan | Original (THUG2 ships most of THUG1's cars) | THUG2's car of the same name, else a sedan |
| Traffic cars: driving | THUG2's Car_Generic01 on the original roads | Same (only the skate shop and Chicago have any; College none) | Same | Same |
| Other vehicles (trains, trams, boats, rides) | Original models | Original models; skinned ones body only (no wheels, orientation guessed) | Original where THUG2 has the model and skeleton | Story vehicles missing |
| Moving objects (fans, doors, presses) | Original models, moved by their scripts | Original | Original | Original (now drawn at their nodes, not the origin) |
| Bouncy objects (cones, trash cans, boxes) | Static, no collision | Static (as level objects) | Original, with physics | Original, with physics; story-only props missing |
| Collision on moving objects and vehicles | Missing (skater passes through) | Where the original had collision | Original | Where the original had collision |
| Ride cars locked to a head (Obj_LockToObject) | n/a | Missing: they stay put | Missing | n/a |
| Proximity areas (things that appear or switch near you) | Original (watched by generated ghost objects) | Missing: their scripts run once at the start | Original (THUG2 node) | Missing |
| Ladders and ledges (hang, climb) | n/a | n/a | Original | Missing (ClimbingNode) |
| Skitching | n/a | n/a | Original (THUG2's cars) | Missing |
| Particles (smoke, steam, sparks, fire) | Missing | Missing (emitters left out) | Original where THUG2 has the texture | Converted (those there at the start); THUG2 texture of the same name or a stand-in |
| Sound effects | Missing | Missing | Mostly original (THUG2 ships THUG1's level sounds; 29 missing) | Missing (glass uses THUG2's breaking sound) |
| Ambient sound emitters | Missing | Missing | Original | Missing (Casino's EmitterObjects) |
| Music, ambient streams | Missing (THUG2's soundtrack plays) | Missing | Missing | Missing |
| Goals, career, pickups (letters, tapes, stat points) | Missing | Missing | Missing | Missing |
| Cutscenes, panels, skater-state scripts | Missing | Missing | Missing | Missing |
| Object variables (Obj_Var*) | Missing | Missing | (THUG2 has them) | (THUG2 has them) |
| Lighting | Original (THPS3's lights) | Original | Original; LevelLights missing | Stand-in (THUG2's default; THAW's time of day not converted) |
| Sky | Original | Original | Original | Stand-in (one shared sky for every level) |

## 3. Workarounds for THUG2 crashes (each one costs something)

Each of these was found in a game run and is fixed in the data we write, not in the engine. The decomp may let some go
back: the crash is in THUG2's code, which we are rewriting.

| Crash | Where in THUG2 | What the converters do now | Cost |
|---|---|---|---|
| A TrickObject / Cluster on geometry with no drawn sector | ParseNodeArray | Strip the marking (all games) | 2-89 nodes per THAW level aren't trick objects |
| Visible / Kill / Shatter of collision-only geometry | CFunc_Visible 0x5A5050 | Leave the statement out, or expand `prefix=` to drawn nodes only | 1-74 statements per THAW level; a few in THPS3 |
| THUG2's Car_Generic01 on a LevelObject | its member calls on missing components | Traffic made a real THUG2 Vehicle | THPS3 cars are THUG2 look-alikes |
| TriggerType with a bare name | 0x004349B2 | THPS4: made THUG2's array form; others: left out (false) | THPS3 / THUG1 / THAW landing-test gaps |
| IsAlive / other commands naming a missing node | CFunc_IsAlive | Parameters naming nodes the level lacks are checked | Statements left out |
| Too many script components | THUG2's script component pool | THUG1's shared node parts made global structures again | None (NJ 58k -> 40k) |
| An animation the ped's set hasn't | 0x00482BA0 | All object animation commands left out; animal peds left out | THUG1 anims, animals |
| A skeleton THUG2 doesn't load | 0x004A3D30 | Vehicles needing `car_6` left out | HI's bus, VC's 6 trucks |
| GetFlag of a THUG1 flag number | 0x005844C0 | Flag commands left out | THUG1 story flags |
| An endless loop that lost its waits | hang | `Wait 1 frame` added to such loops | One frame per loop |
| A gap script that isn't defined (gap scan) | ForEachScriptCall 0x0045C950 | Empty scripts defined for the kept models' gap scripts | Car-hop gaps of THUG1 models do nothing |
| A shader pass blending on an unwritten register | THUG2's ps.1.1 builder 0x004DD2C0 | A third pass blending like the second is dropped | One Santa Monica material |
| Particle boxes not there at the start | LoadQBData, Kyoto (cause unknown) | Left out | JA 9, SZ 1, SV 1, BH 14 boxes |
| Peds / cars added to THPS4 College | CNodeManager::HashNodes (cause unknown) | College has no THUG2 walkers or cars | College traffic |
| An object following a path in a network game (no path manager there) | 0x004695D0 from Obj_FollowPathLinked | nodes.absent_in_net_games: CreatedAtStart path followers get AbsentInNetGames (all games) | Tokyo's shinkansen, College's blimp, Alcatraz / SF objects absent online |
| A pedestrian in a network game (THUG2 loads no ped animations there) | 0x00482BA0 from the anim controller's PlayAnim | The same pass: CreatedAtStart peds and vehicles get AbsentInNetGames (as THUG2's own levels) | No peds or cars online (THPS4's skate shop guy) |
| A restart whose own script "kills" (respawn loop) | a restart's TriggerScript runs when the skater is put there | nodes.is_kill_call: camera animations (KillSkaterCamAnim) and other "kill" names that aren't kills don't count; add_kill_restarts never gives a restart a kill | THPS4 skate shop's start point and THAW BH's Gap14 restart lose their (camera / goal) scripts |
| No Multiplayer restart (single-player levels) | a network game's spawn | nodes.net_restarts: Player1 restarts also Multiplayer and Horse (as THUG2's skate shop) | None |
| A ported level named in a network game (2026-10-19) | GameNet::Manager::GetLevelName 0x005369A0 (THUG2's list only) | Fixed in the engine (batch 122): the ported lists are searched too | None |

## 4. THPS3

**Rendering**
- Vertex colour wibbles (animated vertex colours) not converted.
- The env-mapped passes (uv flag 0x80) and 23 materials' second passes not converted.
- Reflection overlays (PS2 blend 0x8) drawn as additive: an approximation that fixed Foundry's black halfpipe.
- Warehouse: the dark set (SkWare_Dark_*) dropped. THPS3 flickers between the lit and dark sets; we keep the lit one.

**Scripts** (THPS3's own, translated; a called script gets its caller's parameters as in THPS3). Statements left out,
counts across all levels:
- SetParticleSysVisibility: 743 (no particles).
- Sound and music commands (LoadSound, PlayStream, Obj_PlaySound...).
- BouncyObj_Go: 191 (no bounce physics).
- TriggerType lip checks: 112. Those lip gaps never score (Roswell, Cruise Ship, Skater Island).
- DuplicateTrigger: 71.
- Object variables (Obj_VarSet / VarEQ / VarInc).
- Clump animations and scaling (Obj_PlayAnim, Obj_ApplyScaling), Obj_SetGroundOffset.
- Skater state and cutscenes (MakeSkaterGoto, SpawnSkaterScript, PlaySkaterCamAnim), panels.
- The tutorials' lessons and goals.

**Life**
- Peds and crowds are THUG2 people on THPS3's places and paths. Women use THUG2's female setup.
- Resting birds are THUG2 pigeons / seagulls (fly up within 15 ft); flying birds missing (Canada 55, Suburbia 8,
  Skater Island 23). Sharks are THUG2's.
- Traffic: THUG2 look-alike cars (police -> veh_cop, vans / buses -> veh_van...). THPS3's spare cars, made by its car
  scripts, missing (Burnside 44, LA 1).
- Other vehicles (bullet trains, planes, boats, snowmobiles, karts) use their THPS3 models.
- Skinned game objects missing (Suburbia 2, LA 1).

## 5. THPS4

**Rendering**: vertex colours halved (THPS4's PC files are 255 = full, THUG2's 128). Without it every level drew twice
as bright.

**Scripts**: statements left out:
- Sounds and streams.
- Goal manager, panels, screen elements, flags.
- MakeSkaterGoto, Printf.
- Obj_Var* (object variables).
- DuplicateTrigger, setup_ped_speech.
- Obj_LockToObject: Carnival's coaster and train cars stay put while the head moves.

**Vehicles**
- Traffic cars (Car_Generic01): THUG2's cars. Only the skate shop (2) and Chicago (2) have any.
- Other vehicles keep their THPS4 model. Skinned ones (with a skeleton) keep only the body: wheels missing (bones not
  converted), orientation a guess.
- 8 vehicles have no model found (Carnival 4, London 4).

**Peds**
- Walkers use THUG2's ped AI on their THPS4 paths. Other peds stand, with a THUG2 profile chosen from the THPS4 type
  (pros, guards, workers, women...).
- Skate shop pros: the ones THUG2 has (Burnquist, Koston, Mullen, Hawk) skate the halfpipe with THUG2's ped AI.
  Bam Margera is left out (his vert loop looked broken). The places of pros THUG2 lacks get random skateboarders
  standing.
- Goal peds and objects never created in free skate: left out (22-143 per level).

**Also missing**
- Animals.
- Proximity nodes: their scripts run once at the start, so anything they switch stays as it starts.
- Particle emitters.
- Goal objects (money, letters, icons, tapes, combo letters).

## 6. THUG1

THUG1 is THUG2's predecessor: its node arrays use THUG2's classes, so they're kept nearly whole and checked against
what THUG2 has. Peds, cars, bouncy objects, emitters, proximity nodes, ladders, ledges and particles are THUG1's own
nodes, run by THUG2's code.

**Left out**
- LevelLights, goal nodes, CAS submesh nodes.
- Skinned sectors, plus two skinned level objects (NJ 1, SE 1).
- Vehicles and game objects whose model THUG2 lacks: NJ 22, FL 64 (goal pickups and THUG1's own flags), RU 35 + 4
  vehicles, VC 2 vehicles. Vehicles needing the `car_6` skeleton (HI's bus, VC's 6 trucks).
- Particle objects whose texture THUG2 lacks.
- Animal peds (dogs, sharks, SJ's MonsterPed crowd).

**Peds**
- Where THUG2 lacks a ped's THUG1 profile, a random THUG2 one is used: NJ 80, NY 51, FL 43, SD 49, HI 34, VC 22,
  SJ 91, RU 74, SE 8, VN 5, HN 1, SC2 5.
- All object animation commands and THUG1's own animation lists (drunk, bum, drug dealer...) are dropped; THUG2's
  defaults play.

**Scripts**
- TriggerType (THUG1's gap landing test): those conditions read false.
- Goal manager calls (and conditions on them: false), panels, printf, cutscene cameras, MakeSkaterGoto / KillSkater.
- THUG1 level / career flags (GetFlag, SetFlag...).
- Obj_LockToObject, Obj_ObjectInRadius, Obj_AdjustSound, light members.
- 29 LoadSound calls of sounds THUG2 lacks. Music and voice streams.
- Classic levels: DuplicateTrigger gap sectors get the trigger script of the node they link to (as THUG1).

## 7. THAW

**Which version of each level**
- The converter reads each level's online (`_net`) archive, not the story one. Story-only pieces aren't there:
  - 12-15 props per level (shop shelves, parking meters, planters, roof edges);
  - bouncy props: Chicago 79, Santa Cruz 40, Vans Park 78, Santa Monica 82;
  - story peds, goal peds and story vehicles;
  - nodes the story creates from its own flags.

**Moved**
- Levels whose collision reaches beyond 20,000 units are moved to the origin: The Mall, Kyoto, Santa Cruz, Beverly
  Hills, East LA, Santa Monica, Skate Ranch, Casino, Oil Rig.
- Positions written literally inside scripts aren't moved. None were seen in the scripts that were kept.

**Rendering**
- Lighting is THUG2's default. THAW's time of day isn't converted, and night-only sectors (lamps, glows) are dropped.
- One shared sky for every level.
- LOD copies dropped. Glows (billboards, 9 in all) dropped.
- Pass features THUG2 lacks are dropped: texture animation (0x800), 0x80, 0x80000.
- Blend modes above 13 become plain blend.
- Guesses in the material reader: double-sided, z bias, uv wibble (12 floats down to 8).
- Santa Monica: one material's third pass dropped (section 3).

**Collision and nodes**
- Collision face bits 0x2000, 0x4000 and 0x8000 dropped.
- Collision-only geometry loses its TrickObject / Cluster marking, and statements naming it are left out (section 3).
- The Mall: an invisible story blocker across the entrance doors removed (Z_MA_Mall_First_Rail_Blocker).
- How THAW closes the open ends of the LA areas in online mode: not checked.

**Scripts** (THAW's own; its token stream is THUG2's plus an inline structure token). Left out:
- RailAccelerate (THAW's rail speed-ups).
- TriggerType / IsCreated conditions (false).
- `object:member` calls.
- Goal commands, screen elements and panels, flags, slow motion, IGC cameras.
- Renamed to THUG2's: SpawnScriptLater/Now -> SpawnScript, ShatterFromSkater -> Shatter.

**Glass and gameplay**
- Breakable glass and fences run a generated THUG2 script: THUG2's glass sound and shatter. Only for glass that has
  visible geometry in the online version (0-98 per level).
- Skitching, ladders (ClimbingNode) and Casino's EmitterObjects missing.

**Life**
- Peds are THUG2 people picked by words in THAW's profile (female, skater, guard...), walking THAW's waypoints or
  standing. Only those there in free skate (CreatedAtStart / permanent).
- THAW's "life node" spots get a standing ped each, at most 24 per level.
- Animals missing.
- Cars: THUG2's car of the same name, else a sedan; those whose script drives follow THAW's paths, the others parked.
- Game objects with THAW models (chairs, tables, barricades, goal letters) missing.
- Particle boxes there at the start are converted, with THUG2's particle texture of the same name or a stand-in.
- THAW's own sounds are silent.

## 8. What an authentic copy needs

### Where the originals' assets are

Formats marked "not identified" haven't been looked into yet.

| | THPS3 (`C:\Games\THPS3`) | THPS4 (`C:\Games\THPS4`) | THUG1 (`C:\Games\THUG1`) | THAW (`C:\Games\THAW`) |
|---|---|---|---|---|
| Engine family | RenderWare 3.1 + Neversoft plugins | Neversoft's own (THUG2's ancestor) | THUG2's direct predecessor | Later engine (GH3 family) |
| Models | `Data\Models` (.dff; 339 .skn skins); ped folders Pedestrian_A-D, Pedfem_A-F, PedPro_* | `Game\data\models` (.dat: mdl / tex / col) | `Game\Data\models` (.mdl, .skin, .tex, .col) | `Game\data\models`, inside .pak.wpc archives |
| Skeletons, animations | `Data\Anims` (958 .ska; pedestrian_a, Pedfem_a) | `Game\data\skeletons` (34 .ske), `Game\data\anims` | in the .pre archives (format close to THUG2's) | `Game\data\anims`, in archives |
| Sounds | `Data\Sounds` (495 .wav) | `Game\data\sounds` (506 .wav) | `Game\Data\sounds` (720 .snd); THUG2 already ships most | `Game\data\sounds`, likely the two .wad files (490 MB, not identified) |
| Music, ambient streams | `Data\Streams`, `Data\music` (.mp3, .mus) | `Game\data\streams`, `Game\data\Music` | `Game\Data\streams` | `Game\data\streams` |
| Particle definitions | in level scripts / data (reader needed) | script-driven emitters (reader needed) | THUG2's node format already | particle boxes (converted); textures in archives |

### The work, in order

1. **Particles.** THAW's own particle textures (a format conversion we already do for levels). Readers for THPS3's
   and THPS4's particle systems. THUG1 only needs the textures THUG2 lacks.
2. **Sounds.** One sound converter into THUG2's format, installed in the data overlay: THPS3, THPS4, THAW, and THUG1's
   29. This brings back LoadSound / PlaySound statements now left out, THAW's glass sound and the ambient emitters.
   Per-level ambient streams come with it.
3. **Cars.** The original models in THUG2's car setup. First check whether each game's car models have separate
   wheels. If so it's renaming parts; if not, splitting off the wheel geometry by position. The same work fixes
   THPS4's wheel-less skinned vehicles and THUG1's `car_6` vehicles (ship that skeleton in the overlay).
4. **Peds and animals.** The original models with their own skeletons and animations: one skeleton and animation
   converter per game (THPS3, THPS4, THAW; THUG1's are probably close to THUG2's). THUG2's ped AI then plays the
   original animations through a name map. This brings back THUG1's animation lists, crowds, animals and flying
   birds. The biggest job.
5. **Gameplay gaps.**
   - TriggerType gap checks for THPS3, THUG1 and THAW (THPS4's translation already works).
   - Collision on THPS3's moving objects, and its bouncy physics.
   - THPS4's and THAW's proximity areas (THPS3's approach: watcher ghost objects).
   - Linked ride cars (Obj_LockToObject: THPS4, THUG1).
   - THAW's ladders, skitching, rail speed-ups (RailAccelerate) and EmitterObjects.
   - Object variables (THPS3, THPS4) mapped to THUG2's tags.
   - Each workaround in section 3 that costs content, once the crash is understood in the decomp.
6. **THAW's story content.** Read the story archive alongside the online one for the missing props, peds and
   vehicles.
7. **Lighting and skies.** THAW's time of day, night sectors and per-level skies; THUG1's LevelLights. THPS3's
   vertex colour wibbles, env-mapped passes and Warehouse's dark set.
8. **Music.** Each game's soundtrack and level streams.
9. **Goals and pickups.** A project of its own (porting career mode), outside free skate.
