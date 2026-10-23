# thp8_levels - Tony Hawk's Project 8 levels in THUG2

Converts the levels of your own Tony Hawk's Project 8 data (the Xbox 360 version's, as a fan-made native recompilation
installs it: `C:\Games\THP8\game\DATA` - only the game data is read, never the recompilation's own programs) to
THUG2's formats and puts them in THUG2's level select (a "TONY HAWK'S PROJECT 8" section: Up / Down switches games).
Nothing of either game is in the repo: the files are made from your copies each time.

## Running it

Needs Node 24+ (`npm install` once - docs/typescript.md), NeverScript (`tools\build_neverscript.ps1` ->
`out\neverscript\ns.exe`) and, to install the levels, tools/menu_level/make_menu_level.ts.

    node tools/thp8_levels/thp8_levels.ts convert [zones...] [--thp8 C:\Games\THP8] [--thug2 C:\Games\THUG2]
    node tools/menu_level/make_menu_level.ts [--game C:\Games\THUG2]

`convert` writes `<thug2>\thug2-decomp-thp8\` (not read by the game): `levels\thp8_<zone>\thp8_<zone>.scn.xbx /
.tex.xbx / .col.xbx / .qb` (the network levels: `thp8_net_<id>`), `levels\thp8_sky` (THP8's default sky) and
`levels.json`. Name levels to convert only some (`convert houses shops net_skatecentral` - `z_houses`, `houses` or
`thp8_houses`; `net_capitol`, `capitol` or `thp8_net_capitol` all work); the default is all twenty-two (~2.5 min; the
whole town ~1 min of it). Converting only
some updates their entries in `levels.json` and keeps the others. `--no-mips` skips the mip levels (faster).
`make_menu_level.ts` then copies them into the data overlay and adds them to Levels.qb and the level select.

    node tools/thp8_levels/thp8_levels.ts selfcheck
    node tools/thp8_levels/thp8_levels.ts list

`selfcheck` reads every file of every zone archive (scenes and their strips, textures' base levels untiled,
collision, qb / node arrays) and round-trips THUG2's LA / TestLevel files; `list` shows the levels (zones and network
levels, what each is made of).

| Zone | THUG2 level (Load_...) | Sign text | Pictogram | Level select | Network barriers added |
|---|---|---|---|---|---|
| z_houses | thp8_houses | SUBURBIA | mailbox | 110 | z_online_suburbs |
| z_outschool | thp8_outschool | SCHOOL | schoolbell | 111 | |
| z_inschool | thp8_inschool | SCHOOL INSIDE | locker | 112 | |
| z_shops | thp8_shops | MAIN STREET | trafficlight | 113 | z_online_shops |
| z_ghetto | thp8_ghetto | SLUMS | bricks | 114 | z_online_ghetto |
| z_factory | thp8_factory | FACTORY | gear | 115 | z_online_factory |
| z_center | thp8_center | THE CAPITOL | capitolflag | 116 | |
| z_citypark | thp8_citypark | CITY PARK | bench | 117 | |
| z_cretepark | thp8_cretepark | SKATE PARK | bowl | 118 | z_online_skatepark |
| z_funpark | thp8_funpark | FUN PARK | coaster | 119 | |
| z_parking | thp8_parking | HILL TOP | parking | 120 | |
| z_training | thp8_training | TRAINING | target | 121 | |
| z_cartoon | thp8_cartoon | CARTOON | cloud | 122 | |
| z_skateshop | thp8_skateshop | SKATE SHOP | skatetool | 123 | |
| z_bedroom | thp8_bedroom | BEDROOM | bed | 124 | |

THP8's network levels come from its own list (qb.pak's `online_level_select_menu_level_info`, each a
`multiplayer_level_<x>` structure: text, the zones whose network spawns it uses - `spawn_in` -, its barrier archive -
`area_pak`). Those of one zone are that zone's level above (SKATE PARK, FACTORY, SLUMS, SUBURBIA, MAIN STREET: its
barriers added). Those of several are levels of their own, the zones merged in THP8's shared frame:

| THP8 network level | THUG2 level (Load_...) | Zones (the first's lights) | Barriers | Pictogram | Level select |
|---|---|---|---|---|---|
| BEAVER VALLEY (the whole town) | thp8_net_beavervalley | houses, cretepark, shops, center, citypark, ghetto, funpark, cartoon, factory, parking, inschool, outschool | none (THP8 has none) | citymap | 125 |
| FUN PARK (sign FUN PARK AREA) | thp8_net_funpark | funpark, cartoon | z_online_funpark | balloon | 126 |
| SCHOOL (sign SCHOOL AREA) | thp8_net_school | outschool, inschool | z_online_school | books | 127 |
| INDUSTRIAL ZONE | thp8_net_industrial | factory, ghetto | z_online_industrial | chimneys | 128 |
| CAPITOL BUILDING | thp8_net_capitol | citypark, center, parking | z_online_capitol | pediment | 129 |
| SKATE & FUN PARKS | thp8_net_skatefunparks | funpark, cretepark, cartoon | z_online_funskatepark | ticket | 130 |
| SKATE CENTRAL | thp8_net_skatecentral | center, houses, shops, citypark, parking | z_online_central | compass | 131 |

All are in the level select's THP8 section - free skate and the network level select alike. BEAVER VALLEY reaches
~21,000 units from its middle, past the half units THUG2 sends network positions in (+-16,384): its entry gets
IsBigLevel (whole units - src/game/net/thugpro_compat.h). It needs this project's bigger limits
(src/game/core/memory_sizes.h: the node name hash, the rail table, the script heap, the pools) - with the original's
sizes (`[memory] original_sizes=1`) it can't load. It takes ~7 minutes to load in the Debug build.

The city's sign texts are THP8's own English area names (qb.pak's `zone_info`, its map's labels; the school's inside
zone gets " INSIDE"); training, the cartoon world, the skate shop and the bedroom have none there: ours. The
pictograms are ours (tools/menu_level/level_select_icons.ts, THP8_LEVELS in level_select.ts).

## The world and how it's cut

THP8's world is one city streamed in zones: each `ZONES\z_<zone>.pak.xen` has a far-away stand-in `z_<zone>_lod`, and
the zones share one frame (metres, y up). Each zone is a level of its own (as THAW's areas are), moved so the middle of
its collision is at the origin; a network level merges its zones in the shared frame, moved as one. Merging: a
material of one checksum that two zones define differently (other light maps: 53 of the 276 shared across the town)
is made twice; each zone's light maps are lit by its own sun; the node arrays follow one another (Links moved up; no
node name repeats across the zones); a texture of one checksum is made once. The whole town as converted: 1.26 million
drawn triangles, 8,781 sectors, 2,791 textures, 3,346 collision objects; 81 MB scene + 64 MB textures + 3.5 MB
collision + 4.8 MB qb. THP8's network areas (`z_online_<x>`) are barrier walls, added as collision only (THP8 draws
them as a shimmer when the skater is near). The separate zones: training,
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
