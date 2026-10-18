# thug1 - THUG1 levels in THUG2 (tools/thug1_levels, shared code in tools/level_port)

Agent "thug1". How to run it: tools/thug1_levels/README.md.

## Result

- `python -I tools/thug1_levels/thug1_levels.py convert` converts 16 levels by default: the 9 main ones (NJ, NY, FL,
  SD, HI, VC, SJ, RU, SE = Hotter Than Hell) and the 6 classic ones (SC, DJ, PH, VN, HN, SC2), each with its sky, and
  NJ_Skateshop (no sky). `skateshop` (THUG1's front-end shop: THPS4's skate shop again), `CAS_BEDROOM` and
  `TestLevel` convert when named. Kill zones: NJ 12, NY 22, FL 53, SD 48, HI 78, VC 13, SJ 7, RU 52, SE 5, DJ 16,
  VN 87, HN 2 (SC, PH, SC2 have no kill triggers).
- `selfcheck`: THUG2's LA / TestLevel round trips byte-identical, all 333 level files of THUG1's archives parse
  (scenes, collision + BSP trees, textures byte-identical written again, qb). THPS4's selfcheck still passes (139
  files), and its `convert` output is the same as before the split (scene / col / tex byte-identical; the .qb files
  differ only in the order of NeverScript's checksum name table, which isn't deterministic - same tokens and names).
- Game runs: NOT run in the game yet (the lead stopped game runs while the user uses the PC).

## Level select metadata

The table `LEVELS` at the top of `tools/thug1_levels/thug1_levels.py` (code, sign text, real-world location, THUG2
sign texture to use as an icon, level number, default). `levels.json` in each staging folder carries per level:
folder, level, sky, display, name, **game** ("THPS4" / "THUG1"), **location**, **icon**, level_num, default_stats,
lighting - documented at the top of `tools/level_port/ported_levels.py` (`load_manifest(game)` merges THPS4's and
THUG1's). THPS4's entries got game / location / icon too (`INFO` in thps4_levels.py). Level numbers: THPS4 19-31,
THUG1 40-58 (unique; no records - the info panel skips them as before).

## Tool layout

- `tools/level_port/` (moved from tools/thps4_levels with git mv): binio, prx (archives + LZSS), thug2_scene,
  thug2_col (reads v9 too), tex, qb (was qb4: `weights=True` for THUG1/THUG2 random tokens), nodes (+ THUG1 include
  expansion, kill commands with parameters), geometry (was convert.py: generic per-sector-vertex-array scenes and
  collision), common (ns compile, writing, lighting, THUG2 round trips), ported_levels (install: manifest, Levels.qb,
  signs, copying).
- `tools/thps4_levels/`: thps4.py (+ to_thug2_material, face_flags moved here), thps4_levels.py.
- `tools/thug1_levels/`: thug1.py (scene / collision readers, v9 -> v10), thug1_levels.py.
- `tools/menu_level/make_menu_level.py`: only `thps4_levels.*` -> `ported_levels.*` (5 lines + the header comment):
  installs both staging folders.

## Formats (THUG1 vs THUG2)

- `.pre` = `.prx`. Textures identical (classic skies: 8-bit paletted, swizzled - unswizzled to 32-bit).
- Scene: materials identical; sectors THPS4-style (per-sector vertex arrays) with THUG2-style mesh headers and LOD
  index lists (strips; 1 LOD in every shipped mesh). Colours 128 = full. Ends with hierarchy count 0.
- Collision v9 = v10 minus reaches (no large-face byte array, small faces' 4th byte 0, scale 0). BSP trees are
  THUG2's (both games have a few splits outside the object box - harmless): kept; reaches computed. Face flags:
  0x80 = camera collides (as THUG2); 0x2000 on every face and 0x4000 / 0x8000 dropped (mask 0x1FFF).
- QB: THUG2's tokens exactly; node arrays include `ncomp_*` global structures by name; `Pos=`.

## Reference bugs

- io_thps_scene's THUG1 collision exporter writes zero reaches only for THUG2 (THUG1 has none - right), its THUG1
  scene importer reads the LOD lists correctly; nothing new beyond batch 108's list.
- Our own: thug2_col.read_col took the reach flag from a variable the per-object loop reuses (fixed while adding v9).

## Left

- Game runs (load, skate, grind, kill zones) when the lead allows them again.
- THUG1's music, sounds, goals, peds, vehicles, moving objects (LevelObjects static), net games (no scn_NET).

## Game runs (2026-10-18, after main's levelselect merge)

- **The Hawaii pool bug - cause:** THUG2's SK3_KillSkater needs a restart. Without nodename= it passes the global
  kill_skater_nodename (0 until some kill names one) to `KillSkater node=`, which finds no node: the skater stays
  dead and is never put back. Hawaii's pool (Pool_Water) runs HI_Pool_Water, a level script whose own
  Sk3_Killskater_Water names TRG_Restart_Boardwalk; the converter took the call from the trigger script
  (`HI_Pool_Water`, no parameters) and wrote a bare SK3_KillSkater_Water. THPS4's kill zones were all written bare,
  so they had the same bug.
- **Fix:** a level script's own kill parameters are carried over (nodes.kill_args / kill_commands), and any kill
  left without a restart gets the restart nearest its trigger node (nodes.add_kill_restarts; THPS4 and THUG1). Now
  every kill script of the 27 levels names a restart (scratch check).
- **Tested in the game:** Hawaii with the Player1 restart moved over the pool for the test: the skater falls in,
  "WATER... BAAAD", and is put back at TRG_Restart_Boardwalk. Hawaii from its own start and Moscow load and skate.
  The level select's THUG1 section shows (Down from THUG2), NEW JERSEY ... MOSCOW signs; no globe stars showed for
  THUG1 levels.
- **Also fixed (level_select.py, from the levelselect merge):** switching to the THUG1 section crashed the game
  (0x0057F550 null read under GetLevelRecords): the high-score panel's skip list for our level numbers was empty -
  the numbers already in `text` (the gaps script's, added just before) were taken as present. Now each script's own
  body is checked.
- Trigger faces: kept for every kill node whose THUG1 collision has them (e.g. FL 15 objects); some THUG1 kill
  scripts sit on objects with no trigger faces in THUG1 itself (FL_boa_ground_01...): nothing to keep there.
