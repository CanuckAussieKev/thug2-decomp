# Level ports: state and compromises

What the THPS3, THPS4, THUG1 and THAW level ports in THUG2 bring over, what they don't bring over exactly as the
original, and what an authentic copy still needs. State of main as of 2026-10-19 (all four "scripts, peds, vehicles"
branches merged; all 56 levels installed in C:\Games\THUG2). THPS2 (2026-10-21, phase 1: geometry, collision,
restarts, rails) is section 9.

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
| Converter | `tools/thps3_levels` | `tools/thps4_levels` (+ `thps4_scripts.ts`) | `tools/thug1_levels` (+ `thug1_port.ts`) | `tools/thaw_levels` (+ `thaw_scripts.ts`) |
| Static-only switch | (none) | `--no-scripts` | `--static` | `--static` |
| Every level loads in free skate | Yes | Yes | Yes | Yes |
| Played by a person | Geometry, glass, grinds; scripts / peds not yet | Not yet (scripts, peds, vehicles) | Not yet | Glass; scripts / peds not yet |

The agents' game runs only loaded each level and let it run (about 10-60 s each, nobody skating), so gaps, peds, cars
and moving objects have mostly not been seen working. The "what to try" lists are in the pending records above.

## 2. Across all four games

| What | THPS3 | THPS4 | THUG1 | THAW |
|---|---|---|---|---|
| Level geometry, textures | Original | Original (vertex colours halved to THUG2's scale) | Original | Original, from the online (`_net`) version (see 6) |
| Mip levels | Made (THPS3 ships one level): 2 x 2 averages down to a shorter side of 4, as THUG2's own | Original | Original | Original |
| Z bias of layers on other surfaces (decals, dirt / light layers) | Made from the geometry (THPS3 has none; tools/level_port/overlay_bias.ts) | Made from the geometry (THPS4 has none) | Original | Original |
| Collision, rails, restarts, kill zones | Original | Original | Original | Original (2 face flags dropped) |
| Where the camera goes through (face flag 0x80: THUG2's "camera passes through"; THPS3 / THPS4's "camera collides") | Original (flag inverted) | Original (flag inverted) | Original (kept; inverted in DJ, PH, SC and the skate shop, which THUG1 shipped with the old meaning) | Original (kept) |
| Level scripts (gaps, triggers, moving things) | Original, translated | Original, translated | Original, translated (node array nearly whole) | Original, translated |
| Gap checks that test how you landed (TriggerType) | Translated to THUG2's form | Translated to THUG2's form | Translated to THUG2's form | Translated (THAW's Smack / Natas / AcidDrop / STALL: no THUG2 event, left out - false) |
| Peds: model | Original (THPS3's own models, `tools/thps3_levels/thps3_peds.ts`); pros and crowds: THUG2 stand-ins (`--thug2-peds`: all stand-ins) | Stand-in (THUG2 people) | Original profile where THUG2 has it, else a random THUG2 one | Stand-in (THUG2 people, chosen by keyword) |
| Peds: where they stand or walk | Original | Original (walkers only; others stand) | Original | Original; ambient "life node" spots get standing peds (max 24) |
| Peds: AI and animations | THUG2's ped AI playing THPS3's own animations on THPS3's skeletons (each animation it asks for: THPS3's of that name or role) | Stand-in | THUG2's ped AI (THUG1's is its predecessor); THUG1-only animations dropped | Stand-in |
| Story and goal peds | Missing | Missing | Missing | Missing |
| Crowds | Stand-in (THUG2 standing peds) | n/a | Missing (SJ's MonsterPed crowd) | n/a |
| Animals | Stand-in (THUG2 pigeons, seagulls, sharks); flying birds missing | Pigeons as THUG2's own (Chicago, London, San Francisco: THPS4's script is THUG2's Pigeon_Generic); the rest missing (Zoo's among them) | Missing (dogs, sharks crash THUG2's ped AI) | Missing (pigeons) |
| Traffic cars: model | Stand-in (THUG2 look-alike) | THUG2's car of the same name, else a sedan | Original (THUG2 ships most of THUG1's cars) | THUG2's car of the same name, else a sedan |
| Traffic cars: driving | THUG2's Car_Generic01 on the original roads | Same (only the skate shop and Chicago have any; College none) | Same | Same |
| Other vehicles (trains, trams, boats, rides) | Original models | Original models; skinned ones body only (no wheels, orientation guessed) | Original where THUG2 has the model and skeleton | Santa Monica's ferris cars: original (THAW's model, its rails); story vehicles missing |
| Moving objects (fans, doors, presses) | Original models, moved by their scripts | Original | Original | Original (now drawn at their nodes, not the origin) |
| Bouncy objects (cones, trash cans, boxes) | Static, no collision | Static (as level objects) | Original, with physics | Original, with physics; story-only props missing |
| Collision on moving objects and vehicles | Missing (skater passes through) | Where the original had collision | Original | Where the original had collision |
| Ride cars locked to a head (Obj_LockToObject) | n/a | Missing: they stay put | Missing | Original: Santa Monica's ferris wheel turns, its cars hang upright (THAW's no_rotate, added to THUG2's lock) and carry their grind rails |
| Proximity areas (things that appear or switch near you) | Original (watched by generated ghost objects) | Missing: their scripts run once at the start | Original (THUG2 node) | Missing |
| Ladders and ledges (hang, climb) | n/a | n/a | Original | Missing (ClimbingNode) |
| Skitching | n/a | n/a | Original (THUG2's cars) | Missing |
| Particles (smoke, steam, sparks, fire) | Missing | Missing (emitters left out) | Original where THUG2 has the texture | Converted (those there at the start); THUG2 texture of the same name or a stand-in |
| Sound effects | Missing | Missing | Mostly original (THUG2 ships THUG1's level sounds; 29 missing) | Missing (glass: HitGlassPane2x, which THUG2 has too) |
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
| Visible / Kill / Shatter of collision-only geometry | CFunc_Visible 0x5A5050 | Leave the statement out, or expand `prefix=` to drawn nodes only (THPS3 since PC-THPS3-17: Visible / Invisible / Shatter only - Create / Kill reach the geometry only through CSector calls that skip a null one) | 1-74 statements per THAW level; a few in THPS3 |
| THUG2's Car_Generic01 on a LevelObject | its member calls on missing components | Traffic made a real THUG2 Vehicle | THPS3 cars are THUG2 look-alikes |
| TriggerType with a bare name | 0x004349B2 | Made THUG2's array form (script_port.triggerType, every game); a form it can't make: left out (false) | THAW's four THUG2-less flags |
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
| An object following a path in a network game (no path manager there: THUG2's Levels.qb / ReloadScene call AllocatePathManMemory only `if ! InNetGame`, Cleanup frees it, and the motion component's 0x00423FC0 adds to a path group unconditionally - THUG2 itself never makes a path follower online: they're all AbsentInNetGames, as THUGPRO's Tokyo trains and planes) | 0x004695D0 from Obj_FollowPathLinked | nodes.absent_in_net_games: CreatedAtStart path followers get AbsentInNetGames (all games) | Tokyo's shinkansen, College's blimp, Alcatraz / SF objects absent online |
| A pedestrian in a network game (THUG2 loads no ped animations there) | 0x00482BA0 from the anim controller's PlayAnim | The same pass: CreatedAtStart peds and vehicles get AbsentInNetGames (as THUG2's own levels) | No peds or cars online (THPS4's skate shop guy) |
| A restart whose own script "kills" (respawn loop) | a restart's TriggerScript runs when the skater is put there | nodes.is_kill_call: camera animations (KillSkaterCamAnim) and other "kill" names that aren't kills don't count; add_kill_restarts never gives a restart a kill | THPS4 skate shop's start point and THAW BH's Gap14 restart lose their (camera / goal) scripts |
| No Multiplayer restart (single-player levels) | a network game's spawn | nodes.net_restarts: Player1 restarts also Multiplayer and Horse (as THUG2's skate shop) | None |
| A restart over nothing (THAW's shop interiors: separate zones) or over a kill zone (respawn loop / under the world) | the kill's nodename= restart, a network spawn | nodes.ground_restarts (after the collision is written, all games): no network spawn there; kills sent there go to the nearest grounded restart | East L.A.'s freeway kill no longer goes "into" the CAS shop; Hollywood's hill kills skip the sign restart; Suburbia's hell box |
| A ported level named in a network game (2026-10-19) | GameNet::Manager::GetLevelName 0x005369A0 (THUG2's list only) | Fixed in the engine (batch 122): the ported lists are searched too | None |

## 4. THPS3

**Rendering**
- Vertex colour wibbles (animated vertex colours) not converted.
- 23 materials' second passes not converted. (The env-mapped passes - uv flag 0x80, the *_reflect overlays - are
  THUG2's env passes since 2026-10-21; before, their uv wibble scrolled them over the ramps: docs/pending/shimmer-2.md.)
- Reflection overlays (PS2 blend 0x8) drawn as additive: an approximation that fixed Foundry's black halfpipe.
- THUGPRO's invisible collision overlays (Suburbia's 5 `*_collfix`, Foundry's 3 `*_coll`) left out by default: the user
  tried every spot by hand without them (2026-10-21) - THPS3's own collision is enough there; THPS3's picnic box is
  back (`--collfix` keeps THUGPRO's; docs/pending/collfix-spots.md).
- Warehouse: the dark set (SkWare_Dark_*) dropped. THPS3 flickers between the lit and dark sets; we keep the lit one, always
  shown (the flicker script's Invisible of the lit set left out - with no dark set under it, half the room went black;
  docs/pending/warehouse-flicker.md).
- **Free skate's world is THUGPRO's offline free skate, not THPS3's, where THUGPRO rewrote a level script (TP-41, on
  purpose: the user's decision 2026-10-21).** THUGPRO's free skate is THUG2's offline single session (not a network
  game: its peds, all AbsentInNetGames, are there); its world differs from THPS3's free skate only where it edited
  THPS3's scripts. Ours makes THPS3's own scripts take the same branches: Foundry's GENERATOR 1 CORE doors open (the
  generator room reachable) and the five valve booths cleared; Subway's houses unboarded, blockers gone, open rails,
  dish turned, curtain lit (online too, as THUGPRO's); Skater Island's cannon trigger not made at the start; Shipyard's
  helicopter made and flown off at the start. What THUGPRO's rewrites kill that is the level's life (Subway's Thin Man,
  Shipyard's ambient boats, Foundry's bucket - absent in THUGPRO's game, made by its scripts all the same) stays in ours.
  Skater Island's outer door stays (THUGPRO's offline free skate has it). `--compat none`: THPS3's own free skate.
  Not matched: THPS3's visibility culling (Rio's Sul bowl, Skater Island's inside / outside, Shipyard's sections,
  Tokyo's areas, Foundry's generator: hidden from where they can't be seen and shown on entering), which THUGPRO
  emptied - drawing only, collision the same; docs/pending/thps3-free-skate-thugpro.md.
- Free skate's start state follows THPS3's own free skate (PC-THPS3-16): what only a career / goal / multiplayer
  branch of a start script makes (Tokyo's secret buildings and destroyed sign, Skater Island's cannon-damage set,
  Airport's tickets, LA's car-fall triggers) isn't there at the start any more. THPS3's multiplayer, goal and career
  states otherwise aren't brought over: no goals, no career flags. Collision-only pieces (invisible walls, blockers,
  trigger planes) follow THPS3's scripts' Create / Kill since PC-THPS3-17 (before, they stayed as the level loaded
  them: Shipyard's ramp and anchor-chain collision THPS3 kills in free skate stayed solid).

**Scripts** (THPS3's own, translated; a called script gets its caller's parameters as in THPS3). Statements left out,
counts across all levels:
- SetParticleSysVisibility: 743 (no particles).
- Sound and music commands (LoadSound, PlayStream, Obj_PlaySound...).
- BouncyObj_Go: 191 (no bounce physics).
- (TriggerType lip checks and DuplicateTrigger are translated since docs/pending/translator-fixes.md.)
- Object variables (Obj_VarSet / VarEQ / VarInc).
- Clump animations and scaling (Obj_PlayAnim, Obj_ApplyScaling), Obj_SetGroundOffset.
- Skater state and cutscenes (MakeSkaterGoto, SpawnSkaterScript, PlaySkaterCamAnim), panels.
- The tutorials' lessons and goals.

**Life**
- Peds are THPS3's own people (models, skeletons, animations - docs/pending/ped-models.md, phase 2) on THPS3's places
  and paths, driven by THUG2's ped AI; the pros and the crowds are THUG2 people (the pros THUG2 has as its own
  PedPro_ models). `--thug2-peds`: THUG2 people everywhere, as before.
- Resting birds are THUG2 pigeons / seagulls (fly up within 15 ft); flying birds missing (Canada 55, Suburbia 8,
  Skater Island 23). Sharks are THUG2's.
- Traffic: THUG2 look-alike cars (police -> veh_cop, vans / buses -> veh_van...). THPS3's spare cars, made by its car
  scripts, missing (Burnside 44, LA 1).
- Other vehicles (bullet trains, planes, boats, snowmobiles, karts) use their THPS3 models.
- Foundry's bucket and Tokyo's bullet trains collide with THPS3's own drawn shape (PC-THPS3-15; THUGPRO's smaller bucket collision left out, 2026-10-21); THUGPRO's invisible carplant_collision boxes on Rio's cars and Suburbia's ice-cream truck are not brought (inert here: their trigger runs a THUGPRO script).
- Skinned game objects missing (Suburbia 2, LA 1).

## 5. THPS4

- The skate shop's door is open (2026-10-19, the user's request): port changes PC-THPS4-1 / PC-THPS4-2 (tools/compat/thugpro/thps4_levels.json port_changes) leave out the shop-side face of InvisColl01, the invisible box THPS4 put round the skate park outside its menu shop, and the shop's (Object10's) two invisible faces that closed the doorway (the fence's gap at the end of the halfpipe stays closed); the fence piece there, SSO_Fence05, is created at the start (port change PC-THPS4-3: THPS4's menu killed it and its practice run created it again); the street is skateable collision, its far edges may have no walls.

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
- Goal manager calls (and conditions on them: false), panels, printf, cutscene cameras, MakeSkaterGoto / KillSkater.
- THUG1 level / career flags (GetFlag, SetFlag...).
- 29 LoadSound calls of sounds THUG2 lacks. Music and voice streams.
- Classic levels: DuplicateTrigger gap sectors get the trigger script of the node they link to (as THUG1).

