# thps3-thugpro - THPS3 level ports: THUGPRO compatibility (tools/thps3_levels)

Agent "thps3-thugpro", 2026-10-20. The same treatment the THAW ports got (docs/pending/thaw.md "THUGPRO"), for the
THPS3 ports. Everything is generated from THPS3's own files (C:\Games\THPS3, read only) and THUG2's own data; THUGPRO's
installed levels (%LOCALAPPDATA%\THUG Pro\data\pre) were only read to compare with (`thugpro_compare.ts`) - nothing of
theirs is copied - except the two register entries the user approved afterwards (TP-10, TP-11 - "Register entries"
below). **NOT run in the game yet.**

## What travels online (from our decompilation, see thaw.md)
The level is known only by its load script's checksum (0x46 / 0x33); positions travel as world coordinates (0x23);
KOTH picks a crown by its index among the Crown nodes; CTF flag pick-ups are decided on the server
(SK4_Scripts Team_Flag: `if OnServer` -> CTF_Team_Flag_Trigger), each side draws the flags where its own node array
puts them.

## Before (THUG2-1, main bc9f01d) - `thugpro_compare.ts --details`
- Names: every load script differs (Load_thps3_foun vs load_foun).
- Coordinates: THPS3's frame on both sides - every shared node at the same place except THUGPRO's hand moves (peds,
  a few particles: Foun 5, Sub 3, SI 2, Shp 6, Burn 9 nodes; none online-relevant).
- KOTH crowns: same count, order and places in all 9 levels THUGPRO ports.
- Multiplayer restarts: ours 16 / theirs 20 etc. - **the only differences are THUGPRO's four TRG_CTF_Restart_<colour>**
  (every THPS3 restart is in both). No filtering, no converter bug. Ware: THUGPRO turned THPS3's four CTF1 restarts into
  its TRG_CTF_Restart_* (so 14 / 14 with different names).
- CTF: ours none - THPS3's flag bases (GenericNode Type CTF_1 / CTF_2) were dropped with the other generic nodes.

## Changes (tools/thps3_levels)
- **Names from THPS3's own load scripts** (levels.qb master_level_list: Load_Foun -> `foun`): foun, can, rio, sub, si,
  tok, shp, ware, burn, ros - the same as THUGPRO's 9 (checksums are case-blind). Kept `thps3_<lvl>`: Tut and SkateShop
  (THPS3 has no regular_level on them: not network levels; "skateshop" is also the overlay's front-end shop and the
  other games' shops), the park editor's sets, and Ap / LA (THUG2 has its own Load_AP / Load_LA - its classic remakes,
  which THUGPRO plays; Canada THUGPRO also plays as THUG2's CA, but THPS3's `can` clashes with nothing). The rule reads
  THPS3's levels.qb and THUG2's Levels.qb (qb_scripts.prx) at convert time; `--legacy-names` gives the old names.
  Skies follow their level (rio_sky). The made objects' / materials' checksums keep `thps3_<lvl>`: scn / tex / col are
  byte-identical to before (checked, all 14 levels).
- **Capture the flag from THPS3's data**: gamemode.qb's flag1_info / flag2_info ("blue flag" node_type ctf_1, "red
  flag" ctf_2) -> TRG_CTF_Blue / TRG_CTF_Red (+ _Base) at THPS3's base nodes, in THUG2's own form (its Airport's: Type
  Flag_<c>, NeverSuspend, gameobjects\flags\ models, lod 800/801, `script TRG_CTF_<c>Script { Team_Flag <c> }`), and
  TRG_CTF_Restart_<c> (Type CTF, [CTF MultiPlayer Horse], "Team: CTF <c>") at THPS3's CTF_1 / CTF_2 restart nearest to
  the team's base. THUG2's skater code picks TRG_CTF_Restart_<colour> by team in netctf (0x00566100: team 0 Red, 1 Blue,
  2 Green, 3 Yellow - checksums 0xACD3422F...). All 12 network levels (incl. Can, Ap, LA). Appended at the end of the
  node array: no Links move.
- `thugpro_compare.ts` (THPS3's sibling of the THAW tool, its pieces imported - the THAW tool only got `export`s):
  names, frame, crowns, MP restarts (and which only one side has), CTF nodes (and which moved), node classes, trigger
  scripts only one side has, scripts, unreachable collision. Can / Ap / LA are compared with THUG2's own CA / AP / LA.
- `migrate_layouts.ts`: saved layouts <crc(thps3_foun)>.txt -> <crc(foun)>.txt, the scene line changed, nothing moved
  (old file kept as .before-thps3-names). Checked on a made-up layout in THUG2-1 (removed); no game folder had any.
- tools/game/README.md and boot_level.ts examples: `foun`.

## After (THUG2-1)
- Load script the same as THUGPRO's in all 9; crowns same; MP restarts 18 / 20 (THUGPRO's Green / Yellow team
  restarts - THPS3 has two teams); ours 0 unreachable collision faces in every level.
- CTF: Foun, Burn, Ros flags at the same place as THUGPRO's (both THPS3's bases). **Rio, Sub, SI, Tok, Shp, Ware:
  THUGPRO placed its own flags** (round-number positions; SI and Ware with the colours swapped against THPS3's
  ctf_1 = blue) - matched since by TP-10 (below). Team restarts: THUGPRO's mostly on the flag itself; ours at THPS3's team restart (local only, no effect online).
