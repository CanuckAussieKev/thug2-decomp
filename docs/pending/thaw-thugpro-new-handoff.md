# THAW: THUGPRO's own content (thugpro_new) and port_changes - handoff (WIP, 2026-10-20)

Branch `worktree-agent-a4244cc241e89291a`. Stopped mid-task (the user switched accounts). Nothing run in the game.

## The task (instructions given)
1. **thugpro_new** (user's decision, Morten's permission): include everything THUGPRO made that is in no source game
   (the old `awaiting` list: 74 new meshes + collision, 98 reshaped, 81 content nodes, 59 TriggerScripts naming its
   scripts, its level-script changes, host-option defaults) in ONE section `thugpro_new` of
   `tools/compat/thugpro/thaw_levels.json`, **by reference only**: the converter reads THUGPRO's bytes from
   `--thugpro <dir>` (default %LOCALAPPDATA%\THUG Pro) at convert time; missing install / CRC mismatch -> warn and
   skip, never fail. Nothing of THUGPRO's committed. Extraction committed (`--extract-thugpro`), never typed in.
2. Switches: `--compat none` byte-identical to before (no THUGPRO content); `--compat thugpro --no-thugpro-new`
   equals today's default output; default = everything.
3. **Every entry in `thugpro_new` and `port_changes` has `op`** (`add` / `set` / `remove`; `set` on something
   missing = a reported error, never an add), plus `id`, `target`, `why`. A rule may be one entry (target describes
   it). Don't retrofit TP-1..9; document in the README that the two new sections use `op`.
4. **port_changes** (user, via lead): a second separate section `port_changes` (same name in every game) for OUR
   converter's deliberate deviations from THAW's data, moved out of code into data read through compat.ts, PC-n ids,
   README section "Our own changes (port_changes)". Stays on under `--compat none`. New switch `--no-port-changes`
   (pure source output where possible); entries needed for the level to load get `"required": true` and stay on.
   Default output stays byte-identical. Candidates: the Zone_Origin frame move, restarts kept out of net spawns
   (nodes.groundRestarts), breakables as THUG2 shatters, TrickObject crash guard (stripMissingTrickObjects -
   required), night sectors and LOD sectors dropped, trigger faces only for kill zones (crash guard - required),
   BLEND_BY_PREVIOUS pass dropped (crash - required), billboards left out, FACE_KEEP / PASS_KEEP, net-game node
   marks (nodes.netGameNodes), breakables' Type removed. The THPS4 agent uses PC-1 for the skate shop door; pick a
   non-clashing block (proposal: PC-201...) and tell the lead. Report the full PC list with each op.
