# Ported levels: non-car Vehicles as moving game objects (PC-THPS3-15) - 2026-10-21

The change (user-approved): objects that THUGPRO (thugpro_new) or our converters made `Class=Vehicle` but which aren't
road traffic are built THUG2's way: an ordinary moving GameObject with the model kept, its path and timing unchanged,
solid through the collision component made from the model's .col, its behaviour kept. Real cars stay Vehicles.

## How THUG2 does it (checked in THUG2's own data)

- `gameobj_add_components` (ped.qb): a GameObject with `model=` gets the model component, with `CollisionMode` (not
  none) the collision component; `gameobj_composite_structure` (skutils.qb) gives every game object suspend, **motion**
  (Obj_MoveToNode / Obj_FollowPathLinked / Obj_RotX), objectproximity, sound; CreateGameObject (0x00468CB0) adds lockobj
  (Obj_LockToObject). The collision component builds the movable collision from the model's own collision (the asset's
  data: models\<model>.col) and places it at the object every frame (batch 193, Obj::CCollisionComponent).
- THUG2's own levels' solid moving props: LevelObjects with CollisionMode=Geometry (BO's subway trains, NO's helicopter
  blades, TR's gates). `car_add_components` (a Vehicle) adds skeleton, model, collision, the **nodearray** component
  (models\<model>.qb), the car **rail manager** and skitch - the parts that crashed Foundry (docs/pending/foun-crash.md).
- A trigger face of a moving object's collision (0x00434BF0): with a nodearray component the node is looked up in the
  object's own array; without one (a game object) **in the level's array by the collision object's name**
  (CTriggerComponent::TriggerNode 0x00434A60 with a null array -> GetNode). So the model's trigger nodes get stand-ins
  in the level's node array (Class LevelGeometry, no sector, named like the collision object).
- THUG2 itself keeps some non-cars Vehicles: BA's boat (veh_ba_boat: 17 rails, a ledge and a ladder in its node array), NO's
  streetcar - a Vehicle is how a moving model gets grind rails, ladders and skitch hooks.

## Implementation

- `tools/level_port/movers.ts` (shared): `makeMovers` turns each listed node that is a Vehicle with the listed model
  into `Class=GameObject` + `CollisionMode=Geometry` (skeletonName / NoRail / NoSkitch / NoCollision dropped; Type,
  flags, model, TriggerScript, Links, lod / suspend kept), and adds the stand-ins for the model's trigger nodes (from
  the model's .qb node array and its .col's trigger objects); a trigger listed in `kill_triggers` gets
  `SK3_KillSkater nodename=<the restart nearest the object's node>` (THUGPRO's own trigger scripts call commands of its
  DLL - `#4917D1E6`, `#A972042C`, the carplant `#C653CCB8` - that neither THUG2 nor any THUGPRO .qb defines). A level
  node of the trigger's name already there is kept (reported); a listed node that isn't a Vehicle with that model
  (`--compat none`, `--no-thugpro-new`) is left as it is (reported).
- `tools/thps3_levels/thps3_levels.ts` convertNodeArray: applied right after TPN-THPS3's nodes
  (`thugproNew.applyNodes`), from the register's PC-THPS3-15 `objects`; the models from TPN-THPS3-6 (the level's
  THUGPRO model files). TPN-THPS3-3's `drop_sector` still drops our drawn copy (THUGPRO's model draws the object).
- Register: `tools/compat/thugpro/thps3_levels.json` port_changes PC-THPS3-15 (op set, required false, `online` note,
  `objects` with level / name / model / what / kill_triggers, `kept_vehicles`); README row. `--no-port-changes`: the
  entry is off - THUGPRO's Vehicles as before (checked: Foun, Tok).
- Tests: `tools/level_port/movers.test.ts` (5): model file names, the level filter, trigger objects from a .col, a
  bucket made a solid game object with its kill stand-in to the nearest restart, a car untouched, a trigger without a
  kill, a second run / another model changing nothing.

## Inventory (every Vehicle-class node of the converted levels, all four games, default options)

