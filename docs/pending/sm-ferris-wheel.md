# THAW Santa Monica's ferris wheel: turning, cars upright, grindable moving rails - 2026-10-21

Branch `worktree-agent-a5cf86f807a8a36c9`. The user's decision: "Try to be most authentic with THAW. If we can make it
rotate and grind it, do it." Online: keep THUGPRO's frozen wheel + static rails (TP-8) unless both sides can agree
otherwise.

## How THAW runs the wheel (its z_sm_net.pak / z_sm_scripts.qb, data\models\Veh\Veh_SM_FerrisCar)

- `Z_SM_PacificParkFerrisWheel`: a LevelObject (CollisionMode=Geometry, CreatedFromVariable FERRISAFRAME_BEFORE - on
  in THAW's non-story games), TriggerScript `Z_SM_PacificParkFerrisWheelScript` -> `SM_FerrisWheelRotation`:
  `if NOT wheel:Obj_IsRotating` / `if NOT InMultiplayerGame` / wait until `ZoneLoadedAndParsed Z_SM` / the 12 cars
  `Z_SM_TRG_Veh_FerrisCar01..12:Obj_LockToObject ObjectName=Z_SM_PacificParkFerrisWheel (-67.2, y, z) no_rotate`
  (offsets on a 576-unit circle) / `wheel:Obj_RotX speed=15` (continuous, 15 degrees a second) / a spawned loop sound.
  THAW turns the wheel only outside multiplayer games.
- The 12 cars: `Class=Vehicle Type=Default AbsentInNetGames NoSkitch model="veh\veh_sm_ferriscar\veh_sm_ferriscar.mdl"`
  (SuspendDistance 10000, LOD 3000 / 3500). THAW's model (`.mdl` - the scene layout of its levels -, `.tex`, `.col`
  182 faces, `.qb`): one sector, 6 textures, and a node array with two rail nodes `TRG_Rail01` -> `TRG_Rail02` at
  (-89.9, -25.2, -/+32.1) (Metal, CreatedAtStart, LipOverride, TriggerScript TRG_Rail01Script ->
  SM_FerrisCarBalanceReset, which eases lip balance by difficulty).
- THUGPRO (its installed Z_SM_scripts.prx, read only): the same SM_FerrisWheelRotation, but the wheel's script emptied
  (frozen), the cars `NoRail` (its own model copy with the same rails) and 24 static rails (TP-8) at the cars' places.

## What our port did before

