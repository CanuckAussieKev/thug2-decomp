# THUG1 Vancouver: no SJ arrow, the SJ kill zone back, the drivable limo (agent vc-sj-limo, 2026-10-21)

The user's report / decisions about THUG1's Vancouver (vc):
1. "In THUG1 Vancouver does not allow us to enter SJC. So we should remove the arrow too."
2. "In THUG1 there is a limousine that allows us to drive in front of where the killzone / arrow is, but in our
   version there is not. Why?"

## The limo: what it is in THUG1

- Node `TRG_drive_veh_limo` (VC.qb): Class Vehicle, Type Limo, model `veh\veh_limo\veh_limo.mdl` (THUG2 ships it),
  skeleton car, AbsentInNetGames, NoSkitch, **not** CreatedAtStart, no path (parked), at (872, 2.5, 1176) - right by
  `TRG_Restart_SJ` (770, 0, 1178), in front of the SJ kill zone `trp_death_SJ` (490, 0, 1152). Exit node
  `TRG_drive_veh_limo_end`. TriggerScript `TRG_drive_veh_limoScript` -> `VC_Driveable_limo` ->
  `SetupDrivableCar id=TRG_drive_veh_limo control_type=Limocar restart_node=TRG_drive_veh_limo exit_node=...`.
- Made by `VC_startup` (the level's startup_script) -> `VC_Create_DriveableLimo`: `if ! InMultiplayerGame` and
  `if GoalManager_HasWonGoal name=VC_Goal_Race4` (the limo race of THUG1's story) -> `create`. Also on the goal's
  success. So in THUG1 it's there once that goal is beaten (the user's save had it).
- Driving it: THUG1's `scripts\game\misc\car_scripts.qb` (SetupDrivableCar -> an inner-radius exception on the car ->
  "Press (circle) to drive." speech box -> car_begin_driving_run -> goal_initialize_skater control_type=LimoCar ...
  Exitable; ExitVehicleRequest -> back at the exit node) and `scripts\game\player_controlled_vehicle.qb`
  (GetVehicleSetup -> LimoCarSetup, ToggleUserControlledVehicleMode, createPlayerVehicle: a composite object with the
  Vehicle / VehicleSound / Input / Model components and a VehicleCamera object). VC's leaf blower
  (TRG_drive_veh_leaf, goal VC_Goal_Race2) works the same way.

## Why ours had none (four causes, each enough)

1. **THUG2 ships both driving script files empty** (2 bytes each, in qb_scripts.prx and loose) although its qdir.txt
   loads them and its engine keeps everything they use: the Vehicle / VehicleCamera / VehicleSound / StaticVehicle
   components (src/game/obj/component_types.cpp), the skater's SetDriving / UnsetDriving / Driving, PausePhysics,
   Vehicle_* members, BroadcastEnterVehicle, and its own goal_initialize_skater (calls GetVehicleSetup, MakeSkaterGoto
   ... NewScript=TransAm) and switch_control (TransAm -> ToggleUserControlledVehicleMode). Every command / member in
   THUG1's two files exists in THUG2 (checked against THUG2's CFunction and member tables). The user's own Anteara Mod
   restores the same two files (its car_scripts is THUG1's, word for word) and drives cars in THUG2.
2. **Our converter**: SetupDrivableCar isn't a THUG2 script (the file is empty) -> left out, VC_Driveable_limo empty.
3. **No goals in our ports**: `GoalManager_HasWonGoal` is left out, so the condition was made never-true
   (`GotParam thps3_port_never`) - the limo was never created.
4. **THUGPRO** (TPN-THUG1-3) emptied `TRG_drive_veh_limoScript`, `TRG_drive_veh_leafScript` and `VC_startup`
   (THUGPRO has no driving); it made the leaf blower CreatedAtStart as a static prop (TP-34 on).

## Changes

Two own changes (port_changes, tools/compat/thugpro/thug1_levels.json; register: tools/compat/thugpro/README.md):
- **PC-THUG1-21** (remove, not required) VC <-> SJ as THUG1: THUGPRO's VC arrow `TRG_SCJ_arrow_01` + restart
  `TRG_Restart_SJVC`, SJ's arrow `TRG_VC_arrow_01` + waypoint `TRG_Restart_VCSJ`, both levels' `transition_arrowScript`
  and the transition_arrow model files are left out; VC's `trp_death_SJ` stays CreatedAtStart (TP-34's "off" for it
  taken back) with THUG1's own script `Sk3_Killskater nodename=TRG_Restart_SJ` (THUGPRO's "set", which killed only in
  multiplayer, taken back). Nothing else used the arrows' restart / waypoint (not in TP-31's spawns, no other script).
