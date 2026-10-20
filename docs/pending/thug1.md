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

## THUGPRO hand edits: applied (TP-32 ... TP-39; agent thug1-handedits, 2026-10-20) - NOT run in the game yet

The user's decision: "Everything [THUGPRO] changed needs to be in the differences files that we can control with the
compat flag." So every hand edit that is data is now in the register (tools/compat/thugpro/README.md TP-32 ... TP-39,
`tools/compat/thugpro/thug1_levels.json`), written by `node tools/thug1_levels/thugpro_compare.ts --extract-thugpro`
(`thugpro_edits.handEdits`: THUGPRO's node arrays and collision against THUG1's own; never typed in) and applied by
`tools/thug1_levels/hand_edits.ts` - the node edits to THUG1's node array before conversion (`applyNodeEdits`: our
converter then treats them as THUG1's own), the collision edits to the converted collision (`applyColEdits`). `--compat
thugpro` (the default) applies all; `--compat none` gives the output of before (checked: all 95 files the same, .qb
as code + sorted names). Each section can also be dropped from the JSON on its own.

What's applied (numbers from the data):
- TP-32 moved nodes: NJ TRG_TrainTest_00-03 (the El train's path) and LO_Fluff_Chopper, HI Chopper_Hard_Rotors, RU
  tkr_cannonRail01 / 02 (8).
- TP-33 AbsentInNetGames: off on 1006 (SD's demo area 344 - 67 geometry, 240 rails, 36 markers, the kart; VC 350; NJ
  189; RU 105; NY 12; HI 5; SC2 1), on for 16 (HI's 15 goal pick-ups, NJ's vis proxim).
- TP-34 CreatedAtStart: off on 64 in 11 levels (SC2 13, NJ 19, HI 6, RU 6 - its 4 occluders, HH 8, HN 4, VC 2 -
  trp_death_SJ -, the secret tapes and Old Skool icons), on for 7 (RU's race4 ladders 6, VC's leaf blower).
- TP-35 values on 665 nodes: NJ's 4 class changes (dogs, skitch node, vis proxim made invisible GameObjects), rail /
  waypoint types and terrains (NY 359, SC2 84, VN 29, HI 20, SD 17, NJ 5), clusters / TrickObject (SD's demo area 123,
  NY 8), IgnoreTOD (NY 7), NeverSuspend (NJ 2), RU's occluders' Occluder flag and godrays' times of day, HI's tiki sound
  types (3), doors' CollisionMode (4) and a ped waypoint's SkateAction.
- TP-36 rails: 62 added from node data (NJ 20 ed_rail, NY 11, RU 17, VN 3, SC2 11) + NJ's reused TRG_rail_e_midhouse4_12
  (moved to the clinic roof, retyped) and 4 links (NJ 2, NY 1, HI 1 - a ped path).
- TP-37 face values: wall-ride 0x4 on SD 90, VC 12, RU 238, HN 74 faces; non-collidable 0x10 on RU 2; face terrain on
  NY 39, SC2 108. Matched by object and face: by index where both have the same faces (vertex indices), else by vertex
  places (within 0.1); every edited object's face count is checked when applied.
- TP-38 collision out: HH's 81 props (1915 faces), SC2's 8 shadow objects (47 faces) and 4 faces of Qtr_Pipes29 (the
  object's BSP tree and reaches rebuilt).
- TP-39 collision reshaped: HN Inviso_SnowBlocker (17 vertices, up to 956 units), HH SE_JO_CatWalk41 (1 vertex, 4 units).

Checks (all 16 converted into THUG2-3, installed with make_menu_level): `thugpro_compare.ts` per level "hand edits as
THUGPRO's" - every applied edit shows in our output as THUGPRO has it except (our converter's doing): TP-34 the Old
Skool icons of NJ / HI / RU and VC's leaf blower (our converter leaves them out: models not in THUG2 - THUGPRO ships
veh_leafblower itself), TP-33 HI's 15 goal pick-ups (left out likewise), TP-35 NY's 3 LevelGeometry IgnoreTOD and RU's
godrays' Day_On ... Night_On (our converter keeps only some keys and flags of geometry - `nodes.KEEP_KEYS / KEEP_FLAGS`:
time-of-day rendering only). Everything else: NJ TP-32 5/5, TP-33 190/190, TP-35 12/12, TP-36 23/23; SD TP-33
344/344, TP-35 140/140, TP-37 10/10; SC2 all; RU TP-37 4/4, TP-36 17/17... (the full table: the report, section 11).
col_check: every face reachable in all 16; links in range and ns.exe decompiles every level; `--compat none` the same
as before; THUG1 selfcheck 333 / 0, THPS3 323 / 0, THPS4 139 / 0, THAW 735 / 0; typecheck / lint / npm test 60 / 0
(tools/thug1_levels/hand_edits.test.ts: JSON values, node and collision edits, the check).

Not taken - translation by THUGPRO's toolchain or no effect (`--extract-thugpro` prints each with names): peds'
settings (THPS5 -> THPS6 skeletons as ours, profiles, AI; peds are out of net games on both sides) - NJ 175, RU 144, SD
129, SJ 111, FL 88...; goal pick-ups' models set to "none" (not in THUG2) - NJ 115, FL 64, VC 48...; trigger scripts
taken off (goals THUGPRO doesn't port) - NJ 30, HI 19, SJ 10...; rigid-body settings dropped (NJ's folding chairs, SJ's
crowd, HH's freak stage); SelectRenderOnly written sRenderOnly (FL 7, NJ 1: its compiler); NY's 5 rails with a terrain
written as a flag next to their TerrainType; NY's 11 TERRAIN_GRINDRUSTYRAIL rails (not a THUG2 terrain: THUG2 reads
through a null for an unknown one - `nodes.fixTerrains`; ours keep THUG1's TERRAIN_GRINDMETAL); empty collision
objects dropped (NY 639, RU 462, SC2 136, HN 109, HH 30); zero-area and duplicate faces dropped (HN's 12 "reshaped"
objects: no play effect); HH hj_DirtFloater01's vertices (within 0.07: fixed-point rounding); the scene sectors' boxes
(NJ 1287, SC2 994: rendering).

Was awaiting the user's decision - THUGPRO's own content (now included by reference: "New in THUGPRO: included" below):
- NJ El train: 2 Vehicle nodes (TRG_TrainA01 / 02, Type El_Train) with THUGPRO's own model veh_El_Train (mdl 37 KB, tex
  55 KB, col 4.6 KB, qb 1.8 KB) and scripts (TRG_TrainA0n_Script -> NJ_TrainTestLoop01 / 02: move between the
  TRG_TrainTest waypoints - TP-32 moved those -, hide, wait, sound). AbsentInNetGames: neither side has it online;
  single player: THUGPRO's players see a train, ours an empty track.
- HI boat: 1 Vehicle (TRG_HI_Boat01) using THUG2's own BA boat model (veh_ba_boat, in BA_scripts.prx), with a THUGPRO
  script (27 bytes). AbsentInNetGames; single player only.
- VC <-> SJ transition arrows: TRG_SCJ_arrow_01 (VC) / TRG_VC_arrow_01 (SJ), GameObjects with THUGPRO's own model
  transition_arrow (mdl 5.7 KB, tex 8.3 KB, col 1.4 KB) and script transition_arrowScript (27 bytes), plus VC's
  TRG_Restart_SJVC (a Player1 restart) and SJ's TRG_Restart_VCSJ (a waypoint). AbsentInNetGames: online neither side
  has them (the earlier inventory's "teleport THUGPRO players" holds for single player only).
- Venice's gulls and bum: 3 Anl_Seagull peds using THUG2's AU seagull (model / anims in THUG2's AU archives, not loaded
  in VN) with THUG2's AU gull scripts copied into vn.qb (au_gull, AU_Gull_Fly / Return, sounds: 630 bytes); THUG1's bum
  TRG_546 renamed TRG_BUM_01 and given the model skater_olliethemagicbum (in neither THUG2's nor THUGPRO's archives that
  I could find) and a THUGPRO script. All AbsentInNetGames.
- Venice's 58 sand kill zones (Sand_* / Pit_* trigger scripts): THUGPRO calls Sk3_Killskater_sand, a script of its own
  (qb\thugpro\thugpro_utils.qb: the smashcake sound, two more death messages, SK3_Killskater_Finish with "sand" and
  other colours); THUG2 has no such script (its skutils.qb only names it), so it isn't a value swap. Ours keep THUG1's
  Sk3_Killskater_Water (a splash). Local only: kills don't travel.
- Level-script edits: NJ's 85 emptied trigger scripts (occlusion / vis / proximity triggers that Kill / Create
  occluder and visibility sets - rendering), other emptied scripts (RU 23 tank scripts, HI 13 gap scripts, SD 11...,
  mostly goals), added scripts (NJ 43 / 10 KB, RU 46 / 17 KB, NY 44 / 14 KB, SJ 35 / 6 KB: much of it THUGPRO's
  compiler's own small helpers), and sound / ped-AI / car parameter changes inside scripts.
- Host-toggled level changes: nj / ny / fl / sd / hi / vc / sc2_thugpro.qb (2.6, 3.3, 2.0, 1.2, 2.4, 1.0, 0.3 KB) behind
  SERVER_LEVEL_CHANGE flags - THUGPRO's protocol; without it neither side runs them.
- No new meshes or collision of THUGPRO's own in the 12 levels besides the models above (no added scene sectors or
  collision objects).

Concerns: NY's TERRAIN_GRINDRUSTYRAIL in THUGPRO's own files (a crash in THUG2 when loading an unknown rail terrain, as
THPS3 Tokyo's typo did - unless THUGPRO adds the terrain; worth knowing for mixed games, not ours to fix); NJ's class
changes give our NJ 4 invisible GameObjects with no script (harmless, they're THUGPRO's way of keeping goal hooks);
TP-34 makes VC's SJ death zone (trp_death_SJ) not created, so falling there no longer kills (as THUGPRO); untested in
the game: SD's demo area in a net game, the new rails (grind them in NJ / NY / RU / VN / SC2), wallriding the new
faces (RU's church, SD's courtyard walls, HN's outer walls), HH props passable, RU's ladders, HN's snow blocker.
- Shared code: none changed (tools/thug1_levels only; the register README and the profile JSON).

## New in THUGPRO: included (TPN-THUG1-1 ... 4) and our own changes (PC-THUG1-1 ... 20) - agent thug1-thugpro-new, 2026-10-20 - NOT run in the game yet

The user's decision (Morten agreed): THUGPRO's own content goes into our ports, in one separate section `thugpro_new` of
`tools/compat/thugpro/thug1_levels.json`, BY REFERENCE (references + crc32, never THUGPRO's bytes - the repo is public);
the converter reads the files from THUGPRO's install (`--thugpro`, default %LOCALAPPDATA%\THUG Pro) and warns / skips
when one is missing or differs. And: our converter's own deviations from THUG1's data are data too, section
`port_changes` (PC-THUG1-n, op add / set / remove, `required`), on whatever `--compat` says.
Register: tools/compat/thugpro/README.md "New in THUGPRO (not in any source game)" and "Our own changes (port_changes)".
Code: tools/thug1_levels/thugpro_new.ts, port_changes.ts (tests: thugpro_new.test.ts); `thug1_levels.ts convert`
options `--thugpro`, `--no-thugpro-new`, `--no-port-changes`.

Included (counts: entries in the data; all as THUGPRO's in our converted levels per `thugpro_compare.ts` "new in
THUGPRO as THUGPRO's", except the 2 refused scripts):
- TPN-THUG1-1 models (27 file entries): NJ veh_El_Train, VC / SJ transition_arrow, VC veh_LeafBlower (THUG1's leaf
  blower now in our VC - TP-34's "on" for it shows 3/3), RU THPS2_Case and veh_tank_ru (THUG1's RU tanks and Old Skool
  icon now kept), the bum Skater_OllieTheMagicBum (FOUND: loose in THUGPRO's data\models\skater_secret; used in VN and
  NY). Written to <staging>\models, installed into the overlay by make_menu_level (ported_levels.installFiles).
- TPN-THUG1-2 nodes: add 10 (NJ's 2 El trains, HI's boat - THUG2's own BA boat model -, VC's arrow + TRG_Restart_SJVC,
  SJ's arrow + waypoint TRG_Restart_VCSJ, VN's 3 gulls - THUG2's AU seagull, loose in THUG2's Data), set 10 (VN's 5 bums
  - TRG_546 renamed TRG_BUM_01 -, NY's 3 bum peds, FL's gator on the gorilla model and goat). Kept whole (NodePass forced).
- TPN-THUG1-3 scripts: add 299 (the new nodes' scripts, Sk3_Killskater_sand from thugpro_utils.qb...), set 604 (VN's 58
  sand kills, gap scripts rewritten with GetTriggerArray / TriggerType, ped / car / sound parameters...), remove 130
  (emptied: NJ 69 of its 85 occlusion / vis / proximity triggers - the other 16 aren't in our port -, RU's tanks...).
  Applied when they can run here (scriptCheck); refused: SJ_startup, RU_Cannon at conversion, ~30 at extraction
  (LoadZone, THUG1 goal calls, unknown commands - listed by `--extract-thugpro`).
- TPN-THUG1-4 host-toggled files: nj / ny / fl / sd / hi / vc / sc2_thugpro.qb copied as they are (inert: THUGPRO's
  protocol messages 155-159 run them; all off = THUGPRO's default).

Left out and why: NJ / HI Old Skool icons and HI's 15 lei pick-ups (THUGPRO itself: model "none", no model shipped);
NY's 11 TERRAIN_GRINDRUSTYRAIL rails (deliberately: unknown terrain in THUG2, a likely crash); scripts needing
THUGPRO's own commands - above all its LoadZone (VC <-> SJ level change: the arrows are decoration in ours).

C++ needs (not implemented; proposals in our style, credited to THUGPRO for the idea): (1) a "change level and start
at a named restart" for the VC <-> SJ transition (THUGPRO's LoadZone + its DLL's loading screen): a script command of
ours that sets the next level's start node and runs change_level; (2) THUGPRO's protocol messages 155-159 to switch
the host-toggled level changes (`<level>_thugpro.qb` toggle scripts) - a net message handler of ours calling the
toggle script of an option item; (3) its THUGPRO_SafeKill / THUGPRO_ForceCreate (used by those toggle scripts).

port_changes: PC-THUG1-1 ... 5 switchable (kill restarts, net spawns for the classics, Player1-first swap,
DuplicateTrigger resolution, ground restarts), PC-THUG1-6 ... 20 required (README table). PC-THUG1-7 (goal GenericNodes
left out) is marked required without proof of a crash: NEEDS VERIFICATION.

Checks: `--compat none` == before (95 files, .qb as code + sorted names); `--no-thugpro-new` == the previous default;
an empty `--thugpro` folder: a warning and the previous default; `--no-port-changes`: PC-THUG1-1 ... 5 off, the rest
the same; the extraction is a fixed point; all 16 reconverted into C:\Games\THUG2-3 and installed with make_menu_level
(overlay models\ present); every face reachable, links in range, ns.exe decompiles every .qb whole; selfchecks THUG1 333
/ THPS3 323 / THPS4 139 / THAW 735, 0 failures; typecheck / lint / npm test 85 / 0.
Finding for the script audit (not changed here: tools/level_port/script_port.ts): our translator treats THUG2's
TriggerType as unknown, so THUG1's gap conditions become never-true; THUG2's own levels use it.
Shared code: tools/compat/compat.ts (portChanges), tools/level_port/ported_levels.ts (installFiles copies
<staging>\models) - both additive.
To try in the game: NJ's El trains, HI's boat, the VC / SJ arrows, VN's gulls / bums / sand deaths, VC's leaf blower,
RU's tanks, the replaced gap scripts (HN, SC2, SD), NJ's occlusion (emptied triggers).
After merging main (THPS4 / THPS3 / THAW): main's compat.ts API only - port_changes read through compat.portChanges
(entries in compat.Entry's shape: rule, target {levels, what}), thugpro_new through compat.thugproNew (one entries list,
target {level, name}; levels: THUGPRO key and archive per level - thugpro_new.toSection / fromSection), re-extracted (a
fixed point). THPS3's models copy in ported_levels.installFiles kept (mine dropped), changed to merge instead of
replace per top-level folder (two stagings may both bring modelseh). Checks re-run: --compat none / --no-thugpro-new /
empty --thugpro the same as before (95 files); new in THUGPRO as THUGPRO's (all but SJ_startup, RU_Cannon); faces,
links, ns.exe whole in all 16; selfchecks THUG1 333 / THPS3 323 / THPS4 139 / THAW 735, 0 failures; npm test 95 / 0.