**Drivable cars** (THUG1's "Press ... to drive" cars, unlocked by story goals)
- THUG2 ships its driving scripts empty (`player_controlled_vehicle.qb`, `misc\car_scripts.qb`) but keeps the engine
  side (Vehicle / VehicleCamera / VehicleSound components, the skater's SetDriving) and the scripts calling into it.
  PC-THUG1-22 installs THUG1's two files in the data overlay (made from the user's THUG1 at conversion, in their THUG2
  form: THUG2 renamed the proximity events - ObjectInRadius / ObjectOutofRadius -, RunScriptOnObject + Block hangs, the
  player's car needs a Motion component; tools/thug1_levels/driving.ts) and keeps
  VC's limo, its unlocking goal (VC_Goal_Race4) counted as won - so it's parked by the SJ restart from the start, as in
  a finished THUG1 story. The other levels' drivable cars (NJ rally car, NY Impala, FL police car, SD gardener's cart,
  HI car, VC leaf blower - THUGPRO made it a static prop -, RU Lada, HH baja) stay as before (parked or absent): their
  goals aren't counted as won and their SetupDrivableCar calls are left out. Adding one is a level in PC-THUG1-22's
  `won_goals` (and taking back THUGPRO's emptying of its script, if any) - each needs trying in the game.
- With the Anteara Mod on, its own copies of the two files (THUG1's car scripts - given the same THUG2 edits by
  make_menu_level -, its own vehicle setups with LimoCar) are used instead.
- The prompt and help texts keep THUG1's button codes (`\m5`, `\b3`...), which THUG2's font shows as other glyphs.

**VC <-> SJ** (PC-THUG1-21): as THUG1 - no transition arrows (THUGPRO's, which change level with its own LoadZone),
and VC's SJ kill zone (trp_death_SJ) kills, sending you to TRG_Restart_SJ.

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
- Collision face bits 0x4000 and 0x8000 dropped (0x2000, not taggable, kept).
- Collision-only geometry loses its TrickObject / Cluster marking, and statements naming it are left out (section 3).
- The Mall: an invisible story blocker across the entrance doors removed (Z_MA_Mall_First_Rail_Blocker).
- Node flags (pieces THAW makes from `CreatedFromVariable=NODEFLAG_...`): THAW's non-story values, **except 30 flags
  set as THUGPRO's table has them, for online play with THUGPRO - so these areas differ from the real THAW** (TP-40,
  tools/compat/thugpro/README.md): Skate Ranch has the dino head instead of its bare mound, the telescope, the crane's
  top and the camper corner's props; Oil Rig has its derrick and grate pieces before they were taken (not the
  wrecked derrick and the grate's 16 grind rails); Beverly Hills' exit to Skate Ranch is fenced and blocked and
  Rodeo's awning rails are gone; Downtown's Chinatown green is unbroken (no "after" rails); East LA has the roof
  couch, the hood park shack and the "before" statues and metro HQ rails; Hollywood's El Capitan doors are open, the
  metro work site (scissor lift, barriers, 17 rails) and the roof boards are gone; Santa Monica's pier telescope is
  gone. `--compat none`: THAW's own values.
- How THAW closes the open ends of the LA areas in online mode: not checked.

**Scripts** (THAW's own; its token stream is THUG2's plus an inline structure token). Left out:
- RailAccelerate (THAW's rail speed-ups) is ours, from THAW's; its parameters (the rails' rate=) passed on to the loop as THAW's do (`params=<...>`). Its trigger check is THUG2's rail event, grind + onto (`TriggerType [3, 13, -1]`), not THAW's land + onto: THUG2's TriggerType needs every listed event, and a THUG2 rail never trips land (until thaw-railspeed2 the loop never ran - only THUG2's own landing boost was felt).
- TriggerType with THAW's Smack / Natas / AcidDrop / STALL (false: THUG2 has no such event); IsCreated becomes THUG2's IsAlive name= for converted nodes, false otherwise;
  InManual becomes THUG2's state tag (GetSingleTag state = Skater_InManual).
- `object:member` calls.
- Goal commands, screen elements and panels, flags, slow motion, IGC cameras.
- Renamed to THUG2's: SpawnScriptLater/Now -> SpawnScript, ShatterFromSkater -> Shatter.

**Glass and gameplay**
- Breakable glass and fences run THAW's own scripts (THUG2 has ShatterAndDie and BloodSplat too), with THUG2's
  breakable-glass face flags. Only for glass that has visible geometry in the online version (0-98 per level).
- Skitching, ladders (ClimbingNode) and Casino's EmitterObjects missing.
- Santa Monica's ferris wheel (2026-10-21, docs/pending/sm-ferris-wheel.md): as THAW - the wheel turns at THAW's 15
  degrees a second (its SM_FerrisWheelRotation), the 12 cars are THAW's own model (converted, PC-THAW-14) locked to it
  with THAW's `no_rotate` (THUG2's lock always took the target's orientation: src/game/obj/lock_obj.cpp adds the flag),
  so they hang upright, and each carries THAW's two lip-able grind rails (its model's node array, moved by the car's
  rail manager). Left out: the rails' TriggerScript (SM_FerrisCarBalanceReset, a goal helper easing lip balance) and
  the wheel's loop sound. **Network games keep THUGPRO's frozen wheel**: THAW itself doesn't turn the wheel in a
  multiplayer game (`if NOT InMultiplayerGame`) and has the cars AbsentInNetGames; THUGPRO keeps its cars there, frozen,
  with 24 static rails (TP-8) instead of the model's. Ours: no cars online (THAW's flag, also THUG2's rule for
  vehicles), and TP-8's static rails made only in network games (PC-THAW-15, by a ghost object's script) - so both
  sides grind the same rails online, and offline the static rails don't hang in the air where the cars were at the
  start. Split screen: the wheel stands still too (THAW's check), the cars stay at their nodes with their own rails.

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
   - Collision on THPS3's moving objects, and its bouncy physics.
   - THPS4's and THAW's proximity areas (THPS3's approach: watcher ghost objects).
   - Linked ride cars (Obj_LockToObject: THPS4, THUG1) - kept since translator-fixes; not seen working yet.
   - THAW's ladders, skitching, rail speed-ups (RailAccelerate) and EmitterObjects.
   - Object variables (THPS3, THPS4) mapped to THUG2's tags.
   - Each workaround in section 3 that costs content, once the crash is understood in the decomp.
6. **THAW's story content.** Read the story archive alongside the online one for the missing props, peds and
   vehicles.
7. **Lighting and skies.** THAW's time of day, night sectors and per-level skies; THUG1's LevelLights. THPS3's
   vertex colour wibbles and Warehouse's dark set.
8. **Music.** Each game's soundtrack and level streams.
9. **Goals and pickups.** A project of its own (porting career mode), outside free skate.

## 9. THPS2 (phases 1 - 3, 2026-10-21 / 22)

Converter `tools/thps2_levels` (README there; records in docs/pending/thps2.md). All ten levels - Hangar, School II,
Marseille, NY City, Venice Beach, Skatestreet, Philadelphia, The Bullring, Chopper Drop, Skate Heaven - from THPS2's own
ALL.PKR, as `thps2_<name>` in a THPS2 level select section (THUG1's remakes of Hangar / School II / Venice / Philly stay
in THUG1's). Every level loads in free skate (agents' load checks only - 2026-10-22, all fifteen again after the glow / cloud fixes; nobody has skated them yet).

| What | THPS2 |
|---|---|
| Level geometry, textures | Original (the PC version's newtex BMPs where there is one, else the PlayStation texture) |
| Vertex colours | Original (THPS2's 128 = neutral is THUG2's 128 = full) |
| Mip levels | Made (2 x 2 averages, as THPS3's) |
| Collision | Original faces; vert from THPS2's surface flag; wallride on every steep face (THPS2's own wallride flag not found); invisible walls kept (camera passes), trigger planes and the invisible walls on vert lips left out |
| Texture mapping | Original: uvs as texel numbers over the size, repeating textures wrap-addressed (phase 2 - phase 1 clamped them into stripes); each face lying on another given a z bias above it, exact duplicate triangles left out (THPS2 has no depth buffer: its ordering table drew them in turn); the strips' stray join triangles (the user's z-fighting) gone since phase 3 |
| Terrain sounds | Original numbers (THPS2's 16 terrains as the nearest THUG2 terrain: metal pole and metal both smooth metal); rails concrete / metal / wood from their flags |
| Restarts, rails | Original (Player1 / Player2 as the start node names them; 2P / HORSE / generic by name); restarts with no floor at all left out |
| Gaps | Original triggers and gap numbers; **stand-in names and scores**: "GAP <number>", 250 points (THPS2's gap table is in its executable - SafeDisc-encrypted in the PC release; gap_table.ts reads it from a readable one, `--thps2-exe`); StartGap with CANCEL_GROUND for every gap |
| Kill zones | Original (KillBruce triggers, back to the restart they pulse) |
| Other trigger commands | Original (phase 3): pulses, signals, kills (loud ones shatter: glass), SendVisible, pulse budgets, the start's commands as the level's start state; missing: career-only blocks (free skate runs none), sounds, visibility boxes (THPS2's draw culling), camera, fog / sky colours |
| Moving / breakable objects (the _o models) | Static, bouncy and spinning platform objects as LevelObjects of THPS2's models (bouncy ones not collided with: THPS2 knocks them about); scripted ones (phase 3) with their scripts translated - waits, signals, velocities, spins, paths of script points, shattering, dying (Hangar's propeller and door, glass, roof debris; Chopper Drop's boats and planes); gravity left out (Skate Heaven's thrown rocks fly straight), random velocities at their middle, sounds / sparks left out |
| Semi-transparency | Faces with flag 0x40 in the PlayStation blend mode of bits 8-9 (half or added); whole faces (the PC's textures have no per-texel bit) |
| Sky | Original: BackgroundCreate's panorama models as THUG2 skies (Hangar, Skatestreet: none - black); SetSkyColor / SetFadeColor not decoded |
| Backdrops (Hangar's / Philadelphia's dark spiky shapes) | Original: world objects THPS2 draws and collides with (flag 0x2000 marks large objects, not backdrops) - kept as they are |
| Lighting | THUG2's default |
| Peds, cars, music, goals | Missing (no peds; the cars - Car_* objects, C_*.psx models - not converted; music is Bink audio) |
| Level select signs | Pictograms of our own |

## 10. THP8 (phase 1, 2026-10-21)

Converter `tools/thp8_levels` (README there; records in docs/pending/thp8.md). Tony Hawk's Project 8's Xbox 360 data
(the user's own, as the fan-made recompilation installs it in C:\Games\THP8\game\DATA - only the data is read). THP8's
world is one streamed city; each of its zones is a level here (the whole city is ~10 times what THUG2's memory takes):
the eleven city zones (Suburbs, School, School inside, Shops, Slums, Factory, Capitol, City park, Skate park, Fun park,
Parking - sign names ours), Training, the Cartoon world, the skate shop and the bedroom, as `thp8_<zone>` in a
"TONY HAWK'S PROJECT 8" level select section (numbers 110-124). All fifteen load in free skate (agents' load checks
only - 2026-10-22, all fifteen again after the glow / cloud fixes; nobody has skated them yet).

| What | THP8 |
|---|---|
| Level geometry, textures | Original (each zone's own scene; instanced pieces - MasterName nodes - baked in at their places) |
| Materials | Stand-in: each one pass with its first colour texture - THP8's shaders layer normal, specular, ambient and shadow maps over it (not drawn); multi-layer (blended terrain) materials show their first layer |
| Lighting | Stand-in: THUG2's default; THP8's vertex colours kept (mostly white: THP8 lights with its ambient / shadow maps and time of day) |
| Mip levels | Made (the Xbox packs its small levels into one tile: not read) |
| Collision | Original (Havok meshes; boxes as boxes, cylinders / capsules / spheres as 8-sided prisms); face flags and terrains THP8's (its terrain numbers as THUG2's nearest) |
| Space | THP8's metres x 39.37 (inches); each zone moved so the middle of its collision is at the origin |
| Restarts, rails | Original (the skate shop has none: one made over its floor) |
| Kill zones | Original where the trigger script calls a kill-skater script (0-4 per zone) |
| Node flags | THP8's non-story values (game_progress_set_non_story_mode_nodeflags: gates open, barriers down); nodes left not made are left out of the scene and collision |
| Network barriers | Original (z_online_<x>'s walls, for the areas that are one zone), collision only |
| Sky | Original clouds (THP8's default sky), faded out towards the horizon (cutting them there left the dome showing through as grey spikes); its atmosphere domes (a shader) as a stand-in clear-sky gradient |
| Glows, light shafts | Stand-in blend: drawn adding, picked by their sectors' names (THP8's blend mode in the material's parameter block not found yet), at the strength of the material colour's alpha where the block has one |
| Billboards | Missing (camera-facing quads, as THAW's) |
| Gaps, scripts, peds, vehicles, game objects, particles, sounds, music, goals | Missing (phase 2) |
| Level select signs | Stand-in: THUG2's training sign (placeholders) |
