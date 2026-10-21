# thps2 - Tony Hawk's Pro Skater 2's ten levels in THUG2 (phase 1)

Agent "thps2", branch worktree-agent-af1bef8bfb5fcc71c (continuing a stopped agent's start: its uncommitted
tools/thps2_levels/pkr.ts and thps2.ts, reviewed - layouts checked against the data, the restart / rail notes and the
transparency fixed). Workspace used: C:\Games\THUG2-3 (under its lock). NOT run by a person yet.

User decisions (2026-10-21): all ten THPS2 levels from THPS2's own data (C:\Games\THPS2\ALL.PKR) - Hangar, School II,
Venice Beach and Philadelphia too, never THUG1's or THUGPRO's remakes - as their own entries (thps2_<name>) in a
"THPS2" level select section, beside THUG1's remakes in the THUG1 section.

## What was made

- `tools/thps2_levels/pkr.ts` - ALL.PKR (PKR2: folders, files, stored or zlib) - reader and a writer for tests.
- `tools/thps2_levels/thps2.ts` - .psx worlds / texture libraries, .trg nodes (restarts, rail points); the formats in
  its header.
- `tools/thps2_levels/thps2_levels.ts` - `convert [levels] --thug2 <game> --thps2 <thps2>`, `selfcheck`, `list`
  (README.md there). Staging `thug2-decomp-thps2`, levels `thps2_hangar` ... `thps2_skateheaven`, level select numbers
  100-109.
- `thps2_levels.test.ts` - 14 tests on synthetic files (PKR round trip, a world with a textured gouraud quad and an
  invisible trigger, a texture library with magenta / 0x0000, a .trg with aligned restart / rail data, the converter's
  space, winding, uvs, colours, collision flags, textures with a BMP, restart kinds, the node array).
- Shared files (small, additive): `tools/lib/install.ts` + `tools/game/game_exe.ps1` (an install kind `thps2`, marker
  ALL.PKR), `tools/level_port/ported_levels.ts` (STAGINGS + thug2-decomp-thps2), `tools/menu_level/level_select.ts`
  (THPS2_LEVELS, a "TONY HAWK'S PRO SKATER 2" section after THPS3's), `tools/menu_level/make_menu_level.ts` (passes
  the THPS2 manifest).

## Formats found (beyond io_thps_scene's notes - documentation only, no licence)

- ALL.PKR: header "PKR2", u32 1, folder / file counts; folders {name[32], first entry offset, count}; files {name[32],
  compression (0xFFFFFFFE stored - all of the PC release), offset, size, stored size}.
- Levels: skhan Hangar, sksl2 School II, skmar Marseille, skny NY City, skven Venice Beach, skss Skatestreet, skph
  Philadelphia, skbul Bullring, skb1 Chopper Drop (its BMPs in newtex\Hawaii), skhvn Skate Heaven. THPS1's levels and
  the park editor's sets are in ALL.PKR too (not converted).
