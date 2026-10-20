# THPS3: THUGPRO's new content (thugpro_new) and our own changes (port_changes) - handoff (WIP)

Agent for THPS3 (tools/thps3_levels), 2026-10-20, stopped mid-task (account switch). Branch
`worktree-agent-ab007c78a14463244`. **NOT run in the game. Lint / unit tests / col_check / ns.exe / selfcheck /
make_menu_level NOT run yet on these changes.** Typecheck passes.

## The instructions (from the lead / the user)
- THUGPRO-authored content (in no source game) goes in ONE separate top-level section `thugpro_new` of
  tools/compat/thugpro/thps3_levels.json, with its own register row(s) in tools/compat/thugpro/README.md titled
  "new in THUGPRO (not in any source game)". On by default (`--compat thugpro`); `--no-thugpro-new` leaves it out and
  keeps TP-10..19; `--compat none` byte-identical to before.
- **By reference only**: the register holds archive / file / name / CRC; the converter reads the bytes from
  `--thugpro <dir>` (default %LOCALAPPDATA%\THUG Pro) at convert time. Missing install / item or CRC mismatch: warn and
  skip, never fail. Never commit THUGPRO's bytes. Extraction committed (`--extract-thugpro`), never typed in.
- Merge sectors / collision / textures with tools/level_port; rebuild BSP trees (every face reachable); avoid checksum
  clashes.
- Things needing THUG2 C++ we don't have (THUGPRO DLL commands): list and propose our own implementation, don't
  implement.
- `port_changes` section (same file): our own deviations from THPS3's data, as data read through compat.ts. Kept under
  `--compat none`. `--no-port-changes` gives source output where possible; entries the game needs stay, marked
  `"required": true`. Default output byte-identical.
- Every entry in `thugpro_new` and `port_changes` has `op` ("add" / "set" / "remove"; "set" on something missing is a
  reported error, not an add), `id` (PC-n / TP row), `target`, `why`. Don't retrofit TP-10..19; document in the README
  that the two new sections use `op`.
