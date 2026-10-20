# THPS4 levels: THUGPRO compatibility (agent thps4-thugpro, 2026-10-20) - NOT run in the game yet

Goal: a THUGPRO player and one of ours in the same THPS4 level online - the treatment the THAW ports got
(docs/pending/thaw.md "THUGPRO"). Everything we generate comes from THPS4's own files (C:\Games\THPS4, read only) and
THUG2's; THUGPRO's installed levels (%LOCALAPPDATA%\THUG Pro\data\pre) were only read to compare
(`tools/thps4_levels/thugpro_compare.ts`). The THUGPRO-only choices taken are in the register (tools/compat/thugpro):
TP-20 (the skate shop's load script name) and TP-21 (the skate shop's crown and spawns) - both approved by the user
2026-10-20.

## Before (verified the lead's comparison)

`node tools/thps4_levels/thugpro_compare.ts --thug2 C:\Games\THUG2-2 --legacy-names` on a fresh convert:
- Positions: every shared node at THUGPRO's place (offset 0) in all 10 levels THUGPRO has, except THUGPRO's hand
  moves: Carnival TRG_G_HS_RestartNode (38 units, pitch straightened), College TRG_53 / TRG_813 (rail nodes, 131 /
  59 units) and LO_Elevator_Door_Floor1_01/02 (48: THUGPRO's own elevator teleport, sch_thugpro.qb).
- KOTH crowns: same count, order, places in all 9 THPS4 levels (THPS4's TRG_KOTH*); skate shop 0 vs THUGPRO's 1.
- Multiplayer restarts: the same in the 9; skate shop 1 vs 9.
- Names: ours Load_thps4_<lvl>, THUGPRO's Load_<lvl> (and Load_practice for the skate shop) - no level loaded across.
- CTF / team flags: ours 0, THUGPRO 12 per level.

## Changes (tools/thps4_levels)

- **Names**: THPS4's own - its Levels.qb loads `Load_Alc`, `Load_Cnv`, `Load_Hof`, `Load_Jnk`, `Load_Kon`,
  `Load_Lon`, `Load_Sch`, `Load_SF2`, `Load_Zoo`, `Load_Motox` (checksums ignore case): each level is `alc`... -
  folder, files, Load_ / Level_, `<lvl>_NodeArray`, sky `<lvl>_sky` (`thps4_levels.levelName` / `skyName`). No THUG2
  level, THPS3 / THUG1 port or THUG2 level select sign has those names. Not a register entry: THPS4's data.
- **Skate shop**: THPS4's `Load_SkateShop` can't be used (THUG2's CheckForInvalidLevels turns load_skateshop into the
  main menu; `levels\skateshop` is THUG2's shop, make_menu_level's SKATE SHOP). Its files stay `thps4_skateshop` (the
  front end's backdrop loads `thps4_skateshop` / `thps4_skateshop_sky` by name: src/game/front/menu_backdrop.h - no
  C++ change); its load script / structure take THUGPRO's name `Load_practice` / `Level_practice` (TP-20,
  `tools/compat/thugpro/thps4_levels.json` `load_names`, read through `compat.ts`; `--compat none` gives
  Load_thps4_skateshop). levels.json gets an optional `load` (ported_levels: Load_ / Level_ / level select signs).
- **The level's setup script** is `<lvl>_ported_setup` (was `<lvl>_setup`: with THPS4's names that is THPS4's own
  `alc_setup`, which ours spawns - it would have spawned itself).
