# Level ports: the compromises so far

What the THPS3, THPS4, THUG1 and THAW level ports in THUG2 don't bring over exactly as the original, as of
2026-10-18. This is the baseline before the "exact assets" work: the original game's own models, particles, sounds and
animations, with THUG2's behaviour only where the original logic is engine code that can't run.

**Sources:** docs/pending/thps3.md, thps3-scripts.md, thug1.md, thaw.md, pedscars.md, and the three agents' branches
(not merged yet when this was written): thps4-scripts, thug1-scripts, thaw-scripts. The THPS4, THUG1 and THAW
scripts / peds work has **not been run in the game yet**. THPS3's has: all 14 levels load.

How to read the tables:
- **Original**: the original's own data, converted.
- **Stand-in**: a THUG2 asset or behaviour in its place.
- **Static**: there, but doesn't move or react.
- **Missing**: not brought over.

## 1. Across all four games

| What | THPS3 | THPS4 | THUG1 | THAW |
|---|---|---|---|---|
| Level geometry, textures | Original | Original | Original | Original (online `_net` version, see 5) |
| Collision, rails, restarts, kill zones | Original | Original | Original | Original |
| Level scripts (gaps, triggers, moving things) | Original, translated | Original, translated | Original, translated | Original, translated |
| Peds: who they are (model) | Stand-in (THUG2 people) | Stand-in (THUG2 people) | Original profile where THUG2 has it, else a random THUG2 one | Stand-in (THUG2 people, chosen by keyword) |
| Peds: where they stand or walk | Original | Original | Original | Original (story and goal peds missing) |
| Peds: how they move (AI, animations) | Stand-in (THUG2 ped AI) | Stand-in | THUG2's ped AI (THUG1's is its predecessor) | Stand-in |
| Animals | Stand-in (THUG2 pigeons, seagulls, sharks); flying birds missing | Missing (Zoo's animals among them) | Kept as nodes where THUG2 has the model | Missing (pigeons) |
| Traffic cars: model | Stand-in (a THUG2 car like it) | THUG2's car of the same name, else a sedan | Original (THUG2 ships most of THUG1's cars); others missing | THUG2's car of the same name, else a sedan |
| Traffic cars: driving | THUG2's Car_Generic01 on the original roads | Same | Same | Same |
| Other vehicles (trains, trams, boats, rides) | Original models | Original models; skinned ones body only (no wheels, guessed orientation) | Original where THUG2 has the model | Story vehicles missing |
| Moving objects (fans, doors, presses) | Original models, moved by their scripts | Original | Original | Original |
| Bouncy objects (cones, trash cans, boxes) | Static, no collision | (as level objects) | Original, with physics | Original, with physics; story-only props missing |
| Collision on moving objects and vehicles | Missing (skater passes through) | Where the original had collision | Original | Where the original had collision |
| Particles (smoke, steam, sparks, fire) | Missing | Missing (emitters left out) | Original where THUG2 has the texture | Converted; textures THUG2's same-named or a stand-in |
| Proximity areas (things that appear or switch near you) | Original | Missing: their scripts run once at the start | Original | Missing |
| Sound effects | Missing | Missing | Mostly original (THUG2 ships THUG1's level sounds; 29 missing) | Missing (glass uses THUG2's own breaking sound) |
| Music, ambient streams | Missing (THUG2's soundtrack plays) | Missing | Missing | Missing |
| Goals, career, pickups (letters, tapes, stat points) | Missing | Missing | Missing | Missing |
| Cutscenes, panels, skater-state scripts | Missing | Missing | Missing | Missing |
| Lighting | Original (THPS3's lights) | Original | Original; LevelLights missing | Stand-in (THUG2's default; THAW's time of day not converted) |
| Sky | Original | Original | Original | Stand-in (one shared sky for every level) |

## 2. THPS3

**Rendering**
- Vertex colour wibbles (animated vertex colours) not converted.
- The env-mapped passes (uv flag 0x80) and the 23 materials' second passes not converted.
- Reflection overlays (PS2 blend 0x8) drawn as additive: an approximation that fixed Foundry's black halfpipe.
- Warehouse: the dark set (SkWare_Dark_*) dropped. THPS3 flickers between the lit and dark sets; we keep the lit one
  always.

**Scripts**: statements left out (counts across all levels):
- SetParticleSysVisibility: 743 (no particles to show).
- Sound and music commands.
- BouncyObj_Go: 191 (no bounce physics).
- TriggerType lip checks: 112. Those lip gaps never score in Roswell, Cruise Ship and Skater Island.
- DuplicateTrigger: 71.
- Object variables (Obj_VarSet / VarEQ / VarInc).
- Clump animations and scaling (Obj_PlayAnim, Obj_ApplyScaling).
- Obj_SetGroundOffset.
- Skater state, cutscenes and panels.
- The tutorials' lessons.

**Life**
- Peds and crowds are THUG2 people. Women now use THUG2's female setup.
- Resting birds become THUG2 pigeons and seagulls; flying birds are missing (Canada 55, Suburbia 8, Skater Island 23).
- Sharks become THUG2's.
- Traffic uses THUG2 look-alike cars. Spare cars made by THPS3's car scripts are missing (Burnside 44, LA 1).
- Skinned game objects missing (Suburbia 2, LA 1).

## 3. THPS4

**Scripts**: statements left out:
- Sounds and streams.
- Goal manager, panels, screen elements, flags.
- MakeSkaterGoto, Printf.
- Obj_Var* (object variables; THUG2 has tags instead).
- DuplicateTrigger, setup_ped_speech.
- Obj_LockToObject: Carnival's coaster and train cars stay put while the head moves.

**Vehicles**
- Skinned vehicle models (cars and trams with a skeleton) keep only the body. The wheels are missing because the bones
  aren't converted, and the orientation is a guess.
- 8 vehicles have no model found (Carnival 4, London 4).

**Peds**
- Walkers use THUG2's ped AI on their THPS4 paths. Other peds stand.
- The skate shop's pros: the ones THUG2 has skate the halfpipe with THUG2's ped AI. Bam Margera is left out (his vert
  loop looked broken). The places of the pros THUG2 lacks get random skateboarders standing.

**Also missing**
- Animals.
- Proximity nodes: their scripts run once at the start, so anything they switch stays as it starts.
- Particle emitters.
- Goal objects (money, letters, icons, tapes).

## 4. THUG1

THUG1 is THUG2's predecessor, so its nodes are kept nearly whole and checked against what THUG2 has.

**Left out**
- LevelLights, goal nodes, CAS submesh nodes.
- Skinned sectors, plus two skinned level objects (NJ 1, SE 1).
- Vehicles and game objects whose model THUG2 lacks: NJ 22, FL 64 (goal pickups and THUG1's own flags), RU 35 + 4
  vehicles, VC 2 vehicles.

**Peds**
- Where THUG2 lacks a ped's THUG1 profile, a random THUG2 one is used: NJ 80, NY 51, FL 43, SD 49, HI 34, VC 22,
  SJ 91, RU 74, SE 8, VN 5, HN 1, SC2 5.
- THUG1's own animation lists (drunk, bum and others) are dropped; THUG2's defaults play instead.

**Scripts**
- TriggerType (43, THUG1's gap landing test): those gap conditions read false.
- Also left out: Obj_LockToObject, Obj_ObjectInRadius, Obj_AdjustSound, light members, goal manager, cutscene cameras.
- 29 LoadSound calls of sounds THUG2 lacks.
- Music and voice streams.

## 5. THAW

**Which version of each level**
- The converter reads each level's online (`_net`) archive, not the story one. Story-only pieces aren't there.
- 12-15 props per level exist only in the story version (shop shelves, parking meters, planters, roof edges).
- Story-only bouncy props: Chicago 79, Santa Cruz 40, Vans Park 78, Santa Monica 82.
- Story peds, goal peds and story vehicles.
- Nodes the story creates from its own flags stay absent.

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
- Santa Monica: one material's third pass dropped. It crashed THUG2's shader builder.

**Collision and nodes**
- Collision face bits 0x2000, 0x4000 and 0x8000 dropped.
- Collision-only geometry loses its TrickObject / Cluster marking (2-89 nodes per level). Scripts may not name it;
  1-74 statements per level are left out for that.
- The Mall: an invisible story blocker across the entrance doors removed (Z_MA_Mall_First_Rail_Blocker).
- How THAW closes the open ends of the LA areas in online mode: not checked.

**Glass and gameplay**
- Breakable glass and fences run a generated THUG2 script: THUG2's glass sound and shatter. Only for glass that has
  visible geometry in the online version.
- Scripts left out: RailAccelerate (THAW's rail speed-ups), Triggertype / IsCreated conditions, object member calls,
  goal commands.
- Skitching missing. Ladders (ClimbingNode) missing. Casino's EmitterObjects missing.

**Life**
- Peds are THUG2 people picked by words in THAW's profile (female, skater, guard...).
- THAW's ambient "life node" spots get a standing ped each, at most 24 per level.
- Animals missing.
- Cars use THUG2's car of the same name, else a sedan.
- Game objects with THAW models (chairs, tables, barricades, goal letters) missing.
- Particle boxes are converted, with THUG2's particle texture of the same name or a stand-in (THAW's own textures not
  converted).
- THAW's own sounds are silent.

## 6. The exact-assets work, by size

1. **Particles: THAW's own textures, and THPS3's and THPS4's systems.** The textures are a format conversion we
   already do for levels. THPS3's and THPS4's particle definitions need a reader each.
2. **Sounds: THPS3, THPS4, THAW, and THUG1's 29.** One sound converter into THUG2's format, installed in the data
   overlay. Per-level ambient streams come with it.
3. **Cars: the original models in THUG2's car setup.** First check whether each game's car models already have
   separate wheels. If so it's renaming parts; if not, splitting off the wheel geometry by position. The same work
   fixes THPS4's wheel-less skinned vehicles.
4. **Peds and animals: original models with their own skeletons and animations.** One skeleton and animation
   converter per game (THPS3, THPS4, THAW; THUG1's may already match THUG2's). THUG2's ped AI then plays the original
   animations through a name map. This is the biggest job.
5. **Gameplay gaps.**
   - Collision on THPS3's moving objects and its bouncy physics.
   - The TriggerType gap checks (THPS3, THUG1, THAW): THUG2 takes a different array format, which can be translated.
   - THPS4's and THAW's proximity areas.
   - THPS4's linked ride cars (Obj_LockToObject).
   - THAW's ladders and skitching.
6. **THAW's story content.** Read the story archive alongside the online one for the missing props, peds and
   vehicles.
7. **Music, and THAW's lighting and skies.** Separate features.
8. **Goals and pickups.** A project of their own (porting career mode), outside free skate.
