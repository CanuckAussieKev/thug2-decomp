# level-crashes-2 - the five ported levels that crashed or hung in free skate - 2026-10-22

The shared-fields load checks (docs/pending/shared-fields.md) found tok, shp (THPS3), z_ho (THAW), vn and hh (THUG1)
crashing or hanging with main's own conversion. Each reproduced in C:\Games\THUG2-1 (`-level <name>`, load_check.ps1,
this branch's DLL), the cause found from the log's new lines (docs/pending/crash-handling.md) + symbolize, fixed in
the converters, and each level then ran 6 minutes clean. Workspace ini as made (debug_mode=1).

## Per level

| level | what happened | cause | fix |
| --- | --- | --- | --- |
| tok (THPS3 Tokyo) | access violation in `ComponentCallMemberFunction` (a member call on an object without the component), soon after loading | **every TPN-THPS3 node was left out** ("node ...: differs from the register (CRC)") since qb.nameText's 2026-10-21 fix (#checksums in file byte order) changed `thugpro_new.nodeBytes`' text, so no node fingerprint matched the register any more - THUGPRO's Tokyo cars / planes / trains stayed our objects (no Vehicle `set`), the restarts TRG_Restart_EventL / R were missing, while THUGPRO's scripts driving them were still added | `thugpro_new.nodeBytes` writes names as the register's form (`#<hex digits>`) again: 2 nodes added + 23 set in Tokyo, Shipyard's waterslide sound node back, every other THPS3 level's TPN nodes too |
| shp (THPS3 Shipyard) | `GetArraySize: no array in SHP_TP_WindowCounter's parameters` (~2 min in) | THUGPRO's window counter (TPN-THPS3-4 `add`) uses globals of its own file (`shp_window_objects`, `SHP_NUM_WINDOWS_BROKEN`, `HAS_TRIGGERD_SECRET`) that weren't brought | `loadNew` keeps the globals of THUGPRO's file each loaded script names; `resolveGlobals`: a name THPS3 defines keeps THPS3's value (brought along by the translator), the rest are written as THUGPRO's (`globalsNs`). `BDJ_SHP_ANNOUNCE_ON` is THPS3's level flag 14 but a 0 / 1 variable to THUGPRO: its `Change BDJ_SHP_ANNOUNCE_ON=0 / 1` becomes `UnSetFlag / SetFlag flag=BDJ_SHP_ANNOUNCE_ON` (`adaptScript`, the same 16 bytes), so THPS3's announcer scripts still see it |
| z_ho (THAW Hollywood) | `nodes: no node named 0x00000000` then CStruct::GetChecksum on null - a trigger script (skater's feeler) | the translator guards `<param>:member` with `IsAlive name=<param>`; THAW's scaffolding / roof shatter scripts do `Obj_GetID` `<ObjID>:Obj_GetPosition` run by the skater, whose id is 0 - IsAlive (0x005A5380 -> 0x005A4EF0) looks the id up as a node (GetNode, asserting) and crashed | `<param>:member` is guarded by `ObjectExists id=<param>` (the object tracker's lookup - the one the call itself makes; THUG2's own scripts' guard). A node's `name:member` keeps `IsAlive name=` (Translator.guardCondition). Also changes THUG1 output (the same guard) |
| vn (THUG1 Venice) | `anim: no animation 0x1CA1FF20 (default) in the set 0xA431C689 (Anl_Seagull)` -> FrameIndex 0x00482BA2 | THUGPRO's 3 seagull Pedestrians (TPN-THUG1-2, THUG2's anl_seagull) name `AnimName=animload_anl_seagull`, but our load_level_anims (THUG1's own people's, THUGPRO's left out) never loaded it | the node pass adds a THUGPRO node's AnimName / Extra_Anims (THUG2's sets) to load_level_anims: Venice now loads animload_anl_seagull and animload_THPS6_human (THUGPRO's human node) |
| hh (THUG1 Hotter Than Hell) | the game stops responding once loaded (a core busy) | THUGPRO's `SE_ATVDriver02 / 04` (TPN-THUG1-3 `set`: `Obj_LockToObject ...; while { Obj_WaitAnimFinished }` - THUGPRO took THUG1's `Obj_PlayAnim anim=Ped_ATV_Drive` out) on our riders, who play no animation: the wait ends at once (CScript::Update calls UpdateWait straight after the command; the blender's check finds nothing playing), so the loop runs for ever in one frame. The new log line named it: `script: SE_ATVDriver02 ... has run 5000000 steps in one frame without waiting` | the converted .qb's idle loops `while { Obj_WaitAnimFinished }` get a bare `Wait` instead (CFunc_Wait without a count: a 0 ms timer, one frame - same 5 bytes): `script_check.yieldingIdleLoops`, in thug1_levels' writeQb. (The riders sit still, as on THUGPRO; THUG1's own had them play Ped_ATV_Drive - not done: Obj_PlayAnim stays out of the THUG1 translation) |

## The same class across the games
- `tools/level_port/script_check.ts` (new; `node tools/level_port/script_check.ts [--thug2 <THUG2>] <levels folder>...`):
  endless loops with no wait / spinning on Obj_WaitAnimFinished, `name=` constants of IsAlive / Create / Kill /
  Visible / Invisible / Shatter that aren't nodes, arrays read (GetArraySize, `(<a> [<i>])`) no file defines (THUG2's
  globals from its qb_scripts.prx count), nodes whose AnimName / Extra_Anims set load_level_anims doesn't load.
  Node lookups checked: IsAlive (0x005A3D10), Visible / Invisible (0x005A3B80), Create (0x005A32A0) - they use the
  node unchecked; Kill (0x005A32C0) returns when there's no node and Shatter (0x005A5090) finds the sector, so those
  aren't reported (sf2's Test_Shatter_For_Dave, nj's TRGP_LO_forleasedown03 / 04, z_dt's / z_el's were such).
  On a full conversion of all five games (THPS3, THPS4, THUG1, THAW, THPS2) after the fixes: **no findings**; before
  the hh fix it found exactly SE_ATVDriver02 / 04.
