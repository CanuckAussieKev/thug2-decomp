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

## THUGPRO compatibility (agent thug1-thugpro, 2026-10-20) - NOT run in the game yet

Goal: a THUGPRO player and one of ours in the same THUG1 level online, as the THAW ports got (docs/pending/thaw.md).
Everything is generated from THUG1's own files; THUGPRO's install (%LOCALAPPDATA%\THUG Pro\data\pre, read only) was
only the reference and is compared against (`tools/thug1_levels/thugpro_compare.ts`) - nothing of theirs is copied.

- **Level names** (the only thing that kept the levels from loading across - 0x46 / 0x33 carry the load script's
  checksum): THUG1's own names now (its Levels.qb's `level=` loaded by `Load_<level>`, `thug1_levels.ownName`): nj,
  ny, fl, sd, hi, vc, sj, ru, vn, hn, sc2 (folders, Load_ / Level_, <name>_NodeArray, skies <name>_sky) - THUGPRO's
  ports load exactly these (checksums compared with THUGPRO's Levels.qb). A THUG1 name THUG2 already has keeps
  `thug1_` (checked against THUG2's own Levels.qb at convert time): thug1_sc, thug1_dj, thug1_ph, thug1_testlevel -
  THUGPRO plays THUG2's own SC / PH / DJ there (no ports). Levels THUG1 doesn't load by their own name keep it too:
  thug1_nj_skateshop, thug1_skateshop (THUGPRO's front end Load_Mainmenu = THUG2's front end here: not taken),
  thug1_cas_bedroom. Hotter Than Hell: THUG1's SE is THUG2's SE (Pro Skater), THUGPRO calls it hh - **TP-30** in
  the register, approved by the user 2026-10-20 (`--compat none`: thug1_se). `--legacy-names`: thug1_<code> as before.
  Saved layouts: `tools/thug1_levels/migrate_layouts.ts --game <folder>` (none existed in any game folder; checked on
  a made-up one in THUG2-3, removed after). `ported_levels.loadManifest` now refuses two staging folders naming a
  level (or sky) alike.
- **Frame, crowns, game-mode nodes**: ours = THUG1's = THUGPRO's. Every shared node at the same place except THUGPRO's
  own hand edits (NJ TRG_rail_e_midhouse4_12 moved 4468 and re-terrained, TRG_TrainTest_00-03 by 46-85,
  LO_Fluff_Chopper 2000 down; HI Chopper_Hard_Rotors 16; RU tkr_cannonRail01/02 by 1.3-1.8; VC TRG_restart_SCJ 326;
  HN TRG_Restart01 43; SC2 TRG_CTF_Restart_Yellow 165). Crowns: same in all 12. CTF / flag / team nodes: all 21 (VC
  17: THUG1 has no flag bases there) with the 16 (12) flag objects in every level THUGPRO has - nothing missing. The
  four without any (SC, DJ, PH, NJ_Skateshop) have none in THUG1 either (single-player levels in THUG1). THUG2's own
  SC / PH / DJ have them (17 mode nodes, 10 crowns, 15-18 MP restarts) and are the same levels in nearly the same
  frame (941 / 970, 1284 / 1389, 504 / 552 shared nodes at the same place) - a proposal, not done: take THUG2's own
  game-mode nodes for the THUG1 classics, or (simpler) play THUG2's own SC / PH / DJ online.
- **Multiplayer restarts**: only NJ's differ from THUG1 / THUGPRO by our doing: the converter puts the Player1 restart
  first (nodes.convertNodes - the skater starts at the first player restart), swapping TRG_Restart_Player1 and
  TRG_restartforgary01 (83 units apart) - deliberate, kept (positions travel; whether the spawn order matters online
  isn't decompiled yet). The rest are THUGPRO's edits: VC (TRG_restart_SCJ moved), SJ (THUGPRO dropped Multiplayer
  from TRG_G_Collect2_RestartNode: 10 vs 9), HI (renamed TRG_Proset1_restartnode to _trg_Proset1_restartnode), HN /
  SC2 (moved).
- **Level lights**: THUG1's LevelLight nodes (NJ 85, NY 141, FL 5, SD 6, SJ 73, RU 202, NJ_Skateshop 1) were left out;
  now kept as they are (THUG2's own levels have 60-230 such nodes, THUGPRO's ports keep THUG1's unchanged). NOT run.
- **DuplicateTrigger** (the classics' second gap triggers): THUG2 has it after all - a member function of the
  skater's SkaterGap component (0x00512CC0), not a CFunction; THUGPRO keeps the scripts. Ours gives such nodes the
  linked node's trigger script at build time (same effect); the comment in thug1_port.resolveDuplicateTriggers says so.
- **Audit (what THUGPRO's have that ours don't)**: goal objects (collectibles, race objects, SJ's 71 monster peds,
  game objects whose THUG1 goal models THUG2 lacks: FL 64, RU 35, NJ 22, SD 24, HI 16) - goals aren't ported; the
  car_6 vehicles (HI's 2 buses, VC's 6 tour buses: THUG2 ships skeletons\car_6.ske but doesn't load it - THUGPRO adds a
  LoadSkeleton; proposal); animal peds (NY / FL pigeons, HI shark / dogs, NJ dogs: crashed THUG2's ped AI in our game
  run; THUGPRO keeps some as NoPedLogic or invisible GameObjects); THUGPRO's own additions (NJ El train and 22 ed_rail
  rails, RU 17 / SC2 11 / VN 3 Natas rails, VN 3 gulls and a bum, HI boat, VC <-> SJ transition arrows) - not THUG1
  data, not taken.
- Checks (THUG2-3, all 16 converted, installed with make_menu_level): ns.exe decompiles every level's qb whole; links
  in range (rail -> ClimbingNode links are THUG1's own); every collision face reachable in all 16 (THUGPRO's own: NY
  1934, RU 2216, SC2 160 unreachable); THUG1 selfcheck 333 / 0, THPS3 323 / 0, THPS4 139 / 0, THAW 735 / 0; npm test
  41 / 0. Geometry byte-identical to before (scene / collision / textures); only names and node arrays changed.
- Shared code: `tools/thaw_levels/thugpro_compare.ts` exports its helpers (nameOf, isCrown, isMpRestart, sequence,
  sameSequence, maxDiff, CTF_NAMES, TOLERANCE); `tools/level_port/ported_levels.ts` duplicate level / sky name check.
