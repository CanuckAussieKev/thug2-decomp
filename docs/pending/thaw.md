# thaw - THAW (Tony Hawk's American Wasteland, PC 1.01) levels in THUG2 (tools/thaw_levels)

Agent "thaw". How to run it: tools/thaw_levels/README.md.

## Result

- `node tools/thaw_levels/thaw_levels.ts convert` converts all 15 levels from their `_net` archives: Chicago,
  The Mall, Minneapolis, Kyoto, Santa Cruz, The Ruins, Vans Park, Hollywood, Beverly Hills, Downtown, East LA, Santa
  Monica, Skate Ranch, Casino, Oil Rig (1.9-11 MB each), plus the shared sky (TestLevel_Sky). The kill zones are 0-30
  per level, and each one names a restart.
- `selfcheck`: THUG2's LA files round-trip byte-identical. Every file of every THAW `_net` archive, the skies and
  qb.pak reads: 735 files, 0 failures (the collision written back byte-identical).
- Level select: a "TONY HAWK'S AMERICAN WASTELAND" section after THPS3's (level_select.THAW_LEVELS: pictograms and
  globe stars; LA areas at their own LA neighbourhoods; no star for The Mall and The Ruins). Numbers 80-94.
- **NOT run in the game yet**: the two attempts on 2026-10-18 overlapped with the user using the game (it reached
  the front end, then the keys landed in the wrong menu). The overlay in C:\Games\THUG2 was regenerated with the
  merged make_menu_level.py (main 79204a4) and has all 15 THAW levels installed.

## Formats

- Pak / tex / col / qb: see the README and thaw.ts. qb: 851 of 851 files read, and every script's token walk ends
  exactly at its end (THUG2 tokens with weighted random tables, plus 0x4A inline structures). Type 4 is a *local*
  string, not a wide one. Uncompressed 32-bit textures store each level's size as {u16 pitch, u16 rows}.