- The TPN-THPS3 node CRCs: no "differs from the register" warning in any THPS3 level after the fix.

## DLL (NOT ORIGINAL, log only)
- `CScript::Update` (src/game/script/cscript_exec.cpp): a script that runs 5,000,000 steps in one Update is logged once
  per script - `script: <name> (object id <id>) has run ... steps in one frame without waiting - a loop that never yields
  (the game hangs here)`. The hang itself is the original's.

## Verified
- Each level in C:\Games\THUG2-1, load_check.ps1 with this branch's DLL: tok, shp, z_ho, vn, hh each `running after
  ~365 s`, no exception / report, no `no node named` / `anim:` / `GetArraySize:` / `script:` line.
- Every other level of THPS3 (12), THUG1 (14) and THAW (15), fully reconverted with this branch: load_check 60 s
  (~95 s up) each - all running, loaded, no report. z_el once hung at start-up, before its level loaded (the log ends
  at the intro movies' skip lines); run again alone it ran 160 s clean - a start-up flake, not the level.
  THPS4 (11 levels; its output changes only through the `<param>:` guard): THPS4_RESULT.
- Before the fixes, the same runs reproduced each failure (tok with main's tools: the member-call crash; z_ho:
  no node named 0; vn: anim default in Anl_Seagull; hh: the hang, named SE_ATVDriver02 by the new log line).
- npm typecheck / lint / test (305 pass); new tests: thugpro_new (nodeBytes form, flag Changes, resolveGlobals),
  script_port (guardCondition), script_check. thug2_check on THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe: 611 early
  self-tests pass, crash handling 8/8; check_code_writes: no overlaps.
- NOT checked by a player: Shipyard's window counter secret (64 windows) and its announcer pause; Tokyo's THUGPRO cars,
  planes and trains as Vehicles now that their `set`s apply again (PC-THPS3-15 keeps the bullet trains our solid
  LevelObjects).

## Re-verified after merging main (0ac0ce9, 2026-10-22) - NOT run by a person yet
- Merge of main (skater SK1-SK6, OC5, THP8 phase 2, shared node fields...) clean, no conflicts. Build (out\build\lc2):
  thug2_check 0 MISMATCHED (4314 replacements), 627 early self-tests pass, crash handling 8/8; npm typecheck / lint /
  test (318 pass); check_code_writes: no overlaps.
- THPS3, THPS4, THUG1 and THAW reconverted with the merged converters into C:\Games\THUG2 (the main install - the user
  allowed it for this, the workspaces were busy; under its game lock) and installed (`make_menu_level.ts --game`).
  No "differs from the register" line in any conversion.
- `script_check.ts --thug2 C:\Games\THUG2` over all four games' converted levels: **no findings** in any.
- load_check.ps1 -Seconds 200, this branch's DLL, ini with automation=1 and a 1280x720 window (put back on release):
  tok, shp, z_ho, vn, hh each `running after ~206 s`, responding, loaded; their logs have no exception, run-time check,
  debug-CRT report, `no node named`, `anim: no`, `GetArraySize:`, `script: ... steps in one frame` or register line.

## The lead must
- Reconvert THPS3, THAW, THUG1 and THPS4 (every level's .qb may change: THPS3's TPN nodes / globals, the `<param>:`
  guards of the shared translator in THAW / THUG1 / THPS4, THUG1's anim sets and idle loops), then
  `node tools/menu_level/make_menu_level.ts`. Optionally `node tools/level_port/script_check.ts <staging>\levels` on
  each staging folder afterwards (expect "no findings").
- Install the DLL only for the new log line (no behaviour change).
