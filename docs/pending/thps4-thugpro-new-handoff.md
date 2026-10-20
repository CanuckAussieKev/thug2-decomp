# THPS4: thugpro_new + port_changes - handoff (2026-10-20, agent stopped mid-task: account switch)

Branch: `worktree-agent-a33cab85b7cc09963`. Nothing run in the game. Workspace: C:\Games\THUG2-2 (not reconverted yet).

## Done (commit 8efcfc2, verified)
- **port_changes PC-1 ... PC-11** (our own deviations from THPS4's data) moved out of converter constants into
  `tools/compat/thugpro/thps4_levels.json` `port_changes.entries` ({id, op add/set/remove, rule, target, why}), read
  through `compat.ts portChanges(tool)`; `thps4_levels.ts` `PORT`, `setPortChanges`, `portChanges(rule, base)`,
  `portChange`, `pedsCarsWanted`; `thps4_scripts.convert(..., options)` (leftOut / neverCreated / emptyRestartScripts).
  PC-1 / PC-2 the skate shop door (InvisColl01 z-min faces, Object10 invisible faces), PC-3 SSO_Fence05
  CreatedAtStart (set: a missing node throws), PC-4 College peds/cars, PC-5 zero Angles, PC-6 never-created objects,
  PC-7 TrickObjects without sector, PC-8 left-out goal/panel commands (list now in the JSON), PC-9 empty restart
  scripts, PC-10 ground restarts, PC-11 net-game nodes.
- **`--compat` rule chosen**: port_changes are not THUGPRO data, so they apply with `--compat none` too;
  `--no-port-changes` leaves all of them out (pure THPS4, crash guards included). `--no-thugpro-new` flag parsed
  (setCompat(name, withNew)).
- Checked: default (`thugpro`) and `--compat none` output identical to HEAD before the change (75 files each, .qb by
  code + sorted names, scratch `qbsame.ts`); `--no-port-changes` differs in 14 files as expected. typecheck / lint /
  test (79 pass, new test in thps4_levels.test.ts) / thps4 selfcheck 139 files 0 failures.
- README.md (tools/compat/thugpro): sections "The two sections with `op`" and "Our own changes (port_changes)" (PC
  table); TP-29 row / door_walls note / docs/level-port-compromises.md point at PC-1 / PC-2 / PC-3.
- compat.ts: `Entry`, `Op`, `OPS`, `portChanges()`, `thugproNew()` (shared file, additive).

## WIP (this commit, NOT verified, not typechecked)
- `tools/thps4_levels/thugpro_new.ts`: extraction (`extractNew`) and applying (`applyNew`, `hookStart`) of
  `thugpro_new` by reference (files: prx + entry + size + SHA-256; bytes read from `--thugpro` at convert time; missing
  / different -> warning, level's content skipped). Rules: script, trigger_script (set), node, level_object (set,
  LevelChanges moveto), proxim_object, level_option (run `off` at start = THUGPRO's default), sound, model
  (shown_by_thugpro false). Our helper scripts (thps4_tp_*) replace THUGPRO_SafeKill / ForceCreate / Televator
  (credit comment). Register ids chosen as TP-N4-1 ... TP-N4-5 (to avoid clashing with parallel agents' TP-40+).
- Not wired in yet: thps4_levels.cmdConvert (call applyNew after addNetNodes, before peds/cars offset; add returned
  text to `text`; hookStart into `<level>_ported_setup`, set entry.setup_script when it creates one; `--thugpro <dir>`
  and `--thugpro-banners` options; COMPAT.thugproNew already holds entries, files need `profile.file(...).thugpro_new.files`).
- Not wired: thugpro_compare.ts `--extract-thugpro` (call extractNew for each level into a Section, write
  `profile.thugpro_new = {_register, _source, _note, files, entries, _not_taken}`; keep `port_changes` in the file -
  the extractor rewrites the whole JSON: it keeps unknown keys since it loads and re-dumps the object).
- Not done: thugpro_inventory check for thugpro_new; README "New in THUGPRO (not in any source game)" rows;
  docs/pending/thps4-thugpro.md; report HTML section; reconvert + checks in THUG2-2.

## Findings (important for the user)
- **THUGPRO doesn't show its THPS4 banners / posters / billboard**: its shipped thugpro_dynamic.qb
  `thugpro_levelchanges_add_static_model` returns for `replace_texture` (all banners have it) and the billboard /
  poster branch is compiled out (`GIVE_ME_BILLBOARDS` commented out in Morten's source, confirmed in the installed
  thugpro_qb.prx). So parity = not placed; planned as opt-in `--thugpro-banners`.
- THUGPRO's skate shop kill trigger is inert: TRG_TP_SkaterKiller's trigger script is empty and nothing calls
  TP_SS_KillSkater. The skate shop's own host option (PRACTICE_LEVEL_CHANGE_01 "Open Fence") force-creates InvisColl01
  (the door walls) in its default state: left out (backdrop / open door; `LEFT_OUT` in thugpro_new.ts).
- Host options' default: THUGPRO runs every `<lvl>_LEVEL_CHANGE_0n off` at each game start (HandleLevelOptionObjects
  in gameflow) - e.g. Shipyard kills prefix "Bucky" (the barge), SF2 kills Geo_Tetris2*, Zoo hippo bridge up, London's
  race track killed in net games, RowleyPro_Rollin01 killed.
- All THUGPRO helpers used (SafeKill, ForceCreate, Televator, the proxim object script) are QB scripts using THUG2
  commands: **no THUG2 C++ needed** for the default state. C++ needed only to switch options: THUGPRO's protocol
  (messages 155-159, the SERVER_LEVEL_CHANGE_0n_ON flags). THUGPRO_AutoKick (THUGPRO's own setting) is left out.
- Sounds: THUGPRO's level start-ups LoadSound THUG2's "VC\VC_ElevatorDing02", "Shared\Misc\elevatording /
  elevatordooropen / elevatordoorclose" (THUG2 has the files); College's door sounds aren't loaded by THUGPRO either.
- London: THUGPRO's 4 kill zones (TRGP_Lon_AdArchKill01, TRGP_Lon_WaterlooBridgeKill01, Lon_AdArch_Roof / _Roof01)
  call its Lon_Killskater (ours: THPS4's Lon_LeavingMessage, emptied); TRG_ParkingGarrage_ElevatorScript is a
  televator with jump boost to TRG_LON_Televator_Waypoint.
- port_changes candidates not made entries (translation rules, no source form to fall back on): listed in the README
  section. The lead's later "`required`" field for port_changes was not added yet.

## Next steps
1. `npm run typecheck` / `lint` - fix thugpro_new.ts.
2. Wire extraction into thugpro_compare.ts extract(); run `node tools/thps4_levels/thugpro_compare.ts --extract-thugpro`;
   check that the JSON's other sections are unchanged (git diff) and look at the entries / `_not_taken`.
3. Wire applyNew into cmdConvert (+ `--thugpro`, `--thugpro-banners`); unit tests (rawScripts / renamed / hookStart /
   missing install -> warning).
4. Checks: `--compat none` and `--no-thugpro-new` identical to the base (scratch base_none / base_tp from HEAD
   ab89386 with `qbsame.ts`); an empty `--thugpro` folder -> warnings, output = `--no-thugpro-new`.
5. Reconvert into C:\Games\THUG2-2 (`thps4_levels.ts convert --thug2 C:\Games\THUG2-2`), then col_check, links,
   ns.exe decompiles, `node tools/menu_level/make_menu_level.ts --game C:\Games\THUG2-2` (backdrop: 44 rail nodes, 138
   peds / cars, 3 trigger nodes + 6 scripts - unchanged), thps4 selfcheck, the other converters' selfcheck,
   typecheck / lint / test.
6. thugpro_inventory: a thugpro_new check per level; README rows TP-N4-1..5; docs/pending/thps4-thugpro.md; the report
   HTML (scratchpad thps4tp\thps4-thugpro-report.html: a "New in THUGPRO, now included" section, section 10 marked).

## Files touched
tools/compat/compat.ts, tools/compat/thugpro/thps4_levels.json, tools/compat/thugpro/README.md,
tools/thps4_levels/thps4_levels.ts, thps4_scripts.ts, thps4_levels.test.ts, thugpro_compare.ts (text), thugpro_edits.ts
(comment), thugpro_new.ts (new, WIP), docs/level-port-compromises.md, this file.

## Commands
- `node tools/thps4_levels/thps4_levels.ts convert --thug2 C:\Games\THUG2-2 --ns C:\Dev\thug2-decomp\out\neverscript\ns.exe --out <scratch> [--compat none] [--no-port-changes] [--no-thugpro-new]`
  (the worktree has no out\neverscript: pass --ns).
- `npm run typecheck`, `npm run lint`, `npm test`, `node tools/thps4_levels/thps4_levels.ts selfcheck --thug2 C:\Games\THUG2-2`.
