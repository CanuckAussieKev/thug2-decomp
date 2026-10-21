# shared-fields - every node kind's common fields shared, last, in all four converters - 2026-10-21

Follows docs/pending/kyoto-crash.md (PC-THAW-13, rails only) and docs/pending/shared-fields-audit.md (the
recommendation: share last, every node kind, all four games). Two agents' work (the first one stopped; continued and
finished here).

## The change
- `tools/level_port/nodes.ts`: `shareNodeFields(data, arrayName, prefix)` - a converter's LAST step on a level's
  compiled .qb (scripts appended): the node array's common fields (everything but `OWN_KEYS`: Pos / Position / Angles /
  Name / Links / TriggerScript) of every node kind as `<level>_inc_<n>` global structures the nodes name (one per set of
  identical fields at least two nodes have; `shareParts`, THUG1's old compressIncludes grouping), defined just before the
  node array; every other byte kept. As THUG2's own levels (`ncomp_*`) and THUGPRO's compiler do it.
  - Runs once (a node array naming a global structure already is an error) and checks itself: `sharedDifferences`
    (every node resolves - its includes put in place - to the same fields as before; every other global the same) or
    it throws.
  - `assertUnshared`: the text passes that read nodes' own fields (stripMissingTrickObjects, absentInNetGames,
    netRestarts, groundRestarts) refuse a node array that is shared already.
  - `unshareNodeFields`: make_menu_level's front end reads the converted THPS4 skate shop's nodes by their Class -
    it gets them expanded.
- Converters (each shares in its .qb write, so the TrickObject strip / groundRestarts rewrite go through it too):
  - THPS3 `writeLevelQb`: **PC-THPS3-18** (required; new - main's PC-THPS3-17 is the collision-only Create / Kill).
  - THPS4 `writeQb`: **PC-THPS4-14** `shared_fields` (required; new).
  - THUG1 `writeQb`: **PC-THUG1-17** now this (thug1_port.compressIncludes removed - it shared before the text passes,
    so groundRestartsAt never saw restarts whose Class went into an include: the gap the audit found).
  - THAW `writeLevelQb`: **PC-THAW-13** `shared_parts` now every node kind, last (was rails only, before the text
    passes).
  - Register entries: tools/compat/thugpro/README.md and the four `<game>_levels.json`.
- THUG1's THUGPRO script check (`thugpro_new.scriptCheck`, TPN-THUG1-3): with the nodes no longer shared when it runs,
  it saw geometry without a scene sector as such and refused 8 THUGPRO "set" scripts main's output had (NJ's
  TRGP_LO_forleasedown03-08Script, SD_Race_SwitchGate2, HI_Initialize_Objects, HN2_Heli_Move) - all only `Kill` that
  collision-only / trigger geometry. Before, compressIncludes' sharing had hidden the nodes' Class and they passed by
  accident. Now a name of geometry kept without a sector passes when the script only uses it as `Create / Kill /
  IsAlive Name=` (THUG2's Create 0x005A2E50 / Kill 0x005A32C0 skip a missing sector - PC-THPS3-17's finding); any other
  use is still refused. Output: the same scripts as main.
- Tools: `tools/level_port/components.ts` (a level's script-component count, the audit's method),
  `tools/level_port/compare_nodes.ts` (two conversions compared as THUG2 resolves them: every node with its includes
  in place, every other global, every script's bytes, and the component counts).
- DLL (NOT ORIGINAL, log only): `Script::LogScriptPoolUse` (src/game/script/qb.cpp) - once a `-level` level is up
  and settled, start_level.cpp logs `script pools in use in <level>: CComponent <used> (peak <peak>) of <count>,
  CStruct ...`; tools/game/load_check.ps1 prints the CComponent / CStruct part and any `pool ... full` line.

## Verified
- After merging main (2026-10-22: THPS3 / THUG1 peds, the strip fix, HD textures - off by default -, THPS2 / THP8):
  all four games converted twice, with main's tools and with this branch's (`--thug2 C:\Games\THUG2-2`); `compare_nodes`
  main vs branch: **all 56 levels** - every node resolves to the same fields, every other global and every script the
  same. The branch's output also compares equal (same resolution, same component counts) to the pre-merge conversion the
  first load checks ran on, so those results stand for the merged output. Converters' selfchecks 0 failures (thps3 329,
  thps4 139, thug1 2190, thaw 735 files); `npm run typecheck` / `lint` / `test` (299) pass; thug2_check: 569 early
  self-tests pass.
- Script node components per game (the levels' node arrays as THUG2 parses them, main -> branch): THPS3 185,732 ->
  121,448 (35% fewer; shp 26,503 -> 16,643), THPS4 208,814 -> 122,081 (42%; jnk 28,041 -> 15,746), THUG1 259,141 ->
  259,141 (main already shared, before the text passes - now after them: same count), THAW 270,047 -> 232,655 (14%;
  kyoto 26,056 -> 23,555).
- In the game (C:\Games\THUG2-2, `-level <name>`, load_check.ps1; pools logged once the level is up 90 frames): every
  level loads; the pools once up, of CComponent 184,000 / CStruct 49,600:
  - THPS3: CComponent 56,271 (thps3_tut) - 70,411 (shp), CStruct 13,852 - 17,305
  - THPS4: 56,645 (motox) - 71,501 (jnk), CStruct 14,040 - 17,775 (thps4_skateshop: a shop, not a level's load script)
  - THUG1: 55,775 (thug1_nj_skateshop) - 83,530 (ny, the most of all: 45%), CStruct 13,739 - 19,856
  - THAW: 58,999 (skatepark) - 76,273 (kyoto: the level whose crash started this), CStruct 14,502 - 18,494
  - Main's output for comparison: alc 77,719 -> 69,593, can 69,792 -> 65,152, foun 65,375 -> 62,351, shp 78,840 ->
    70,411, tok 74,032 -> 67,793, z_ho 70,804 -> 69,364. CStruct rises a little (the shared structures are globals:
    tok 16,357 -> 16,679). No pool full / 80% warning for the script pools in any level.
- Failures - all the same with main's conversion (re-run with main's output installed), so not this change:
  - tok (THPS3), z_ho (THAW), shp (THPS3, at ~120-150 s) crash in trigger scripts (a skater / ped feeler's TriggerNode:
    CFunc_GetArraySize; z_ho a CStruct::GetChecksum on null through InNetGame's slot); vn (THUG1) crashes at
    THUG2PM.exe+0x82BA2 (anim controller's FrameIndex).
  - hh (THUG1) hangs once loaded: a loop that never ends in CAnimBlenderComponent::PassToChannels walking its channel
    list (a minidump of the hung game: CScript::UpdateWait -> PassToChannels -> Ended / IsHead; a circular list). Its
    window stops responding while a core stays busy, which game_health's hang watch doesn't count (it waits for an idle
    hang) and run_game's last PrintWindow then waited for ever (the first pass: 51 min): run_game.ps1 now reports a
    window that doesn't respond at the end of the run as trouble and closes it.

## The lead must
- Reconvert all four games (every level's .qb changes; scenes, collision, textures don't):
  `node tools/thps3_levels/thps3_levels.ts convert`, the same for thps4_levels, thug1_levels, thaw_levels, then
  `node tools/menu_level/make_menu_level.ts` for the main install.
- Install the DLL only for the pool log (not needed for the data change).
- Not from this change, open on main: the tok / z_ho / shp trigger-script crashes, vn's FrameIndex crash, hh's
  endless PassToChannels loop (above).