- Object positions 20.12 fixed point (vertex units); vertices i16; no rotations.
- Transparent texels: 0x0000 and magenta 31,0,31 (the cut-outs: 48,219 texels of Hangar's library). newtex BMPs (24-bit,
  twice the size, by the texture name's checksum) are black where the PlayStation texel is transparent: the converter
  takes the alpha from the PlayStation texture.
- Face flags: 0x80 not drawn (collision only - none has the 0x1000-0x4000 field all drawn faces have); 0x40
  semi-transparent (inferred); 0x20 has 4 more bytes.
- Collision ("surface"): 0x20 vert (the steep ramp tops against the 0x40 walls' feet), 0x40 invisible walls on vert
  lips, 0x02 trigger planes (3,711 of 4,332 invisible), bits 9-12 a terrain number. io_thps_scene's 0x10 wallride /
  0x40 vert / 0x100 not skatable don't fit the data (0x10 is on floors).
- Normals: a face's plane normal is -(v1 - v0) x (v2 - v0) (3,188 of 3,209 Hangar faces).
- Quads: corner order 0 1 / 2 3 (2,010 of 2,031 Hangar quads; 21 circular).
- .trg: restart {links, i32 x y z, i16 angles (4096 = a turn), name}; rail point {links, i32 x y z, u16 flags (low
  nibble 0/1/2 - a terrain?, 0x10 / 0x20 / 0x40 more)}; 32-bit data aligned to 4 in the file. Rail links go to rail
  points (and to type 12 nodes - named command nodes).

## The space (worked out in the game)

- THUG2 = (x, -y, -z) / 2.25. The reflection (x, -y, z) (the PlayStation GTE's right-handed space taken literally)
  drew Hangar's MAKE 'EM PAY sign mirrored; the half turn reads it right. 2.25: THPS2's Hangar is 2.26 times THUG1's
  remake across (25,093 units vs 11,100 inches); the skater against the ramps looks right.
- Triangles 0 2 1 / 1 2 3 (THUG2's collision takes (b - a) x (c - a) as the outward normal: THUG1 Hangar's floors 887
  up / 380 down).
- Restart heading: a node's Angles y = h faces the skater h from +z towards +x (a test: h = 90 degrees gave the
  sidecar's heading 90); THPS2's 0 faces THUG2's +z (Hangar's start: down its drop-in, seen in the game), turning
  towards -x: Angles y = -angle * 2 pi / 4096 (16 of the 20 off-axis restarts face open floor that way, 4 the other).

## Game runs (C:\Games\THUG2-3, own build's DLL, make_menu_level --game)

Each level started with `-Level thps2_<name>` (run_game.ps1), 60 frames in free skate, a capture at the skater and one
from an overview camera (`camera`), the log read: **all ten load and run** (no exception, run-time check or CRT
report; responding), the skater stands / rolls on the floor at the Player1 restart (collision works) facing the way
THPS2 starts him (Hangar: down the drop-in into the hangar; checked in a capture). Pictures in
C:\Games\THUG2-3\Screenshots\thps2_<name>_start / _over (frame_0001.png + series.txt), Hangar's sign test in
h_sign2, the level select in levelselect_a (THPS2 section: ten signs - THUG2 sign textures as placeholders - stars
on the globe, HANGAR first).

| Level | Loads | Skater at start (THUG2 x, y, z) | Scene / collision objects | Textures (from newtex) |
|---|---|---|---|---|
| Hangar | yes | (-445, 274, -362) rolling down the drop-in | 354 / 382 | 88 (56) |
| School II | yes | (701, 0, -2836) | 948 / 1000 | 202 (178) |
| Marseille | yes | (1500, 119, -423) | 624 / 682 | 151 (131) |
| NY City | yes | (-26939, -13, -12419) | 927 / 1087 | 171 (107) |
| Venice Beach | yes | (13839, 76, -2748) | 793 / 847 | 151 (117) |
| Skatestreet | yes | (-25255, 1202, -9236) | 756 / 799 | 124 (97) |
| Philadelphia | yes | (2321, 199, -2796) | 817 / 846 | 184 (129) |
| The Bullring | yes | (236, 0, -377) | 517 / 576 | 58 (50) |
| Chopper Drop | yes | (-419, 0, -90) | 142 / 172 | 45 (36) |
| Skate Heaven | yes | (-181, 0, -593) | 512 / 564 | 91 (83) |

Restarts other than the start (offline - `convert` checks the floor under each with the converted collision): all on
the floor but Hangar's Control Room (124 inches up) and Re_Start_2P_NY (no floor: a leftover of NY's), NY's Ho_Pit_QP /
Ho_Pit_Side_Rail (~290 up - HORSE spots in the air) and Skatestreet's Re_Start_2P_SS_Wave_RollIn (180 up).

Seen in the pictures: the overviews show dark spiky shapes outside the play areas (Hangar's mountains - one model
14,000 units across, object flag 0x2000 - and similar backdrops in Philadelphia): THPS2's backdrops, drawn dark by
their vertex colours; worth a look by a player. The sky is black (THPS2 draws no sky model; THUG2's default sun shows).

What a player should try (not done by an agent - needs skating): grinding the rails (terrain sounds are metal for
all), the vert ramps (Hangar's halfpipe, Skatestreet's bowls: vert flag from surface 0x20), wallrides on walls,
semi-transparent faces (Skate Heaven's glass), invisible walls at the levels' edges, the restart list (pause menu).

## Not done (phase 2 plan)

1. **The .trg command language** (types 1, 4, 5, 6, 12, 13, 15: u16 opcodes such as 0x8000 / 0x4280 / 0x2134 /
   0x2128 / 0x4290, checksums, link lists): decode it from the files (statistics, as the formats here) - it holds the
   gaps (trigger planes, surface 0x02, linked to named nodes; rails link to type 12 nodes - their gap names), kill
   zones (water in Venice / Marseille / Skate Heaven), the level's start commands (type 4: start points, the _o
   library) and the objects' behaviour. Then: gaps as THUG2 gap scripts (TriggerType / StartGap / EndGap through
   tools/level_port/script_port.ts's forms), kill zones as SK3_KillSkater with a restart (nodes.addKillRestarts).
2. **The _o models**: moving and breakable objects placed by type 1 nodes (Hangar's helicopter and fans, Philly's
   fountain, School II's...) as LevelObjects of THPS2's own models; their movements from the commands.
3. **Terrain sounds**: surface bits 9-12 -> THUG2 TERRAIN_* (listen in THPS2 / compare textures); rail flags' low
   nibble (0 / 1 / 2) -> rail TerrainType and Type.
4. **Rendering**: a sky (THPS2's clear colour, if the level commands set one, or a THUG2 sky as a stand-in); check the
   0x40 semi-transparency guess and THPS2's other blend modes (bits 12-14?); face flag 0x400 (double-sided?); the 21-242
   quads per level whose corners go round (drawn as bowties now); lighting values for the skater.
5. **Signs**: pictograms for the ten (level_select_icons.ts), replacing the THUG2 sign textures.
6. **Peds / music**: THPS2 has no peds in these levels; its soundtrack (Music folder) as level music - with the other
   games' (docs/level-port-compromises.md section 8).
7. **Online**: THUGPRO has no THPS2-data ports (its Hangar / School II / Venice / Philly are THUG1's remakes, which
   stay in THUG1's section), so the thps2_ names have nothing to agree with; check no Load_ checksum clashes.

# Phase 2 (2026-10-21) - agent "thps2-phase2", branch worktree-agent-a30b37994b0d6aaf2, workspace C:\Games\THUG2-1

NOT played by a person yet. Every step converted into C:\Games\THUG2-1 (under its lock; main's DLL of 21:47 copied
in - no C++ changed), installed with make_menu_level --game, loaded and captured through the automation channel.

## 0. The textures (the user's report: smeared stripes, wrong strips, flat-looking faces)

Cause: THPS2's uvs are texel numbers that run past their texture - 55% of the ten levels' textured faces; u up to
255 on 16-texel textures; the spans mostly n x size - 2 (126, 190, 254 on 64-wide). Phase 1 mapped every number at or
past the last texel to the far edge and clamped: a face over 0..190 of a 64-wide texture drew one column stretched
(stripes), 37..95 drew a sliver (a wrong strip), all past it one texel (flat colour). Now uv = texel / size, linear
(shared edges stay continuous), and a texture any face repeats is wrap-addressed (the rest clamped). Checked: the uvs'
corner order is right (of 6 permutations per triangle / 24 per quad, the identity gives the squarest texels for 80-86% of
faces), neighbouring faces' uvs differ by whole tiles at shared vertices. Decals and painted lines get a z bias
(overlay_bias, 3-25 layer materials a level). Before: C:\Games\THUG2\Game\Screenshots\thug2_2026-10-21_21-42-37 / -46 /
-53 / 21-43-03.png (the user's). After, the same spots: C:\Games\THUG2-1\Screenshots\fix2_thps2_hangar,
fix2_thps2_school2, fix2_thps2_marseille, fix2_thps2_venice (frame_0001.png): Marseille's hills, Venice's palm trunks
and ground, School II's walls fixed. What stays streaky is THPS2's own mapping: low-resolution decals stretched over
large faces (School II's dirt patch by the start: 64x32 texels over 220 inches - an offline render of the data,
out\scripts\topdown.ts, shows the same), a few faces whose uvs are degenerate in the data (33 in School II).

## 1. The .trg command language

The game ships its trigger symbol file in ALL.PKR - data/symbols.tdf ("symbol definitions for use in defining TRG
files", generated from Neversoft's symbols.s): node types (1 Baddy, 2 Crate, 4 AutoExec, 5 PowrUp, 6 CommandPoint, 8
Restart, 10 RailPoint, 12 TrickOb, 13 CamPt, 15 AutoExec2), the command names and numbers (SendPulse 3, SendKill 11,
KillBruce 152, SetRestart 140, BackgroundCreate 171, GapPolyHit 201, SetSkyColor 202...), the object script language
(C_* 0x41xx-0x47xx, V_* 0x21xx) and the terrain names T_CONCRETE 0 ... T_SIDEWALK 15. (Game data, not source code; the
executable itself is SafeDisc-encrypted - nothing read from it.) The argument layouts were worked out from the data;
every command list of the ten levels parses to its 0xFFFF (selfcheck). thps2.ts: readCommands, commandPointOf,
restartCommands, startCommands, objectOf.

A command point's u32 is the name checksum of the world model whose surface-0x02 faces trigger it (89 of 90 in
Hangar; the others are in the split-screen world _2.psx only - 85 in Venice, 37 in the Bullring: 2P-only, left out).
Triggers converted: Hangar 36, School II 115, Marseille 126, NY 108, Venice 187, Skatestreet 68, Philadelphia 139,
Bullring 211, Chopper Drop 13, Skate Heaven 158 - 371 gaps, 127 kill zones.

- Gaps: GapPolyHit(other, gap number). Numbers per level (Hangar 2700-2721, NY 22400-22440...; +10000 / +20000 the
  other kinds, as THPS1's table in partymod.dll shows: 0x13 air, 0x11, 0x19 grinds). Names and scores are in the
  encrypted executable (partymod.dll carries THPS1's table only): stand-ins "GAP <number>", 250 points.
- Kills: verified in the game - the skater teleported onto Venice's sea (8000, 37, 6000) is put back at the start 20
  frames later (C:\Games\THUG2-1\Screenshots\kill_t0 / kill_t20).
- Gaps not verified in the game (a gap needs the skater in the air through two triggers - a player's job).

## 2. Terrains: as in the README; LoadTerrain loads DIRT, ROCK, GENERIC1 / 2 too. Not heard in the game (a player).

## 3. Skies: eight panoramas; captures C:\Games\THUG2-1\Screenshots\sky_thps2_* (Marseille's sunset, Venice's blue,
School II's pale sky), hvn_sky (Skate Heaven's space and Earth). Skate Heaven's six background models have every
face flagged 0x80 ("not drawn" in the world): taken as an empty sky scene, loading it crashed THUG2 (0x004C62CA, a
null read) - found by the final load check; a background's 0x80 is now ignored, an empty sky is never written and a
sky's untextured materials draw a 1 x 1 white texture. Final load check (load_check.ps1): all ten load and run. The "dark spiky shapes": Hangar's mountains are a world object (flag 0x2000 - set on 140 large
objects of six levels, not a backdrop flag), drawn dark by THPS2's own vertex colours: kept.

## 4. Objects: 195 platform objects (kind 402) with _o models: 5 static, 104 bouncy, 8 spinning converted; 77 scripted
left (Hangar 30, Chopper Drop's 17 moving ones...), 1 turned about x / z left. Venice's roof fans turn
(C:\Games\THUG2-1\Screenshots\ventD: frames 1 and 6 differ), the bouncy oil cans stand in Hangar (han_over).

## 5. Checks

- Flag 0x40: the PlayStation's semi-transparency; bits 8-9 of the flags are its blend mode (only ever set with 0x40:
  mode 1 on 431 faces with dark vertex colours - added light beams, now THUG2's add). The PC's textures carry no
  semi-transparency bit on any texel, so whole faces blend.
- Flag 0x400 (2,573 faces of six levels): textured, visible, solid, any orientation, as open-edged as other faces -
  not double-sided; meaning not found, ignored.
- Bowties: 780 quads (8-232 a level) have their corners going round; split along 0-2 now.
- Off-floor restarts: Re_Start_2P_NY in Hangar (no floor at all: NY's leftover) left out; Hangar's Control Room (124
  inches up), NY's Ho_Pit_QP / Ho_Pit_Side_Rail (~290) and Skatestreet's 2P wave roll-in (180) kept - THUG2 drops the
  skater onto the floor below as THPS2 does. Player2 is the start node's SetRestart2 (Hangar had two Player2s).

## 6. Signs: windsock, pencil, sailboat, Empire State, roller skate, helmet, pretzel, bull, volcano, halo and wings
(C:\Games\THUG2-1\Screenshots\ls_up2: the THPS2 section). Music: THPS2's Music\LTIX*.dat are Bink audio - not trivial.

## For the user to try
1. Gaps: Hangar's (GAP 2700... - over the halfpipes), School II's, any level's - does the HUD show "GAP <n>"?
2. Kill zones: Venice's sea, the Bullring's pens, Skate Heaven's lava - put back at the right restart?
3. Terrain sounds: Skatestreet's wood, Bullring's dirt, Venice's sand / grass, rails' wood / concrete grinds.
4. The textures in all ten (anything still smeared that THPS2 doesn't smear?).
5. The skies, the spinning fans (Venice's rooftops), the bouncy props (pass-through).

## Phase 3
Gap names / scores (a gap list from a running THPS2, or the user's); the scripted objects (the object script language:
C_WAIT_FOR_SIGNAL, C_MOVE_TO_NODE paths of ScriptPoints, C_SHATTER glass, V_VELOCITY falls) driven by the trigger
pulses; the cars (Car_* objects on ScriptPoint paths, C_*.psx models); sound emitters (ScriptOnlyBaddy: C_PLAY_SFX /
looping sounds - THPS2's audio\*.wav); SetSkyColor / SetFadeColor as THUG2 fog; flag 0x400; music (Bink audio).

Shared files changed: tools/menu_level/level_select.ts (THPS2 table icons), tools/menu_level/level_select_icons.ts
(ten pictograms) - additive.

# Gap names from the executable (2026-10-21) - agent "thps2-gaps", branch worktree-agent-adfbcd13a0825e80b

The user's note: C:\Games\THPS2 has THPS2.exe and THawk2.exe, "one of them is unencrypted". Checked (read only):
**neither is**. Both are 2,193,512 bytes, SafeDisc 2.05.030-wrapped (the loader's "BoG_ *90.0&!!  Yy>" signature at
0xFD4 with version words 2 / 5 / 30; sections stxt774 / stxt371; entry 0x25DC1FD in stxt371). .text (0x1000-0x115000)
and .data (0x121000-0x15D000) are encrypted: 7.95-7.99 bits a byte in every 4 KB block, almost no zeros, no strings
(only .rdata's imports / Bink names and the PDB path H:\TonyHawk\PC2\Release\THawk2.pdb are plain). THPS2.exe is
THawk2.exe with PartyMod's partypatcher BPS patch applied - 35 bytes differ (15 at 0xF4E10 in .text, 20 at 0x158A6D in
.data: literal bytes such as "ymod.dll\0" written over ciphertext). partypatcher's patch expects an input of crc32
d8e631f0 (output 87c66f76); the user's THawk2.exe is 3d908efd, THPS2.exe fb775fe5: PartyMod was made for an unwrapped
THawk2.exe of the same size, and the user's patched THPS2.exe is not what PartyMod expects (and still encrypted). The
loader decrypts only at run time, with the disc and secdrv.sys - the secdrv service here is registered but its driver
is missing / blocked (exit 1275), so THPS2 can't run on this machine to read the table from memory either. Nothing was
decrypted (no DRM circumvention); ALL.PKR has no gap list (its only gap names are the park editor's sample parks').

What was made anyway (so a readable executable just works):
- `tools/thps2_levels/gap_table.ts`: probeExe (PE, SafeDisc version, encrypted data by entropy), findGapTable (a
  table of fixed-size records holding a gap number the levels use - u16 / u32 -, a name - pointer into the image or
  inline text - and a score, found at any record size 4-64, any field order; 4+ used numbers, names in 9 of 10
  records, scores mostly multiples of 25 - no false table in THUG2.exe or THPS3.exe with THPS2's 341 gap numbers),
  loadGapTable (the first readable executable of THawk2.exe / THPS2.exe / --thps2-exe; a report).
- thps2_levels.ts: EndGap's score / text from the table (quotes made safe), else "GAP <n>" / 250 per gap; convert's
  report says what each executable is and how many gaps each level got named; `gaps [levels] [--thps2-exe F]` lists
  each level's gap numbers, scores, names; `--thps2-exe` for convert.
- Tests: gap_table.test.ts (6: a SafeDisc-like encrypted exe recognised and yields nothing, a readable exe, pointer
  records, inline-name records, unrelated data - a plain number list, 3 named records - no table, loadGapTable's order
  and fallback report) and the node-array test with a named gap (score 500, text with quotes).

Result: **0 of 341 gaps named** in every level (the stand-ins, as before): Hangar 15, School II 38, Marseille 30, NY 39,
Venice 41, Skatestreet 30, Philadelphia 37, Bullring 31, Chopper Drop 8, Skate Heaven 72 gap numbers. Converted into
C:\Games\THUG2-1 (under its lock) and installed with make_menu_level --game; the scripts are unchanged against the
previous converter (decompiled hangar / venice / ny .qb identical but for ns.exe's checksum-table order - its .qb isn't
byte-deterministic). No game run (nothing changed in the game's data). npm typecheck / lint / test (257) pass,
selfcheck 1275 files 0 failures.

For the user: the gap names need a readable THawk2.exe - the unwrapped one PartyMod's patcher expects (crc32
d8e631f0, the same size). With one anywhere on disk: `node tools/thps2_levels/thps2_levels.ts gaps --thps2-exe <it>`
shows what the finder makes of it (layout + names per level); if it names them, convert with the same option (or put
it in the THPS2 folder as THawk2.exe - the user's own choice; agents never write there). If the finder finds nothing
in a readable exe, its layout differs from the records assumed: look at the strings around a known gap name there.

# Phase 3 (2026-10-22) - agents "thps2-phase3" (stopped by accident) and "thps2-phase3b" (its continuation), branch worktree-agent-a22bcae150da2e941, workspace C:\Games\THUG2-1

The user's report (on phase 2): "THPS2 textures are for the most part OK. There is some Z fighting. In e.g. Hangar, the
glass does not shatter, and the door to the outside is not open. We may not have imported scripts." NOT played by a
person yet. The first agent's work (thps2_scripts.ts, the per-face layers, turned objects, automation's
`teleport ... board`) was merged and its uncommitted files brought over; the continuation found the strip bug, tested
in the game and wrote this.

## 1. The levels' logic (tools/thps2_levels/thps2_scripts.ts - README "Phase 3")

The .trg's command points and the objects' scripts as THUG2 scripts: the start's commands followed once offline (what
exists at the start), triggers running their command points (pulses, signals, kills, loud kills shattering crates,
SendVisible, pulse budgets), the object script language translated (waits, signals, registers, jumps, velocities,
spins, script-point paths, shattering, dying), timers' links pulsed when they end. Career-only blocks left out.

| Level | point scripts | object scripts | script-only | crates (glass) | created at start / later |
|---|---|---|---|---|---|
| Hangar | 42 | 30 | 1 | 26 (22) | 43 / 24 |
| School II | 5 | 6 | 0 | 2 (1) | 24 / 0 |
| Marseille | 37 | 3 | 0 | 17 (1) | 14 / 0 |
| NY City | 13 | 2 | 1 | 5 (0) | 26 / 0 |
| Venice Beach | 15 | 2 | 7 | 0 | 27 / 4 |
| Skatestreet | 6 | 0 | 0 | 0 | 30 / 0 |
| Philadelphia | 16 | 5 | 0 | 9 (3) | 30 / 0 |
| The Bullring | 85 | 2 | 0 | 53 (0) | 4 / 0 |
| Chopper Drop | 9 | 17 | 1 | 0 | 8 / 12 |
| Skate Heaven | 6 | 4 | 7 | 2 (0) | 5 / 0 |

Left out (counted per level in convert's report): sounds (THPS2's audio isn't converted - 627 sound commands in all),
sparks, FMV, screen flashes, water levels, goal counters, gravity (Skate Heaven's 4 thrown rocks fly straight, their
random velocities at the middle), visibility boxes (THPS2's draw culling), fog / sky colours.

**Hangar's "door to the outside"**: the door at the end of the wind tunnel (the halfpipe with the propeller) is closed
at the start in THPS2's free skate too - the data: the start pulses node 250, which creates the two propeller blades
(objects 96 / 105) waiting for a signal; the door (crate 424, world object 172) and the way out stay until trigger
538139a1 - a strip across the door at the propeller hub's height, 220-290 inches above the halfpipe's floor - is met:
its point 104 signals the blades (they spin up, slow, stop), loudly kills the door (it shatters) and creates the big fan
outside (object 167, turning for good) and four more objects. No career flag on any of it. So a player opens it by
flying into the door at the hub's height.

## 2. Z-fighting: the strips' stray triangles

Most of it wasn't coplanar layers but the strips: trisToStrip (THPS2's, copied from THPS3's) joined triangles with
last, a (and a again only for parity), which left (last, a, b) a real triangle at every other join - drawn over the
next face's first triangle with the previous face's material and uvs, coplanar with it, so the two fought. Now last,
last, a, a (+ a for parity): every join triangle degenerate (a test checks a strip draws exactly its triangles). The same
fix in tools/thps3_levels/thps3_levels.ts (the THPS3 port had the same stray triangles; thps3_peds.ts already joined
its own strips right and its comment says so now). Also: the per-face layer search bucketed faces by rounded normal
components, so near-coplanar faces whose normals straddled a rounding step were never compared - now by the normal's
main axis and sign.

Measured offline (`layer_check.ts`' overlap: square inches of drawn surface lying on another material's with the same z
bias - what can fight; out\scripts\fights.ts in the worktree prints it), phase 2 -> now:

| Level | phase 2 | now |
|---|---|---|
| Hangar | 1,386,561 | 0 |
| School II | 3,235,677 | 6,289 |
| Marseille | 600,043 | 2,439 |
| NY City | 5,843,910 | 0 |
| Venice Beach | 3,344,636 | 234,551 (LevelObjects' own spaces overlapping at the origin: not real) |
| Skatestreet | 1,078,508 | 0 |
| Philadelphia | 5,065,131 | 0 |
| The Bullring | 1,156,431 | 23,279 (one object's faces 0.8 / 1.7 inches apart) |
| Chopper Drop | 36,368 | 0 |
| Skate Heaven | 2,783,541 | 0 |

(frame_diff.ts on drifting-camera captures can't separate this from ordinary edge crawl: the first agent's Hangar
captures before / after its layer change measured 1.66 / 4.66 / 4.11 / 2.47% flickering pixels vs 1.66 / 4.67 / 3.95 /
3.52% - unchanged within noise; the offline measure above is the one that tells.)

## 3. Game runs (C:\Games\THUG2-1 under its lock; this branch's DLL - automation's `teleport ... board`)

Captures in C:\Games\THUG2-1\Screenshots:
- hd_before / hd_open: the wind tunnel's door closed at the start, propeller still (walking into its foot does
  nothing: the trigger is high up). hd_trig20 / hd_trig120 / hd_trig520 / hd_through: its trigger script run
  (automation `run thps2_hangar_Trigger_538139a1`) - the door shatters, the propeller spins, the way through is open.
- hg_before / hg_shatter (30 frames) / hg_gone: two glass panes' triggers run - the lower row above the quarter pipe
  shatters (THUG2's glass pieces) and is gone.
- cd_t0 ... cd_t25 (5 s apart): Chopper Drop - a boat comes past the ship's tower and leaves (its path), the start's
  drop gap scored ("GAP 1200").
- Load check of all ten: LOADCHECK
The first agent's captures there: h3_door_*, h3_glass_*, h3_event, z_before_* / z_after_*.

## For the user to try
1. Hangar: fly into the wind tunnel's end door at the propeller's height - the door should shatter, the propeller spin
   up and the fan outside turn; the way out open.
2. Hangar: air from the quarter pipes through the glass panes above them - each pane shatters (glass sound) when flown
   through; the control room's windows don't break (THPS2's don't either).
3. Hangar: the roof debris (its trigger, bb62e280) - pieces fall, shake the camera and shatter.
4. The z-fighting: any surface still flickering? (Hangar, NY, Philadelphia, Skate Heaven had the most.)
5. Chopper Drop's boats / planes, Venice's timers, Skate Heaven's thrown rocks (straight, no gravity), the Bullring's
   crates (53, killed / shown by its triggers).

## Not done
Sounds (THPS2's WAVs and the sound emitters), cars (Car_* objects), gravity, SetSkyColor / SetFadeColor, gap names
(encrypted executable), music. A trigger crossing in flight (gaps, glass, the door) can't be scripted by an agent: the
trigger scripts were run directly.

Shared files changed: tools/thps3_levels/thps3_levels.ts (trisToStrip's join), tools/thps3_levels/thps3_peds.ts (a
comment), src/game/front/automation.cpp + test_view.cpp / .h (`teleport <x> <y> <z> [heading] board` - the first
agent's).
