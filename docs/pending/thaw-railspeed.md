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