- `.geom`, from THAW.exe (Ghidra project C:\Games\THAW-ghidra, loader 0x005083B0, materials 0x00530470) and the files:
  - 0x20: material block {u16 0x1002, u16 count, u32 size, u32 16, u32 -1}, 0x120 bytes per material: +0 checksum,
    +4 name, +8 passes, +0x0D double-sided (guess: set on the transparent ones), +0x10 alpha cutoff, +0x11 z bias
    (guess), +0x14 runtime texture pointers, +0x2C draw order, +0x30 pass flags x4 (THUG2's bits), +0x40 textures x4,
    +0x50 colour 4f x4 (rgb, fixed alpha/128), +0x90 blend x4 (mode | fixed alpha << 24), +0xA0 u/v address x4,
    +0xB0 env tiling x4, +0xD0 filtering x4 (0x00010004 as THUG2's), +0xE0 uv wibble offsets x4 (12 floats each),
    +0xF0 floats x4 (mip bias?), +0x100 count + offsets of colour wibble / texture animation data.
  - Scene: 0xBABEFACE, S = +8 + second word. S+0x80 sector count, +0x84 sectors (0x30: vtable, checksum, flags),
    +0x88 per-sector nodes (0x68: box at +0x18, mesh count at +0x44), +0x90 0x14 bytes per sector (runtime),
    +0x94 meshes (0xE0: flags word with 0x40 set, sphere, material checksum, stride, u16 vertex count at +0x26,
    index count, FVF at +0x40).
  - Data: 0xAA filler, then per mesh in order: (sector flag 0x800: a colour-wibble byte per vertex), the vertex
    buffer(s) {u32 size, data} (two in the 0x800 sectors), aligned to 4, {u32 size, u16 strip}, aligned to 4.
  - Vertices are D3D FVF (0x152 / 0x252 / 0x352 / 0x452 / 0x52 / 0x142) in THUG2's own layout, 128 = full
    colour. Sector flag 0x800000 = billboard: every vertex is at the pivot, with the corner offset in the normal.
    In THAW they are all glows: left out.
- Sector checksums are the node names (Z_CH_...). LOD copies (LOD_Name) and night-time sectors (NightOn_*,
  *_tod_light_*) are left out along with their nodes.
- Terrain values and names are THUG2's (THUG2 has every TERRAIN_ name THAW uses).

## Left

- Game runs: load Chicago, then a story area (Hollywood) and the Oil Rig (recentred), grind, a kill zone.
- Unchecked guesses: material +0x0D (double-sided), +0x11 (z bias), the uv wibble's 12 floats -> THUG2's 8.
  THAW-only pass bits (0x80, 0x800 texture animation, 0x80000) are dropped. Blend modes above 13 become 5.
- How THAW closes the open ends of the LA areas in net mode: not checked (whether walls or kill planes are needed).
- Lighting: THUG2's default (THAW's time-of-day manager is not converted). Sky: TestLevel_Sky as it is.
- The areas whose collision reaches beyond 20,000 units are moved to the origin: The Mall, Kyoto, Santa Cruz, BH,
  EL, SM, SR, LV, OI (Oil Rig by 56,064 in z).
- Not converted: peds, vehicles, goals, ladders (ClimbingNode), moving / bouncy objects (static), particles, music,
  the main-menu room, shops, CAP shells.
- (lead) Merged into main (9cb6031). Converted with main's level_port (LoadTerrain, fix_terrains) and installed; Chicago (the first THAW level) loads and skates in the game - the other 14 not tried yet.
- (lead) The Mall: the glass doors by the start couldn't be passed (user). Two parts: (1) breakables - a LevelGeometry node whose TriggerScript (or a script it calls, 2 deep) shatters it (THAW's MA_BreakGlass = Kill + ShatterAndDie) gets THUG2's Shatter name=<it> ... + Kill as its trigger script, with its trigger faces kept (breakable_scripts; 14 in The Mall, 0-98 per level); the glass faces are already non-collidable triggers. (2) **Corrected 2026-10-20:** Z_MA_Mall_First_Rail_Blocker is not across the doors - it is a thin invisible wall (12 faces, flag 0x1000, box x -15767..-14044, y 1367..2278, z -5641..-5600 in THUGPRO's frame) running alongside the start area, about 280 units from Z_MA_TRG_restart and ending about 430 units before the entrance glass; THAW and THUGPRO keep it (CreatedAtStart, CollisionMode Geometry, no script removes it). It was left out here (LEFT_OUT) by a miscommunication and is **restored on the user's decision (2026-10-20)**: scene node and collision as THAW's. The doors with the "closed / cerrado" sign are the entrance glass Z_MA_Sk_JOW_Mall_Entrance02/03/04 (sticker ESign08): our THUG2 shatter conversion (port_changes PC-THAW-4).
- (mall-collision) The Mall: invisible walls at both ends of the escalator (user, after the level editor). Cause: the
  recentring moved each collision object's vertices and box but not its BSP tree, whose split points are world
  coordinates (sixteenths; all 1362 of THAW's Mall splits lie inside their boxes in world terms, none relative). A
  moved object's queries then went down one side of every split: 6242 of the Mall's 15719 faces (197 objects) could
  never be found - among them the floors at both ends of the escalator (Z_MA_Mall_TB_Room_02, split x -36652 /
  TB_Room_03b_top, split z -18627: the halves under the escalator's foot and head), Wall040, the elevator glass,
  railings. The walker stops at a drop with nothing below: the "walls". The debug_collision log had no hit at the
  ends (only the escalator's own side walls/railings, drawn and collidable in THAW too). Fixed (move_tree); every
  face of every level is now reachable through its tree (offline check: a segment through each face's centroid,
  THUG2's traversal). Also hit in the 8 other recentred levels: JA 24220/34402 faces unreachable, SZ 30472/44225,
  BH 15587/26020, EL 12273/22293, SM 16708/24737, SR 20169/31550, LV 16775/25502, OI 27791/37809; CH, DN, SV2, SV,
  HO, DT not moved - fine. THAW's face bits 0x2000/0x4000/0x8000 (dropped) aren't it: the faces hit are 0x4 / 0x0,
  drawn, and 0x8000 is on most floors (2140 Mall faces). Reconverted and installed (all 15 levels: every face reachable in the installed files). NOT tested in the game yet.
- (lead) The Mall's glass still stopped the skater (user, sticker-slappable): the entrance glass (Z_MA_Sk_JOW_Mall_Entrance04...) kept THAW's Type=Crate (its physics breakables) and THAW's face flags 0xF0. Breakables now lose Type, and their faces are written as THUG2's own (AP_Shatter_Roof: 0x1150). Also: dropped nodes (LOD copies, night sectors, LEFT_OUT) were removed after convert_nodes renumbered the Links - every later node's links one off (The Mall hung loading); now placeholders before converting; all 15 levels' rail links checked.
- (lead) Breakables had no sound (user): the breakable script plays HitGlassPane2x (THAW's MA_BreakGlass does; THUG2 has the sound, loaded per level), loaded by the level's LoadTerrain in levels with breakables (the level-specific LoadingSounds script goes by the career prefix, shared by all ported levels).
- Minneapolis crashed in the loading screen (0x0058977C ParseNodeArray TrickObject registration, checksum 0xFDF3290B):
  LevelGeometry nodes marked TrickObject / with a Cluster whose sector the scene doesn't have. The THPS3 guard moved
  to `level_port/nodes.strip_missing_trick_objects` and the THAW convert runs it after the scene is written: all 15
  levels had some (2 to 89 nodes each). Installed; NOT confirmed in the game yet.
- (lead) Santa Monica crashed in the loading screen (0x004DD754): THUG2 builds a ps.1.1 pixel shader per material from
  its passes' blend modes (0x004DD2C0) and doesn't check that D3DX assembled it. Modes 12/13 mix by the previous
  pass's alpha (r1.a), and a 12/13 pass writes only r1.rgb - so 12/13 after 12/13 reads an unwritten r1.a and the
  shader doesn't assemble. One material in all the ported levels (Santa Monica 734D2826: 0, 13, 13); the converter
  leaves the second such pass out. Installed; NOT confirmed in the game yet.

## THUGPRO compatibility (agent thaw-thugpro, 2026-10-20) - NOT run in the game yet

Goal: a THUGPRO player and one of ours in the same THAW level online. Everything is generated from THAW's own files;
THUGPRO's toolchain (thugpro_toolchain/thaw_level_toolchain) and installed levels (%LOCALAPPDATA%\THUG Pro\data\pre)
were only read as the reference and to compare against (`thugpro_compare.ts`) - nothing of theirs is copied.

What travels online and has to agree (from our decompilation): the change-level message 0x46 / start info 0x33 carry
the load script's checksum only (the other side runs that script); skater updates (HandleObjectUpdates 0x0056E470)
carry world positions as int16 (x/z x2, y x4: +-16,384 / +-8,192 units); King of the Hill picks a crown by its index
among the Crown nodes (CFunc_SpawnCrown 0x00538170); SpawnScript 0x49 carries the script checksum and, from a trigger,
the node's *name* checksum (CTriggerComponent::TriggerNode passes the name - not an array index: node order doesn't
travel). No THAW level (THAW's or THUGPRO's) has a NetEnabled node, so no 0x49 comes from their triggers.

Changes (tools/thaw_levels):
- Names: each level is named after its zone (`z_ma`: Load_z_ma, Level_z_ma, levels\z_ma\z_ma.*, z_ma_NodeArray), as
  THUGPRO names 11 of them. Chicago, Kyoto, Santa Cruz and Vans Park THUGPRO renamed (its own choice, not THAW data):
  (lead, the user's decision) those four take THUGPRO's names - skatepark, kyoto, sz, vans (TP-1 in the
  compatibility profile tools/compat/thugpro) - as protocol identifiers, since online the load script's name is the level: all 15 now named as
  THUGPRO's. `--legacy-names`: thaw_<xx>.
- Frame: an area with a <zone>_Zone_Origin node moves by minus its Pos, every axis, in whole sixteenths (THUGPRO's
  rule; BSP splits stay exact). Vans Park is left in THAW's frame as THUGPRO's toolchain left it. Result vs THUGPRO's
  installed node arrays: CH, MA, DN, JA, SZ, SV2, SV, HO, BH, DT, SR, LV, OI the same (all but 0-17 hand-moved nodes
  within 0.1 unit); East LA and Santa Monica THUGPRO moved by offsets of its own (not in THAW's data:
  THUGPRO = Zone_Origin frame + (-1319.656, -111.047, 269.924) in EL, + (-5060.44, -580.625, -3203.87) in SM) -
  (lead, the user's decision) taken as TP-3 in the compatibility profile, since positions travel online as world
  coordinates: EL 3404/3404 nodes now at THUGPRO's place, SM 3644/3661 (the rest THUGPRO's hand moves); faces past
  the network range EL 96 -> 88, SM 152 -> 144; every face reachable. `--frame recentre`:
  the older frame. JA and SZ are no longer recentred (THUGPRO's frame: THAW's own; 76 / 7 collidable faces lie past
  the network's +-16,384, far from the skate areas).
- Game modes: TRG_CTF_*, TRG_Flag_*, TRG_KOTH*, TRG_Team_Restart lose the zone prefix (THUG2's net\ctf / SK4_Scripts
  look them up by those names - THUGPRO's toolchain strips the same list); the CTF / team flag game objects are kept
  (Type Flag_*, THUG2's own gameobjects\flags\ models, THAW's trigger scripts calling THUG2's Team_Flag): 12 per level.
  Crowns: same count, order and places as THUGPRO's in all 15.
- The Mall's teleports (and every other level's): THAW calls THUG2's own TeleportSkaterToNode, which the converter
  left out ("THUG2 script needing components the runner hasn't") - the skater's animblender / input / vibration
  weren't in the thaw_skater context. Added: MA's 5 (Trgp_teleport -> Z_MA_TRG_restart, Teleport_Spc01/02, the book
  sale gap in / out) now convert. (MA_Teleport_Helper is called by THAW but defined nowhere - THAW's own dead call.)
- RailAccelerate (THAW's skutils.qb rail speed-up, 12 Mall rails, others): a THUG2 version written from THAW's
  (without THAW's TriggerType test - THUG2 has none) in every level's qb.
- trigger_jump_boost (The Ruins' 10 jump boosts): THUG2's own script is THAW's line for line; script_needs over-counts
  its components (follows parameter names) - allowed for the skater (SKATER_SCRIPTS_CHECKED).
- `object:member` statements on the made objects are kept, checked as run by that object and guarded with IsAlive
  (script_port's existing `objects` form): The Mall's bouncy boxes are kicked (Rigidbody_Kick), movers move.
- Layouts: `node tools/thaw_levels/migrate_layouts.ts --game <folder>` renames <crc(thaw_xx)>.txt to <crc(z_xx)>.txt and moves every record
  by the frame difference (sector / rail / clone offsets: offset + d - Q d; objects / copies: pos + d); the old file
  kept as .before-thugpro-frame. Checked on a made-up Mall layout in THUG2-1 (removed after); no game folder had any.
- `thugpro_compare.ts` (read-only on THUGPRO's install) and `tools/level_port/col_check.ts` (every face reachable
  through its BSP tree, as THUG2's queries walk it) are new.

Checks (THUG2-1, all 15 converted and installed): every face reachable (0 unreachable in all 15); links in range, rail
links to rails; every level's qb decompiles through ns.exe except East LA's (THAW's own EL_Spring nests `repeat`
inside an `if` - valid for the interpreter, and behind a left-out TriggerType condition, never run); thaw selfcheck
735 files 0 failures; THPS3 / THPS4 / THUG1 selfchecks 0 failures. No C++ changed.

Still different from THUGPRO (and what it means online): EL / SM frames (positions off by the offsets above - remote
skaters drawn displaced; a position translation in the skater-update messages would fix it); CH / JA / SZ / SV names
(the level doesn't load across: a load-script alias or a checksum translation in 0x46 / 0x33); restarts: SZ - ours
has 10 fewer Multiplayer restarts (Killox01_restart*... - CORRECTED 2026-10-20 by the TP-4 pass below: they are not
over water or kill zones; THAW gives them a malformed restart_types, the string "Player 1", and THUGPRO turned them
into network spawns by hand), CH 2 / 1 different, BH 1 moved by THUGPRO (both now TP-4); node arrays: THUGPRO kept
46-723 more nodes per level (story / goal / sfx nodes) - node order doesn't travel, so no effect.

## TP-4: THUGPRO's hand-edited network spawns (agent thaw-thugpro, 2026-10-20, approved by the user) - NOT run in the game

Why: competitive fairness - a player shouldn't start somewhere their opponents never do. Scope: every restart with
Multiplayer / Horse / CTF / Team types that THUGPRO moved, retyped, added or took out of the network spawns, in all
15 areas, against THAW's own node arrays.
- Data: `tools/compat/thugpro/thaw_levels.json` `spawns`, written by `node tools/thaw_levels/thugpro_compare.ts
  --extract-thugpro` (THUGPRO's installed node arrays against THAW's `_net` ones; positions stored in THAW's frame).
  Applied by `thaw_levels.applySpawns` (after convertNodes, before the frame shift) from the profile only;
  `--compat none` output unchanged (all 15 .qb the same as code plus sorted names, the other files byte-identical).
- What it changes (8 edits in 4 areas - "7" in the first count was a miscount; 18 with Santa Cruz's 10, below): CH - TRG_CTF_Restart_Green [CTF Multiplayer Horse] -> [CTF];
  Z_CH_TRG_G_TrickAttack_RestartNode moved 434 units, turned, [Multiplayer Horse] -> [Player1] (no longer a spawn);
  one spawn added (THUGPRO's half-pipe drop-in: our form TRG_Net_Spawn01, Generic, [Player1 Multiplayer Horse],
  running `<level>_net_dropin` = MakeSkaterGoto DropIn). SZ - Z_SZ_TRG_StartOfLevel loses PriorityRestart. BH -
  Z_BH_TRG_Player_Start01 13.9 units lower. EL - Z_EL_way_kill_metrohq loses PriorityRestart; the two
  TransitionParkTeleport restarts stop being spawns (ours already dropped them: over the freeway kill).
- Order (the lead, from the THUG1 agent's finding: THUG2 starts player n at the n-th Multiplayer restart of the node
  array and respawns at one of the first 8 - Skate 0x00566100 -> GetRestartNode 0x0046B790 ->
  CSkater::MoveToRandomRestart 0x00580830, Rnd(8)): the profile also holds each area's full spawn order (THUGPRO's,
  by our names; `order`), and applySpawns refills the spawn slots in that order, renumbering every Links (TP-31's
  approach). nodes.convertNodes' Player1-first swap is kept: it changes no THAW area's spawn order - the refill moved
  0 spawns in all 15 (the data now guards it).
- Result (thugpro_compare, all network spawns - place and types, in order): the same as THUGPRO's in 14 areas; SZ the
  same 35 in the same order, THUGPRO's 10 more not applied (below) - since then applied: all 15 the same (below).
- **Santa Cruz's 10 - approved by the user and applied (2026-10-20, see the end of this section)** (Z_SZ_TRG_restart01, _derby01, _fromalley01 / 02 (the same
  spot), _fromboardwalk01, _Killox01_restart ... restart05). In THAW their restart_types is the string "Player 1" (not
  a list: no network spawn in THAW, nor in ours); THUGPRO made them [Player1 Multiplayer Horse]. Listed in
  `spawns_not_applied`. Checked against our collision: all 10 stand on ground (roads, the derby rim, a roof, grass),
  none over water or a kill zone. As it is: THUGPRO players can start a network game at 45 places, ours at 35; each
  side sees the other appear wherever it spawned. Applied: both 45.


- Santa Cruz's 10, approved (2026-10-20): the extraction rule now treats a THAW restart whose restart_types isn't a
  list like any other - THUGPRO's types win - so the 10 are in `spawns` (with their places in THUGPRO's `order`) and
  `spawns_not_applied` is empty (regenerated with --extract-thugpro). Converted into C:\Games\THUG2-4: TP-4 sets
  11 Santa Cruz spawns (the 10 and StartOfLevel), 0 reordered; thugpro_compare - crowns, multiplayer restarts and every
  network spawn (place, types, order) the same in all 15 areas; every collision face reachable; links fine; ns.exe
  decompiles all but East LA (THAW's own EL_Spring); --compat none unchanged. Left as THAW has it: THUGPRO also took
  CreatedAtStart off fromalley01 and fromboardwalk01 (not a spawn type - outside TP-4). NOT run in the game.

## THUGPRO's other hand edits in its THAW levels - inventory (2026-10-20; since applied as TP-5...TP-9 or awaiting the user - next section)

THUGPRO's installed node arrays, scripts and collision against THAW's own full zone archives (with their _sfx
sub-zones), in THUGPRO's frame. Caveat: THUGPRO converted THAW's *Xbox* files, so a few differences may be the Xbox
build's rather than hand edits (the cluster renames, some terrain types). The scratch script and its JSON are listed
in the report (inventory.ts). Effect on play between our players and THUGPRO's in brackets.
- Moved nodes (beyond the frame): rails - SM 17 (PacificParkStoresBuilding_a_*, 94 units), SZ 3 (levelevent99/100
  16.5), JA 3 (TRG_783 10.8), DN 1 (Player1_start02 rail, 56.6), SR 1 (6.4), CH 2 (2.5) [a rail one side grinds where
  the other has none: a remote skater grinds in the air]; geometry - DT DT_99Cent_02 463 and Objects_Mesh23 42, MA 6
  lamps (DLamp10 66), CH Outby701/702 trigger polys 21-27, LV 2, BH 2 [walls / props in different places where
  collidable]; SM's 12 ferris-wheel cars 17. SFX: BH 54, SR 18, OI 23 sound emitters THUGPRO appended without the
  frame shift (moved by exactly the zone origin) - a THUGPRO bug (its ambient sounds play in the wrong place), no
  effect on us (we have no _sfx nodes).
- Renamed: EL 110 / SM 103 night level lights, CH 11 (goal letters), SZ 2 (one rail to "bastardrail1") [none: names
  don't travel].
- Added: DN 35 (14 geometry pg_* + 21 bouncy caution objects; 10 collidable collision objects) [props / walls only
  THUGPRO has]; JA 120 (a DDR machine with 3 rails TRG_TP_DDR_RAIL_*, 2 building pieces, 46+51 car/ped nodes, 10 peds, 4
  scooters); SM 24 static ferris-car rails + 13 shadows [rails only THUGPRO has]; EL 88 car waypoints + a cop car; SZ 27
  car nav nodes + 3 cars; MA 3 light glows (collidable); SV 2 sauce machines (1 collidable); LV LV_Elevator_2ndFloor
  (collidable); BH 2 plants; SR 2 waypoints; OI the ferry; CH 1 spawn (TP-4).
- Removed: SV 7 meshes, CH 2 rail collision pieces (SkVans_Rail_Col01/02), DT Mesh15 (collidable), BH
  g_ground_gas_station_13b01 collision [a floor / wall only we have].
- Changed flags: AbsentInNetGames taken off - JA 168 geometry (light poles, bushes), SZ 93, EL 44 rails (metro HQ) + 23
  geometry (the limo), BH 23 [objects / rails present in THUGPRO's network games, absent in ours]; added - CH 73
  shadows; CreatedAtStart taken off / added - SV 13 (elevator / bathroom doors), LV 15 occluders, EL
  65 / SM 43 night-off pieces, SZ 18 (6 of them rails); class changed - BH 3 rails made game objects (inner_rail_023, Gaps25 floaters: rails
  only we have), SZ 13 (a restart, life peds), CH 17, DN 31 / JA 4 life nodes -> peds, SV2 / SR sfx; CollisionMode
  geometry -> none - SZ 7, CH 2 (collision only we have); TrickObject / Cluster - CH 10 and JA 6 and SZ 8 added, LV 10
  removed; cluster names prefixed with the zone - DN 1468, JA 3418, SZ 116 [possibly graffiti / trick-object messages
  - not checked]; TerrainType - SZ 43 rails wire -> wood railing, SR 20 wood -> metal, DN 2, JA 1, EL 1 [grind sound
  only].
- Changed scripts (by the names each calls): mostly THAW goal / story / camera calls taken out and THUGPRO's own
  additions (CTF / team flag scripts, level-change options, LoadingSounds, its peds); CH's gap scripts gated with
  THUGPRO's GetTriggerArray / TriggerType [gaps score differently]; MA's door sound swapped; EL glass breaks with
  no_sound. Per area: changed 4 (MA) to 182 (SM); added 1-79; removed 2-301 (the _sfx scripts THUGPRO dropped).
- Collision (by object): added collidable - DN 10 (pg_* building and cars), JA 3 (DDR machine...), MA 3 light glows,
  CH 1, SV 1, LV 1 (elevator floor); removed collidable (not _sfx) - SZ 8 (beach waves, booster streamers, a roof
  pipe, a barrier), SV 7 (meshes, a sauce and a snack machine), CH 3 (two rail collision pieces, a sign LOD), DT 1,
  BH 1 (gas station ground piece), LV 1 (elevator wall) [walls / floors on one side only]; face counts changed - DN 3, JA 2, SV 4, HO 2, DT 3, SM 3, SR 2, LV 1; moved objects - MA 20, DT 23
  (freeway cars), others 1-7. Plus THUGPRO's unreachable faces (earlier section).

## TP-5...TP-9: THUGPRO's other hand edits as profile data (agent thaw-tp59, 2026-10-20) - NOT run in the game

The user's decision: "Everything [THUGPRO] changed needs to be in the differences files that we can control with the
compat flag." So every hand edit THUGPRO made to its THAW levels is data in `tools/compat/thugpro/thaw_levels.json`
(register rows TP-5...TP-9 in its README.md): the default `--compat thugpro` applies it, `--compat none` gives THAW's
own level exactly as before (checked: all 15 .qb the same as code plus sorted names, every other file byte-identical).
What would copy THUGPRO's own content (its meshes, models, scripts) is listed in the profile's `awaiting`, not applied.

Tools (all read only on both installs):
- `thugpro_extract.ts` (new): the extraction, run by `node tools/thaw_levels/thugpro_compare.ts --extract-thugpro
  [--thug2 <game>]` with TP-4's spawns - THUGPRO's installed node arrays, scenes and collision against THAW's net
  archive (what we convert) and its full archive (the story's pieces, the _sfx sub-zones), in THUGPRO's frame. Never
  typed in. A sector / collision object is compared as a set of points (THUGPRO converted THAW's Xbox files, whose
  meshes weld and order vertices otherwise - a per-vertex comparison called 659 Hollywood sectors "changed"): the
  same, a rigid move (rotation about y + translation, fitted from the vertices, the node's own move or the box; checked
  to 0.1 units), or reshaped (awaiting).
- `thugpro_edits.ts` (new): applies it - `applyGeometryEdits` (THAW's scene / collision before converting: moves,
  the imported pieces with their materials / textures, the collision taken out; a moved collision object gets float
  vertices, a new box, BSP tree and reaches), `applyNodeEdits` (THAW's array before converting, our game-mode names:
  renames, moves, Links, flags / keys, cluster renames, removals as placeholders, added nodes linked by name), and
  `applyFaceEdits` (the converted collision: per-face flags and terrain, the face count checked). `thaw_levels.ts`
  calls them only with the profile's data (`editsFor`); `fullArchive` reads THAW's `<zone>.pak.wpc` for TP-6.
- `thugpro_inventory.ts --ours <levels>` (new mode): our converted levels against THUGPRO's per register category.
- `compactJson` (thugpro_compare.ts) writes the profile (short lists of plain values on one line): 580 KB.

Applied (counts over the 15 areas; per area in the register and the profile):
- TP-5: 60 nodes moved / turned / relinked (SM's 17 PacificParkStoresBuilding rails, DN's Player1_start02 rail, JA's
  TRG_783 / 3552 / 4384, SZ's levelevent99/100, MA's 6 lamps, DT's 99Cent_02 and Objects_Mesh23 nodes, CH's 2 rails and
  Outby701/702, SM's 12 ferris cars...); 23 sectors / collision objects moved rigidly (MA's 6 lamps - DLamp10 turned
  -0.105 rad -, CH's Outby701/702 collision, DN's 5, DT's stickers, LV's 8 backdrop pieces); 226 renames (EL 110 / SM
  103 / SZ 1 night lights - the zone prefix off, probably THUGPRO's toolchain, no effect here: our port leaves level
  lights out -, CH's 11 goal letters, SZ's bastardrail1). No rename or removal was skipped for a THAW script naming it.
- TP-6: 1,061 nodes' flags / keys (CreatedAtStart -216 / +113 incl. SZ's fromalley01 / fromboardwalk01; AbsentInNetGames
  -378 / +110; TrickObject +24 / -13; CollisionMode 26; Class 69, Type 124; TerrainType 67; TriggerScript off 30;
  SV's TrickAttack restart_types); 519 clusters renamed (DN 218, JA 288, SZ 13 - the zone prefixed; JA's 34 that THAW
  scripts name kept); 485 sectors / collision objects of the 310 nodes whose AbsentInNetGames THUGPRO took off,
  brought from THAW's full archive (Kyoto: 805 -> 939 sectors, 631 -> 706 textures). NetEnabled: THUGPRO changed none.
- TP-7: 10 nodes, 7 sectors, 22 collision objects out (CH's rail collision, SV's 7 meshes, DT's Mesh15, SZ's 9, BH 1,
  LV 1).
- TP-8: 238 nodes added: SM's 24 ferris-car rails, JA's 3 DDR rails, 211 car-path waypoints (JA 97, EL 88, SZ 24, SR 2).
- TP-9: 2,523 faces in 111 collision objects (wallride / skatable / non-collidable / camera flags; terrain - DN's
  1,043 faces in 61 objects, SZ 506, SR 324: maybe the Xbox data, applied, sound only).

Remaining differences (`thugpro_inventory.ts --ours`, all 15, before -> after): nodes elsewhere 44 -> 0; flags / keys on
rails, geometry, restarts and objects 5,626 -> 0; nodes only ours 12 -> 1 (TP-4's added spawn, ours TRG_Net_Spawn01,
THUGPRO's TRG_SkaterDropIn - names don't travel); nodes only THUGPRO's 4,151 -> 3,988 (classes our port leaves out or
makes its own way - 912 level lights, 505 peds, 571 game objects, 373 level objects, 86 climbing nodes, sound nodes -,
our night-time / LOD pieces left out, THAW's full-archive story nodes, the awaiting nodes); collision 1,094 -> 741
(709 objects only THUGPRO's: the full archive's story pieces and its own; 32 reshaped, awaiting); scene 3,110 -> 2,826
(sectors only THUGPRO's for the same reasons; 14 collision objects reshaped, awaiting); our port's own 1,143 -> 1,058
(TrickObject / Cluster off 365 geometry nodes without a sector, breakable glass faces, CollisionMode off objects
without collision). `thugpro_compare.ts`: every shared node at THUGPRO's place in all 15 (before: 0-17 per area off),
crowns, mp restarts and net spawns the same.

Awaiting the user (`awaiting` in the profile at the time - **since included as `thugpro_new`, TPN-THAW-1...5, see "THUGPRO's own content" below**; what each side saw without it):
- New meshes / collision THUGPRO built (74): DN's parking garage `z_dn_pg_*` (8 sectors, 1,303 vertices; 11 collision
  objects, 754 faces, 750 collidable) - THUGPRO players skate a garage ours don't have (they ride on air here, ours
  pass through theirs); JA's DDR machine (633 vertices, 318 collidable faces) - TP-8's 3 DDR rails hang in the air
  here without it; MA's 3 light glows (collidable), SV's 2 sauce machines, LV's elevator second floor (50 collidable
  faces), SM's 13 shadows, BH's 2 plants, CH's sign LOD collision, `Cylinder01` in all 15.
- Reshaped meshes (98): THAW's sector / collision changed in shape (DN's cars' collision rebuilt, SV's machines, HO's
  signs, DT's 99Cent_02 cut and Objects_Mesh23 turned about x - the node moved / turned here, its mesh not -, trigger
  planes of MA / JA / SV2 / LV teleports and elevator, LV's backdrop, SM's carousel / floor, SZ's barrier).
- Nodes of THUGPRO's content (81): its cars (TRG_Car_Street_Script; JA's scooters / ricer / van, SZ 3, EL's cop car -
  models THUG2 hasn't) and OI's ferry, JA's 10 peds with its scripts, its sound triggers, the new meshes' nodes (DN's
  caution signs, cones, chairs). Peds and cars are absent in network games anyway.
- THUGPRO's scripts (59 TriggerScripts and its level-script changes): CH's CTF / flag scripts, SV's CTF bases, the life
  peds' Ped_Stand_Script (our port makes those peds with its own script), SZ's parking meters (TP-6 gave them
  TrickObject), DT's dog, EL's coin08, SM's performers, LV's Cylinder49; CH's gap scripts gated by GetTriggerArray /
  TriggerType (gaps score differently), level-change options, LoadingSounds...

Concerns:
- TrickObject on collision-only geometry: THUGPRO keeps it on 365 nodes (and adds 10 in Chicago); ours drops it
  (stripMissingTrickObjects: ParseNodeArray 0x00588630 reads through the missing sector - THPS3 Airport and THAW
  Minneapolis crashed). Graffiti on those can't match until the C++ checks the sector (or they get one).
- SM's ferris rails (TP-8) hang where the ferris cars are; our port makes no ferris cars (vehicles not created at start
  are left out), THUGPRO draws them. JA's DDR rails likewise without the DDR machine (awaiting).
- TP-6's class changes take things out offline: CH's 10 standing peds and 7 waypoints (THUGPRO made the life peds and
  points of interest ghosts), SZ's 12 life peds and its boardwalk01 restart, BH's 3 rails; DN's 30 life nodes are peds
  either way.
- Probably THUGPRO's conversion rather than hand edits, applied as harmless: the cluster renames (zone prefixed), the
  night-light renames (prefix off), per-face terrain on DN / SZ / SR, the small rigid moves under a unit (MA's
  graffiti 0.3, exit sign 0.24).
- Not applied, values our port doesn't carry (it makes those nodes its own way): ped / car lod_dist and
  SuspendDistance, CreatedFromVariable, lightgroup, particle settings (THUGPRO's THUG2 textures for THAW's particles),
  pedai_type, compassbliptype... (`awaiting` notes per area).

Checks (C:\Games\THUG2-4, all 15 converted with the profile and installed with make_menu_level): every collision face
reachable (col_check, all 15); links in range, rails linked only to rails; ns.exe decompiles every level but East LA
(THAW's EL_Spring); `--compat none` unchanged; thaw / THPS3 / THPS4 / THUG1 selfchecks 0 failures; typecheck, lint,
tests (62, 7 new in thugpro_edits.test.ts). Not run in the game: worth a look - Kyoto (168 poles / bushes and their
textures now in network games, 97 waypoints, DDR rails), Santa Cruz (93 pieces, rails wood), Minneapolis (cluster
names), Santa Monica (ferris rails, moved store rails), Vans (7 meshes out), The Mall (lamps moved, wallride doors).

## THUGPRO's own content (thugpro_new, TPN-THAW-1...5) and our own changes (port_changes, PC-THAW-1...12) - agent thaw-tpnew, 2026-10-20 - NOT run in the game

The user's decisions (2026-10-20): THUGPRO's own content goes into our ports under the default `--compat thugpro`, in
one separate section `thugpro_new`, **by reference** (Morten1337 agreed; his files aren't republished); our own changes
to THAW's data go in a separate `port_changes` section. Both give every entry `op` add / set / remove, `id`, `target`,
`why`. Register: tools/compat/thugpro/README.md ("New in THUGPRO", "Our own changes (port_changes) - THAW").

- `thugpro_new.ts` (new): reads THUGPRO's level (`<level>scn.prx` / `col.prx` - its own `levels\<level>\<level>.*`
  only -, its node array, every script of its level .qb files and thugpro_qb.prx) from `--thugpro` (default
  %LOCALAPPDATA%\THUG Pro), checks each entry's CRC-32, and gives the converter what to merge. Missing install / item,
  or a CRC that differs: a warning, that item skipped (checked with an empty folder: all 15 convert, "no THUGPRO level
  ... skipped"). The extraction (`thugpro_compare.ts --extract-thugpro`, `thugpro_extract.ts`) writes `thugpro_new`
  and `notes` in place of `awaiting`; every other section came out byte-for-byte the same.
- Included: TPN-THAW-1 31 new sectors, 32 collision objects (DN's garage, JA's DDR machine, MA's glows, SV's sauce
  machines, LV's elevator floor, SM's shadows, BH's plants, CH's sign collision); TPN-THAW-2 51 reshaped sectors, 46
  collision objects, 1 emptied (HO Box04); TPN-THAW-3 58 nodes; TPN-THAW-4 37 scripts, 21 TriggerScripts (CH / SV CTF,
  SZ meters, EL coin08 and glass, MA door sound); TPN-THAW-5 12 host options in THUGPRO's default state and 11 levels'
  LoadingSounds (setup script `<level>_thugpro_setup`, the level structure's `setup_script`).
- Left out (`left_out`, with reasons): `Cylinder01` (THAW's sky dome from THUGPRO's bundled thaw_sky - ours converts
  THAW's own), peds' scripts (our ped AI), EL's cop car and OI's ferry (not there at the start - no models needed), DN's
  21 cones / signs / chairs (no sector anywhere), THUGPRO's banners and TOD scripts, ~100 scripts needing commands
  THUG2 hasn't (`_needs_cpp`: TriggerType first; CinematicCamera_*, PlayIGCCamScene, IGC_Camera_MoveToPlayer,
  SpawnScriptLater / Now, IsCreated, ChangeNodeFlag, DeleteGapFromScore, StickerOn - our own C++ later, crediting
  THUGPRO). At convert time a THUGPRO script naming a node ours hasn't is left out (`X:` on a missing object runs on
  the skater): MA_Dooropening (Z_MA_Obj_Skidmark_Grate01), SZ's SZ_Goal_Gaps17, EL's 2, OI's ferry toggle.
- `port_changes` (12, `compat.portChanges`, kept under `--compat none`): PC-THAW-1 Zone_Origin frame, -2 night
  sectors, -3 LOD sectors, -4 breakables as THUG2 shatters (under audit: ShatterAndDie / BloodSplat are THUG2 scripts;
  MA_KillStickers dropped - not changed here), -6 ground restarts: switchable by `--no-port-changes`; -5 TrickObject
  guard, -7 trigger faces, -8 blend-by-previous pass, -9 billboards, -10 unknown flags, -11 net-game nodes, -12 objects
  our way: `required`.
- The Mall's `Z_MA_Mall_First_Rail_Blocker` restored (LEFT_OUT removed; see the corrected note above).

Checks (outputs in the scratchpad, against baselines converted before any of this): `--compat none` and
`--no-thugpro-new` the same as before for all 15 but z_ma (the blocker: its node, collision and the Links after it
renumbered), the .qb files the same code with the same checksum names (their 0x2B texts' case / order vary run to run,
as before); every collision face reachable (col_check, all 15); links in range, rails to rails; ns.exe decompiles all
but East LA (known); the blocker's 12 faces in our collision with THUGPRO's flag (0x1000) and box.
`thugpro_inventory.ts --ours` before -> after: TP-8 only THUGPRO's 3,988 -> 3,932 (levelgeometry 1,296 -> 1,257, peds
505 -> 495, vehicles 43 -> 36), TP-9 collision 741 -> 676 (objects only THUGPRO's 709 -> 676, other faces 20 -> 0, other
flags 7 -> 0, other terrain 5 -> 0), geometry 2,826 -> 2,782 (collision elsewhere 14 -> 0, sectors only THUGPRO's 2,812
-> 2,782); TP-5 / TP-6 / our own unchanged. What still differs: THAW's full-archive story pieces THUGPRO shows (not hand
edits), the left-out items above, our port's own (PC-THAW-5, peds / cars our way).

Try in the game: Minneapolis' parking garage (on at start, as THUGPRO's default), Kyoto's DDR machine under its rails,
The Mall's start area (the blocker back), Santa Cruz's parking meters, Vans' / Chicago's CTF bases and flags.
