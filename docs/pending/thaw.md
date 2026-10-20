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
- (lead) The Mall: the glass doors by the start couldn't be passed (user). Two parts: (1) breakables - a LevelGeometry node whose TriggerScript (or a script it calls, 2 deep) shatters it (THAW's MA_BreakGlass = Kill + ShatterAndDie) gets THUG2's Shatter name=<it> ... + Kill as its trigger script, with its trigger faces kept (breakable_scripts; 14 in The Mall, 0-98 per level); the glass faces are already non-collidable triggers. (2) the actual wall: Z_MA_Mall_First_Rail_Blocker, an invisible collidable wall across the doors that THAW's story scripts remove - LEFT_OUT per zone (scene, nodes and now collision). NOT confirmed in the game yet.
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
has 10 fewer Multiplayer restarts (over water / kill zones: Killox01_restart*, kept by THUGPRO), CH 2 / 1 different,
BH 1 moved by THUGPRO; node arrays: THUGPRO kept 46-723 more nodes per level (story / goal / sfx nodes) - node order
doesn't travel, so no effect.