- Checks: thps3 selfcheck 323 files 0 failures; every level's qb decompiles through ns.exe except thps3_skateshop's
  (as before: a 0x37 token ns.exe doesn't read); make_menu_level into THUG2-1: all 14 THPS3 levels in the level select
  (signs sign_foun..., AP / LA texture signs as before); npm typecheck / lint / test pass (3 new tests).

## Register entries TP-10 / TP-11 (approved by the user 2026-10-20, implemented)
- Data: tools/compat/thugpro/thps3_levels.json, read from THUGPRO's install once by
  `node tools/thps3_levels/thugpro_compare.ts --extract-thugpro tools/compat/thugpro/thps3_levels.json` (not typed in).
  TP-10 `ctf_flags`: TRG_CTF_Red / _Blue (+ _Base) for the levels where THUGPRO's flag isn't at THPS3's base of that
  colour - the tool finds exactly Rio, Sub, SI, Tok, Shp, Ware. TP-11 `team_nodes`: TRG_CTF_Green / _Yellow (+ _Base),
  TRG_Flag_Red / Blue / Green / Yellow, TRG_Team_Restart in all 9 (THUGPRO has no TRG_Flag_*_Base there).
- Applied in `thps3_levels.ctfNodes` through `levelCompat` (compat.ts); `--compat none` leaves both out. Only places
  (Pos, THUG2's world), angles and colours come from the data; the nodes are ours (Airport form, THUG2's flag models,
  Team_Flag scripts; TRG_Flag_<c>Script, TRG_Team_Restart as THUG2's Airport: Type Team, [Team]). Team restarts stay
  THPS3's: Red / Blue the THPS3 CTF restart nearest to the (moved) flag, Green / Yellow the THPS3 multiplayer restart
  nearest to theirs (no THUGPRO restart position copied). Levels THUGPRO doesn't have (can, thps3_*) keep THPS3's CTF.
- `--compat none` checked against the earlier conversion: scn / tex / col byte-identical, levels.json identical, every
  qb identical in code and names (ns.exe orders its name table differently each run).
- With thugpro (THUG2-1): thugpro_compare CTF game objects 17 / 17 at THUGPRO's places in all 9 (the four team
  restarts are ours, as above); MP restarts 20 / 20 in Foun, Rio, Shp, Burn (16 / 16 Sub, SI; Ros 18 / 18; Tok 16 / 17 -
  THUGPRO's extra TRG_Restart_1P; Ware 18 / 14 - THUGPRO reused THPS3's CTF1 restarts). Links in range, rail links to
  rails, 0 unreachable faces, every qb decompiles (skate shop as before), make_menu_level fine; 4 unit tests.

## Register entry TP-12: THUGPRO's spawn points (approved by the user 2026-10-20, implemented)
The user's reason: players spawning at different places on the two sides could be an advantage or a disadvantage.
- Data: `spawns` in tools/compat/thugpro/thps3_levels.json, extracted (`--extract-thugpro`) by comparing THUGPRO's
  node arrays with THPS3's own: every restart with a network type (MultiPlayer, Horse, Team, CTF*) on either side
  that THUGPRO moved, retyped (Type, restart_types, RestartName, CreatedAtStart), renamed or added (`changed`: place,
  angles, types), and THPS3's network restarts THUGPRO dropped (`removed`). TRG_Team_Restart stays TP-11's.
  Per level: foun 6 (the 4 TRG_CTF_Restart_<c>, TRG_CTF1_Restart_1P/2P lost CTF_1), rio 4, sub 4, si 5 (+
  TRG_Restart_Horse05 moved 4 units), tok 5 (+ THUGPRO's TRG_Restart_1P, multiplayer), shp 4, ware 8 + 4 removed
  (THUGPRO reused TRG_CTF1_Restart_* as its team restarts; TRG_CTF2_Restart_* turned and retyped), burn 4, ros 4.
- Applied by `thps3_levels.applySpawns` after `ctfNodes` (compat.ts; `--compat none` leaves it out): a changed
  restart gets THUGPRO's place (Pos), angles and types, the node's other entries kept; an added one is appended in
  our restart form; a removed one becomes Type / restart_types Generic (kept: kills and scripts name it).
- `--compat none`: identical to before (scn / tex / col byte-identical, levels.json identical, qb code and names
  identical). With thugpro: every network spawn of ours equals THUGPRO's in all 9 (place, angles, Type,
  restart_types, RestartName, CreatedAtStart - spawnChanges(ours, THUGPRO's) empty); thugpro_compare CTF 17 / 17
  with nothing moved; multiplayer restarts the same set and places (20 / 20 Foun, Rio, Shp, Burn; 16 / 16 Sub, SI;
  17 / 17 Tok; 14 / 14 Ware; 18 / 18 Ros). Links, collision, ns.exe, make_menu_level fine; 5 unit tests here.
- **Order (correction from the lead, after the THUG1 agent's finding)**: the spawns' order counts - THUG2 starts
  player n at the n-th Multiplayer restart of the node array and respawns at one of the first 8 (Skate 0x00566100 ->
  GetRestartNode 0x0046B790; CSkater::MoveToRandomRestart 0x00580830, Rnd(8)). The extraction now also writes
  `order` (every network spawn's name in THUGPRO's array order), and `applySpawns` refills the spawns' slots in that
  order and renumbers every Links (as tools/thug1_levels' TP-31); `nodes.convertNodes(..., player1First=false)` for
  levels with TP-12 data (no Player1-first swap - 0x00566100 finds restarts by type). Reordered: Rio 21, Sub 24, SI
  26, Tok 26, Shp 24, Ware 15, Burn 21, Ros 19 spawns (Foun was already in order). thugpro_compare: "spawns same" (every
  network spawn by name in the same order) and "mp restarts same" in all 9; `--compat none` still identical; links in
  range, rail links to rails, 0 unreachable faces, ns.exe, make_menu_level, 6 unit tests.

## Register entries TP-13 .. TP-19: THUGPRO's hand edits as data (the user's decision 2026-10-20, implemented)
The user: "Everything [THUGPRO] changed needs to be in the differences files that we can control with the compat
flag." **NOT run in the game yet.**
- Extraction: `node tools/thps3_levels/thugpro_compare.ts --extract-thugpro tools/compat/thugpro/thps3_levels.json`
  now also writes `moved`, `flags`, `terrain`, `removed`, `renamed`, `rails`, `collision` (and `_notes`: what it found
  and left out, with the reason) - `nodeEdits` (THUGPRO's node arrays against THPS3's own) and `collisionEdits`
  (THUGPRO's col / scn against THPS3's bsp). The TP-10..12 sections came out identical. Nothing typed in.
- Applied (`--compat thugpro`, the default): `applyNodeEdits` edits THPS3's own node array in place before anything is
  made of it (so the converter's created-at-start / hidden analysis and objectSectors follow the edits), the removed
  nodes join the hidden set; `appendRails` appends the added rails after the CTF nodes (before TP-12's ordering);
  `collisionObjects` takes the faces out by their three corners in turning order.
- Per level: TP-13 moved Foun 5, Sub 4, SI 1, Shp 6, Burn 9; TP-14 flags Foun 12, Rio 12, Sub 16, SI 14, Tok 226, Shp 18,
  Ware 29 (19 of them the dark set we leave out), Ros 4; TP-15 terrains Rio 313, Sub 36, SI 18, Tok 296, Shp 125, Ware
  46, Ros 11; TP-16 removed SI 2, Shp 2, Ware 6, Burn 2, Ros 2; TP-17 none; TP-18 rails Foun 8, Rio 23; TP-19 SI 33
  faces + 3 objects' collision, Ware 2 faces, Ros 26 faces.
- Checks: `--compat none` identical to before (79 files: scn / tex / col byte for byte, levels.json, every .qb's code
  and sorted names). With the profile (THUG2-1): `thugpro_inventory.ts --applied` - every edit found in our output but
  Ware's 19 dark-set flags (the set isn't in ours: LEFT_OUT_PREFIXES); `thugpro_compare.ts`: every shared node at
  THUGPRO's place in all 9 (worst 0.000 - before: Foun 56, Sub 7, SI 59, Shp 302, Burn 347), spawns / crowns / mp
  restarts / CTF unchanged ("same"); kept faces have THUGPRO's turning; Links in range, rail links to rails, no
  duplicate names; col_check 0 unreachable in all 14; ns.exe decompiles every level qb but thps3_skateshop (as
  before); thps3 selfcheck 323 files 0 failures; make_menu_level into THUG2-1 fine; npm typecheck / lint / test (59
  tests, 4 new).
- Translation, not edits (not applied): class changes (THPS3's geometry-less EnvironmentObjects - lights, tapes,
  particle boxes, LOD helpers: none has a sector on either side - as GameObject / GenericNode; static peds as
  GameObjects; AnimatingObjects as Pedestrians; proximity objects as ProximNodes); THUGPRO's face flag bits on every
  face; collision bounding boxes over unused vertices (the "moved" collision objects: Tok 3, Sub 4, Shp 1, Foun 1 -
  the same faces); NeverSuspend / Permanent; the MEATALSMOOTH typo fix (our fixTerrains gave the same). Our own
  translation also shows in a THUGPRO-vs-ours comparison: TrickObject taken off geometry without a sector (crash
  fix), waypoints without AbsentInNetGames, CreatedAtStart on geometry the start scripts create.
- Concerns: Foundry's 8 wire rails are invisible here (their mesh is THUGPRO's - awaiting); Suburbia's blockers
  THUGPRO turned off include Inviso_PicnicCollide01, which it replaced with its own `_collfix` (awaiting) - ours has
  neither now; SI's THPS3 ocean noise is gone (THUGPRO's emitters await) - quieter there; Tokyo's 185 waypoints lost
  AbsentInNetGames (THUGPRO drives its trains / cars online; ours has no follower there - harmless, but untested in
  a network game); Foundry's presses created at start; nothing was run in the game.

## Awaiting the user (THUGPRO-authored content) - since included: TPN-THPS3, "New in THUGPRO" below
Each needs a separate decision: Morten's permission is for insights, not 1:1 copies. "Without it": what each side
sees as things are now (our side lacks it).
| Level | What | Size | Without it |
|---|---|---|---|
| Foun | `foun_wire`: the mesh of its 8 grind wires | 1 sector, 498 vertices, no collision | We grind the wires (TP-18) but see no wire; THUGPRO players see ours grinding its wires |
| Foun | `SouthDeck_QP01_coll`, `SouthDeck_QP04_coll`, `Trench_HP04_Back_coll`: invisible collision fixes on two quarter pipes and a half pipe's back | 4 faces each | Ours can go through / fall where THUGPRO's are stopped; seen from the other side as passing through |
| Foun | `Obj_Bucket` as a Vehicle with collision (and its node class) | 134 faces | The furnace bucket is solid for THUGPRO, not for us |
| Rio | `ES_Building_collision`: an invisible wall at a building | 4 faces, ~2100 x 1200 x 2100 units | Ours reach where THUGPRO's can't |
| Rio | 8 `TRG_Car_*_carplant_collision` (car collision) | 8 vehicle nodes + collision | THPS3's cars are solid for THUGPRO only |
| Rio | `TRG_Ped_2084` and its loop `TRG_Node_2076`-`2083` (the "untyped TRG_Node_*" are waypoints, not rails); `rio_tp_snd_*` (3 sounds) | 1 ped, 8 waypoints, 3 objects | Visual / sound only, local |
| Sub | `SUB_TRamp06_collfix`, `SUB_TRamp04_collfix`, `SUB_plywood_08_collfix`, `SUB_HHBYardWall_collfix`, `Inviso_PicnicCollide01_collfix` | 8, 8, 24, 192, 28 faces | THUGPRO's fixed ramps / yard wall / picnic collider: ours collide with THPS3's shapes (and no picnic collider - TP-14) |
| Sub | `TRG_IceCreamTruck_Carplant_collision`, the truck / plane as Vehicles; `TRG_BBQ_BurningNoise` | 1 collision, 2 classes, 1 sound | Truck solid for THUGPRO only; sound local |
| SI | `TRG_SFX_SOB_OceanWaves01` / `02` (emitters replacing THPS3's ocean noise, which TP-16 removed); `TRG_FireCannonsProximity01` (+ script) | 2 emitters, 1 node | Ours has no ocean sound there; cannons as TP-14 left them (not online, not at start) |
| Tok | Trains (shinkansen), planes and cars as THUG2 Vehicles run online; `TRG_Restart_EventR` / `EventL` (Generic restarts with its scripts) | 23 nodes reclassed, 2 restarts | Moving vehicles on THUGPRO's side only in network games (they can hit its players) |
| Shp | `SHP_TP_Particle_Smoke` / `_Sparks`, `shp_tp_snd_waterslide` | 3 objects | Visual / sound only |
| Ware | `Ware_RoomFlicker` (+ script) | 1 object | Visual only |
| all | THUGPRO's level scripts (added: Foun 184, Rio 94, Sub 191, SI 131, Tok 442, Shp 307, Ware 19, Burn 16, Ros 17 - ours has same-named ones for our own CTF nodes), its `_TP_` particles, small scene-only sectors without names (Rio 9, Sub 5, SI 2, Tok 1, Shp 2, Burn 1, Ros 1: 4-421 vertices, no collision) | | Local; scripts decide THUGPRO's own features (its events, vehicles) |

## THUGPRO hand edits: inventory (against THPS3's own data; nothing changed but the spawns)
(Written before TP-13 .. TP-19: those are applied now - the section above; the counts below are still THUGPRO
against THPS3's own data, `thugpro_inventory.ts` without `--applied`.)
`node tools/thps3_levels/thugpro_inventory.ts --thug2 <folder> [--examples N] [--json out]` lists them per level.
Counts: moved / renamed / added / removed / changed nodes; scripts added / changed; collision objects differing.

| level | moved | renamed | added | removed | changed | scripts + / ~ | collision |
|---|---|---|---|---|---|---|---|
| foun | 5 | 0 | 29 | 0 | 33 | 184 / 35 | 5 |
| rio | 0 | 0 | 61 | 0 | 376 | 94 / 21 | 1 |
| sub | 4 | 0 | 27 | 0 | 107 | 191 / 149 | 9 |
| si | 2 | 0 | 20 | 2 | 207 | 131 / 59 | 11 |
| tok | 0 | 0 | 20 | 1 | 608 | 442 / 15 | 3 |
| shp | 6 | 0 | 20 | 2 | 208 | 307 / 311 | 1 |
| ware | 4 | 1 | 17 | 5 | 75 | 19 / 32 | 20 |
| burn | 9 | 0 | 17 | 2 | 9 | 16 / 18 | 0 |
| ros | 0 | 0 | 17 | 2 | 31 | 17 / 15 | 4 |

Can affect play between our players and THUGPRO's (the user decides; none applied):
- **Rails THUGPRO added** (one side grinds, the other doesn't): Foun 8 (`_TRG_Wire_Rail0`-7 + their `foun_wire`
  geometry), Rio 23 (`QSilverBanner_01_Rail_*`, `rio_balcony*_rail*`, `rio_awning*_rail*`) + 8 untyped `TRG_Node_*`.
- **Rails THUGPRO switched off online**: Tok 17 (`TRG_SRail_01`-16, `TRG_GS_SRail_00`: AbsentInNetGames added);
  Foun 2 (`TRG_Ledge_Crane_Beam04` / `048`: CreatedAtStart and TrickObject removed).
- **Collision THUGPRO added** (walls / floors, mostly fixes): Foun 3 (`SouthDeck_QP01_coll`, `_QP04_coll`,
  `Trench_HP04_Back_coll`) + `Obj_Bucket` (134 faces); Rio `ES_Building_collision`; Sub 5 `*_collfix` (TRamp04/06,
  plywood_08, HHBYardWall 192 faces, PicnicCollide01) + 3 unnamed sectors; Ware 19 `SkWare_Dark_*` (the dark copies of
  the lit set we leave out on purpose - same surfaces).
- **Collision changed**: SI 8 objects with fewer faces (`SI_Grating*`, ScaffBarrier2, ShipCoping01, VertRamps) and 3
  only ours (`Ramp_ReFlection`, `si_VRDirt01/02`); Ros 4 (`SkRos_Rail_Col06` 24 -> 16, `_Col07`, `RailWall11`,
  `Radar02`); Tok 3, Shp 1, Sub 4 objects moved > 1 unit (road lines, a railing).
- **Geometry present on one side only (flags)**: Sub invisible blockers dropped (`InvisoBox01`,
  `SUB_InvisoPlantsBlocker01`, `Inviso_PicnicCollide01` (replaced by its `_collfix`), `DISHSTART_*`); Rio
  `ES_Sul_Fence_Broken01`-12 kept online (AbsentInNetGames removed); Shp `Box07` / `Box08` removed, `stt_EmptyPool`,
  `sft_RaiseFerryBox` AbsentInNetGames; SI `TRGP_FireCannons`, `SI_OuterDoor` AbsentInNetGames; Ros autopsy / garage
  doors CreatedAtStart; Ware secret-room glass loses NetEnabled.
- **Spawns**: unified (TP-12).
Visual or local only: rail terrain fixes (Rio 313, Tok 296, Shp 125, Ware 46, Sub 36, SI 18, Ros 11 - THPS3's
`TERRAIN_MEATALSMOOTH` typo, wires as electric wire: grind sound / sparks), peds and waypoints moved or reclassed
(Burn 9 peds, Foun 3, SI 1, Shp's landing path 300 units), particles (Foun 5, Sub 4 moved; THUGPRO's `_TP_` particle
and sound objects added), Tokyo's trains / planes / cars (flags), node class changes (lights `Omni*` / `Spot*`,
`Tape*`), and scripts (THUGPRO's own `TRG_Flag_*` / `TRG_CTF_*` / startup scripts added; THPS3 scripts changed in
translation - restart scripts Foun 16, Ware 14).

## Still different from THUGPRO
- Spawns: unified by TP-12 - places, types and order (above).
- Features THUGPRO has and we don't (all THUGPRO-authored unless noted): particles (Foun_TP_*, *_TP_Particle_Sprinkler,
  NextParticle*), Tokyo's bullet trains and planes, the restarts' own THPS3 trigger scripts (ours run the start
  scripts once from the setup script instead), bouncy objects as THUG2 Bouncy game objects (THPS3 data: BouncyObject
  params - AbsentInNetGames in THPS3 itself), Burnside's 44 spare cars (THPS3 data, made by its car scripts), THPS3's
  control zones / zone key (inert in THUG2). Ours has and THUGPRO hasn't: peds and crowds in Tokyo / Rio (THUGPRO
  dropped them), THPS3's rail / sector trigger scripts on LA / Ap / Can (vs THUG2's own remakes).
- THUGPRO's collision: faces its own BSP trees can't reach (col_check): Foun 517 (220 collidable), Rio 831 (173),
  Sub 430 (355), Tok 1079 (11; 1068 trigger faces), Shp 237 (112), Ware 33, Burn 61; SI and Ros 0.

## Not ported, THUGPRO has
THPS3 Oil Rig (Load_Oil is in THPS3 PC's levels.qb, no Data\Levels\Oil in the user's install) and Rooftops (Load_Roof,
no data) - nothing to convert from. THUGPRO's z_riod (labelled THPS3) - no THPS3 data either.

## Shared files touched
- tools/thaw_levels/thugpro_compare.ts: helpers exported (TOLERANCE, CTF_NAMES, nameOf, isName, isCrown, isMpRestart,
  Placed, sequence, maxDiff, sameSequence) - no behaviour change.
- tools/game/README.md, tools/game/boot_level.ts: example level name.
- TP-13 .. TP-19: tools/compat/thugpro/README.md (rows, "Changing approach", the awaiting list) and
  tools/compat/thugpro/thps3_levels.json (new sections, TP-10..12's unchanged) - only THPS3's parts.
- TPN-THPS3 / PC-THPS3 (below): tools/compat/compat.ts (`portChanges`, `PortChanges`, `PortChange` - additive),
  tools/level_port/nodes.ts (`netGameNodes(text, scripts, withRestarts = true)`), tools/level_port/ported_levels.ts
  (`installFiles` copies `<staging>\models` into the overlay's `models`), tools/menu_level/make_menu_level.ts (the
  overlay may hold a `models` folder), tools/compat/thugpro/README.md (the two new sections). Not touched:
  tools/level_port/script_port.ts.

## New in THUGPRO, now included (TPN-THPS3, the user's decision 2026-10-20) - **NOT run in the game yet**
Morten confirmed to the user that THUGPRO's own content may be in our ports. Everything new in THUGPRO (in no THPS3
file) is in one section of the register, `thugpro_new` (rows TPN-THPS3-1 ... 6 in tools/compat/thugpro/README.md),
apart from the TP-10 ... TP-19 hand edits. **By reference**: archive, file, name and a CRC-32 per item; the bytes are
read from THUGPRO's install at convert time (`--thugpro`, default %LOCALAPPDATA%\THUG Pro) - none of THUGPRO's bytes
are in the repository. Missing or different: warned and skipped.
- Code: tools/thps3_levels/thugpro_new.ts (reading, CRC checks, applying: nodes, sets, geometry with BSP trees rebuilt,
  scripts with `replace_calls`), thugpro_new_extract.ts (the rules; run by `thugpro_compare.ts --thug2 <folder>
  --extract-thugpro ...`), thps3_levels.ts (`--thugpro`, `--no-thugpro-new`, `--no-port-changes`), thugpro_inventory.ts
  `--applied` (checks each thugpro_new entry in our output), thugpro_new.test.ts (5 tests).
- Included per level: Foun `foun_wire` (the wires visible, their TrickObject / Cluster kept), 3 `*_coll` quads,
  `Obj_Bucket` collision + the bucket a Vehicle with THUGPRO's model; Rio `ES_Building_collision`, `TRG_Ped_2084` and
  its waypoint loop, 3 sound objects, 8 carplant collision vehicles (CreatedAtStart: THUGPRO creates them from its car
  scripts, which ours replaces with THUG2's cars) + models; Sub 5 `*_collfix` (the picnic collider back), BBQ noise,
  the ice-cream truck's carplant, the truck and plane as Vehicles with THUGPRO's models; SI the 2 ocean-wave emitters,
  their scripts (edmsg2 -> printf) and THUGPRO's ocean proximity scripts (the ocean sound back); Tok 23 vehicles
  (trains, planes with THUGPRO's models; cars with THUG2's), the 2 event restarts, train / plane scripts
  (THUGPRO_ForceCreate -> Create); Shp waterslide sound, window counter + 64 glass scripts; Ware `Ware_RoomFlicker`;
  every level THUGPRO's other runnable level scripts.
- Left out (the register's `_left_out`, with reasons): particles (THUGPRO_SpawnNodeParticles), scripts needing
  THUGPRO_SafeKill / TP_DoFlash / Editor_LoadCameraAnimParams / THUGPRO_Televator / Obj_PlayRandomStream /
  Obj_PlayRandomAnim / PlayRandomStream (SI's cannons proximity, Shp's setup, cutscenes, host level changes), the
  "small unnamed scene-only sectors" - **they are THUGPRO's sky sectors: THPS3's own skies under its toolchain's
  checksums** (vertex counts equal THPS3's: leftovers, ours converts THPS3's sky), its conversions of THPS3's `.dff`
  objects, Sub's 3 unnamed geometry nodes (no geometry anywhere: toolchain leftovers), THUGPRO's models of THPS3's
  Rio / Burnside cars (ours uses THUG2's), script names ours makes itself (load_level_anims, `<level>_setup`, CTF).
- `thugpro_inventory.ts --applied` (THUGPRO's new content in our output), before -> after: Foun sector 0 -> 1/1,
  collision 0 -> 4/4, nodes 0 -> 4/4, set 0 -> 1/1, model 0 -> 1/1, scripts 0 -> 7/7; Rio collision 1/1, nodes 21/21,
  models 2/2, scripts 14/14; Sub collision 5/5, nodes 7/7, set 2/2, models 3/3, scripts 18/18; SI nodes 2/2, scripts
  6/6, set scripts 2/2; Tok nodes 2/2, set 23/23, models 2/2, scripts 6/6, set scripts 3/5 (`JS_BulletTrainWest/
  EastControl`: ours doesn't bring those THPS3 scripts - reported, not added, so THUGPRO's train spawning doesn't run);
  Shp nodes 1/1, scripts 4/4, set scripts 64/64; Ware node 1/1, scripts 8/8; Burn 4/4, Ros 9/9 scripts (all
  0 before). The TP-13 ... TP-19 checks as before.
- Checks: `--no-thugpro-new` = the previous default output and `--compat none` = the previous `--compat none` (79
  files: .qb code and sorted names, the rest byte for byte); an empty `--thugpro` folder gives the `--no-thugpro-new`
  output with one warning per level; reconverted into THUG2-1: 0 unreachable collision faces in all 14, Links in range,
  rail links to rails, no duplicate names (thps3_la's 4 as before), ns.exe decompiles every level qb but
  thps3_skateshop's (as before), make_menu_level fine (the 7 models in the overlay's `models`), thps3 selfcheck 323
  files 0 failures, npm typecheck / lint / test.
- C++ THUG2 hasn't (THUGPRO DLL commands - proposals, not implemented; ours would be our own code with a credit
  comment): `THUGPRO_SpawnNodeParticles` (a particle system of a named type at the running node - a CFunction over
  THUG2's particle manager, the types as data), `THUGPRO_SafeKill` (Kill only when the object exists and has its
  geometry - a guard over Kill), `THUGPRO_ForceCreate` (Create ignoring AbsentInNetGames online - offline Create
  does it; online needs a flag on our Create), `TP_DoFlash` (a timed screen flash - a script over THUG2's DoFlash may
  do), `Editor_LoadCameraAnimParams` (its cutscene cameras), `THUGPRO_Televator` (teleporting the skater - a script
  over THUG2's skater move commands may do), `Obj_PlayRandomStream` / `Obj_PlayRandomAnim` / `PlayRandomStream`
  (random choice helpers - scripts with `random` would do).
- Not sure "THUG2 lacks it" (for the audit): the extraction counts a name as THUG2's when THUG2's CFunction table, the
  exe's member tables (`scriptPort.Thug2.members`, incomplete), THUG2's scripts, or a call in any THUG2 script
  (qb_scripts.prx and every `*_scripts.prx`) has it. `Output`, `edmsg2` were adapted to printf, not dropped; the
  `Obj_PlayRandom*` / `PlayRandomStream` names might be THUGPRO global scripts rather than DLL commands (then
  they'd be portable as scripts). Vehicles: our translated Tok / Sub / Foun object scripts now run on a Vehicle (they
  ran on a LevelObject) - untested.

## Our own changes: port_changes (PC-THPS3-1 ... 14)
The user's decision 2026-10-20: the converter's own deviations from THPS3's data are data too, in the register's
`port_changes` section, read through compat.ts `portChanges` whatever the profile (`--compat none` keeps them);
`--no-port-changes` leaves out all but the `required` ones. The list (op): PC-THPS3-1 remove Warehouse's dark set (its
prefixes now data, was LEFT_OUT_PREFIXES); 2 set start scripts once from `<level>_setup` (required); 3 set kill zones
as SK3_KillSkater (required); 4 add net restart types on single-player levels' Player1 restarts; 5 set
AbsentInNetGames on path followers / peds / cars (required); 6 set TrickObject / Cluster off sector-less geometry
(required); 7 set restarts over nothing / kill zones not spawns; 8 add CTF from THPS3's bases (kept while TP-10 ...
TP-12 data is present); 9 set Player1 restart first; 10 set traffic as THUG2's cars (required); 11 set peds / animals
as THUG2's (required); 12 remove goal pickups and other unconvertible objects (required); 13 remove statements THUG2
can't run (required); 14 remove non-rail Links (required). Checked: default output unchanged; `--compat none
--no-port-changes` differs in 13 levels' node arrays and Warehouse's scene / collision (the dark set back).
