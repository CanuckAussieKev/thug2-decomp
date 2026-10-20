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
- (lead, 2026-10-20) Merged; TP-30 approved by the user.

## TP-31: THUGPRO's spawns (agent thug1-thugpro, 2026-10-20) - approved by the user, implemented, NOT run in the game

Why the order counts: THUG2 starts player n at the n-th Multiplayer restart of the node array and respawns at one of
the first 8 (Skate 0x00566100 -> GetRestartNode 0x0046B790, which walks the node array; CSkater::MoveToRandomRestart
0x00580830 passes Rnd(8)). So NJ's Player1-first swap did matter (player 0 started 83 units from where a THUGPRO
player 0 does), and so do VC's reordering and SJ's removed spawn.
- Data: `tools/compat/thugpro/thug1_levels.json` `spawns`, written by `node tools/thug1_levels/thugpro_compare.ts
  --extract-thugpro` (`thugpro_edits.spawnEdits`: THUGPRO's node array against THUG1's, every Multiplayer / Horse /
  CTF / Team restart - moved, types, renamed, added, removed, and THUGPRO's spawn order by THUG1's names). Found: VC
  TRG_restart_SCJ moved 326 (onto TRG_Restart_SJ's spot) and made the first spawn; HN TRG_Restart01 moved 43; SC2
  TRG_CTF_Restart_Yellow moved 165; SJ TRG_G_Collect2_RestartNode [Player1 Multiplayer Horse] -> [Player1]; HI
  TRG_Proset1_restartnode -> _trg_Proset1_restartnode (no script or kill names it - the converter checks); no spawns
  added or removed; every other level's order is THUG1's.
- Applied by `thug1_levels.applySpawns` (our node form; the spawns' array slots refilled in THUGPRO's order, every
  node's Links renumbered) with `nodes.convertNodes(..., player1First=false)` (shared, additive). THUG2's start-point
  picker (0x00566100) looks restarts up by type through GetRestartNode (Multiplayer, else Player1), so the
  single-player start should still be the first Player1 restart without the swap - to confirm in the game (NJ).
- Checks: `thugpro_compare.ts`: spawns same (names, places, order) in all 12; `--compat none` output equal to before
  TP-31 (95 files: .qb as code + sorted names, the rest byte for byte); ns.exe whole, links in range, every face
  reachable in all 16; make_menu_level into THUG2-3; selfchecks THUG1 333 / THPS3 323 / THPS4 139 / THAW 735, 0
  failures; npm test 52 / 0 (applySpawns' moves, types, renames, order and Links tested).

## THUGPRO hand edits: inventory (`thugpro_compare.ts --details`; nothing changed in our ports but the spawns)

Counts per level (NJ NY FL SD HI VC SJ RU HH VN HN SC2); "play" = whether it can make a THUGPRO player's game differ
from ours in the same session.
- **Rails added** (play: yes - grindable only for THUGPRO players, they look floating to ours): NJ 20 ed_rail, NY 11
  ed_rail, RU 17 natas_* / Natas_Alley_Barrier, VN 3 TRG_TP_VN_Natas, SC2 11 TRG_TP_SC2_Natas. NJ also reuses THUG1's
  unlinked rail point TRG_rail_e_midhouse4_12 (moved 4468 to the clinic roof, relinked from TRG_rail_e_clinic_24).
- **Geometry present in net games** (AbsentInNetGames taken off; play: yes - solid only on THUGPRO's side in a net
  game): NJ 182 geometry + 7 objects (74 with collision, 1087 faces: the LO gates...), NY 11 (2 solid: plants), SD 67
  geometry + 240 rails + 36 race markers (THUG1's demo skate area - Geo_Demo_LedgeBox / Bowls / BankOb / Pyramid,
  1799 faces, its ledges grindable: a whole skate area only THUGPRO players have online), HI 3 chopper death planes +
  the Chopper, VC 350 (184 solid, 2836 faces: g_sj_detail_*, park trees), RU 105 (21 solid, 934 faces: dish base,
  drain pipes, lights, trees), SC2 Gym_Doors01. The other way (+AbsentInNetGames): HI 15 goal game objects, 4 peds.
- **Not created at start** (CreatedAtStart off; play: yes where solid): SC2 13 objects (whirly vents, trash cans,
  cones, boxes, gym doors - 150 faces) gone on THUGPRO's side; VC trp_death_SJ (a kill trigger: THUGPRO players don't
  die there); NJ NJ_Small_SouthEast07 / IC_StationBlock (160 faces); HI GOLF_BALL_START / HI_ElevatorDoor (84 faces);
  RU 4 occluders (render only), HN 4 (mountains / trees scenery, a cursor killer); secret tapes and Old Skool icons in
  all (goal pick-ups). Made created: RU race4 ladders (4 climbing nodes + 2 geometry), VC the leaf blower.
- **Collision**: face flags - wall-ridable (0x4) added on SD 90, VC 12, RU 238, HN 74 faces (play: yes - THUGPRO
  players wallride walls ours can't), RU 2 faces made non-collidable; HH: 81 prop objects lost their collision (1915
  faces: sodas, napkins, trays, dt_choptail - THUGPRO players pass through what ours hit); HN 12 objects with fewer
  faces (DeltaSign01, dt_planeramp01, fuselage01 - small), SC2 Qtr_Pipes29 (24 -> 20, box 68: a quarterpipe
  reshaped), SC2 8 shadow objects (47 faces) removed; hundreds of empty (0-face) objects dropped (NY 639, RU 462...: no
  effect). Unreachable faces in THUGPRO's own trees: NY 1934, RU 2216, SC2 160 (earlier section).
- **Moved / changed nodes** besides the spawns: NJ train path waypoints (46-85), LO_Fluff_Chopper (2000 down), HI
  Chopper_Hard_Rotors (16), RU tkr_cannonRail01/02 (1.3-1.8: grind line slightly different); rail terrain / type
  (grind sound only, no play effect): NY 370, SD 257 (+105 demo rails made trick objects), SC2 25, HI 20, NJ 11;
  waypoint terrains VN 29, SC2 59; peds: THPS5 -> THPS6 skeletons, profiles, AI types (as ours do; NJ 175, RU 144,
  SD 129, SJ 111...); goal game objects' models set to "none" (NJ 115, FL 65, VC 49, RU 36...); restarts / rails /
  geometry losing goal trigger scripts; LevelObject rigid-body settings dropped (NJ 8 folding chairs, SJ crowd, HH
  freak stage). Dogs / skitch node / a proxim node reused as invisible GameObjects (NJ).
- **Added nodes** besides rails: NJ El train (2 vehicles), HI boat, HI _trg_ proset restart (the rename), VC / SJ
  transition arrows and their restart / waypoint (TRG_Restart_SJVC: Player1 only), VN 3 gulls + a bum.
- **Scripts**: added NJ 49, NY 50, FL 31, SD 27, HI 36, VC 1, SJ 35, RU 46, HH 29, VN 6; removed NJ 417, NY 33, FL
  199, SD 163, HI 179, VC 234, SJ 91, RU 155 (mostly goal scripts); "changed" NJ 229 ... SC2 14 - largely the
  recompile (THUG1's long if / else tokens 0x25 / 0x26 written as the short jumps 0x47 / 0x48, as ours are), with
  real edits among them: VN 58 sand kill zones made Sk3_Killskater_sand (from _Water), NJ occlusion triggers emptied,
  sound / ped-AI / car parameter changes. Kill zones and gaps are local (no effect on the other player's game).
- **Scene**: NJ 1287 and SC2 994 sectors with different bounding boxes; their collision is unchanged, so this is
  rendering data (THUGPRO's exporter) - no play effect.
- Also THUGPRO-only (not node edits): host-toggled "level changes" in nj / ny / fl / sd / hi / vc / sc2_thugpro.qb
  (banners, teleports, HI's Luau stage / chopper / kicker) behind SERVER_LEVEL_CHANGE flags - THUGPRO's protocol.
