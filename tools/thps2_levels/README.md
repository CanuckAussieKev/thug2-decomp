# thps2_levels - Tony Hawk's Pro Skater 2 levels in THUG2

Converts the ten levels of your own THPS2 (PC) install to THUG2's formats and puts them in THUG2's level select (its
THPS2 section: Up / Down in the level select switches games). Every level is THPS2's own data - not THUG1's or
THUGPRO's later remakes of Hangar, School II, Venice or Philadelphia (those stay in their own sections): the user's
decision, 2026-10-21. Nothing of either game is in the repo: the files are made from your copies each time.

## Running it

Needs Node 24+ (`npm install` once - docs/typescript.md), NeverScript (`tools\build_neverscript.ps1` ->
`out\neverscript\ns.exe`) and, to install the levels, tools/menu_level/make_menu_level.ts.

    node tools/thps2_levels/thps2_levels.ts convert [levels...] [--thps2 C:\Games\THPS2] [--thug2 C:\Games\THUG2]
    node tools/menu_level/make_menu_level.ts [--game C:\Games\THUG2]

`convert` reads `<thps2>\ALL.PKR` (the PC version keeps every data file in it) and writes `<thug2>\thug2-decomp-thps2\`
(not read by the game): `levels\thps2_<name>\thps2_<name>.scn.xbx / .tex.xbx / .col.xbx / .qb` and `levels.json`. Name
levels to convert only some (`convert hangar venice` - our names, THPS2's file names `skhan` or `thps2_hangar` work
too); the default is all ten. `--no-mips` skips the mip levels. `make_menu_level.ts` then copies them into the data
overlay and adds them to `Levels.qb` and the level select's THPS2 section (run it after every `convert`; converting
only some rewrites `levels.json` with only those - finish with a full convert).

    node tools/thps2_levels/thps2_levels.ts selfcheck
    node tools/thps2_levels/thps2_levels.ts list

`selfcheck` parses every file of the ten levels (worlds, texture libraries, low-detail and object files, nodes,
newtex BMPs) and round-trips THUG2's LA / TestLevel files; `list` shows the levels and what ALL.PKR has of each.

| Level | THPS2 file | THUG2 level (Load_...) | Level select |
|---|---|---|---|
| Hangar | skhan | thps2_hangar | 100 |
| School II | sksl2 | thps2_school2 | 101 |
| Marseille | skmar | thps2_marseille | 102 |
| NY City | skny | thps2_ny | 103 |
| Venice Beach | skven | thps2_venice | 104 |
| Skatestreet | skss | thps2_skatestreet | 105 |
| Philadelphia | skph | thps2_philly | 106 |
| The Bullring | skbul | thps2_bullring | 107 |
| Chopper Drop | skb1 (newtex\Hawaii) | thps2_chopperdrop | 108 |
| Skate Heaven | skhvn | thps2_skateheaven | 109 |

ALL.PKR also has THPS1's levels (skware, skschl, skmall, skdown, skburn, skjam, sksf, skros, skvans, skfway - PC
leftovers) and the park editor's sets (sked1-4): not converted.

## THPS2's formats

`pkr.ts` (ALL.PKR) and `thps2.ts` (.psx worlds and texture libraries, .trg nodes) have the layouts in their header
comments. Format notes from io_thps_scene's `import_thps2.py` (a Blender importer for THPS1 / THPS2 scenes, no
licence: read as documentation only, none of its code) were checked against the files; thps-formats (Morten1337, MIT)
has THPS2 only as an enum, no reader. What the data showed beyond those notes: the transparent magenta and 0x0000
texels, the newtex BMPs (by the texture's name checksum), the meaning of face flags 0x80 / 0x40 and the collision's
0x20 (vert) / 0x40 / 0x02 / terrain bits, the restart and rail node layouts.

## What's converted (phase 1)

- Scene: each world object (a model at a position - THPS2 never turns them) a sector, its faces per material (a
  texture and whether it's semi-transparent), quads split in two; faces flagged 0x80 (collision only) not drawn.
  Vertex colours as THPS2 lit them (gouraud from the RGBs palette, else the face's flat colour; 128 = neutral in both
  games).
- Space: THPS2's vertex units, y down, as THUG2's inches, y up: (x, -y, -z) / 2.25 - a half turn about x (the
  reflection (x, -y, z) drew Hangar's MAKE 'EM PAY sign mirrored). 2.25 units an inch: THPS2's Hangar is 2.26 times
  THUG1's remake of it across.
- Textures: the PC version's newtex BMPs (24-bit, twice the PlayStation's size) where there is one, with the
  PlayStation texture's transparency as their alpha; else the PlayStation texture. 32-bit with mip levels; transparent
  texels take a neighbour's colour (no dark fringes when filtered).
- Collision: an object per world object, every face but the trigger planes and the invisible walls on vert lips;
  vert (surface 0x20) as THUG2's vert, steep faces wallridable, invisible walls let the camera through. Terrain
  sounds: THUG2's default (THPS2's terrain numbers not mapped yet). Ramps (2026-10-23, level_port/ramp_collision.ts):
  THUG2 takes an unflagged face steeper than ~65 degrees as a wall, so the tops of THPS2's non-vert quarter pipes
  (67 - 73 degrees) stopped the skater; a steep face (up to ~84 degrees) joined concavely to a rideable face below it
  with a bend of 40 degrees or less - across objects too - is flagged vert (and so on up the ramp), and a face
  lying on a more rideable one (NY's decal models over its vert walls) takes that face's flags. Vert as THUG2's own
  quarter pipes have it (2026-10-24): from ~45 degrees up across the whole ramp (unflagged ramp faces of 45 degrees
  or more under a vert face are vert too), never under 40 (THPS2's vert bit on low transitions is left out - an ollie
  there was vert air and a bail). Only flags change: the drawn scene and the geometry are the same.
  `node tools/level_port/ramp_check.ts <levels folder>` measures it, `--flags` compares a port's flags by slope with
  THUG2's own levels' (docs/pending/thps2-quarterpipes.md).
- Nodes: restarts (the level's start first as Player1; 2P, HORSE and the rest as THUG2's types; THPS2's "Dummy" ones
  left out) and rails (every rail point a RailNode linked to the next ones). `convert` reports restarts with no floor
  under them or high above it.
- Level structure: THPS2's name; no sky (THPS2's backdrops are part of its worlds), THUG2's default lighting, no
  music, no goals.

## Phase 2 (2026-10-21)

- Textures tile: THPS2's uvs are texel numbers that run past their texture (n x size - 2 spans), so uv = texel / size,
  linear, and a texture some face repeats is wrap-addressed (the others clamped). Phase 1 collapsed every number past
  the texture onto its last texel - the smeared stripes. Layer materials (decals, painted lines) get a z bias
  (level_port/overlay_bias.ts).
- The .trg command language (thps2.ts): node types and command names from the trigger symbol file the game ships in
  ALL.PKR (data/symbols.tdf), argument layouts from the data (every list of the ten levels parses to its end; `selfcheck`
  checks). Command points (type 6) are run by the trigger faces of the world model their checksum names.
- Gaps: GapPolyHit(other, gap) - meeting the trigger after `other` - as THUG2 trigger faces (both windings, flags
  0x1150) in a collision object per trigger, a GenericNode per trigger and a script: EndGap then StartGap (CANCEL_GROUND)
  for each of its gaps. Names and scores: below.
- Kill zones: KillBruce in a trigger's list (or a command point it pulses) - SK3_KillSkater back to the restart it
  pulses (a "Dummy" one becomes a typeless restart node), else the nearest.
- Level start: SetRestart / SetRestart2 of the start node pick Player1 / Player2. Restarts with no floor at all are
  left out.
- Terrains: the collision's terrain numbers (symbols.tdf's T_CONCRETE ... T_SIDEWALK) as THUG2's Terrain_Types; rail
  points' flags 0 / 1 / 2 concrete / metal / wood; LoadTerrain loads them.
- Skies: BackgroundCreate(model) - a panorama model of the _o file - as <level>_sky (eight levels; Hangar and
  Skatestreet have none).
- Objects: the _o file's platform objects (type 1, kind 402) as LevelObjects of THPS2's models: static, bouncy (drawn,
  not collided with) and spinning (V_ANGULAR_VELOCITY: Obj_RotX / Y / Z) ones.
- Faces: semi-transparent faces in their PlayStation blend mode (bits 8-9: 0 half, 1 added); quads whose corners go
  round split along 0-2 (no bowties).
- Signs: pictograms of their own (tools/menu_level/level_select_icons.ts).

## Gap names and scores (gap_table.ts)

THPS2's gap table (names, scores) is in its executable, not in ALL.PKR - the .trg files only number the gaps (Hangar
2700-2721, School II 3300-3503, NY 22400-22440...). `convert` looks in `<thps2>\THawk2.exe`, then `THPS2.exe` (or
`--thps2-exe <file>`): a readable one's table is found by its records (a gap number the levels use, a name - a pointer
or the text -, a score; any record size and field order) and its names and scores go into the gaps' EndGap; a gap it
doesn't name, or no readable executable, keeps the stand-in "GAP <number>", 250 points. `gaps` shows what was found:

    node tools/thps2_levels/thps2_levels.ts gaps [levels...] [--thps2-exe <file>]

The PC release's THawk2.exe is SafeDisc 2.05.030-wrapped: its .text and .data are encrypted on disk (7.99 bits a
byte, no strings) and decrypted only by the SafeDisc loader at run time, which needs the disc and its driver
(secdrv.sys - blocked on Windows 10 / 11). PartyMod's THPS2.exe is that file with partypatcher's 35-byte patch, so
encrypted too (partypatcher expects a THawk2.exe of crc32 d8e631f0 - an unwrapped one; the retail one is 3d908efd).
Nothing here decrypts anything: with those two, every gap keeps its stand-in. The table's layout is still unknown (no
readable executable seen): the finder is checked on synthetic executables only.

## Phase 3 (2026-10-22): the levels' logic (thps2_scripts.ts)

THPS2's level logic - the .trg's command points and the objects' scripts - as THUG2 scripts in the level's .qb (the
NeverScript text is written to `<staging>\source\<level>.ns` to read):

- The start: the start restart's list is followed once (offline): the objects its pulses create are CreatedAtStart, the
  crates / rail points it hides or kills aren't, the pulses it uses come off the points' budgets (SetInitialPulses).
  Script-only objects created at the start (timers) are spawned by the level's setup_script.
- Triggers: a trigger face runs its command points (`<level>_CP_<node>`): SendPulse (a point runs, an object is created
  and starts its script), SendSignal (a global the waiting object polls), SendKill / SendKillLoudly (objects die; a
  crate - a named world object, a LevelGeometry node `TRG_Crate_<node>` - is killed, a loud kill shatters it once:
  Hangar's glass with THUG2's glass sound), SendVisible (a crate's geometry and collision taken away / put back).
  IfCareerFlag / IfCareerGoal / IfPulseCount blocks are career-only: left out (free skate).
- Glass: a crate whose drawn faces are all semi-transparent and that some trigger breaks lets the skater through (its
  collision non-collidable, as THUG2's own breakable glass) - he flies through it, its trigger fires, it shatters.
- Objects: the object script language (C_* / V_* tokens of symbols.tdf) translated - waits, signals, registers,
  labels and jumps (a loop over labelled blocks), V_VELOCITY / V_ANGULAR_VELOCITY / V_ANGULAR_ACCELERATION as
  Obj_MoveToRelPos / Obj_Rot*, C_MOVE_TO_NODE along script points, C_SHATTER, C_DIE_QUIETLY, C_SHAKE_CAMERA, pulses and
  signals to nodes and links; a timer's links pulsed when it ends. Objects turned about x / z get a turned copy of
  their model.
- Left out (counted in the report): sounds (THPS2's audio isn't converted), sparks, FMV, screen flashes, the water
  level, goal counters, gravity (thrown rocks fly straight), visibility boxes (THPS2's draw culling), fog / sky colours.

Hangar as THPS2's free skate has it: the wind tunnel's end door is closed at the start, its propeller still; flying into
the door at the propeller's height (the trigger strip there) breaks the door, spins the propeller up and starts the big
fan outside - the way out. The glass panes above the quarter pipes break when flown through; the roof debris falls
after its trigger.

Z-fighting: each drawn face lying on another (coplanar, inside it) gets a z bias one above it (per face: faceLayers);
exact duplicate triangles are left out. And the strips: until 2026-10-22 trisToStrip's joins left a real triangle
(last, a, b) at every other join, drawn over the next face with the previous face's material - most of what the user
saw as z-fighting (same-bias coplanar overlap: Hangar 1.39M -> 0 square inches, NY 5.84M -> 0, Philadelphia 5.07M -> 0,
Skate Heaven 2.78M -> 0 - `node tools/level_port/layer_check.ts`). THPS3's trisToStrip had the same join, fixed too.

Not yet: gap names and scores (a readable THPS2 executable), the cars (Car_* kinds, C_*.psx models), sounds and the
sound emitters (ScriptOnlyBaddy; THPS2's audio), SetSkyColor / SetFadeColor (layout not decoded), flag 0x400, THPS2's
music (Bink audio).