- **CTF and team flags** from THPS4's own node arrays: THPS4 had both game modes - every level's TRG_CTF_<colour>,
  TRG_CTF_<colour>_Base and TRG_Flag_<colour> GameObjects (THUG2's gameobjects\flags\ models, trigger scripts
  `Team_Flag <colour>` / `Team_Flag_Base <colour>`, which THUG2's SK4_Scripts has) - kept (12 per level), not created
  at the start (THUG2's net\ctf.qb / Create_Team_Flags create them by name), NeverSuspend as THUG2's own levels'.
  Their restarts (TRG_CTF_Restart_<colour> type CTF, TRG_Flag_Restart type Team) were kept already.
- **Restart trigger scripts**: THPS4's restarts run `MakeSkaterGoto DropIn` (the skater drops in from the lip of the
  ramp the restart stands on) or `StartSkating1`; both are THUG2 skater scripts. Kept (script_port's LEFT_OUT
  MakeSkaterGoto allowed - `keptIn` - with a rewrite that keeps only those two targets); a restart script with
  nothing left (the skate shop's menu restart: FreezeSkater, camera) is dropped, script and all (an empty script also
  broke make_menu_level's script parsing). Restarts with a trigger script now: Shipyard 29, Alcatraz 18, London 7,
  Carnival 5, Kona 4, Chicago 3, Zoo 3, College 2, Motocross 1, San Francisco 0 (Player1's included).
- **Pigeons** (Chicago 5, London 9, San Francisco 10): THPS4's Anl_Pigeon peds use THUG2's own pigeon model and
  their trigger script is Pigeon_Generic, which THUG2 has: made as THUG2's own levels make pigeons (Boston:
  NoPedLogic, dont_target, SkeletonName=Anl_Pigeon, animload_anl_pigeon - THUG2's data). Kona's 7 hawks (Anl_Pigeon
  model, Kon_Hawk, not created at the start) are left out by the existing never-created pass. Other animals still out.
- `thugpro_compare.ts` (THPS4's, imports the THAW one's helpers - those got `export`), `migrate_layouts.ts` (renames
  `<crc(thps4_lvl)>.txt` to `<crc(lvl)>.txt`, old kept as `.before-thps4-names`; no game folder has THPS4 layouts),
  `thps4_levels.test.ts`. `--legacy-names` for the old names.

## After

All 11 reconverted into C:\Games\THUG2-2 and installed with make_menu_level (level select: Load_alc ... Load_practice;
signs sign_alc...; the backdrop found: 28 rails, 138 peds / cars, 3 trigger nodes + 6 scripts from thps4_skateshop).
- Comparison: names the same in all 10 (Load_alc ... Load_practice); CTF nodes 12/12 in the 9; positions, crowns,
  MP restarts unchanged (the same); skate shop crowns 0 / 1 and MP restarts 1 / 9 - now the same with TP-21 (below).
- Offline checks: every collision face reachable in all 21 (levels and skies); links in range, rails link to rails;
  every .qb decompiles through ns.exe; thps4 selfcheck 139 files 0 failures; THPS3 323 / THUG1 333 / THAW 735: 0.
- THUGPRO-only trigger scripts on shared nodes (goal letters, combo, pro lines, cash icons, goal peds - goals aren't
  ported - and the rest below): Alc 187 -> 157, Cnv 54 -> 38, Hof 61 -> 45, Jnk 150 -> 109, Kon 96 -> 81, Lon 132 ->
  104, SF2 63 -> 41, Sch 228 -> 214, Zoo 105 -> 90.

## TP-21: the skate shop's crown and spawns (approved by the user 2026-10-20, implemented)

An approved exception to the data rule: THPS4's shop (single player in THPS4) gets THUGPRO's King of the Hill crown
and its 8 added multiplayer spawns, so KOTH and the spawn set agree with a THUGPRO player there.
- Data: `tools/compat/thugpro/thps4_levels.json` `net_nodes` - places only (pos in THUG2's axes, angles), extracted
  from THUGPRO's install by `node tools/thps4_levels/thugpro_compare.ts --extract-thugpro` (its node array's crowns and
  multiplayer restarts that aren't THPS4's nodes, told apart by THPS4's own SkateShop.qb; THPS4's restarts THUGPRO made
  multiplayer by name: TRG_SS_SkaterDropIn). Never typed in.
- Converter: `thps4_levels.addNetNodes` makes the nodes in our form - TRG_Net_Restart01-08 (Class=Restart
  Type=Multiplayer, restart_types [Multiplayer, Horse]), TRG_KOTH01 (GenericNode Crown) - before the peds and cars
  (make_menu_level needs those last); TRG_SS_SkaterDropIn gains Multiplayer / Horse. Every spawn runs
  `thps4_skateshop_net_dropin` (MakeSkaterGoto DropIn: THPS4's practice start dropped in at TRG_SS_SkaterDropIn).
- THPS4's menu restart TRG_SS_Skater01 is **not** made a ghost: with multiplayer restarts present nodes.netRestarts no
  longer makes it one, so it stays a Player1 restart and the multiplayer set already matches THUGPRO's (9, same order
  and places).
- `--compat none`: the output compared with today's before TP-21 - identical (75 files; the .qb files by code and their
  sorted checksum names, which ns.exe writes in Go map order, different every run). With the profile, only
  thps4_skateshop.qb differs.
- After: thugpro_compare SkateShop: crowns same, mp restarts same (445 / 691 nodes, 436 shared in place); col_check,
  links, ns.exe all fine; make_menu_level into THUG2-2 - the backdrop found (28 rails, 138 peds / cars, 3 trigger
  nodes + 6 scripts).

## Still different (and what it means online)

- (Resolved by TP-21, below.) Skate shop: THPS4 has 1 Player1 restart (TRG_SS_Skater01, its menu spot), 1 generic
  one (TRG_SS_SkaterDropIn, its practice start) and no crown; THUGPRO added 8 drop-in restarts and a crown at its own
  places and made TRG_SS_Skater01 a ghost.
- THUGPRO's hand moves (above): one Carnival goal restart, two College rail nodes, College's elevator doors. Rails and
  restarts don't travel; ours are THPS4's.
- Motocross: THUGPRO has none (ours only).
- Node arrays: THUGPRO keeps 109-616 more nodes per level (goal objects, goal peds, cash icons, ghosts) - node order
  doesn't travel.
- THUGPRO's own additions (not THPS4's, not brought): sch_thugpro.qb's banners and elevator teleport, lon_thugpro /
  hof / sf2 / zoo `_thugpro.qb` (props, banners, fixes), the skate shop's extra restarts / crown.

## THUGPRO has, we still don't (estimates)

- Level sounds: THPS4's wavs (Game\data\sounds\Wav\<lvl>) as THUG2 .snd (THUG2's own format: RIFF, fmt says PCM,
  data 4:1 compressed, the byte-rate field holding the uncompressed size - THUG2's own BO sounds and THUGPRO's are
  alike), LoadSound kept, Obj_PlaySound / SpawnSound / proximity sound areas. 1-2 days (the encoder from the decoded
  loader, the overlay's sounds folder, a game check).
- Proximity nodes (Jnk 12 SFX areas, Lon garage / fountain areas, Zoo's aquarium / aviary / monkey): THUG2's
  ProximNode is a box of the scene; THPS4's a radius. ~0.5 day (THPS3's ghost-object watcher approach).
- Particles (Chicago's 5 TRG_Spew, London's fountains, College's sprinkler): THPS4's CreateParticleSystem calls; ~1
  day.
- Other animals (Zoo 97, Carnival 19 - gator, Kona hawks, seals, sharks): THPS4's models / skeletons; 1-2 days with
  the animation risk (docs/level-port-compromises.md section 3).
- Streams / music (Playstream, StopStream: Carnival's haunted house...): ~0.5 day.
- Goals (letters, combo, pro lines, cash icons, goal peds): not ported by design.

## Spawns: nothing left to unify (no TP-22)

Every restart a network game uses - Multiplayer, Horse, CTF, Team - compared ours (as converted, TP-21 in) with
THUGPRO's in all 10 shared levels: the same count, order and places for every type in every level (Alc 17 / 12 / 4 / 1,
Cnv 8 / 8 / 4 / 1, Hof 8 / 12 / 4 / 1, Jnk 12 / 12 / 4 / 1, Kon 8 / 9 / 4 / 1, Lon 21 / 14 / 4 / 1, SF2 12 / 7 / 0 / 1,
Sch 8 / 11 / 4 / 1, Zoo 8 / 8 / 4 / 1, skate shop 9 / 9 / 0 / 0). THUGPRO moved, renamed or retyped none of its THPS4
levels' network spawns beyond the skate shop's, which TP-21 covers. So no TP-22 and no converter change; the
reconvert into THUG2-2 is unchanged. Listed only (not network spawns):
- Carnival TRG_G_HS_RestartNode moved 38.5 and straightened, TRG_G_PS_RestartNode rotated; Shipyard
  TRG_Re_Wtr_KillPlne_03 rotated (a kill plane's put-back point); Chicago TRG_G_HighCombo_RestartNode renamed
  TRG_G_PS_RestartNode; goal restarts' trigger scripts dropped (Cnv 2, Kon 1).
- Added generic restarts: San Francisco TRG_Elev_Warp2 (its elevator teleport's target), Zoo TRG_ParkingLotRestart01-03.
- Skate shop Player1 restarts: THUGPRO's 9 drop-ins are Player1 too and TRG_SS_Skater01 / _SkaterChoosing /
  _Appearance became ghosts; ours keep THPS4's TRG_SS_Skater01 as the Player1 start (free skate only, not online).

## THUGPRO hand edits: inventory

`node tools/thps4_levels/thugpro_inventory.ts --thug2 C:\Games\THUG2-2 [--json out]` (read only on both installs):
THUGPRO's node arrays, level scripts, `<level>_thugpro.qb` files and collision against THPS4's own files (collision
against our converted collision, which is THPS4's). Nothing changed in our output; the user decides. "Online" says
whether it can make the two sides differ for players sharing the level.

Moved / renamed / reused nodes
- College: rail nodes TRG_53 (131 units) and TRG_813 (59) moved - **online: a rail where one side grinds and the other
  doesn't** (each side grinds its own rail; positions sync, so a THUGPRO player seen grinding beside ours' rail).
  Elevator doors LO_Elevator_Door_Floor1_01 / 02 moved 48 (THUGPRO's elevator teleport; doors are level objects with
  collision - a door open on one side).
- Carnival 2 goats moved (4.5 / 6.0), goal restarts above; Chicago 1 goal ped + 1 goal restart renamed. Online: no.

Added nodes
- College 6 rails TRG_TP_RailRoofLedge_01-06; skate shop 16 rails roof_ledge_*; San Francisco 12 rails ed_rail_*
  (level-editor rails) - **online: rails only THUGPRO's players can grind**.
- Zoo 5 rails TRG_NatasNode_01-05 (a Natas-spin spot) - online: as above.
- Kona 19 level lights (THUGPRO's night lighting); London 1 waypoint (its teleport); skate shop TRG_TP_SkaterKiller
  (a kill trigger) - online: no (lights), the kill only on their side.
- `_thugpro.qb` run-time additions: banners / posters (Chicago 5, College 13, London 1 + 1 billboard + 5 other),
  proximity-triggered teleports ("televators": College 1 + elevator doors, San Francisco 2, London 2 elevator door
  pairs) - online: teleports and moving doors only on their side.
- Host level options (THUGPRO's own protocol flags SERVER_LEVEL_CHANGE_*): Chicago bridge state (5 states: creates /
  kills bridge geometry and its rails), Shipyard barge, London race track / rooftop roll-in, San Francisco rooftop
  objects, Zoo hippo bridge - **online: geometry and rails differ whenever the THUGPRO host picks a non-default state**
  (we have no such options).

Removed nodes, changed flags
- Removed: Zoo 1 meerkat (an animal). Nothing else removed.
- **Shipyard: 68 rails lose AbsentInNetGames** (the crane tracks TRG_Rl_Crne_B_Trk_*, Bucky_Rl_*): THUGPRO has them in
  network games, THPS4 (and we) don't - online: rails one side grinds and the other can't.
- College: 6 geometry pieces + 6 stair rails made TrickObject (graffiti / trick attack scoring); 14 rails lose
  CreatedAtStart (TRG_820-828...: not there at the start) - **online: rails ours have and theirs don't**; DT_tennisBall
  loses AbsentInNetGames, dt_blimp gains it.
- Created at the start changed on geometry: Zoo 11 (hippo collision, lion gate, rhino collisions, hippo bridge down -
  and bridge up gained it: **the hippo bridge in the other position**), Carnival 10 + 7 objects (goal triggers, shooting
  gallery), London elevator / 4 garage doors, San Francisco 7 goal gap triggers (+), Chicago 5 hydrant rails and 2 trash
  cans, Alcatraz 2 froth, skate shop fences (SSO_Fence02 off, SSO_Fence05 on - ours: SSO_Fence05 on as well) and signs.
  Online: geometry / rails present on one side only where THPS4's scripts don't create them on ours.
- Rail sounds: TerrainType / Type on 37-164 rails per level (metal -> grind wire, concrete -> metal...) - online: no
  (sounds, sparks only).
- Toolchain-wide (not hand edits): Pos written for Position, bouncy objects' physics keys, peds' skeletons / anims,
  hundreds of goal / ped / object trigger scripts dropped, goal peds made ghosts, lod distances.

Changed scripts
- Level scripts THPS4 has that THUGPRO's files don't: Alc 217, Cnv 9, Hof 94, Jnk 145, Kon 138, Lon 238, SF2 264, Sch
  222, Zoo 228, shop 15 (goals, minigames, peds); added: 0-8 per level (Lon_Killskater, TP_SS_KillSkater, teleports,
  the option scripts). Shared scripts whose command sequence differs: Alc 420, Cnv 221, Hof 70, Jnk 203, Kon 25, Lon 74,
  SF2 171, Sch 183, Zoo 85, shop 28 - mostly the toolchain's translation (THPS4 -> THUG2 forms), not hand edits; not
  told apart further.

Geometry / collision (THUGPRO's col against ours, by object, vertices to 0.1)
- No object's vertices changed in any level compared (Alcatraz not compared: our reader can't read THUGPRO's file).
- Objects only in ours: the vehicles' collision (THUGPRO's vehicles are separate models) - Cnv 29, Hof 10, Jnk 38,
  Lon 40, SF2 21, Sch 20, Zoo 5. Not edits.
- College: 68 objects with fewer faces (duplicate faces removed, e.g. Box331 24 -> 12) - same surfaces, online: no.
- Skate shop: THUGPRO keeps THPS4's invisible door walls (InvisColl01 10 faces, Object10's 2) - **online: the shop door
  is open for ours (the user's request), closed for theirs**.
- Face flags are written differently by the two toolchains everywhere (0x8000 / 0x1 bits in THUGPRO's) - not compared.

## Untested / concerns

- Nothing run in the game. College's node array grew by 12 (flags): THUG2's script pools were tight for College
  before (docs/pending/thps4-scripts.md) - load College once. Hof / Lon / SF2 got pigeons (+ their waypoints).
- The drop-in at the start of free skate (Player1 restarts) and at every restart: THPS4's behaviour, THUG2's DropIn.
- CTF / team flags: the flag models, their trigger scripts and THUG2's net scripts in a LAN / online CTF game.
- Shared edits: tools/level_port/ported_levels.ts (`load`, `loadOf`), tools/menu_level/level_select.ts (uses
  `loadOf`), tools/thaw_levels/thugpro_compare.ts (helpers exported), docs/level-port-compromises.md (THPS4 animals),
  src/game/front/start_level.h (a comment's example name).
