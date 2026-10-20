# THUG1: thugpro_new + port_changes - handoff (WIP, agent stopped 2026-10-20)

Branch: `worktree-agent-ab52827a8c0463b2e`. Workspace: C:\Games\THUG2-3 (not reconverted / not installed yet).
NOT run in the game. Nothing of THUGPRO's bytes is committed (only references + crc32s).

## The instructions (from the lead / user)
1. Everything NEW in THUGPRO (in no source game) goes in ONE separate section `thugpro_new` of
   tools/compat/thugpro/thug1_levels.json, BY REFERENCE: references + checksums; the converter reads the bytes from
   `--thugpro <dir>` (default %LOCALAPPDATA%\THUG Pro); missing install / file / checksum mismatch = warn and skip,
   never fail. Extraction committed (`--extract-thugpro`), never typed in. `--compat none` byte-identical to before;
   `--no-thugpro-new` = the previous default.
2. `port_changes` section: our converter's own deviations from THUG1's data, each entry {id PC-n, op add/set/remove,
   target, why, required}. Stays on under `--compat none`; `--no-port-changes` switches off all not `required`.
   `thugpro_new` entries also carry op/id/target/why. "set" on something missing = an error the converter reports.
   README: a section "Our own changes (port_changes)" and a register row group "new in THUGPRO (not in any source
   game)"; document that the two new sections use `op` (TP-30..39 not retrofitted).
3. Items asked for: NJ El train, HI boat, VC<->SJ arrows (+restart/waypoint), VN gulls + bum (model FOUND: THUGPRO
   data\models\skater_secret\Skater_OllieTheMagicBum - loose files), VN 58 sand kills (Sk3_Killskater_sand from
   thugpro_utils.qb), NJ 85 emptied triggers / other emptied + added scripts, `<level>_thugpro.qb` files (inert until
   a protocol layer for THUGPRO messages 155-159), models THUG2 lacks where THUGPRO ships them (leaf blower, RU
   THPS2_Case icon, RU tank). NY's 11 TERRAIN_GRINDRUSTYRAIL: stay NOT applied (unknown terrain in THUG2, crash).
   C++ needs: list + propose, don't implement.

## Done (code, typechecks; conversions run offline into the scratchpad)
- `tools/compat/compat.ts`: `portChanges(tool)` reads `port_changes` whatever the profile.
- `tools/thug1_levels/port_changes.ts`: PC-1..PC-20 (IMPLEMENTED map), `on(id)` / `required(id)`.
  Switchable (required false): PC-1 kill restarts, PC-2 net spawns for classics, PC-3 Player1-first swap, PC-4
  DuplicateTrigger resolution, PC-5 ground restarts. Required: PC-6..PC-20 (animals, goal nodes, missing models /
  skeletons, particles, peds' skeleton, keys, TrickObject strip, terrains, net-game absent, left-out statements, gap
  scripts, includes, restart_types/kill form, level names, face flags). Data written into thug1_levels.json
  `port_changes.entries` (by a one-off script; if regenerating, the extraction keeps the section).
- `tools/thug1_levels/thugpro_new.ts`: types, `Install` (reads THUGPRO archives / loose models), `rawScripts`,
  `scriptForm` (normalised compare), `scriptUses` / `scriptCheck` (guard: commands THUG2 knows incl. its levels'
  scripts; THUG1 goal/flag commands refused; nodes named must be in our level, undrawn geometry excluded),
  `spliceScripts`, `applyNodes` (TP-41 add/set, crc of node form with names as checksums), `extract`.
  IDs: TP-40 assets (models), TP-41 nodes, TP-42 scripts (add/set/remove), TP-43 host-toggled files.