The wheel was drawn (a LevelObject) but never turned: `ZoneLoadedAndParsed` isn't THUG2's, the translator made the
condition false and the wait loop never ended. The cars were parked sedans (FALLBACK_CAR: THUG2 hasn't the model)
hanging round the wheel, the locks left out (`object:member` on an object scripts couldn't address), and TP-8's 24
static rails were there in every game.

## What it does now

- **Converter** (tools/thaw_levels):
  - PC-THAW-14 `thaw_models` (register: tools/compat/thugpro/thaw_levels.json port_changes, README): `convertThawModel`
    converts THAW's own model into `<staging>\models\veh\veh_sm_ferriscar\veh_sm_ferriscar.mdl.xbx / .tex.xbx /
    .col.xbx / .qb` (the scene through `convertScene`, collision `convertCol`, the node array's rails with THAW's
    entries THUG2 reads - LipOverride kept; their TriggerScript left out, SM_FerrisCarBalanceReset not brought).
    make_menu_level copies it into the overlay's models. `PortedNodes.thawModelVehicle` makes the cars THUG2's mover
    form (Barcelona's drawbridges': Vehicle, the model, no skeleton - car_add_components gives the model's collision,
    node array and a rail manager), addressable by scripts (VEHICLE context), so THAW's locks are kept.
  - THAW's `ZoneLoadedAndParsed <zone>` is true (thaw_scripts.Translator.condition): the wait loop ends at once.
    No other THAW level uses it (checked: all 15 net archives' scripts).
  - PC-THAW-15 `net_only_rails`: TP-8's 24 static rails lose CreatedAtStart; a ghost GameObject
    `Z_SM_TRG_G_FerrisNetRails` (created at the start) runs `if InNetGame { create name=... x24 }`.
- **Engine** (src/game/obj/lock_obj.h / .cpp, new; self-tests src/hook/selftests_lock_obj.cpp): the lockobj component
  (0x5CB5CB7E, vtable 0x006460E4) rewritten in part - InitFromStructure 0x0041F520, Update 0x0041FF90,
  CallMemberFunction 0x0041F410 - plus THAW's `no_rotate` (NOT ORIGINAL: flag bit 0x8 of +0x8C, set / cleared by
  InitFromStructure; Update puts the object's own matrix and display matrix back after the lock's update 0x0041F760,
  keeping the lock's position). THUG2's lock always took the target's orientation (0x0041FD14: matrix = target's), so
  without it the cars would turn upside down with the wheel. Parameter names from THAW's dbg names: off, ObjectName,
  the unnamed vector / BoneOffset, id, bone, LagRange, lagwobble, Angles.
  Not rewritten yet (GameFunction / untouched): the update itself 0x0041F760 (~2 KB: bone path, lag), the destructor
  body 0x0041F400, scalar destructor 0x0041F440, constructor 0x0041F500, factory 0x0041FF20.
  Switch off: `[replacements] disable=lock_obj.cpp` (then `no_rotate` is ignored: the cars turn with the wheel).

## Online (the choice, in the register and docs/level-port-compromises.md)

THAW itself freezes the wheel in multiplayer games and leaves its cars out (AbsentInNetGames; also THUG2's rule for
vehicles in network games, PC-THAW-11). THUGPRO keeps frozen cars (NoRail) with static rails. Ours online: wheel frozen
(THAW's own check), no cars, TP-8's static rails created by the ghost's script - the same rails in the same places as
THUGPRO's, so grinding agrees. Difference left: THUGPRO players see frozen cars (and may stand on their collision); we
don't draw them. Not tested online (agents never go online) - for the user.

## Checks

- npm typecheck / lint / test (198 pass; new: netOnlyRails in thaw_levels.test.ts); `thaw_levels.ts selfcheck`: 735
  files, 0 failures.
- thug2_check: argument sizes 0 MISMATCHED, all 523 early self-tests pass; TestLockObjInit (40 seeds: every parameter
  combination, the target by id / name / none, edge priorities) and TestLockObjNoRotate (Update locked / unlocked vs
  the original; no_rotate set / cleared / by off; orientation kept, position the lock's). Mutations
  (out\scripts\lock_mut.ts): 25 of 26 caught; `priority-int` (int subtraction instead of the double + _ftol) is
  equivalent - both wrap INT_MIN to 0x7FFFFFFF.
- check_code_writes: no overlaps; audit_game_function_calls: 0x0041F760 unknown to Ghidra (its RET is plain - read at
  0x0041FF14); scan_register_assumptions: only the known lines.
- Game (C:\Games\THUG2-3 under its lock; all 15 THAW levels converted, make_menu_level; this branch's DLL; `-Level
  z_sm`, never online): loads, no exception / run-time check / CRT report; the model files read from the overlay.
  Captures from the automation camera (side view, 4 s apart; copies in out\ferris\captures, not committed): the wheel
  turns, the 12 THAW gondolas hang upright from the rim. A temporary log (not committed) of each car every 100 frames:
  position on the 576 circle advancing 25.0 degrees per 100 frames (15 degrees a second), up vector (0, 1, 0) always,
  and the rail manager's world rails (after 0x0042BA10, what the skater's rail search runs first) at the car's position
  plus (-89.9, -25.2, -/+32.1) - the rails follow the cars.

## For the user to try (NOT played yet)

1. Santa Monica (THAW), free skate: the ferris wheel turns slowly, the gondolas hang level all the way round.
2. Grind a gondola: ollie onto the rail along a gondola's outer roof edge (the side facing away from the wheel) - the
   grind should carry you up / round with the car; lip tricks on it work (LipOverride). Jump off at the top for air.
3. Ride / bump the gondolas and the wheel: collision moves with them (THAW's car collision, the wheel's own).
4. Online with THUGPRO (by hand only): the wheel stands still for both; the 24 static rails grindable where THUGPRO's
   are; THUGPRO's frozen cars aren't drawn on our side.
5. Reconvert: the lead must run `thaw_levels.ts convert` (all, so levels.json stays whole) and make_menu_level for the
   main install, and install a DLL with lock_obj.cpp.
