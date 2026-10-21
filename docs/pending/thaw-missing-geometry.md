# thaw-missing-geometry - THAW levels' pieces made from node flags (tools/thaw_levels)

Agent "thaw-missing-geometry", branch worktree-agent-a46796f25d065a204. Bug (user, main install, screenshot
`C:\Games\THUG2\Game\Screenshots\thug2_2026-10-21_18-46-59.png`): Skate Ranch (z_sr) has a hole by the spine at
skater (2451, 18, 1130) - the sky's reflection where ground should be, no collision; "maybe other levels".

## Cause

- Not a converter filter: around that spot (+-400 units) THAW's `z_sr_net.pak` scene and collision, its full archive
  `z_sr.pak` and the transition zones `z_srbh` / `z_bhsr` give exactly the same triangles as our converted
  z_sr.scn.xbx / z_sr.col.xbx (vertical ray casts on a 16-unit grid: 0 differences; sector / collision names the
  same; col_check: 0 unreachable faces).
- The pieces are there but never made. THAW's story areas create much of their geometry, rails and objects from
  **node flags**: `{Name=Z_SR_g_ho_def_ropes_ground_cap Class=LevelGeometry CreatedFromVariable=NODEFLAG_Z_SR_HO_ROPES_DEF}`,
  `{Name=Z_SR_g_ho_ropes_flash_00 ... CreatedFromVariable=NODEFLAG_Z_SR_HO_ROPES}` - no CreatedAtStart. THAW sets the
  flags by script: chapter_info.qb (qb.pak) `game_progress_set_initial_story_mode_nodeflags` for a story game and
  `game_progress_set_non_story_mode_nodeflags` for every other game (it runs the initial one, then its own list:
  NODEFLAG_GAMEMODE_MULTIPLAYER 1, every Skate Ranch piece built - `NODEFLAG_Z_SR_HO_ROPES 1` - and its stand-in off -
  `NODEFLAG_Z_SR_HO_ROPES_DEF 0` -, the areas' "after" states...; applied by a ChangeNodeFlag loop). THAW.exe has both
  scripts' checksums in its code (file offsets 0x192E7A / 0x192ED4); no qb file calls the non-story one.
- THUG2 has no node flags and the converter dropped `CreatedFromVariable`, so a node without CreatedAtStart was never
  made: at the spot, the ropes piece (ROPES, on in THAW) and its ground stand-in (ROPES_DEF, off) were both missing.
- THUGPRO keeps `CreatedFromVariable` and has a table of flag values per level (its Z_SR.qb: `NODEFLAG_... = 0/1`,
  98 entries) for its own DLL - insight only. Its values are THAW's non-story ones except SR's HO_DINO (THUGPRO: the
  built dino on, the stand-in off), CRANE_SNAKE, CRANE_TOP, EL_MATTRESS, SM_SCOPE_OBJ (THUGPRO 1, THAW 0).

## Fix (thaw_levels.ts)

`nonStoryNodeFlags` reads chapter_info.qb from qb.pak and evaluates `game_progress_set_non_story_mode_nodeflags` (its
{flag value} entries in order, the scripts it calls where they're called; a list applied some other way than
ChangeNodeFlag throws); `applyNodeFlags` gives every node whose flag is on CreatedAtStart (in convertLevelNodes, after
THUGPRO's node edits / new nodes, before anything reads CreatedAtStart). Flags off or never set stay not made, as in
THAW. Not a port change (it is THAW's own non-story state): no switch. Test: thaw_levels.test.ts "node flags".

## Survey (all 15 areas; THAW's net node arrays)

Nodes with CreatedFromVariable: CH, MA, DN, JA, SZ, SV2 none; SV 16 (all off); HO 174 (110 now made), BH 438 (205),
DT 217 (151), EL 157 (52; 5 flags never set - sprinklers, a goal's underwear: off), SM 1060 (506), SR 2634 (2208), LV
367 (355), OI 566 (474). Classes: LevelGeometry, RailNode, LevelObject, LevelLight, ClimbingNode, Restart, GameObject,
Vehicle, Pedestrian (peds and game objects then go through thaw_scripts' own rules).

LevelGeometry never made, solid collision faces (before -> after, main install overlay vs THUG2-3 staging):
SR 21,706 / 30,794 (70%!) -> 3,784; OI 9,890 -> 4,214; SM 4,414 -> 2,089; BH 4,592 -> 2,320; HO 3,927 -> 1,402;
LV 2,421 -> 187; DT 1,444 -> 833; EL (ns can't decompile its qb - not measured; 52 nodes made). What stays: the flags
off (stand-ins, story states) and ~130-415 per area with neither flag nor CreatedAtStart (story / goal pieces, shadows,
`NightOff_*` time-of-day pieces - small: 193-421 solid faces per area).

Other games: no node flags (THPS3, THPS4, THUG1 node arrays have no CreatedFromVariable). Their geometry not made at
the start is what the originals create from goals / competitions / scripts (PROSET*, G_COMP_*, ProChallenge*,
Bridge_Damaged, the THPS3 tutorial's lessons, LOD copies) - as in their free skate, not this bug.

## For the lead

- Reconvert all of THAW (`node tools/thaw_levels/thaw_levels.ts convert`, then make_menu_level) into C:\Games\THUG2:
  HO, BH, DT, EL, SM, SR, LV, OI change (SR most); CH, MA, DN, JA, SZ, SV2, SV are unchanged.
- Online with THUGPRO: the five SR flags above differ from THUGPRO's table (the dino head piece and a few objects) -
  a TP-n entry could take THUGPRO's values if that matters.
- Not done: `NightOff_*` / time-of-day pieces with neither flag (THAW's TOD manager) - small, mostly shadows.

## Verification

- npm typecheck / lint / test pass (183 tests); `thaw_levels.ts selfcheck`: 735 files, 0 failures; THUGPRO compare for
  z_sr unchanged (nodes, crowns, mp restarts, net spawns, CTF nodes all the same).
- Offline, at the reported spot: a grid of vertical ray casts over the collision that is *made* (CreatedAtStart or
  not a node), +-400 units around (2451, 1130): before 246 / 1089 points with no ground (a ~300 x 450 unit hole, the
  ropes area at x 2100-2400, z 1080-1530); after 0 / 1089.
- Game (C:\Games\THUG2-3 under its lock, this branch's DLL merged with main 12155c3 - main's bigger script pools):
  z_sr loads into free skate with the 2,208 extra nodes; no pool ran dry, no errors in the log; the picture shows the
  built pieces (the crane). The automation channel can't place the skater, and the nearest restart is ~1000 units
  away, so the exact spot wasn't framed in the game - **the user should check it by hand** (skate to the spine at
  (2451, 18, 1130): ground on its right, no hole; also look round SR, OI, SM, BH, HO for pieces now there).
- Note: another agent sharing THUG2-3 reconverted THAW with code without this fix at 19:06 (the staging and overlay
  were overwritten between my convert and my game run); reconverted under the lock before the run that counts.