- `thug1_levels.ts`: options `--thugpro`, `--no-thugpro-new`, `--no-port-changes`; assets written to
  `<staging>\models\`, `Thug1Port.models` / `extraRoots` extended; nodes applied after the hand edits and kept whole
  (`NodePass` `forced`); added scripts passed as provided to portScripts; guards; set/remove spliced into the compiled
  .qb (inside writeQb, so the ground-restart rewrite keeps them); TP-43 files written to levels\<name>\.
- `thug1_port.ts`: NodePass `forced`, Thug1Port `extraRoots`, portScripts `extraProvided`.
- `ported_levels.ts` (shared, additive): installFiles also merges `<staging>\models` into the overlay.
- `thugpro_compare.ts --extract-thugpro`: writes `thugpro_new` (needs `--thug2 C:\Games\THUG2-3` and a converted
  staging via `--staging`; run it against a staging converted WITH thugpro_new - it's a fixpoint: checked stable).
- Profile regenerated: thugpro_new for 12 levels (counts in the extraction log; e.g. VN TP-40 3, TP-41 add 3 / set 5,
  TP-42 set 90 / add 7). Existing sections (spawns, TP-32..39) unchanged.

## Verified so far
- Default output (no thugpro_new section yet, port_changes present) == baseline (95 files; .qb compared as code +
  sorted names - NeverScript writes the checksum names in random order, so byte compare of .qb never matches).
- Full conversion with thugpro_new: exit 0; only 2 scripts refused at conversion (SJ_startup, RU_Cannon); assets in
  staging models\ (transition_arrow, veh_el_train, veh_leafblower, veh_tank_ru, thps2_case, the bum).

## Left (exact next steps)
1. `--compat none` vs baseline_none, and `--no-thugpro-new` vs baseline_default (normalised compare) - not yet run
   after thugpro_new was written. `--no-port-changes` run once and diff to see only PC-1..5 effects.
2. Empty `--thugpro` folder run (warn + skip path) - not yet run.
3. Check in the converted output: NJ trains present with Links to TRG_TrainTest_00 node; VN sand kills call
   Sk3_Killskater_sand and that script is added; NJ 69 emptied scripts; gulls/bums nodes whole.
4. thugpro_compare default run: hand-edit check + add a "new in THUGPRO" check line per level (not written yet).
5. Reconvert into C:\Games\THUG2-3, `make_menu_level.ts --game C:\Games\THUG2-3`, col_check, links, ns.exe whole,
   `thug1_levels.ts selfcheck` (and the other converters' selfchecks), `npm run typecheck / lint / test`
   (lint NOT run yet - run `npm run format`), add tests (thugpro_new: rawScripts / spliceScripts / scriptUses /
   applyNodes; port_changes: on/required).
6. README register: rows TP-40..43 in a separate "New in THUGPRO (not in any source game)" table, "Our own changes
   (port_changes)" section with PC-1..20 and their ops, note that both use `op`; docs/pending/thug1.md; the report
   HTML (scratchpad thug1_thugpro_report.html): "New in THUGPRO, now included" section, mark section 11 items.
7. Clean-ups: thugpro_new.ts REFUSED comment still mentions LEFT_OUT; the loose-model path code in extract is clumsy.

## Findings / concerns for the report
- VC<->SJ transitions: THUGPRO's g_sj_detail_35Script / trp_death_exitScript call `LoadZone` (THUGPRO's own Levels.qb
  script + its DLL's SkProDisplayLoadingScreen): refused - arrows are decorative only. C++ need: a "change level and
  start at named restart" (proposal: our own level-change-to-node in the DLL, credited to THUGPRO's idea).
- Leaf blower now kept (model from THUGPRO; skeleton `car` THUG2 loads). RU tanks (veh_tank_ru) now kept too - untested.
- NJ/HI Old Skool icons and HI lei pick-ups: THUGPRO itself has model "none" and ships no model -> still left out.
- Our script translator treats THUG2's `TriggerType` as unknown (THUG1 gap conditions become never-true); THUG2's own
  levels use it - THUGPRO's rewritten gap scripts now come in via TP-42 set. Worth fixing in script_port (shared).
- PC-7 (goal GenericNodes) marked required without proof that keeping them crashes.
- 182 VC / 90 VN script replacements: guarded but untested in the game.

## Commands
- `node tools/thug1_levels/thug1_levels.ts convert --thug2 C:/Games/THUG2-3 --ns C:/Dev/thug2-decomp/out/neverscript/ns.exe [--out <dir>] [--compat none] [--no-thugpro-new] [--no-port-changes] [--thugpro <dir>]`
- `node tools/thug1_levels/thugpro_compare.ts --extract-thugpro --thug2 C:/Games/THUG2-3 --staging <converted>\levels`
- Scratch (session-only): baselines and compare script in the session scratchpad `t1new\` (cmpdirs.ts).
