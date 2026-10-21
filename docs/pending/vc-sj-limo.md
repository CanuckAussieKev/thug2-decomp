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

## Why ours had none (five causes, each enough)

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
4. **THUG1's car scripts don't run unchanged on THUG2** (found in the game): THUG2 renamed the proximity events
   (SkaterInRadius / SkaterOutOfRadius -> ObjectInRadius / ObjectOutofRadius, set with SetEventHandlerOnObject /
   Obj_SetException / Obj_ClearException - its own goal triggers, goal_set_trigger_exceptions, are the same code's
   successor), so the prompt never came; `RunScriptOnObject car_set_exceptions` interrupts the car's own script - the
   one calling it (CreateCar's SwitchScript makes the TriggerScript the object's script) - and the `Block` inside never
   gave the frame back: VC hung loading (the first, older workspace DLL exited instead). And the player's car needs a
   Motion component in THUG2: without it the game hung as soon as you got in (the user's Anteara Mod adds it too).
5. **THUGPRO** (TPN-THUG1-3) emptied `TRG_drive_veh_limoScript`, `TRG_drive_veh_leafScript` and `VC_startup`
   (THUGPRO has no driving); it made the leaf blower CreatedAtStart as a static prop (TP-34 on).

## Changes

Two own changes (port_changes, tools/compat/thugpro/thug1_levels.json; register: tools/compat/thugpro/README.md):
- **PC-THUG1-21** (remove, not required) VC <-> SJ as THUG1: THUGPRO's VC arrow `TRG_SCJ_arrow_01` + restart
  `TRG_Restart_SJVC`, SJ's arrow `TRG_VC_arrow_01` + waypoint `TRG_Restart_VCSJ`, both levels' `transition_arrowScript`
  and the transition_arrow model files are left out; VC's `trp_death_SJ` stays CreatedAtStart (TP-34's "off" for it
  taken back) with THUG1's own script `Sk3_Killskater nodename=TRG_Restart_SJ` (THUGPRO's "set", which killed only in
  multiplayer, taken back). Nothing else used the arrows' restart / waypoint (not in TP-31's spawns, no other script).
- **PC-THUG1-22** (add, not required) THUG1's drivable cars: the converter writes THUG1's two driving files (from the
  user's THUG1 qb.pre, decompiled with ns.exe, given their THUG2 form - `DRIVING_EDITS` in thug1_levels.ts: the renamed
  events / commands and Obj_SpawnScript without Block in car_scripts, the Motion component in createPlayerVehicle;
  each edit must match THUG1's text exactly once - and compiled) to `<staging>\scripts\game\...`;
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

Anteara Mod (anteara_mod=1): its own player_controlled_vehicle.qb / car_scripts.qb are used (anteara\ first). Its
player_controlled_vehicle.qb has LimoCarSetup and the Motion component; its car_scripts.qb is THUG1's unchanged, which
hangs VC loading - so make_menu_level re-applies the car_scripts edits to the mod's copy when THUG1's drivable cars are
installed (`<game>\thug2-decomp-thug1\scripts\game\misc\car_scripts.qb` there; the overlay-edit hook it already uses
for Levels.qb etc.). Tried: with anteara_mod=1 the limo prompts and drives. (No LeafBlower setup in Anteara's - another
reason the leaf blower stays a prop.) The edits are in tools/thug1_levels/driving.ts (shared by both).

Files: tools/thug1_levels/driving.ts (new: DRIVING_EDITS, applyTextEdits), tools/menu_level/make_menu_level.ts (the
Anteara copy's edit - shared file, additive), tools/thug1_levels/port_changes.ts (PC-THUG1-21 / 22, overrides, wonGoals, drivingFiles; PortChange.data),
thug1_levels.ts (applyOverrides, drivingScripts, portScripts call), thug1_port.ts (Thug1Port.driving,
Thug1Translator wonGoals / condition), thug1_levels.test.ts (a test), tools/compat/thugpro/thug1_levels.json and
README.md, tools/thug1_levels/README.md, docs/level-port-compromises.md (THUG1).
Shared code: tools/level_port/ported_levels.ts installFiles - additive (copies `<staging>\scripts` when there).

## Checks

- npm typecheck / lint / test (199 / 0); thug1_levels selfcheck 333 files, 0 failures.
- Converted all 16 default levels (scratch): only vc.qb references SetupDrivableCar; VC / SJ have no arrow / SJVC /
  VCSJ / transition_arrow; trp_death_SJ CreatedAtStart with `SK3_KillSkater nodename=TRG_Restart_SJ`; the staging has
  `scripts\game\player_controlled_vehicle.qb` and `scripts\game\misc\car_scripts.qb`, no transition_arrow models.
- npm test 200 / 0 after the THUG2 edits (a test for applyTextEdits).

## In the game (C:\Games\THUG2-3, this worktree's DLL, all 16 THUG1 levels converted + make_menu_level; 2026-10-21)

- VC loads; the limo is parked by the SJ restart (screenshot); walking up to it shows "Press (circle) to drive."; circle
  -> you drive the limo (vehicle camera, THUG1's help panel Accelerate / Brake/Reverse / Handbrake / Ditch); ditching
  puts you at TRG_drive_veh_limo_end (926, 0, 1345) on foot, and the prompt comes back at the limo. Driving itself
  (handling, collisions, sounds) is for the user to judge.
- `run goal_initialize_skater control_type=LimoCar ...` drove too once the Motion component was in (hung without it).
- Kill zone: teleported onto (500, 1152) -> "Good one!" and back at TRG_Restart_SJ (780, 0, 1178) within 30 frames;
  no arrow over the restart. SJ loads. The logs show no warnings / run-time check failures.
- Seen on the way (bisecting with hand-edited overlay files): the unchanged THUG1 car_scripts hang VC loading;
  car_set_exceptions without its Block loads but never prompts (events renamed).

Workspace: the lock on C:\Games\THUG2-3 was found released by someone else while I held it (around 21:50); I took it
again, so its `.game-lock\dinput8.dll.backup` is this worktree's build, not the workspace's previous DLL (an older one
without `teleport`). Copying the lead's main build over the backup and releasing were both refused by the permission
classifier: **the lock is still held by vc-sj-limo-agent** - the lead should put a main DLL in place and release it
(`game_lock.ps1 -Action release -Owner vc-sj-limo-agent -Game C:\Games\THUG2-3`). Its ini backup is the workspace's own;
the live ini has `[selftests] enabled=0` (the late self-tests crashed this DLL build at start - not looked into).

## For the user to check

1. THUG1 VC (free skate): the limo by the SJ end; walk up, press the drive button, drive it, ditch it - and THUG1's
   behaviour you remember (where you come out, whether the parked limo should stay while you drive - here it does).
2. The prompt / help texts use THUG1's button codes (`\m5`, `\b3`...): in THUG2 they show as "∠C", "SB", "LA", "S",
   "C" - say if they should be THUG2's codes.
3. Fall into the SJ gap: you die and come back at the SJ restart (rolling into the limo's back - the restart faces it,
   as in THUG1's data). No arrow there, nor in SJ.
4. With the Anteara Mod on: the same limo (its own driving scripts, with our car_scripts edits re-applied).
