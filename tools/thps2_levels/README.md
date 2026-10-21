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
  sounds: THUG2's default (THPS2's terrain numbers not mapped yet).
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
  for each of its gaps. THPS2's gap names and scores are in its SafeDisc-encrypted executable: named "GAP <number>",
  250 points (stand-ins).
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

Not yet (phase 3): gap names and scores (the encrypted executable), the scripted objects (waiting for signals, moving
along nodes, breaking: C_SHATTER glass, falling roof debris), the cars (Car_* kinds, C_*.psx models), the sound
emitters (ScriptOnlyBaddy), SetSkyColor / SetFadeColor (layout not decoded), flag 0x400, THPS2's music (Bink audio).
