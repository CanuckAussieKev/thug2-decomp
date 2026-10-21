# thaw-railspeed - the THAW converter's RailAccelerate passes the rails' rate on

Follow-up to editor-map1 row 1 ("the THAW converter's RailAccelerate never passes rate= on, so every ported rail runs
at 0.05"). Tools only; NOT run in the game (no game runs for this task).

## What THAW does (read from the user's THAW install, data\pak\qb.pak.wpc and every zone .pak.wpc)
- skutils.qb's `RailAccelerate`: `if TriggerType {land onto}` / `KillSpawnedScript name=RailAccelerate2` /
  `Obj_GetID` / `<objid>:Obj_SpawnScriptLater RailAccelerate2 params=<...>` - it hands **all of its own parameters**
  to the loop. `RailAccelerate2 {rate=0.05}`: while OnRail and climbing, speed * (1 + rate) a frame.
- No node has RailAccelerate as its TriggerScript directly: 223 per-node scripts call it (Z_..._TRG_..._Script, a few
  named ones in *_scripts.qb). Arguments in THAW's data:
  - 128 calls without one (0.05);
  - 85 with an inline `{rate=...}` / `rate=...`: 0.01 (1), 0.02 (6), 0.025 (4), 0.03 (3), 0.04 (1), 0.05 (13),
    0.055 (2), 0.06 (2), 0.08 (6), 0.09 (5), **0.1 (41)**, **0.5 (2, z_el's metro HQ rails)**, **2 (Z_OI_MegaBoost_Extreem)**;
  - 6 in Santa Cruz written `RailAccelerate = 0.005` (... 0.0075, 0.01): a local assignment, not a call - those rails
    do nothing in THAW either, and the port keeps them as they are.
- So other rates are used widely (most of East LA's 33 at 0.1, Kyoto / Casino / Oil Rig mixed): the fix changes play,
  not just correctness. Before it, every ported rail ran at 0.05 (twice as slow as THAW on the 0.1 rails, 10x on 0.5).

## What changed
- `tools/thaw_levels/thaw_scripts.ts` RAIL_SCRIPTS: `Obj_SpawnScript RailAccelerate2 params=<...>` (was without
  params). THUG2's Obj_SpawnScript (0x004617E0, src/game/obj/object.cpp) takes params= as a structure;
  `name=<...>` makes the caller's parameters a structure (src/game/script/parse.cpp, 0x2C with a name). NeverScript
  compiles it to `16 Obj_SpawnScript 16 RailAccelerate2 16 params 07 2C 01` (checked with ns.exe).
- Checked on a conversion into the worktree's out\thaw_test (`--out`, Z_MA Z_SZ Z_OI Z_EL; no workspace written):
  the rail scripts' calls keep their arguments (z_el: 30 x rate=0.1, 2 x 0.5, 1 x 0.05; z_oi: 0.055 x2, 0.08; Mall:
  12 plain) and the spawn carries params=<...>.
- Test: thaw_levels.test.ts "RailAccelerate passes its parameters ... on to RailAccelerate2".
- docs/level-port-compromises.md: the RailAccelerate line says so.
- Level editor (src/game/front/level_editor_chains.h): comment only. Its ed_RailSpeed already passes
  `params={rate=<rate>}` (the embedded bytes don't change); it never relied on the bug. The comment wrongly said THAW's
  own doesn't pass the rate - THAW's does; it was our converter's port. The editor's reading of a level rail's rate
  (rate=, else 0.05) now matches what the converted levels do.

## The user / lead
- **THAW levels must be converted again** (`node tools/thaw_levels/thaw_levels.ts convert` + `make_menu_level.ts`)
  for the rates to take effect: the RailAccelerate script is written into every converted level's .qb.
- To try by hand: East LA (z_el) - grind an uphill rail (e.g. near the metro HQ, rate 0.5): much faster climb than a
  Mall rail (0.05). NOT run in the game yet.

# thaw-railspeed2 - the loop never ran: THUG2's rail event is grind + onto, not land + onto

The user's test (main install, East LA, after the reconversion above): the uphill boost rails (metro HQ, rate 0.5, and
the others) "boost a little bit" but the skater slows and falls back instead of riding to the end. Tools + one
function of the decomp; NOT run in the game (no game runs for this task).

## Cause (evidence)
- THUG2's TriggerType (CTriggerComponent::CallMemberFunction 0x00434970 - read in Morten1337's thug2-decomp dump; also
  tools/level_port/script_port.ts's TriggerType notes) is true only when the trigger's events include EVERY non-negative number of its array (an
  array of arrays: any one of them).
- THUG2's rail code trips a rail node's trigger with grind + onto {13, 3} (the .rdata triple 0x0064BC28): landing on
  the rail (pushed at 0x0050AA9E) and each next segment in the rail update 0x0050DDF0 (0x0050E6D1); land + onto
  {8, 3} (0x0064BB98) only at 0x00508570 / 0x0050D706 - landing on the ground. CTriggerComponent::TripTrigger
  0x00434A60 sets the bits and runs the node's TriggerScript at once (SpawnAndRunScript).
- Our RailAccelerate checked `TriggerType [3, 8, -1]` (THAW's `{land onto}` as THUG2's numbers): onto AND land -
  never true on a THUG2 rail. RailAccelerate2 was never spawned; the "little boost" was THUG2's own Rail_Speed_Boost
  on landing on any rail. (The installed z_el.qb, decoded: `if TriggerType [3, 8, -1]`.)
- Everything else in the loop checked out on THUG2: Wait's `gameframe` unit (0x0044A0D0 -> one game frame),
  OnRail (0x005194B0: state 4, the rail state), Obj_GetPosition / the dot product, default parameters then the
  passed ones (CScript::SetScript 0x00474540), GetSpeed (|velocity|, 0x0045DC80) and SetSpeed (0x004FCE90: the
  velocity's length set) on the same velocity the rail update moves along the rail. The rail update adds only the
  rail gravity along the rail (no friction), so a per-frame multiply sticks; the skater's speed limit (0x00501850,
  Skater_Max_Max_Speed_Stat on the horizontal speed, every frame in every state - seen in Morten1337's dump) caps
  it, so even rate 0.5 / 2 doesn't run away: the skater climbs at the limit.

## THUGPRO / THAW
- THUGPRO (Morten1337's thugpro, source\code\qb\thugpro\thugpro_utils.q): RailAccelerate / RailAccelerate2 the same
  shape as THAW's, but RailAccelerate has NO trigger check (every trigger of the rail node restarts the loop; off-rail
  events end it at once as OnRail is false). Its Z_EL.q rails call `RailAccelerate rate = 0.5` / `{rate = 0.1}` as
  THAW's do. Its DLL sources (thugpro_proxy) touch rails only in the park editor's rail tool - nothing on grind speed or rail physics. It also has its own THUG3_RailBoost (a
  multiply every 2 frames up to MaxSpeed) for other levels - not used for East LA.
- THAW: its own scripts use `GetTriggerArray grind onto` for rail events (East LA's EL_Spring); RailAccelerate's
  `{land onto}` works in THAW's engine (not checked how - THAW's exe not read), not in THUG2's.

## The fix
- tools/thaw_levels/thaw_scripts.ts RAIL_SCRIPTS: `if TriggerType [3, 13, -1]` (scriptPort.triggerFlags(["grind",
  "onto"])). Test: thaw_levels.test.ts "RailAccelerate starts the loop on THUG2's rail event".
  Checked: Z_EL converted with `--thug2 C:\Games\THUG2-3 --out out/thaw_test` - RailAccelerate's TriggerType is
  [3, 13, -1] (the one byte that differs in the script part from the installed z_el.qb).
- The level editor's embedded ed_RailSpeed (src/game/front/level_editor_chains.cpp RailSpeedQb) had the same check:
  now [3, 13, -1] (one byte; the self-test TestEditorMapRailSpeed checks the bytes).
- docs/level-port-compromises.md: the RailAccelerate line says so.

## Debug log: [features] debug_rail_speed=1
- SetSpeed 0x004FCE90 (CSkaterCorePhysicsComponent's script member) rewritten in src/game/skater/core_physics.cpp
  (REPLACE): the unnamed float (0 without one); the velocity normalised (53-bit length, 1/length in double, all four
  components) and scaled; with its squared length under 0.001 the facing (+0x8C, all four words copied) instead.
  NOT ORIGINAL in it: with `[features] debug_rail_speed=1` in thug2-decomp.ini, calls from the rail speed-up loops
  (RailAccelerate2, the editor's ed_RailSpeedLoop) are logged to thug2-decomp.log:
  - `rail speed-up: RailAccelerate2 boosting (rate 0.500): speed 612 -> 918, height 1234` - a loop's first boost
    (a new loop or after half a second without one);
  - `rail speed-up: 1.00 s, 59 boosts: speed 640 -> 1100 (+460 a second; this boost to 1650), height 1234 -> 1600`
    - once a second while it boosts: the speed (game units a second, GetSpeed's) at the first and the last boost.
  A rail that doesn't climb boosts nothing and logs nothing.
- Self-test TestSetSpeed (selftests_skaterA.cpp, Early): original vs ours on stand-in components, 3000 variants
  (around the 0.001 threshold, exactly at it, zero, NaN, a signalling NaN in the facing) + 50 with no speed passed.
  Mutations (out-of-tree spec): 17 of 18 caught; the miss is equivalent in our build (the facing copied as a Vector
  assignment instead of memcpy - the SSE2 build copies the bits too).
- Switch off: `[replacements] disable=0x004FCE90` (the log goes with it).
- thug2_check: THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe - 0 MISMATCHED, all 435 early self-tests pass;
  check_code_writes: no overlaps; npm typecheck / lint / test pass.

## The user / lead
- **The THAW levels must be converted again** (`node tools/thaw_levels/thaw_levels.ts convert` + `make_menu_level.ts`):
  RailAccelerate is written into every converted level's .qb. The DLL must be rebuilt too (SetSpeed + the log; the
  editor's speed-ups).
- Checklist (NOT run in the game yet):
  1. In thug2-decomp.ini `[features]` set `debug_rail_speed=1`.
  2. East LA (z_el): grind one of the metro HQ uphill rails (rate 0.5) from its start - the skater should climb at
     a steady high speed (the skater's speed limit) all the way to the end, not slow and fall back.
  3. thug2-decomp.log: a `rail speed-up: RailAccelerate2 boosting (rate 0.500) ...` line when the grind starts, then
     `rail speed-up: ... s, N boosts: speed ...` once a second while climbing. No line at all = the loop still
     didn't run (say so).
  4. One of East LA's rate 0.1 rails: a gentler climb boost (speed rising each second until the limit).
  5. A plain rail (no speed-up), uphill: no `rail speed-up` lines; the usual THUG2 grind (slows and falls back).
  6. Level editor: a rail point given a speed-up (Rail speed in the feature map) on an uphill segment - the same
     log lines with `ed_RailSpeedLoop`, and the climb carried.
  7. Set `debug_rail_speed=0` (or remove it) again afterwards.
