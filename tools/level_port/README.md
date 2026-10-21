# level_port - shared code of the level converters

Used by `tools/thps4_levels` (THPS4), `tools/thug1_levels` (THUG1), `tools/thps3_levels` and `tools/thaw_levels` (THAW); `ported_levels.ts` also by
`tools/menu_level/make_menu_level.ts`, which installs what they convert.

- `binio.ts` - little-endian reading / writing.
- `prx.ts` - THUG2 `.prx` / THUG1 `.pre` archives (same format), LZSS.
- `thug2_scene.ts`, `thug2_col.ts`, `tex.ts` - THUG2's scene, collision (version 10; reads THUG1's 9) and texture
  files, read and written field by field: THUG2's own files come out byte-identical (each tool's `selfcheck`).
- `geometry.ts` - scenes with one vertex array per sector (THPS4, THUG1) to THUG2's per-mesh vertex buffers;
  collision objects to THUG2's (fixed-point vertices, BSP trees, reaches) for THPS4.
- `overlay_bias.ts` - z bias for the layers lying on other surfaces in scenes whose source has none (THPS3, THPS4):
  found from the geometry (coplanar, overlapping, same facing); without it they z-fight - shimmer - with the surface
  under them. THUG2 applies a material's z bias as D3DRS_DEPTHBIAS (0x004D1F51 / 0x004D2420).
- `layer_check.ts` - CLI: per level, mip chains that stop short and how much layer area has a z bias between -
  a port compared with THUG2's own (`node tools/level_port/layer_check.ts <levels folder> [<level> ...]`).
- `qb.ts` - THPS4 / THUG1 qb files: globals, names, script statements; values as NeverScript text.
- `nodes.ts` - node arrays: what's kept, kill zones, THUG1's included structures.
- `ped_rigs.ts` - every ported human ped on the skeleton its THUG2 profile's model is rigged to (Ped_Female for
  women, else THPS6_human) and the level's load_level_anims for Ped_Female's animations; `ped_rig_check.ts` audits
  converted levels (`node tools/level_port/ped_rig_check.ts <overlay folders>`).
- `peds_cars.ts` - THPS4's peds and cars made again as THUG2's own (THUG2's ped AI walking / vert skating, its
  Car_Generic01 on the same paths, THUG2's profiles and models).
- `script_port.ts` - the script translator (all four games); its TriggerType translation (`triggerType`) is shared.
- `thug2_members.ts` - THUG2's script member functions and the components that run them, generated from the exe
  by `node tools/extract_members.ts` (committed: the converters don't need data/derived).
- `common.ts` - NeverScript compiling, writing a level's files, lighting keys, THUG2 round trips.
- `ported_levels.ts` - the staging folders' `levels.json` (format at its top), the Levels.qb structures, the level
  select signs, copying into the overlay.
- `col_check.ts`, `script_diff.ts` - CLIs: a collision file's BSP check; converted scripts compared between two
  staging folders.

These were Python until 2026-10-20 (docs/typescript.md); tools/parity/level_port has the cases that proved the
TypeScript equal to the Python on the games' data (check out commit 57d7b5e, the last with the Python, to run them).
