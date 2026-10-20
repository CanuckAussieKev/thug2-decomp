# THUGPRO compatibility register

Everything this project does **only so that it agrees with THUGPRO** (Morten1337's THUG2 mod) for online play: choices
that are THUGPRO's, not the original games' data. Each entry has an id (TP-n), what it is, why, where it's applied and
how to take it out. Keep this list complete: a new THUGPRO-only choice gets an entry here, its data in this folder,
and a `TP-n` in the code comment where it's used.

Why separately: these are arbitrary agreements (names, offsets, identities), not improvements. Kept apart they can be
switched off as a whole (`--compat none` for the converters) or later replaced by a run-time translation of the
network messages, e.g. to play the same maps with players of another game or mod (THAW players: THAW's own names and
world frame) - see "Changing approach" below.

Data: `thaw_levels.json` (read by `tools/compat/compat.py`). Comparison against THUGPRO's install:
`tools/thaw_levels/thugpro_compare.py`. Background: `docs/pending/thaw.md`; the comparison report (THAW ports vs
THUGPRO). Rules: insights from Morten1337's repositories are allowed (his permission, 2026-10-20), never copies of his
code or level data; the values here are identifiers and offsets, measured or read, with their source given.

## Build time (persisted in the files we generate)

| Id | What | Why (what travels online) | Applied in | Source |
|---|---|---|---|---|
| TP-1 | THAW level names: Chicago `skatepark`, Kyoto `kyoto`, Santa Cruz `sz`, Vans Park `vans` (the other 11 keep THAW's zone name, `z_ma`..., as THUGPRO's do) | A level is known online only by its load script's checksum (change level 0x46, start info 0x33): both sides must name it alike | `thaw_levels.json` `level_names` -> `thaw_levels.level_name` (folder, files, `Load_` / `Level_` scripts, node array name) | THUGPRO's installed `data\pre` file names |
| TP-2 | Vans Park left in THAW's world frame although it has a `Z_SV_Zone_Origin` | Positions travel as world coordinates (skater updates 0x23, int16): both sides must place the area alike. THUGPRO's toolchain never moved it (it looked the origin up under the new name `vans`) | `thaw_levels.json` `frame_kept` -> `thaw_levels.frame_shift` | thugpro_toolchain `find_zone_origin.py`; confirmed by `thugpro_compare.py` (2192/2192 nodes in place) |
| TP-3 | East LA and Santa Monica moved by THUGPRO's own offsets on top of the Zone_Origin frame: EL (-1319.656, -111.047, 269.924), SM (-5060.44, -580.625, -3203.87) | As TP-2. THUGPRO's "LA crash fix" (commit 361b2c1) moved them by hand-picked offsets that aren't in THAW's data | `thaw_levels.json` `frame_adjust` -> `thaw_levels.frame_shift` | thugpro `source/levels/Z_EL/world_offset.txt`, `source/levels/Z_SM/node_position_patcher.py`; the values measured with `thugpro_compare.py` (EL 3404/3404, SM 3644/3661 nodes in place - the rest moved by hand in THUGPRO) |

Not entries here, although THUGPRO does the same (they're right for THUG2 on their own):
- **The Zone_Origin frame** (each THAW area moved by minus its `<zone>_Zone_Origin` node): THAW's own data, keeps the
  areas near the origin (precision, the network's +-16,384 range). Our base rule; the idea came from THUGPRO's
  toolchain (credited in `thaw_levels.py`).
- **Game-mode nodes without the zone prefix** (`TRG_CTF_*`, `TRG_Flag_*`, `TRG_KOTH*`, `TRG_Team_Restart`): THUG2's
  own scripts look them up by those names (`thaw_levels.multiplayer_names`).

## Run time (in the DLL)

| Id | What | Why | Where | Off by |
|---|---|---|---|---|
| TP-R1 | Online identity: GameSpy game name `thugpro` (key unchanged) | Shows THUGPRO's lobbies ("General", "Newbies") on OpenSpy instead of THUG2's | `src/game/net/online_identity.cpp` | `[Miscellaneous] OnlineGameName=` empty (the default: THUG2's own) |

Not done (would be run-time work): THUGPRO's own protocol - its extra messages (155-159), the scripts-hash check,
LZHAM compression, the level name it adds to 0x46. Mixed games with THUGPRO players need that whatever the files are.

## Changing approach (build time -> run time)

To drop an entry from the files and translate on the wire instead (per session, e.g. when the peer is THUGPRO):
- TP-1: map the four load-script checksums both ways in 0x46 / 0x33 (skate_net 0x00569560, HandleStartInfo).
- TP-2 / TP-3: add / subtract the per-level offset to every world position sent / received (0x23 object updates,
  0x48 RunScript parameters, 0x8F / 0x95 scavenger drops, any positional KOTH / CTF event) before the int16 packing;
  watch the range at the edges.
- Then `thaw_levels.py convert --compat none` (or remove the entry from `thaw_levels.json`) and reconvert.
Saved level-editor layouts are in world coordinates: a frame change moves them (`tools/thaw_levels/migrate_layouts.py`
has the shifting).
