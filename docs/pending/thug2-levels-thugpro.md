# THUG2's own levels vs THUGPRO's (TP-42)

The user's decision (2026-10-21): "For THUG2 node data, compare." THUGPRO's versions of THUG2's own 15 levels were
compared with THUG2's, level by level, from the user's installs (read only: THUG2's `C:\Games\THUG2\Game\Data\pre`,
THUGPRO's `%LOCALAPPDATA%\THUG Pro\data\pre`). Every difference is an entry in the register
(`tools/compat/thugpro/thug2_levels.json`, row TP-42 in `tools/compat/thugpro/README.md`), each **off** until the user
switches it on. NOT run in the game yet (see "Load check").

## How THUGPRO changes THUG2's levels (found in its install)

- Its `qb\game\Levels.qb` (thugpro_qb.prx) points each level at its files: `qb="levels\X\thugpro\X.qb"` - its own node
  array (and trigger scripts) for **TR, BO, BA, AU, LA** (thugpro_<l>.prx); `level_qb=` its own LA_scripts.qb;
  `thugpro_col_pre` its own collision for **AP, TR, DJ, SC** (`<L>col_thugpro.prx`); `thugpro_scn_pre` TR's scene;
  `more_level_qbs` a `<l>_thugpro.qb` for AP, AU, BA, BE, BO, PH, SC, ST; `setup_script` THUGPRO's own for **BA**
  (`thugpro_BA_Setup`) and **AU** (`AU_thugpro_Setup`).
- After loading, its scripts apply `<L>_LevelChanges` (in the `<l>_thugpro.qb`, and CA's in thugpro_dynamic.qb):
  `replace_script` (a node's TriggerScript), `kill`, banner / billboard models (`static ... replace_texture`), ... -
  and its host options `<L>_LEVEL_CHANGE_0n` (`<L>_option_items`: on / off by the host's SERVER_LEVEL_CHANGE_0n_ON
  flags; off after loading unless the level's setup sets the flag - as THAW's, tools/thaw_levels/thugpro_extract.ts).
- Everything else THUGPRO loads from the THUG2 install it runs on: `C:\Games\THUGPRO\Game\Data\pre`'s level archives
  are byte-identical to the user's THUG2 ones (checked: all 15 levels' _scripts / col / scn .prx).
- Not compared: Sk5ed (the park editor's level - THUGPRO replaces its node array and collision too; not one of the 15).

## Per level (parity first)

Classes: **parity** (online play with THUGPRO players: what players grind, stand on, hit or die on), **visible** (seen
otherwise, nothing to stand on), **single_player**, **cosmetic**, **none** (no effect). Entry ids `T2-<L>-n`.

- **AU** (node array replaced; LevelChanges; setup) - parity: T2-AU-6 **131 rails** and T2-AU-5 **37 collision /
  warp nodes** present in net games (AbsentInNetGames taken off: the secret areas `TRG_L_Secret*`, `TRG_R_SecretEntry*`,
  `TRGP_Warp_*`, night lamps with collision) - the biggest parity difference found; T2-AU-4 4 rails added
  (`ed_rail_0_0` ...); T2-AU-10 two pre-event walls not created at start (`Geo_PreEvent_A_InvColl01`, `_Wall01`);
  T2-AU-12 10 graffiti objects (`TRG_PostEvent_A_Planters01-08`, `Box172`, `object202`: TrickObject / Cluster);
  T2-AU-13 a rail's trigger (crane setup) taken off; T2-AU-1 / T2-AU-3 THUGPRO's setup `AU_thugpro_Setup` and 5 changed
  scripts (crane grind triggers). Cosmetic: 86 scenery and 61 lights in net games, 3 shadows, 6 banners. None: 2
  secret restarts in net games (not spawns).
- **BA** (node array replaced; LevelChanges; setup; option) - parity: T2-BA-4 **23 kill triggers** (THUGPRO's
  `thugpro_BA_LeavingMessage` -> sk3_killskater: roof / water / Maremagnum areas, `TRGP_BA_Kill01-05`); T2-BA-1
  THUGPRO's setup `thugpro_BA_Setup` (**the bridge always passable**: the passable gates, 42 rails and trams created,
  the impassable QP / 58 rails killed, the bridge turned); T2-BA-3 host option "Bull Wreckage" (off after loading:
  `BA_Event_BullBefore`); T2-BA-7 a climb wire and T2-BA-8 6 pieces (teleport triggers `TRGP_SE_Tele02/04`, fountains,
  Maremagnum doors) present in net games. Visible: 3 gap scripts, the bridge boat in net games. Cosmetic: 77 changed
  trigger scripts (restart / bull scripts emptied or changed), shadows, effects, sounds, 5 banners.
- **BO** (node array replaced; LevelChanges; option) - parity: T2-BO-5 **6 ledge rails** added on the office
  (`TP_BO_Office03_Ledge_Rail_0-5`) with T2-BO-8 its out-of-bounds kill (`BO_Office03Script` ->
  BO_KillskaterOutOfBounds) taken off; T2-BO-2 host option "Construction Site Ramps" (net only, off after loading:
  the Big Dig before the event - vent house, fences, girders, a rail created; the cannon / QP pieces killed).
  Cosmetic: option 2 (half-pipe RV / cars, no menu item), 64 trigger scripts changed (debug prints added), fires /
  sounds in net games, car LODs, 4 billboards.
- **LA** (node array and scripts replaced) - parity: T2-LA-2 21 level scripts changed / added
  (`LA_Main_Initializer`'s multiplayer set-up no longer touches `Overpass_Car_Rocking` / `DestroyedCar_Geometry_07`,
  car-fall gaps, the circle bench / wall combos), T2-LA-4 `TRGP_CarFall_E03` absent in net games. Visible: 5 gap scripts, a car 123 units
  higher (`TRG_Veh72`). Cosmetic: 4 cars' SuspendDistance.
- **AP** (collision replaced; a banner) - parity: T2-AP-2 **245 faces of 34 objects taken out** (all backs of two-sided
  faces: one-sided walls, passable from behind) and T2-AP-4 **wall-ride on 617 faces** (StartArea778 449...). Visible:
  T2-AP-3 53 back faces of 26 gap / alarm trigger boxes (gaps fire from one side).
- **SC** (collision replaced; LevelChanges) - parity: T2-SC-3 390 back faces of 26 objects out, T2-SC-5 wall-ride on
  188 faces (fences, walls, a QP), T2-SC-2 rails `TRG_615`, `TRG_550` killed after loading. Visible: 10 skylight
  trigger back faces.
- **DJ** (collision replaced) - parity: T2-DJ-2 46 back faces of 10 objects out (generators, signs), T2-DJ-4 wall-ride
  on 106 faces (canyon walls, quarter pipes). Visible: a spray trigger.
- **PH** (LevelChanges) - parity: T2-PH-2 4 Natas pillar rails killed (`TRG_Pillar_Natas03/04/06/07`). Cosmetic: 4
  banners.
- **BE** (LevelChanges, options) - parity: T2-BE-2 host option "Train LevelEvent" (net only, off after loading: the
  pub / rails before the train event created, the destroyed pieces killed). Cosmetic: "Thunderstorm" off, 2 billboards.
- **ST** (option) - parity: T2-ST-2 "Ramp Boards" (`ST_LEVEL_CHANGE_01` off: `g_tub_08` / `g_tub_12` killed and made
  again - the same state; the item is `inverted`).
- **TR** (node array, collision and scene replaced) - none at load: T2-TR-4 THUGPRO's added quarter pipe
  `SkWare_Main_8a_QP` isn't created at start (only `TR_LEVEL_CHANGE_01 on`, a host option without menu item, swaps it
  for `SkWare_Main_8a`); 44 empty collision objects gone (no effect). Cosmetic: 6 pro-skater ped scripts, 25 re-lit
  sectors. T2-TR-2 (that option "off" after loading) does nothing.
- **CA** - cosmetic: a rail trigger guarded by ObjectExists (THUGPRO's crash fix).
- **NO, SE, SE2**: THUGPRO's are THUG2's (only Levels.qb's loading screens / music / menu settings differ, as for
  every level: the `level_settings` entries, cosmetic, not applicable).
- **Spawns, KOTH crowns, CTF flags / bases**: identical in all 15 (no restart with a network type, crown or CTF node
  differs).

## Recommendations (for the user to decide)

1. Switch on the collision entries (T2-AP-2, -AP-4, T2-SC-3, -SC-5, T2-DJ-2, -DJ-4): data-only, exactly THUGPRO's
   faces (checked: no collision difference left), no scripts.
2. Switch on the node-array parity entries (T2-AU-4, -5, -6, -10, -12, -13; T2-BA-7, -8; T2-BO-5, -8; T2-LA-4;
   T2-SC-2; T2-PH-2): rails and pieces as THUGPRO's. Note they change free skate and career too (AU's secret areas in
   net games; PH's pillars / SC's rails gone everywhere, as THUGPRO's).
3. Decide on the script-backed ones (they bring THUGPRO's compiled scripts by reference): BA's kill triggers (T2-BA-4)
   and setup (T2-BA-1, the bridge), AU's setup (T2-AU-1 / -3), LA's scripts (T2-LA-2), and the host options' state
   after loading (T2-BO-2, T2-BA-3, T2-BE-2, T2-ST-2) - those also need THUGPRO's messages 155-159 for a host's
   switching (docs/thugpro-online.md item 3).
4. Leave the cosmetic / visible ones off (banners are THUGPRO's own models and textures - not applicable).
5. A run-time alternative (README "Changing approach"): apply the switched-on entries only when the peer is THUGPRO.

## Mechanism

- `node tools/thug2_levels/thugpro_compare.ts [--details]` - the comparison; `--extract-thugpro` writes the register
  (never typed in; each entry's `enabled` kept on re-extraction).
- `node tools/thug2_levels/thugpro_apply.ts [--enable <ids|parity|all>] [--install --game C:\Games\THUG2-n]` - the
  entries into changed level files in the data overlay (never the game's files): `levels\<L>\<L>.qb` (edited nodes
  in place, added ones appended, all other bytes kept - qb_write.replaceGlobalArray), `<L>_scripts.qb` (THUGPRO's
  scripts by reference, the setup wrapper `thug2decomp_thugpro_<L>_setup` calling the level's setup and each host
  option's toggle with its state), `<L>.col.xbx` (hand_edits.applyColEdits: faces out with new BSP trees, face flags),
  `<L>.scn.xbx` (TR's added sector), the overlay's `scripts\game\Levels.qb` (and the Anteara Mod's) setup_script.
- `make_menu_level.ts` calls `installEnabled()` after building the overlay: the register's switched-on entries
  (none by default: prints "none switched on").
- Checked offline: every parity entry applied (`--enable parity` into `out\thug2_levels`) and compared again with
  THUGPRO's files - no node or collision difference of those entries left (BA's 23 replaced triggers remain: THUGPRO
  sets them at run time, not in its array); THUG2's collision files read and written back byte-identical.
- `npm run typecheck`, `lint`, `test` (tools/thug2_levels/thug2_levels.test.ts: 7 tests).

## Shared files touched

- `tools/menu_level/make_menu_level.ts`: one import and three lines after the Anteara Mod's install (additive).
- `tools/compat/thugpro/README.md`: row TP-42, the data / tools lines, a "Changing approach" note. (If another branch
  took TP-42 meanwhile, renumber: the id is only in README.md, `register.ts` REGISTER_ID and the JSON's `_register`.)
- `docs/thugpro-online.md`: the "THUG2's own 15 levels" row.

## Load check

Done by the finishing session (2026-10-21), workspace C:\Games\THUG2-2 under the game lock, offline: `thugpro_apply.ts
--enable T2-AP-2,T2-SC-2 --install --game C:/Games/THUG2-2` (AP's collision with 245 faces out, SC's node array with 2
rails not created at start), then `tools/game/load_check.ps1 -Levels AP` / `SC` (the level names are case-sensitive:
upper case). AP: collision loaded, game responding, no exception / run-time check failure. SC: the game read
`levels\sc\sc.qb` from the overlay, responding, no exception. Overlay files removed and the lock released afterwards;
every entry stays off in the register (`enabled` count 0). Not skated: whether the faces / rails behave is for the user.
The workspace DLL's early self-tests printed unrelated SELFTEST FAIL lines (texture / anim / text elements; they vary
between runs - timing in a window, nothing to do with level data).
