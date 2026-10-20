# THPS4 levels: THUGPRO compatibility (agent thps4-thugpro, 2026-10-20) - NOT run in the game yet

Goal: a THUGPRO player and one of ours in the same THPS4 level online - the treatment the THAW ports got
(docs/pending/thaw.md "THUGPRO"). Everything we generate comes from THPS4's own files (C:\Games\THPS4, read only) and
THUG2's; THUGPRO's installed levels (%LOCALAPPDATA%\THUG Pro\data\pre) were only read to compare
(`tools/thps4_levels/thugpro_compare.ts`). The one THUGPRO-only choice taken (the skate shop's load script name) is
in the register (tools/compat/thugpro, TP-20) and awaits the user's approval.

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
  MP restarts unchanged (the same); skate shop crowns 0 / 1 and MP restarts 1 / 9 (below).
- Offline checks: every collision face reachable in all 21 (levels and skies); links in range, rails link to rails;
  every .qb decompiles through ns.exe; thps4 selfcheck 139 files 0 failures; THPS3 323 / THUG1 333 / THAW 735: 0.
- THUGPRO-only trigger scripts on shared nodes (goal letters, combo, pro lines, cash icons, goal peds - goals aren't
  ported - and the rest below): Alc 187 -> 157, Cnv 54 -> 38, Hof 61 -> 45, Jnk 150 -> 109, Kon 96 -> 81, Lon 132 ->
  104, SF2 63 -> 41, Sch 228 -> 214, Zoo 105 -> 90.

## Still different (and what it means online)

- Skate shop: THPS4 has 1 Player1 restart (TRG_SS_Skater01, its menu spot), 1 generic one (TRG_SS_SkaterDropIn, its
  practice start) and no crown; THUGPRO added 8 drop-in restarts and a crown at its own places and made TRG_SS_Skater01
  a ghost. Online: KOTH there picks crown 0 - none on our side (no crown shown); spawn points differ (positions sync
  after). Not invented: proposal - make TRG_SS_SkaterDropIn a Multiplayer restart too (THPS4's own practice start; 2
  spawns), and leave KOTH off there (THUGPRO's level structure already disables CTF there).
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

## Untested / concerns

- Nothing run in the game. College's node array grew by 12 (flags): THUG2's script pools were tight for College
  before (docs/pending/thps4-scripts.md) - load College once. Hof / Lon / SF2 got pigeons (+ their waypoints).
- The drop-in at the start of free skate (Player1 restarts) and at every restart: THPS4's behaviour, THUG2's DropIn.
- CTF / team flags: the flag models, their trigger scripts and THUG2's net scripts in a LAN / online CTF game.
- Shared edits: tools/level_port/ported_levels.ts (`load`, `loadOf`), tools/menu_level/level_select.ts (uses
  `loadOf`), tools/thaw_levels/thugpro_compare.ts (helpers exported), docs/level-port-compromises.md (THPS4 animals),
  src/game/front/start_level.h (a comment's example name).