- Update README register, docs/pending/thps3-thugpro.md, the report HTML
  (C:\Users\kevin\AppData\Local\Temp\claude\C--Dev-thug2-decomp\c861af32-ae53-4deb-9c82-1d00b291c5a6\scratchpad\thps3-thugpro-report.html:
  a "New in THUGPRO, now included" section; mark 7b's awaiting items included). Report the full PC list with ops.

## Done
- `tools/thps3_levels/thugpro_new.ts` (convert time): NewEntry format, loadNew (reads THUGPRO archives, verifies
  CRC per item, warns and skips), applyNodes (add nodes with Links by name, `set` fields), applyGeometry (sectors +
  materials renumbered on clash + textures; collision through geometry.convertCol = BSP trees and reaches rebuilt),
  adaptScript (replace_calls by swapping name checksums), extraScripts / addedScripts (clash check against the
  compiled file).
- `tools/thps3_levels/thugpro_new_extract.ts`: the rules (header comment). Called from
  `thugpro_compare.ts --extract-thugpro` (now needs `--thug2 <folder>`); TP-10..19 sections came out identical.
  Extraction keeps the existing `port_changes` section.
- Register JSON regenerated: `thugpro_new` (per level entries + `_left_out` with reasons) and `port_changes`
  (PC-1..PC-14) added.
- Converter (thps3_levels.ts): `--thugpro`, `--no-thugpro-new`, `--no-port-changes`; THUGPRO models written to
  `<staging>\models\...`; ported_levels.installFiles copies `<staging>\models` into the overlay (make_menu_level
  allows a `models` folder). portScripts takes ExtraScripts (THUGPRO script names provided, THPS3 callees brought,
  `set` scripts replace ours).
- compat.ts: `portChanges(tool, enabled)` / `PortChanges.get/on` (reads the section whatever the profile).
- port_changes wired: PC-1 (Warehouse dark set prefixes now data, LEFT_OUT_PREFIXES constant replaced by
  `leftOutPrefixes`), PC-4 (nodes.netGameNodes got a `withRestarts` param - shared file, additive), PC-7
  (groundRestartsAt), PC-8 (our CTF; kept anyway when TP-10..12 data is present), PC-9 (Player1-first swap). The
  others are `required: true` (documented, always applied).
- Verified (scratch, all 14 levels): `--no-thugpro-new` == previous default (79 files: .qb code + sorted names, rest
  byte for byte); `--compat none` == previous `--compat none`. Full default conversion runs (exit 0).

### PC list (op)
PC-1 remove Warehouse dark set (prefixes, levels [ware]); PC-2 set start scripts run once from <level>_setup
(required); PC-3 set kill zones as SK3_KillSkater (required); PC-4 add net restart types on Player1 restarts of
single-player levels; PC-5 set AbsentInNetGames on path followers / peds / cars (required); PC-6 set TrickObject /
Cluster off geometry without a sector (required); PC-7 set restarts over nothing / kill zones not spawns; PC-8 add CTF
from THPS3's bases; PC-9 set Player1 restart first; PC-10 set traffic as THUG2 cars (required); PC-11 set peds /
animals as THUG2's (required); PC-12 remove goal pickups etc. (required); PC-13 remove statements THUG2 can't run
(required); PC-14 remove non-rail Links (required).

### What the full conversion included (from its log)
Foun: foun_wire sector + node (wires' cluster now found by TP-18), 3 *_coll + Obj_Bucket collision, Obj_Bucket set to
Vehicle with THUGPRO's model (our .dff copy dropped), scripts. Rio: ES_Building_collision, ped TRG_Ped_2084 + waypoint
loop, 3 sound objects, 8 carplant collision vehicles (+CreatedAtStart adaptation) + CarPlantNode models. Sub: 5
_collfix, BBQ noise, truck carplant, TRG_IceCreamTruck / TRG_Plane set to Vehicles + models. SI: ocean emitters +
SI_SFX_OceanWaves scripts (edmsg2 -> printf), SITRG_ProximityOcean*Script `set`. Tok: 23 vehicles set (trains, planes,
cars), EventR/L restarts, train/plane scripts (THUGPRO_ForceCreate -> Create). Shp: waterslide sound. Ware:
RoomFlicker. Burn / Ros: scripts.
Left out (in `_left_out`): sky sectors (THPS3's sky under other checksums), THUGPRO's converted .dff objects, Sub's 3
unnamed nodes (toolchain leftovers), particles (THUGPRO_SpawnNodeParticles), anything calling THUGPRO_SafeKill,
TP_DoFlash, Editor_LoadCameraAnimParams, THUGPRO_Televator, Obj_PlayRandomStream / Obj_PlayRandomAnim /
PlayRandomStream; SI cannons proximity (its script needs SafeKill).

## Half-done / not verified / known issues
- Log warnings to look at: `load_level_anims` (and Ware's `ware_Setup` vs our `ware_setup`) clash with names ours has
  - left out, reported (correct behaviour, but check). Tok: `set` on JS_BulletTrainWest/EastControl - ours doesn't
  bring those scripts: reported, not added (so THUGPRO's train spawning via *_Spawned never runs).
- Not checked: col_check on the new collision, Links / rail links, ns.exe decompile of the new qb, make_menu_level
  install (models folder), thps3 selfcheck, `npm run lint` / `npm test` (biome formatting surely needs `npm run
  format`), the THUGPRO-absent path (`--thugpro <empty dir>`), `--no-port-changes` runs.
- thugpro_inventory.ts `--applied` doesn't check thugpro_new yet (planned: each entry found in our output - node /
  sector / collision / script / model file - before vs after).
- README register rows (TP-N3 "new in THUGPRO", "Our own changes (port_changes)", `op` note), docs/pending/
  thps3-thugpro.md, the report HTML: NOT updated.
- The id `TP-N3` is a placeholder for the register row (TP numbers are shared across games): the lead may renumber.
- Possible game-side risks: Vehicle class on our translated Tok / Sub / Foun object scripts; THUGPRO's level col
  `Obj_Bucket` is static at the bucket's rest place (as THUGPRO has it); carplant locking (Obj_LockToObject) onto our
  THUG2 cars; THUG2 Kill / Create semantics vs THUGPRO's ForceCreate.

## C++ needs (propose our own, credit THUGPRO in a comment; not implemented)
THUGPRO_SpawnNodeParticles (particles at a node by type - all THUGPRO particles), THUGPRO_SafeKill (Kill that tolerates
a missing / geometry-less node), THUGPRO_ForceCreate (Create ignoring AbsentInNetGames online; adapted to Create
offline), TP_DoFlash (screen flash), Editor_LoadCameraAnimParams (cutscene cameras), THUGPRO_Televator (teleport),
Obj_PlayRandomStream / Obj_PlayRandomAnim / PlayRandomStream (random choice helpers - could be scripts),
edmsg2 / Output (debug print; adapted to printf).

## Next steps
1. `npm run format`, `npm run lint`, `npm test`; fix.
2. Empty-THUGPRO test: `node tools/thps3_levels/thps3_levels.ts --thug2 C:\Games\THUG2-1 --ns
   C:\Dev\thug2-decomp\out\neverscript\ns.exe --out <scratch>\empty --thugpro <empty dir> convert` - expect warnings,
   output == `--no-thugpro-new`.
3. Reconvert into C:\Games\THUG2-1 (`--thug2 C:\Games\THUG2-1`), then col_check every level
   (`node tools/level_port/col_check.ts <file.col.xbx>`), links check, ns.exe -d each level qb (thps3_skateshop's known
   failure aside), `node tools/menu_level/make_menu_level.ts --game C:\Games\THUG2-1`, `node
   tools/thps3_levels/thps3_levels.ts selfcheck --thug2 C:\Games\THUG2-1`.
4. Extend thugpro_inventory `--applied` for thugpro_new; report before (baseline staging) / after.
5. README rows + `op` note, pending doc, report HTML; commit; push.

## Files touched
tools/thps3_levels/thugpro_new.ts (new), tools/thps3_levels/thugpro_new_extract.ts (new), tools/thps3_levels/
thps3_levels.ts, tools/thps3_levels/thugpro_compare.ts, tools/compat/compat.ts, tools/compat/thugpro/thps3_levels.json,
tools/level_port/nodes.ts (netGameNodes withRestarts), tools/level_port/ported_levels.ts (models copy),
tools/menu_level/make_menu_level.ts (`models` allowed in the overlay).

## Commands
- Extract: `node tools/thps3_levels/thugpro_compare.ts --thug2 C:\Games\THUG2-1 --extract-thugpro
  tools/compat/thugpro/thps3_levels.json`
- Convert: `node tools/thps3_levels/thps3_levels.ts --thug2 C:\Games\THUG2-1 [--compat none] [--no-thugpro-new]
  [--no-port-changes] [--thugpro <dir>] convert`
- Compare two staging folders (scratch script, not committed):
  C:\Users\kevin\AppData\Local\Temp\claude\C--Dev-thug2-decomp\c861af32-ae53-4deb-9c82-1d00b291c5a6\scratchpad\cmptree.ts
  `node cmptree.ts <a> <b>`; baselines in that scratchpad's `thps3new\base_default` / `base_none`.
