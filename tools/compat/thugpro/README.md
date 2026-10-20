# THUGPRO compatibility register

Everything this project does **only so that it agrees with THUGPRO** (Morten1337's THUG2 mod) for online play: choices
that are THUGPRO's, not the original games' data. Each entry has an id (TP-n), what it is, why, where it's applied and
how to take it out. Keep this list complete: a new THUGPRO-only choice gets an entry here, its data in this folder,
and a `TP-n` in the code comment where it's used.

Why separately: these are arbitrary agreements (names, offsets, identities), not improvements. Kept apart they can be
switched off as a whole (`--compat none` for the converters) or later replaced by a run-time translation of the
network messages, e.g. to play the same maps with players of another game or mod (THAW players: THAW's own names and
world frame) - see "Changing approach" below.

Data: `thaw_levels.json`, `thps3_levels.json`, `thps4_levels.json`, `thug1_levels.json` (read by `tools/compat/compat.ts`). Comparison
against THUGPRO's install: `tools/thaw_levels/thugpro_compare.ts`, `tools/thps3_levels/thugpro_compare.ts`, `tools/thps4_levels/thugpro_compare.ts`,
`tools/thug1_levels/thugpro_compare.ts`. Background: `docs/pending/thaw.md`, `docs/pending/thps3-thugpro.md`, `docs/pending/thps4-thugpro.md`,
`docs/pending/thug1.md` ("THUGPRO"); the comparison reports (THAW / THPS3 / THPS4 / THUG1 ports vs THUGPRO). Rules: insights
from Morten1337's repositories are allowed (his permission, 2026-10-20), never copies of his
code or level data; the values here are identifiers and offsets, measured or read, with their source given.

## Build time (persisted in the files we generate)

| Id | What | Why (what travels online) | Applied in | Source |
|---|---|---|---|---|
| TP-1 | THAW level names: Chicago `skatepark`, Kyoto `kyoto`, Santa Cruz `sz`, Vans Park `vans` (the other 11 keep THAW's zone name, `z_ma`..., as THUGPRO's do) | A level is known online only by its load script's checksum (change level 0x46, start info 0x33): both sides must name it alike | `thaw_levels.json` `level_names` -> `thaw_levels.levelName` (folder, files, `Load_` / `Level_` scripts, node array name) | THUGPRO's installed `data\pre` file names |
| TP-2 | Vans Park left in THAW's world frame although it has a `Z_SV_Zone_Origin` | Positions travel as world coordinates (skater updates 0x23, int16): both sides must place the area alike. THUGPRO's toolchain never moved it (it looked the origin up under the new name `vans`) | `thaw_levels.json` `frame_kept` -> `thaw_levels.frameShift` | thugpro_toolchain `find_zone_origin.py`; confirmed by `thugpro_compare.ts` (2192/2192 nodes in place) |
| TP-3 | East LA and Santa Monica moved by THUGPRO's own offsets on top of the Zone_Origin frame: EL (-1319.656, -111.047, 269.924), SM (-5060.44, -580.625, -3203.87) | As TP-2. THUGPRO's "LA crash fix" (commit 361b2c1) moved them by hand-picked offsets that aren't in THAW's data | `thaw_levels.json` `frame_adjust` -> `thaw_levels.frameShift` | thugpro `source/levels/Z_EL/world_offset.txt`, `source/levels/Z_SM/node_position_patcher.py`; the values measured with `thugpro_compare.ts` (EL 3404/3404, SM 3644/3661 nodes in place - the rest moved by hand in THUGPRO) |
| TP-10 | THPS3 capture the flag: the Red and Blue flags and their bases where THUGPRO has them (place, angles and colour) in Rio, Suburbia, Skater Island, Tokyo, Cruise Ship and Warehouse - THUGPRO moved them from THPS3's own bases there (SI and Ware with the colours swapped against THPS3's ctf_1 = blue). Approved by the user 2026-10-20 | CTF pick-ups and captures are decided on the server against its own flags (SK4_Scripts Team_Flag -> CTF_Team_Flag_Trigger `if OnServer`); each side draws the flags from its own node array, so both must place them alike. The nodes' form stays ours (THUG2's Airport form, THUG2's flag models, Team_Flag); the team restarts stay THPS3's (the THPS3 CTF restart nearest to the flag) | `thps3_levels.json` `ctf_flags` -> `thps3_levels.levelCompat` / `ctfNodes` | THUGPRO's installed `datapre<level>_scripts.prx`, `levels<level><level>.qb`: TRG_CTF_Red / TRG_CTF_Red_Base / TRG_CTF_Blue / TRG_CTF_Blue_Base (Pos, or Position with z negated; Angles), read by `tools/thps3_levels/thugpro_compare.ts --extract-thugpro` (a level is listed when THUGPRO's flag isn't at THPS3's base of that colour) |
| TP-11 | THPS3 team nodes THPS3 has no counterpart for, in all 9 levels THUGPRO shares (foun, rio, sub, si, tok, shp, ware, burn, ros): TRG_CTF_Green / _Yellow (+ _Base), the lobby's team flags TRG_Flag_Red / Blue / Green / Yellow and TRG_Team_Restart. Approved by the user 2026-10-20 | THPS3 is a two-team game: without them a 3-4 team CTF has no flags for teams 3-4, the team lobby has no team-select flags and no Team restart. Places as THUGPRO's so that both sides agree; form ours (Airport form, Team_Flag scripts); the Green / Yellow team restarts are THPS3's multiplayer restarts nearest to their flags (no THUGPRO restart positions copied) | `thps3_levels.json` `team_nodes` -> `thps3_levels.levelCompat` / `ctfNodes` | As TP-10: the same files, nodes TRG_CTF_Green(_Base), TRG_CTF_Yellow(_Base), TRG_Flag_<colour>, TRG_Team_Restart |
| TP-20 | THPS4's skate shop's load script `Load_practice` / `Level_practice` (its files stay `thps4_skateshop`) - approved by the user 2026-10-20 | As TP-1. THPS4's own name, `Load_SkateShop`, can't be used in THUG2 (THUG2's CheckForInvalidLevels turns it into the main menu; `levels\skateshop` is THUG2's shop); THUGPRO renamed it `practice` | `thps4_levels.json` `load_names` -> `thps4_levels.loadName` -> levels.json `load` -> `ported_levels` (Load_ / Level_ only) | THUGPRO's installed `data\pre\Practice_scripts.prx` and its Levels.qb `level_practice`; `tools/thps4_levels/thugpro_compare.ts` (436/436 nodes in place) |
| TP-21 | THPS4 skate shop's network-game nodes, THUGPRO's places: its King of the Hill crown (1) and its 8 added multiplayer spawns, plus THPS4's own TRG_SS_SkaterDropIn made a multiplayer spawn - approved by the user 2026-10-20 (an exception to the data rule: THPS4's shop, a single-player level, has none) | KOTH picks a crown by its index among the Crown nodes (CFunc_SpawnCrown 0x00538170): without one, a THUGPRO host's KOTH in the shop shows no crown here; the spawns: the same multiplayer start set (order and places) as THUGPRO's | `thps4_levels.json` `net_nodes` (places only: pos in THUG2's axes, angles) -> `thps4_levels.addNetNodes` (nodes in our form: Restart Type=Multiplayer TRG_Net_Restart01-08, GenericNode Crown TRG_KOTH01, all running the drop-in THPS4's practice start ran: `<level>_net_dropin`, MakeSkaterGoto DropIn). THPS4's menu restart TRG_SS_Skater01 stays a Player1 restart (not a ghost as THUGPRO's): with multiplayer restarts present it isn't made one, so the sets already match. Off: `--compat none` (the output then the same as before TP-21) | THUGPRO's installed `data\pre\Practice_scripts.prx` (`levels\practice\practice.qb`), extracted by `node tools/thps4_levels/thugpro_compare.ts --extract-thugpro` (never typed in); checked by `thugpro_compare.ts` (crowns same, mp restarts same) |
| TP-30 | THUG1's Hotter Than Hell named `hh` (Load_hh, levels\hh, hh_sky) - approved by the user 2026-10-20 | As TP-1: THUG1 names it SE (Load_SE), but THUG2 has a level SE of its own (Pro Skater), so the THUG1 port can't take THUG1's name; THUGPRO chose `hh` (its own identifier, in no THUG1 / THUG2 file). Without the entry it's `thug1_se` and doesn't load across | `thug1_levels.json` `level_names` -> `thug1_levels.levelName` | THUGPRO's installed `data\pre\HH_scripts.prx` and its Levels.qb (`Level_HH`, `load_script=Load_HH`, `name="Hotter Than Hell"`); checked by `tools/thug1_levels/thugpro_compare.ts` |
| TP-31 | THUG1 levels' network spawns as THUGPRO's: every restart with a Multiplayer / Horse / CTF / Team type in THUGPRO's places, types, names and order - VC TRG_restart_SCJ (moved 326 onto TRG_Restart_SJ's spot and made the first spawn), HN TRG_Restart01 (43), SC2 TRG_CTF_Restart_Yellow (165), SJ TRG_G_Collect2_RestartNode (Multiplayer / Horse taken off: 10 -> 9), HI TRG_Proset1_restartnode renamed `_trg_Proset1_restartnode`; all 12 shared levels in THUG1's own order (NJ: no Player1-first swap) - approved by the user 2026-10-20 | Competitive fairness: THUG2 starts player n at the n-th Multiplayer restart of the node array and respawns at one of the first 8 (Skate 0x00566100 -> GetRestartNode 0x0046B790; CSkater::MoveToRandomRestart 0x00580830, Rnd(8)) - a player shouldn't start where the opponents never do | `thug1_levels.json` `spawns` -> `thug1_levels.applySpawns` (in our node form: Pos / Angles / restart_types / Name set, the spawns' array slots refilled in `order`, Links following; the level's array keeps THUG1's order - `nodes.convertNodes` player1First false). Off: `--compat none` (the output then the same as before TP-31: checked, .qb code + sorted names) | THUGPRO's installed `data\pre\<level>_scripts.prx` node arrays against THUG1's own, extracted by `node tools/thug1_levels/thugpro_compare.ts --extract-thugpro` (`thugpro_edits.spawnEdits`; never typed in); checked by `thugpro_compare.ts` ("spawns same" in all 12) |

Not entries here, although THUGPRO does the same (they're right for THUG2 on their own):
- **THUG1's own level names** for the other THUG1 ports (nj, ny, fl, sd, hi, vc, sj, ru, vn, hn, sc2: Load_NJ ... Load_hn,
  Load_sc2): THUG1's Levels.qb names them so (`thug1_levels.ownName`); a THUG1 name THUG2 already uses keeps the `thug1_`
  prefix (SC, DJ, PH, TestLevel - THUGPRO plays THUG2's own SC / PH / DJ there), as does a level THUG1 doesn't load by
  its own name (NJ_Skateshop, the front end's skateshop - THUGPRO's Load_Mainmenu, which is THUG2's front end here).
- **The Zone_Origin frame** (each THAW area moved by minus its `<zone>_Zone_Origin` node): THAW's own data, keeps the
  areas near the origin (precision, the network's +-16,384 range). Our base rule; the idea came from THUGPRO's
  toolchain (credited in `thaw_levels.ts`).
- **Game-mode nodes without the zone prefix** (`TRG_CTF_*`, `TRG_Flag_*`, `TRG_KOTH*`, `TRG_Team_Restart`): THUG2's
  own scripts look them up by those names (`thaw_levels.multiplayerNames`).
- **THPS4's level names** (`alc`, `cnv`, `hof`, `jnk`, `kon`, `lon`, `sf2`, `sch`, `zoo`, `motox`): THPS4's own
  (its Levels.qb's `Load_Alc`..., checksums ignore case), `thps4_levels.levelName`; THUGPRO uses the same. THPS4's
  frame and its game-mode nodes (`TRG_CTF_*`, `TRG_Flag_*`, `TRG_KOTH*`) need nothing: THPS4 has them under THUG2's
  names already (`tools/thps4_levels/thugpro_compare.ts`: every shared node in place but THUGPRO's 1-4 hand moves).

## Run time (in the DLL)

| Id | What | Why | Where | Off by |
|---|---|---|---|---|
| TP-R1 | Online identity: GameSpy game name `thugpro` (key unchanged) | Shows THUGPRO's lobbies ("General", "Newbies") on OpenSpy instead of THUG2's | `src/game/net/online_identity.cpp` | `[Miscellaneous] OnlineGameName=` empty (the default: THUG2's own) |

Not done (would be run-time work): THUGPRO's own protocol - its extra messages (155-159), the scripts-hash check,
LZHAM compression, the level name it adds to 0x46. Mixed games with THUGPRO players need that whatever the files are.

## Changing approach (build time -> run time)

To drop an entry from the files and translate on the wire instead (per session, e.g. when the peer is THUGPRO):
- TP-1: map the four load-script checksums both ways in 0x46 / 0x33 (skate_net 0x00569560, HandleStartInfo).
- TP-20: the same for Load_practice <-> Load_thps4_skateshop; then
  `node tools/thps4_levels/thps4_levels.ts convert --compat none` (only the load script's name changes, not the files).
- TP-21: in the skate shop, map a received crown index past our crowns to a THPS4 restart's place (CFunc_SpawnCrown
  0x00538170) - spawns don't travel and need nothing; then `--compat none` (or drop `net_nodes` from `thps4_levels.json`).
- TP-30: the same for Load_hh <-> Load_thug1_se; then `node tools/thug1_levels/thug1_levels.ts convert --compat none`
  (saved layouts: `tools/thug1_levels/migrate_layouts.ts` shows the renaming - THUG1's frame is the same on both sides).
- TP-31: spawns don't travel - each side picks its own from its node array, so nothing to translate on the wire. A
  run-time alternative: when the session's peer is THUGPRO (or the host asks for it), apply `spawns` to the loaded
  node array's restarts before the first spawn (positions / types / the order GetRestartNode walks). Without it:
  `--compat none` (THUG1's own spawns; regenerate the data with `node tools/thug1_levels/thugpro_compare.ts
  --extract-thugpro`).
- TP-10 / TP-11: nothing to translate on the wire - flags aren't sent, each side makes them from its node array. A
  run-time alternative: when the session's peer is THUGPRO, place the flag / team-node game objects from this data
  after loading (and THPS3's own otherwise); without the entries: `node tools/thps3_levels/thps3_levels.ts convert
  --compat none` (THPS3's two bases, two teams) - or remove `ctf_flags` / `team_nodes` from `thps3_levels.json`.
  Regenerate the data: `node tools/thps3_levels/thugpro_compare.ts --extract-thugpro tools/compat/thugpro/thps3_levels.json`.
- TP-2 / TP-3: add / subtract the per-level offset to every world position sent / received (0x23 object updates,
  0x48 RunScript parameters, 0x8F / 0x95 scavenger drops, any positional KOTH / CTF event) before the int16 packing;
  watch the range at the edges.
- Then `node tools/thaw_levels/thaw_levels.ts convert --compat none` (or remove the entry from `thaw_levels.json`) and reconvert.
Saved level-editor layouts are in world coordinates: a frame change moves them (`tools/thaw_levels/migrate_layouts.ts`
has the shifting).