5. **Restore `Z_MA_Mall_First_Rail_Blocker`** (user's decision 2026-10-20): remove it from `LEFT_OUT` in
   `tools/thaw_levels/thaw_levels.ts` (Z_MA is the only entry: remove the LEFT_OUT map/`leftOut()` cleanly or leave
   it empty), no port_changes entry for it. Default and `--compat none` output then change for z_ma only (the
   blocker: scene, node, collision) - say so. Fix docs/pending/thaw.md: it is NOT "an invisible wall across the
   doors" but a thin invisible wall (12 faces, flag 0x1000, box x -15767..-14044, y 1367..2278, z -5641..-5600 in
   THUGPRO's frame) alongside the start area, ~280 units from Z_MA_TRG_restart, ending ~430 before the entrance
   glass; THUGPRO keeps it (CreatedAtStart, CollisionMode Geometry). Check col_check (z_ma every face reachable),
   its 12 faces in our collision with THUGPRO's flags, thugpro_inventory --ours no longer lists it.
6. Checks: reconvert into C:\Games\THUG2-4; col_check, links, ns.exe (East LA's known failure aside),
   make_menu_level, selfchecks, `npm run typecheck / lint / test`; thugpro_inventory --ours before/after; the
   THUGPRO-absent path (`--thugpro <empty folder>` warns and skips). Update the register README (rows TP-N1..N5,
   switching off, "Changing approach"), docs/pending/thaw.md, the page
   C:\Users\kevin\AppData\Local\Temp\claude\C--Dev-thug2-decomp\c861af32-ae53-4deb-9c82-1d00b291c5a6\scratchpad\thawtp\thugpro-hand-edits.html
   ("New in THUGPRO, now included"). Report: included, left out and why, C++ needs, concerns. Commit, push, no merge.

## Done (this WIP commit)
- `tools/thaw_levels/thugpro_new.ts` (new): entry types (`op`/`id`/`kind`/`target`/`why`/`crc`/`source`...),
  CRCs of THUGPRO items (sector / collision object / node / script bytes), `loadThugproLevel` (its level scn/tex/col
  - only `levels\<key>\<key>.*`, the sky in scn.prx is excluded; a lenient tex reader for z_bh's trailing bytes - node
  array with compressed nodes expanded, every script's raw THUG2 bytes from the level qbs and thugpro_qb.prx),
  `scriptSpans` / `removeScripts`, `calledNames` / `usedNames`, `resolve` (CRC check, skip + warn), `colForThaw`
  (new BSP tree + reaches), `moveSector`, `mergeScene` (materials ours by checksum, THUGPRO's added with textures; a
  texture checksum clash with other pixels gets `thugpro_new_<hex>`), `nodeForThaw`, `optionsSetupNs`.
- `thugpro_extract.ts`: builds `thugpro_new` per zone (TP-N1 new meshes/collision, TP-N2 reshaped (set) / emptied
  (remove), TP-N3 nodes, TP-N4 scripts + trigger scripts, TP-N5 host options + LoadingSounds) and `left_out` with
  reasons; `scriptsNew` resolves each root script's THUGPRO dependencies and leaves out those calling commands
  neither THUG2 nor THUGPRO's scripts define (`needs`). Curated `CHANGED_SCRIPTS` (MA_Dooropening, EL's 6 glass
  scripts); rule: THAW gap scripts THUGPRO gated by GetTriggerArray. Peds/life nodes: our port's own way (left out
  with reason). Vehicles not created at start (EL cop car, OI ferry) and LevelObjects without a sector (DN's 21
  cones/signs/chairs) left out -> no models needed. `Cylinder01` = THAW's sky dome from THUGPRO's bundled
  thaw_sky.scn.xbx (ours converts THAW's own sky) - left out with that reason.
- `thugpro_compare.ts --extract-thugpro`: writes `thugpro_new` (+ `_needs_cpp` summary) and `notes` instead of
  `awaiting`. Checked: every other section of thaw_levels.json identical to before (TP-1..9 unchanged).
- `thaw_levels.ts`: `--thugpro`, `--no-thugpro-new`; `setCompat(name, withThugproNew)`; collision injected before
  convertCol (`newCollision`), nodes/TriggerScripts into THAW's array (`addNewNodes`), sectors merged after
  convertScene, THUGPRO scripts appended (ours of the same names removed via `writeLevelQb(..., drop)`), scripts
  naming nodes ours hasn't left out (`pruneNewScripts`: a `name:` on a missing object runs on the skater), setup
  script `<level>_thugpro_setup` (LoadingSounds + host options' defaults) as the manifest's `setup_script`.
- `thaw_scripts.ts`: `PortedNodes.drivers` (THUGPRO car scripts that drive).
- A full default convert ran OK into the scratch folder (all 15; report lines `thugpro_new: ...` per level).
- Extraction counts: TP-N1 31 sectors + 32 collision add; TP-N2 51 sectors set, 1 remove, 46 collision set;
  TP-N3 58 nodes; TP-N4 37 scripts + 21 trigger scripts; TP-N5 12 host options + 22 scripts; 188 left out.
  Needs C++ (`_needs_cpp`): TriggerType (~100 gap scripts in CH, DN, EL, HO, SV, DT, LV, SR, SV2, SZ, JA), and THAW
  commands THUGPRO implemented: CinematicCamera_DoMorph / _Pan, PlayIGCCamScene, IGC_Camera_MoveToPlayer,
  SpawnScriptLater / SpawnScriptNow, IsCreated, ChangeNodeFlag, DeleteGapFromScore, StickerOn,
  GoalManager_SetFloatTipVisibility (a few list entries are false positives: goal_*_trigger, "sssssssssssss").

## Not done / next steps (exact)
1. Verify: `--compat none` output byte-identical to the baseline (baseline made before any change:
   scratchpad\tpnew\base_none; `--no-thugpro-new` vs scratchpad\tpnew\base_tp). Compare all files (fc /b or a
   node script). Fix anything that differs (watch: manifest setup_script, writeLevelQb drop).
2. Check the default output: col_check on all 15 (`node tools/level_port/col_check.ts <col.xbx>`), ns.exe -d on every
   .qb, links in range; look at the z_dn `z_dn_thugpro_setup` script and the garage nodes (no CreatedAtStart:
   created by `z_dn_LEVEL_CHANGE_02 on`). Check `Create`/`Kill name=` of option scripts against our nodes (pruned
   when missing).
3. THUGPRO-absent path: `--thugpro` an empty scratch folder -> every zone reports "no THUGPRO level ... skipped".
4. port_changes (instruction 4): add `port_changes` to thaw_levels.json and a reader in `tools/compat/compat.ts`
   that works under `--compat none` too; `--no-port-changes`; move each deviation behind a PC entry (op/id/target/
   why/required); default output byte-identical.
5. Restore the Mall blocker (instruction 5); docs/pending/thaw.md fix.
6. `thugpro_inventory.ts --ours <staging>\levels` before (base_tp) and after; report categories.
7. Docs: register README rows TP-N1..TP-N5 ("new in THUGPRO (not in any source game)"), note that thugpro_new /
   port_changes use `op`, "Changing approach", how to switch off; "Our own changes (port_changes)" section;
   docs/pending/thaw.md; the HTML page.
8. `npm run lint` (biome) / `npm test`; thaw_levels selfcheck; reconvert into C:\Games\THUG2-4 and
   `node tools/menu_level/make_menu_level.ts --game C:\Games\THUG2-4`; commit, push.

## Files touched
tools/thaw_levels/thugpro_new.ts (new), thugpro_extract.ts, thugpro_compare.ts, thaw_levels.ts, thaw_scripts.ts,
tools/compat/thugpro/thaw_levels.json (regenerated: `awaiting` -> `thugpro_new` + `notes`).

## Half-done / unverified
- Byte-identity of `--compat none` and `--no-thugpro-new` NOT checked yet.
- `npm run lint` / `npm test` not run.
- THUGPRO scripts appended raw: not run in the game. The host-option defaults call THUGPRO's toggle scripts at
  setup (EL 6, OI 2, DN 2, MA 1, SZ 1) - DN's garage on, the rest off, as THUGPRO after loading.
- EL's COIN and SZ's meter scripts replace our translations (dependencies of THUGPRO's trigger scripts).
- port_changes, the blocker restore and all docs: not started.

## Commands
- Extract: `node tools/thaw_levels/thugpro_compare.ts --extract-thugpro --thug2 C:\Games\THUG2-4`
- Convert: `node tools/thaw_levels/thaw_levels.ts convert --thug2 C:\Games\THUG2-4 --ns C:\Dev\thug2-decomp\out\neverscript\ns.exe [--out <dir>] [--compat none] [--no-thugpro-new] [--thugpro <dir>]`
- Inventory: `node tools/thaw_levels/thugpro_inventory.ts --ours <staging>\levels`
- `npm run typecheck`, `npm run lint`, `npm test`; `node tools/thaw_levels/thaw_levels.ts selfcheck --thug2 C:\Games\THUG2-4`
