# thps3-thugpro - THPS3 level ports: THUGPRO compatibility (tools/thps3_levels)

Agent "thps3-thugpro", 2026-10-20. The same treatment the THAW ports got (docs/pending/thaw.md "THUGPRO"), for the
THPS3 ports. Everything is generated from THPS3's own files (C:\Games\THPS3, read only) and THUG2's own data; THUGPRO's
installed levels (%LOCALAPPDATA%\THUG Pro\data\pre) were only read to compare with (`thugpro_compare.ts`) - nothing of
theirs is copied. No register (TP-n) entry was needed. **NOT run in the game yet.**

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
  ctf_1 = blue). Team restarts: THUGPRO's mostly on the flag itself; ours at THPS3's team restart (local only, no effect online).
- Checks: thps3 selfcheck 323 files 0 failures; every level's qb decompiles through ns.exe except thps3_skateshop's
  (as before: a 0x37 token ns.exe doesn't read); make_menu_level into THUG2-1: all 14 THPS3 levels in the level select
  (signs sign_foun..., AP / LA texture signs as before); npm typecheck / lint / test pass (3 new tests).

## Still different from THUGPRO
- CTF flag places in Rio, Sub, SI, Tok, Shp, Ware (THUGPRO's own choice; ours THPS3's). Online CTF between the two:
  the server decides pick-ups by its own flags, the other side draws them elsewhere. Not matched: positions are not
  protocol identifiers - a TP entry would copy THUGPRO's positions (see the report's proposal).
- Green / Yellow teams, lobby team flags (TRG_Flag_*), TRG_Team_Restart: THUGPRO hand-placed them; THPS3 has none.
  A 3-4 team CTF in our THPS3 levels has no flags / restarts for teams 3-4 (THUG2 checks NodeExists - no crash
  expected; the restart lookup falls back - untested).
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