Non-cars and the cars next to them; the 225 Vehicles in all: THPS3 burn 4, la 6, rio 8 cars + 8 carplant boxes, sub 1
car + 2, tok 18 cars + 5, foun 1; THPS4 skate shop 2; THUG1 fl 30, hh 4, hi 15, nj 23, ny 32, ru 3, sd 11, vc 16 (+
the non-cars below); THAW bh 3, dn 2, dt 8, ho 4, ma 1, sr 1, kyoto 4, hof 2, sz 3. Every one not listed below is a car
(skeleton car, a THUG2 / THUG1 car model, Car_Generic01 or a level's car / driveable / goal-car script).

| Level | Object | Model | Car? | Before | After |
|---|---|---|---|---|---|
| THPS3 foun | Obj_Bucket (molten-metal bucket) | THUGPRO Obj_Bucket | no | Vehicle (TPN-THPS3-3) | GameObject, solid; TRGP_KillSkaterBucket stand-in kills (SK3_KillSkater nodename=TRG_CTF2_Restart_3P) |
| THPS3 sub | TRG_Plane (light plane) | THUGPRO light_airplane01 | no | Vehicle (TPN-THPS3-3) | GameObject, solid; TRGP_Killskater01 kills (nodename=TRG_Restart_Horse05); TRGP_CarPlant01 stand-in, no script |
| THPS3 sub | TRG_IceCreamTruck_Carplant_collision | THUGPRO CarPlantNode (invisible trigger box) | no | Vehicle (TPN-THPS3-2 add) | GameObject riding on the truck (Obj_LockToObject); TRGP_CarPlant01 stand-in, no script |
| THPS3 sub | TRG_IceCreamTruck | THUGPRO icecreamtruck | **yes** (wheels, roof rails, skitch hook) | Vehicle | Vehicle |
| THPS3 tok | TRG_shinkansenEast / West | THUGPRO bullet_train (7 collision boxes) | no | Vehicle | GameObject, solid |
| THPS3 tok | TRG_Plane_NorthWest / SouthWest / East | THUGPRO plane_jet | no | Vehicle | GameObject, solid; Box02 stand-in, no script (THUGPRO's wallplant bonus Box02Script not brought) |
| THPS3 tok | 18 TRG_Car* | THUGPRO Veh_Focus / Galaxie / Smart_car | **yes** | Vehicle | Vehicle |
| THPS3 rio | 8 TRG_Car_Path*_carplant_collision | THUGPRO CarPlantNode (6) / CarPlantNode_Bus (2) | no | Vehicle (TPN-THPS3-2 add) | GameObjects riding on the cars; TRGP_CarPlant01 stand-in, no script |
| THUG1 nj | TRG_TrainA01 / 02 (El trains) | THUGPRO veh_el_train | no | Vehicle (TPN-THUG1-2) | **kept Vehicle**: its node array gives 4 roof grind rails, 2 skitch hooks and a collision trigger - Vehicle-only parts |
| THUG1 hi | TRG_HI_Boat01 | THUG2's veh_ba_boat | no | Vehicle (TPN-THUG1-2) | **kept Vehicle**: 17 grind rails, a ledge and a ladder (climbing nodes) in the model's node array; THUG2's own BA boat is this Vehicle |
| THUG1 ru | JO_Tank_Base, _01, _02, G_COMPNOT_gr_Tank | THUGPRO veh_tank_ru | no (static tanks) | Vehicle (THUG1's own class) | kept: THUG1's own data, not THUGPRO's or ours |
| THUG1 hh | TRG_SE_JO_Qp_Ghost | veh_skitchnode | no | Vehicle (THUG1's) | kept: a skitch node (skitch is Vehicle-only) |
| THUG1 sd / vc / hh | karts, leaf blowers, limo, mini baja, ATVs, the stadium tram (sdkart on Car_Generic01) | THUG1 / THUGPRO models, skeleton car | driveable / goal vehicles | Vehicle (THUG1's) | kept |
| THPS4, THAW | - | - | all cars | Vehicle | Vehicle |

No PC entry for THUG1, THPS4 or THAW: nothing there qualifies (THPS4's and THAW's Vehicles are all cars; THUG1's
non-cars are THUG1's own Vehicles or need the Vehicle-only rails / skitch). `movers.ts` is shared: a THUG1 entry
(e.g. the tanks) would be a register list plus one call in thug1_levels.

## Load checks (workspace C:\Games\THUG2-2, under its lock)

See the results section below.

## What the user should check by hand

1. Foundry (`-level foun`): the bucket moves out along its path and tips (as before); it is solid (ride into its side);
   dropping into it kills the skater and puts him back at the restart nearby; nothing crashes when the bucket appears.
2. Tokyo (`tok`): the bullet trains run (both directions) and are solid; the three jets fly their paths; the cars
   drive as before.
3. Subway (`sub`): the light plane flies and is solid where it can be reached (and kills on its trigger); the ice-cream
   truck drives as before.
4. Rio (`rio`): the cars drive; nothing visible changes (the carplant boxes are invisible).
5. Nothing crashes loading or after a few minutes in these four levels.

## Online

Client-side only - moving level objects aren't sent over the network. THUGPRO's players have Vehicles where ours have
game objects; all 16 objects are AbsentInNetGames, so neither side makes them in a network game. **Needs a hand check
online** (not tested: agents never go online).

## Reconvert

The lead must reconvert THPS3's Foun, Sub, Tok and Rio (`node tools/thps3_levels/thps3_levels.ts convert` - or all)
and run make_menu_level for the main install. The other games' output is unchanged by this branch.
