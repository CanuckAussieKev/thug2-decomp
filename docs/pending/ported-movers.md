# Ported levels: plain moving props made solid LevelObjects (PC-THPS3-15) - 2026-10-21

Branch `worktree-agent-aa1edbf0fa25a716e`. The task (user-approved): objects THUGPRO or our converters made
`Class=Vehicle` that aren't road traffic built THUG2's way; real cars stay Vehicles. Corrected by the lead mid-task:
THUG2's form for a plain solid mover is a **LevelObject with CollisionMode=Geometry**; a **Vehicle** is THUG2's own form
for movers carrying rails or trigger nodes in their model's node array. The first version (GameObjects with THUGPRO's
models, commit 68937d1) is replaced; it also crashed Tokyo in the load check (below).

## THUG2's own forms (checked in its data: Data\pre\*_scripts.prx node arrays, ped.qb, skutils.qb)

- Solid moving props: LevelObjects with `CollisionMode=Geometry` - Boston's subway trains BO_Subway01 / 03 (moved by
  Obj_MoveToNode), New Orleans' helicopter blades Veh_NO_Heli_Blades and fan blades, the Triangle's gates and AC fans,
  AU's bouncy tables. No THUG2 level node is a GameObject with a CollisionMode.
- Vehicles that aren't cars: Barcelona's drawbridges TRG_BA_Bridge_North / South, its boat TRG_BA_Bridge_Boat01
  (veh_ba_boat's node array: 17 grind rails, a ledge, a ladder), its skitch / grind nodes; New Orleans' streetcar. A
  Vehicle's model node array (nodearray component), car rail manager and skitch give a mover rails, ladders, hooks and
  trigger nodes (a trigger face of its collision looks its node up in that array: 0x00434BF0).
- A LevelObject draws the scene sector of its name (in its own space - our moving sectors are made so, movingSector)
  and its collision component takes that sector's collision: the level's collision object of the same name, same
  space (Obj::CCollisionComponent::InitFromStructure 0x0041C7B0 -> GetSector), placed at the object every frame.

## Implementation

- `tools/level_port/movers.ts` (shared): `makeSolid` gives each listed node that is a LevelObject with a drawn sector
  `CollisionMode=Geometry` (anything else is left and reported).
- `tools/thps3_levels/thps3_levels.ts`: cmdConvert leaves TPN-THPS3-3's Vehicle `set` out for the listed nodes (so our
  LevelObject and its drawn THPS3 model stay - no drop_sector); convertNodeArray calls makeSolid (Port.solid);
  convertWorld (`solid` option, `solidCollision`) adds a collision object for each solid moving sector from its drawn
  geometry (faces flag 0 as THPS3's plain faces, the material's terrain) unless THUGPRO's collision of the name is added
  by TPN-THPS3-1 (the bucket's: 134 faces, box x/y -60..60, z -76..60, centred like our drawn bucket, which is larger:
  -111..110, -89..89, -76..78 - THUGPRO's shape kept, as the lead asked; with `--compat none` ours is made).
- Register: `tools/compat/thugpro/thps3_levels.json` port_changes PC-THPS3-15 (op set, required false, `online`,
  `objects` - level / name / what / thugpro_set -, `kept_vehicles`); README row. `--no-port-changes`: off - THUGPRO's
  Vehicles back (checked, Foun).
- Tests: tools/level_port/movers.test.ts (2: the level filter; makeSolid on a LevelObject, a Vehicle, an undrawn
  object, a missing one, run twice), thps3_levels.test.ts (solidCollision: chosen / skipped sectors, faces, terrain,
  box). typecheck, lint, npm test (152 pass), thps3_levels selfcheck (323 files, 0 failures).

## Inventory (every Vehicle-class node of the converted levels - all four games, default options; 225)

All are cars (skeleton car, a car model, Car_Generic01 or a level's car / driveable / goal-car script) except:

| Level | Object | Model | Car? | Before | After |
|---|---|---|---|---|---|
| THPS3 foun | Obj_Bucket (molten-metal bucket) | THUGPRO Obj_Bucket (TPN-THPS3-3) | no | Vehicle | **LevelObject + CollisionMode=Geometry** (THPS3's bucket drawn, THUGPRO's 134-face collision) |
| THPS3 tok | TRG_shinkansenEast / TRG_shinkansenwest | THUGPRO bullet_train (TPN-THPS3-3; its node array only collision boxes) | no | Vehicle | **LevelObject + CollisionMode=Geometry** (THPS3's train drawn, collision from it) |
| THPS3 tok | TRG_Plane_NorthWest / SouthWest / East | THUGPRO plane_jet | no | Vehicle | kept Vehicle: its node array's trigger node Box02 (THUGPRO's wallplant bonus) |
| THPS3 sub | TRG_Plane | THUGPRO light_airplane01 | no | Vehicle | kept Vehicle: trigger nodes TRGP_CarPlant01, TRGP_Killskater01 |
| THPS3 rio | 8 TRG_Car_Path*_carplant_collision | THUGPRO CarPlantNode / _Bus (invisible trigger box on a car, Obj_LockToObject) | no | Vehicle (TPN-THPS3-2 add) | kept - **left for a separate decision** |
| THPS3 sub | TRG_IceCreamTruck_Carplant_collision | THUGPRO CarPlantNode | no | Vehicle | kept - separate decision |
| THPS3 sub | TRG_IceCreamTruck | THUGPRO icecreamtruck (wheels, roof rails, skitch hook) | yes | Vehicle | Vehicle |
| THPS3 tok | 18 TRG_Car* | THUGPRO Veh_Focus / Galaxie / Smart_car | yes | Vehicle | Vehicle |
| THUG1 nj | TRG_TrainA01 / 02 (El trains) | THUGPRO veh_el_train (TPN-THUG1-2) | no | Vehicle | kept: 4 roof rails, 2 skitch hooks, a collision trigger in its node array |
| THUG1 hi | TRG_HI_Boat01 | THUG2's veh_ba_boat (TPN-THUG1-2) | no | Vehicle | kept: rails, ledge, ladder - THUG2's own BA boat is this Vehicle |
| THUG1 ru | JO_Tank_Base / 01 / 02, G_COMPNOT_gr_Tank | THUGPRO veh_tank_ru | no (static) | Vehicle (THUG1's own class) | kept: THUG1's data, static, not THUGPRO's or ours |
| THUG1 hh | TRG_SE_JO_Qp_Ghost | veh_skitchnode | no | Vehicle (THUG1's) | kept: a skitch node |
| THUG1 sd / vc / hh / ru | karts, leaf blowers, limo, mini baja, ATVs, the stadium tram, ladas | THUG1 / THUGPRO models, skeleton car | driveable / goal / traffic | Vehicle | Vehicle |
| THPS4 (skate shop 2), THAW (bh 3, dn 2, dt 8, ho 4, ma 1, sr 1, kyoto 4, hof 2, sz 3) | cars | | yes | Vehicle | Vehicle |

Counts of all Vehicles: THPS3 burn 4, la 6, rio 16, sub 3, tok 23, foun 1; THPS4 2; THUG1 fl 30, hh 5, hi 16, nj 25, ny
32, ru 7, sd 11, vc 16; THAW 28. No entry for THUG1 / THPS4 / THAW: nothing there qualifies (THUG1's non-cars carry
rails / hooks / triggers or are THUG1's own static Vehicles). movers.ts is shared if one is wanted later.

**The bucket's kill:** THPS3's bucket kills no one - its `TRGP_Kill_Bucket` (Foundry's static trigger plane where the
bucket pours) runs only `StartGap Bucket_Trigger_Gap`. The kill inside it is THUGPRO's: its model's trigger node
TRGP_KillSkaterBucket calls FOUN_BucketKillSkater01 (THUGPRO's TP_DoFlash; not in THUG2, nor in what we bring). As a
LevelObject the bucket has no trigger nodes, so it doesn't kill - THPS3's behaviour. A kill inside it would need a
moving trigger of its own (e.g. a second collision-only LevelObject locked to it): the lead's call.

## Load checks (C:\Games\THUG2-2, under its lock; the lead's DLL out\build\lead\dinput8.dll; all four games converted, make_menu_level)

- Final version: `foun` and `tok` reached free skate (load_check: running after 83 s, responding, loaded), no
  EXCEPTION / run-time check / CRT report in the log.
- The first version (GameObjects with THUGPRO's models) crashed Tokyo after loading: 0xC0000005 reading CCCCCCD0 in
  Script::CStruct::ExpandInto under a member-function call on an object (ComponentCallMemberFunction) - not followed up,
  that version is gone.
- **Rio crashes after loading - before this branch too** (checked with PC-THPS3-15 switched off: THUGPRO's Vehicles):
  0xC0000005 reading CCCCCCD0 in Script::CStruct::ExpandInto (struct.cpp:1223), called from 0x00488F40 (the skater
  profile / appearance loader, LoadFromStructure) via 0x00422711 (the model component's model init), a
  CallMemberFunction on an object - probably a ped's or car's model from a profile. Not caused by this change (Rio has
  no PC-THPS3-15 object); needs its own look.
- Sub loaded and kept running in the first round (load_check's "loaded" not reached in 85 s; no exception); it has no
  PC-THPS3-15 object in the final version.

## What the user should check by hand

1. Foundry: the bucket moves out and tips as before; it is solid (ride into it, it carries / blocks the skater where it
   is, also while moving); it doesn't kill (THPS3's behaviour - say if THUGPRO's kill is wanted).
2. Tokyo: both bullet trains run with their timing and are solid (they knock / block the skater, moving); the jets fly
   as before; nothing crashes over a few minutes.
3. Online (needs a hand check): THUGPRO's players have Vehicles there; all three objects are AbsentInNetGames, so
   neither side makes them in a network game - client-side only, moving level objects aren't sent over the network.

## Reconvert

The lead must reconvert THPS3's **Foun** and **Tok** (`node tools/thps3_levels/thps3_levels.ts convert` - all, so
levels.json stays whole) and run make_menu_level for the main install. Nothing else changes.
