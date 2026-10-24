# thp8_levels - Tony Hawk's Project 8 levels in THUG2

Converts the levels of your own Tony Hawk's Project 8 data (the Xbox 360 version's, as a fan-made native recompilation
installs it: `C:\Games\THP8\game\DATA` - only the game data is read, never the recompilation's own programs) to
THUG2's formats and puts them in THUG2's level select (a "TONY HAWK'S PROJECT 8" section: Up / Down switches games).
Nothing of either game is in the repo: the files are made from your copies each time.

## Running it

Needs Node 24+ (`npm install` once - docs/typescript.md), NeverScript (`tools\build_neverscript.ps1` ->
`out\neverscript\ns.exe`) and, to install the levels, tools/menu_level/make_menu_level.ts.

    node tools/thp8_levels/thp8_levels.ts convert [levels...] [--thp8 C:\Games\THP8] [--thug2 C:\Games\THUG2]
    node tools/menu_level/make_menu_level.ts [--game C:\Games\THUG2]

`convert` writes `<thug2>\thug2-decomp-thp8\` (not read by the game): `levels\thp8_<level>\thp8_<level>.scn.xbx /
.tex.xbx / .col.xbx / .qb`, `levels\thp8_sky` (THP8's default sky) and `levels.json`. Name levels to convert only some
(`convert net_skatecentral training` - `net_capitol`, `capitol` or `thp8_net_capitol`; `z_training`, `training` or
`thp8_training` all work); the default is all fifteen (~2.5 min; the whole town ~1 min of it). Converting only some
updates their entries in `levels.json` and keeps the others; every convert removes the levels it no longer makes (the
city's zones as levels of their own, from before 2026-10-24: `thp8_houses` ... - their staging folders and levels.json
entries; make_menu_level makes the overlay again from the staging folders, so they leave it too). `--no-mips` skips the
mip levels (faster). `make_menu_level.ts` then copies them into the data overlay and adds them to Levels.qb and the
level select.

    node tools/thp8_levels/thp8_levels.ts selfcheck
    node tools/thp8_levels/thp8_levels.ts list

`selfcheck` reads every file of every zone archive (scenes and their strips, textures' base levels untiled,
collision, qb / node arrays) and round-trips THUG2's LA / TestLevel files; `list` shows the levels (what each is made
of).

THP8's levels as THUG2's - THP8's own set: its network levels (its own list: qb.pak's
`online_level_select_menu_level_info`, each a `multiplayer_level_<x>` structure - text, the zones whose network spawns it
uses, `spawn_in`, its barrier archive, `area_pak`), the first of them BEAVER VALLEY, the whole town (THP8's open world),
and its standalone levels. Each network level is its zones merged in THP8's shared frame, with its barriers (collision
only) and its zones' network spawns; the city's other zones around it as THP8 shows them from afar (their `_lod`
stand-ins, drawn only). All are in the level select's THP8 section - free skate and the network level select alike.

| THP8 level | THUG2 level (Load_...) | Zones (the first's lights) | Barriers | Pictogram | Level select |
|---|---|---|---|---|---|
| BEAVER VALLEY (the whole town) | thp8_net_beavervalley | houses, cretepark, shops, center, citypark, ghetto, funpark, cartoon, factory, parking, inschool, outschool | none (THP8 has none) | citymap | 113 |
| SKATE PARK | thp8_net_skatepark | cretepark | z_online_skatepark | bowl | 114 |
| FACTORY | thp8_net_factory | factory | z_online_factory | gear | 115 |
| SLUMS | thp8_net_slums | ghetto | z_online_ghetto | bricks | 116 |
| SUBURBIA | thp8_net_suburbia | houses | z_online_suburbs | mailbox | 117 |
| MAIN STREET | thp8_net_mainstreet | shops | z_online_shops | trafficlight | 118 |
| FUN PARK | thp8_net_funpark | funpark, cartoon | z_online_funpark | balloon | 119 |
| SCHOOL | thp8_net_school | outschool, inschool | z_online_school | books | 120 |
| INDUSTRIAL ZONE | thp8_net_industrial | factory, ghetto | z_online_industrial | chimneys | 121 |
| CAPITOL BUILDING | thp8_net_capitol | citypark, center, parking | z_online_capitol | pediment | 122 |
| SKATE & FUN PARKS | thp8_net_skatefunparks | funpark, cretepark, cartoon | z_online_funskatepark | ticket | 123 |
| SKATE CENTRAL | thp8_net_skatecentral | center, houses, shops, citypark, parking | z_online_central | compass | 124 |
| TRAINING | thp8_training | training | | target | 110 |
| SKATE SHOP | thp8_skateshop | skateshop (a menu set: a restart is made) | | skatetool | 111 |
| BEDROOM | thp8_bedroom | bedroom | | bed | 112 |

The standalone ones are levels of their own in THP8 too (Level_Z_Training / _Skateshop / _Bedroom, loaded alone, in
no network level's spawn_in); the city's zones (and the cartoon world, part of the fun park) are THP8's streamed open
world, not levels by themselves - until 2026-10-24 they were levels here, our own split, cut off at the zone's edge.

BEAVER VALLEY reaches ~21,000 units from its middle, past the half units THUG2 sends network positions in (+-16,384):
its entry gets IsBigLevel (whole units - src/game/net/thugpro_compat.h). It needs this project's bigger limits
(src/game/core/memory_sizes.h: the node name hash, the rail table, the script heap, the pools) - with the original's
sizes (`[memory] original_sizes=1`) it can't load. It takes ~7 minutes to load in the Debug build.

The sign texts are THP8's; the pictograms are ours (tools/menu_level/level_select_icons.ts, THP8_LEVELS in
level_select.ts).

## Render audit (z-fighting, missing textures)

    node tools/thp8_levels/render_audit.ts textures [levels...] [--top N]
    node tools/thp8_levels/render_audit.ts zfight [levels...] [--top N] [--csv FILE] [--tolerance IN] [--view M]
    node tools/thp8_levels/render_audit.ts spot <level> "(x, y, z)" [radius]
    (all: [--thp8 C:\Games\THP8] [--thug2 C:\Games\THUG2 | --staging <folder>]; levels as convert names them, the
    default all, BEAVER VALLEY first; writes nothing)

`textures` goes through every material of a level's zones and stand-ins as convertScenes draws it (`baseTexture`:
pass 0) and says what it misses of THP8's: a blank picked (a flat colour or THP8's 16 x 16 see-through dither) while
the material lists a real texture, a material with nothing but blanks, a layer drawn with a blank, layers dropped
(a blended base; a second mask with uvs of its own), a texture not in the zone's .tex (and where it is), a format not
converted, other textures of templates whose passes aren't read (environment / refraction maps) - by cause, shader
template and texture, with area and a spot; then the converted files as THUG2 sees them
(tools/level_port/texture_check.ts). `zfight` runs tools/level_port/zfight.ts over the level's .scn.xbx with THP8's
sector names and why each pair overlaps: our own stand-ins / barrier veils, two zones drawing the same thing, THP8's
decals (their depth bias isn't converted), see-through layers, duplicates in one zone (objects in their own space -
moving objects, props - are left out: the game places them). `spot` lists the meshes at a place (a screenshot
sidecar's position) with every texture THP8's material lists and what pass 0 is drawn with. Findings:
docs/pending/thp8-render-audit.md.

## The world and how it's cut

THP8's world is one city streamed in zones: each `ZONES\z_<zone>.pak.xen` has a far-away stand-in `z_<zone>_lod`, and
the zones share one frame (metres, y up). A network level merges its zones in the shared frame, moved as one (the
middle of their collision at the origin), and draws the city's other zones' `_lod` stand-ins around it. Merging: a
material of one checksum that two zones define differently (other light maps: 53 of the 276 shared across the town)
is made twice; each zone's light maps are lit by its own sun; the node arrays follow one another (Links moved up; no
node name repeats across the zones); a texture of one checksum is made once. The whole town as converted: 1.26 million
drawn triangles, 8,781 sectors, 2,791 textures, 3,346 collision objects; 81 MB scene + 64 MB textures + 3.5 MB
collision + 4.8 MB qb. THP8's network areas (`z_online_<x>`) are barrier walls, added as collision only (THP8 draws
them as a shimmer when the skater is near).

**Narrow holes.** THP8's collision has slits with nothing under them where two bodies meet (z_houses' kerbs end 0.6
m short of the road's box). THP8's skater is a Havok shape that rides over them; THUG2's stands on the one point under
it and fell through, hanging from the kerb's underside. `gapFills` floors them: on a 10-inch grid, cells with no
ground at any height between ground no more than 5 cells away on both sides (along x or z, steps of 24 inches or
less) get a floor at the lower side's height, as `<level>_GapFill_<n>` objects (BEAVER VALLEY 2,400 faces). Only void
is filled, never a dip.

Also floored: void between ground and a wall standing on it, up to 16 cells across (under a house's eaves, where the
yard ends a metre or more short of its invisible shell), looked at along x, z and the diagonals. The fill objects are
cut into 1,600-inch squares of at most 1,500 faces: THUG2's line test gathers a BSP leaf's faces into a buffer of
0x800, and a town-wide fill object overran it (a crash); convert warns of any leaf past that.

The ground is looked at in four points a cell (its quarters): a slit narrower than a cell between two cells' middles
(10 inches by the suburb's flower ring) counts; a wall in it only stops the fill when taller than a step (a kerb's face
doesn't).

**Holes audit.** Convert reports, per level, the drawn ground with no collision under it that is ringed by ground
that has (`uncoveredGround`: up-facing triangles of opaque materials - not glows, water / shimmer, the sky, barriers,
stand-ins or moving objects - on 40-inch cells, against collidable faces within 24 inches of their height, the gap
filler's floors not counted), with its largest holes. What remains is mostly drawn interiors seen through windows,
chimney and dome tops.

**Bodies named after trigger nodes.** A collision body that matches no sector is named after a primitive's node, else a
collision-only trigger node standing in its box - only when the body is a volume (all its faces trigger or
non-collidable): until round 5 a solid body could take a trigger node's name and go with it when that node isn't made
(z_houses' suburb road, 40 m: the chalk goal trigger on it).

**Invisible shells and trigger volumes.** Some buildings' only wall collision is an invisible open-top shell inside the
drawn walls, its top at the roof's eaves and gables (the Grecian houses); THUG2's skater caught on its edge under the
roof. `trimShells` cuts every all-invisible body's steep faces 6 inches under the seen roof faces over them (221 shells
in the town). THP8's 0x40 faces stay solid, invisible or not: they are ground that also triggers (the suburb roundabout's skirt is
four invisible 0x1140 ramps; 81 bodies in the town are made only of 0x40 faces). Round 4 made the invisible ones
non-collidable and the skater went through the skirt into the planter and the tree's roots (round 6) - undone.

**Stand-ins and barriers.** A zone's `_lod` stand-in also covers places of other zones (z_citypark_lod's Box03 stands
where the factory is; z_center_lod's lake plane, two triangles 900 m across, passes over the factory's floor): the
stand-ins' triangles over the level's own ground are left out (`standInOutside`, every 4 m of the triangle looked
at). The network barriers are drawn as THP8 draws them - THP8's is a shimmer (a distortion shader over a normal map),
ours a faint see-through blue (`VEIL_COLOUR`). A `_lod` archive holds instance masters (`__MASTERNODE_` sectors) too: its
node array's instances are placed (`standInPlaced`), so the masters aren't drawn stacked at THP8's origin.

**Draw-only fixes of the render audit (2026-10-24, collision untouched).** `baseTexture` passes over a blank pick (a flat
colour or a 16 x 16 dither, `materials.blankKind`) for the material's biggest real texture, none left: a dither-only
material is drawn untextured, a flat colour stays; a layer whose diffuse is a dither is left out; the scene gets
`overlay_bias`'s z bias; and a later merged zone's sector that an earlier zone draws under its own prefix with the same
box is not drawn (`zoneDuplicates`). THP8's floaters (`F_` sectors: the geometry laid over a surface - sign cells, dirt and
blend patches; the same triangles also stay in the `g_` sector they were cut from) and its other tessellated-twice
sectors (VC_ / G_ pairs) are drawn once: `repeatedSurfaces` leaves out a mesh whose surface another mesh of the same
material (a floater's first) already draws, in the same plane. A `_lod` stand-in's triangle lying on a triangle the
level draws (`standInOnDrawn`) is left out too. NormalMapBlended's own layer layout (the lawns' blended grass maps) is not read: the first grass map only.

**Seam walls.** Each zone's collision has invisible walls (every face 0x1000, most also 0x80 - the camera passes)
along its edges where the next zone begins (z_houses: a 115 m long, 70 m high plane at x = 0, where z_shops starts).
No node or sector owns them: THP8 must switch them off when the neighbour is streamed in. In a level of several zones
they stood across the streets (the user, BEAVER VALLEY 2026-10-24): `seamWalls` leaves out every all-invisible, mostly
steep body with another of the level's zones' ground 4 m beyond its faces at half or more of the places looked at
(43 in the whole town; 1-8 in the other levels of several zones). Kept: one-zone levels' (their edge) and the town's
outer ones. The separate zones: training,
cartoon, the skate shop (a menu scene: a restart is made for it), the bedroom. Not converted: z_mainmenu, z_mocap,
z_navmesh, z_peds, z_anim, z_world, z_viewer and the test levels (`z_testlevel*`).

## THP8's formats

All big-endian (the Xbox 360's) but the script bodies; read from the files (thp8.ts, geom.ts, xenos.ts, havok.ts have
the layouts in their header comments). Format notes: none public for THP8's scenes, collision or textures that we
found; thps-formats (Morten1337, MIT) has no THP8 reader. Everything here was worked out from the data, plus the
public descriptions of the Xbox 360's texture fetch constant / tiling and of Havok's packfile layout.

- **Compression**: every file under `COMPRESSED` is one raw deflate stream. `compress.toc.xen` (TOC1) lists them by
  path checksum with their sizes; `compress.toc.lst.xen` is the same as text.
- **Archives** (`.pak.xen` + `.pab.xen`): THAW's pak, big-endian. A zone's archive has the zone (flags 0) and its
  `_gfx` / `_sfx` sub-zones (flags 2 / 4), plus models (`.mdl`, `.skin`, `.stex`) of its game objects.
- **QB** (`.qb`, `.nqb`): THAW's item format with big-endian words; a structure entry's type byte is 0x80 | type (THAW:
  type << 1 | 1), and an unnamed type 0x1A entry is an include (the node arrays' `<zone>_NodeArray_compressed_node_n`
  structures) or a flag. Script bodies: LZSS'd THUG2 tokens, little-endian, with THAW's 0x4A inline structures
  (big-endian here) and two of THP8's own: 0x4B ("$": the next name's global value) and 0x4C (a wide string: byte
  count, UTF-16LE). Checksum names: `PAK\dbg.pak.xen`. English text: qb.pak's string globals (`str_z_cretepark_scripts_2
  = "Over the Steps"`, which scripts name); the other languages' in the `_f` / `_g` / `_i` / `_s` paks.
- **Scenes** (`.geom`): THAW's layout grown: 0x134-byte materials (a list of up to 16 texture checksums, a parameter
  block naming the shader template, the texture count and the terrain), then 0xBABEFACE and the scene: 0x50-byte
  sectors (the node names), 0x60-byte sector nodes (box, first mesh, mesh count), 0x80-byte meshes (sphere, material,
  vertex layout, colour / uv offsets, counts, the vertex and index blocks). Vertices: float3 position, packed normal,
  0-2 tangent words, ARGB colour, 16-bit float uv pairs (one per 2-bit field of the layout word). Indices: u16
  triangle strips separated by 0x7FFF.
- **Materials** (materials.ts has the details): a material names a shader template of `FXFILES\MaterialLibrary.bin`
  ("MATL": the templates by name checksum - DiffuseTerrain_3Pass, NormalMapTerrain_4Pass... - each with its
  techniques, "_ENGINE_Default_Pass0_InteriorLighting"...), its technique by number, then float4 parameters (pass
  colours, specular power, tilings) and its textures in the template's order: pass 0's diffuse (normal, specular)
  map, each further pass a diffuse map and a blend mask, then the zone's two light maps - an **ambient map** (sky light,
  coloured) and a **shadow map** (the sun's light), atlases shared by the lit materials (1024 / 2048 texels in the
  skate park), read through the vertex's last two uv fields. The game objects' models (`.mdl`) have the scene's
  layout, their textures (`.stex`) the .tex layout, their physics a Havok packfile.
- **Instances**: a node with `MasterName` places a copy of a `__MASTERNODE_` sector (drawn at the origin) at its Pos /
  Angles (radians, R = Ry Rx Rz - checked against the rails along instanced fences) / scale.
- **Textures** (`.tex`): 0xFACECAA7, 0x28-byte records pointing at each texture's base level, packed mip levels and
  Direct3D header, whose GPU fetch constant gives the format (DXT1 / DXT2-3 / DXT4-5 / DXN / 8888), the 16-bit swap
  and the tiling (32 x 32-block tiles).
- **Collision** (`.hkc`): Havok 4.0 packfiles, read by member name from the class descriptions the file carries. Fixed
  rigid bodies (identity transforms, world space) of MOPP trees over storage meshes, or list shapes of transformed
  boxes / cylinders / capsules / spheres (the node array's CollisionPrimitive nodes). A triangle's material word is
  terrain << 16 | THUG2's face flags; terrain numbers index THP8's `terrain_types` (qb.pak). The bodies are unnamed:
  each is matched to the scene sector whose box is the same.

## What's converted (phases 1 and 2)

- Scene: every sector of the zone, the instances baked in; each material one pass with its first non-normal-map
  texture (THP8's shaders add normal, specular, ambient and shadow maps - not drawn); textures with alpha blended
  (double-sided). Vertex colours as THP8's (255 = full as THUG2's 128); opaque materials' vertex alpha full (THP8 uses
  it to blend layers).
- Textures: DXT1 / DXT5 kept compressed (base level untiled, mip levels made from it), DXT3 and 8888 as 32-bit, DXN
  normal maps left out. (8888 textures came out with their alpha as blue until 2026-10-22: the light maps of the
  interiors are 8888.)
- Collision: every body, primitives as triangles (boxes; cylinders, capsules and spheres as 8-sided prisms), named
  after its sector (or its primitives' node); face flags as THP8's (trigger faces only on kill zones), terrains mapped
  to THUG2's nearest.
- Nodes (level_port's static path): restarts, rails (with their Links and TerrainTypes), level geometry, kill zones
  where THP8's trigger script calls a kill-skater script by name; positions in inches.
- Glows and light shafts (lamp halos, sun shafts: sectors named `...Glow`, `..._Glo`, `...LightShaft...`) drawn adding,
  at the strength of their material colour (the four floats before the first texture's checksum in the material's
  parameter block: R, G, B, A); THP8's blend modes themselves are not decoded yet.
- Sky: THP8's default sky dome, its cloud layers faded out towards the horizon.
- Layers (phase 2): the terrain templates' further passes (up to three) as THUG2 passes blending by alpha (mode 5):
  the layer's diffuse map with its mask's brightness (times the map's own alpha and the pass colour's opacity) as its
  alpha - a texture made per (diffuse, mask) pair, or, where the mask has a uv field of its own, the mask read into
  the vertex alpha (one layer per material). Pass colours as THUG2's pass colours (tints: the skate park's red-striped
  wave, its red ledge edges). The tilings (float4s) aren't applied.
- Lighting (phase 2): THP8's light maps baked into the vertex colours: ambient map x 1.5 + the zone's sun colour
  (its `<zone>_tod_manager`, halfway to white) x shadow map x 0.6; interiors (technique `..._InteriorLighting`: the
  factory, school inside, skate shop, bedroom - or a real ambient map with the 32 x 32 white shadow stand-in) ambient
  map x 4 alone. Lit triangles are split where the maps vary
  inside them (materials.refine: the shadows keep their shape), within a budget (half as many triangles again or
  40,000 more, at most 190,000 a zone). THUG2's level lights (skater, objects) from the zone's tod_manager.
- Scripts (phase 2, tools/thaw_levels/thaw_scripts.ts's translator - THP8's scripts are THAW's token stream): the
  geometry's and rails' trigger scripts and everything they call, from the zone's own qbs, its sub-zones' and the
  other zones' (a gap may end in the next zone's script) - the **gaps** (StartGap / EndGap, their names as THP8's
  English text); statements THUG2 hasn't left out (goals, chalk gaps, sounds, particles, cameras). Kill zones also
  through the skater (`Skater:KillSkater`); THP8's trigger volumes (faces 0x20 + non-collidable: the Capitol's lake)
  as THUG2 trigger faces, named after the collision-only node in them. Script objects (Ghost game objects), moving
  objects (LevelObject: in their own space, not moved with the zone) and waypoints made as THAW's are; THP8's Vehicle
  nodes (its grid pieces) left out. Guards: a script with an endless loop whose waits can't run is emptied, a
  RunScriptOnObject of a script that isn't there made harmless (both hung / crashed the game).
- Restarts (phase 2): THP8's network spawns (Multiplayer) also single-player restarts (Player1) - most zones have
  one Player1 restart; restarts over nothing or a kill zone no network spawns (nodes.groundRestarts).

- Props (phase 2): THP8's bouncy physics props (GameObject Type GO_Cafe_Chair, GO_TrafficCone, GO_Aluminum_Can, the
  cartoon world's skulls...: their models from ZONES\global.pak or the zone's archive) as THUG2's bouncy LevelObjects -
  the model drawn under the node's name in its own space, a box of its size as collision and contacts, THAW's bouncy
  coefficients. Scripted ones (the Capitol's pendulum) left out.
- Interiors' ambient rolled off (x 6 / (1 + 4 x)): the factory's floor came out white at x 4.

Not yet (docs/pending/thp8.md): peds, vehicles, other game objects with models (the zone's own .mdl / .skin), particles,
the sub-zones' (_gfx / _sfx) nodes, sounds, music, goals, normal / specular / environment maps, the network areas of
several zones.