- **PC-THUG1-22** (add, not required) THUG1's drivable cars: the converter writes THUG1's two driving files (from the
  user's THUG1 qb.pre, as they are - THUG1's .qb is THUG2's token format) to `<staging>\scripts\game\...`;
  `ported_levels.installFiles` copies a staging's `scripts\` into the overlay's (where they replace THUG2's empty ones,
  always used - THUG2's own goal editor's vehicle goals get their scripts back too). For the levels in the entry's
  `won_goals` (VC: VC_Goal_Race4) the files' scripts are callable by the level's scripts (portScripts extraProvided)
  and `GoalManager_HasWonGoal name=<goal>` becomes always true (`! GotParam thps3_port_never` -
  `Thug1Translator.condition`); TPN-THUG1-3's emptying of `TRG_drive_veh_limoScript` and `VC_startup` taken back.
  Result in vc.qb: `VC_startup` = VC_LoadingSounds + VC_Create_DriveableLeaf (still never: Race2 not counted) +
  VC_Create_DriveableLimo (creates the limo); `TRG_drive_veh_limoScript` -> `VC_Driveable_limo` -> SetupDrivableCar.
  Other levels' drivable cars (NJ, NY, FL, SD, HI, RU, HH, VC's leaf blower) are unchanged (their SetupDrivableCar
  still left out: only VC's vc.qb references it) - each can be added to `won_goals` once tried in the game.
- Mechanism: an entry's `overrides` {thugpro_new: [{level, name}], created_at_start: [{level, name}]}
  (`port_changes.overrides`), applied in `thug1_levels.setCompat` (`applyOverrides`; an override matching nothing is
  an error). `--no-port-changes` gives THUGPRO's entries back and no driving.

Online parity with THUGPRO: unaffected - the arrows / their restarts and the limo are AbsentInNetGames, the limo's
create checks InMultiplayerGame, kills are local. thugpro_compare.ts lists these as differing from THUGPRO (by design).

Anteara Mod (anteara_mod=1): its own player_controlled_vehicle.qb / car_scripts.qb are used (anteara\ first); they have
LimoCarSetup and SetupDrivableCar, so the limo works there too (no LeafBlower setup in Anteara's - another reason the
leaf blower stays a prop).

Files: tools/thug1_levels/port_changes.ts (PC-THUG1-21 / 22, overrides, wonGoals, drivingFiles; PortChange.data),
thug1_levels.ts (applyOverrides, drivingScripts, portScripts call), thug1_port.ts (Thug1Port.driving,
Thug1Translator wonGoals / condition), thug1_levels.test.ts (a test), tools/compat/thugpro/thug1_levels.json and
README.md, tools/thug1_levels/README.md, docs/level-port-compromises.md (THUG1).
Shared code: tools/level_port/ported_levels.ts installFiles - additive (copies `<staging>\scripts` when there).

## Checks

- npm typecheck / lint / test (199 / 0); thug1_levels selfcheck 333 files, 0 failures.
- Converted all 16 default levels (scratch): only vc.qb references SetupDrivableCar; VC / SJ have no arrow / SJVC /
  VCSJ / transition_arrow; trp_death_SJ CreatedAtStart with `SK3_KillSkater nodename=TRG_Restart_SJ`; the staging has
  `scripts\game\player_controlled_vehicle.qb` and `scripts\game\misc\car_scripts.qb`, no transition_arrow models.
- In the game: see below.
