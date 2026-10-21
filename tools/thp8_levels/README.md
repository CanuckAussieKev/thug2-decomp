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
.tex.xbx / .col.xbx / .qb`, `levels\thp8_sky` (THP8's default sky) and `levels.json`. Name zones to convert only some
(`convert houses shops` - `z_houses`, `houses` or `thp8_houses` all work); the default is all fifteen. Converting only
some updates their entries in `levels.json` and keeps the others. `--no-mips` skips the mip levels (faster).
`make_menu_level.ts` then copies them into the data overlay and adds them to Levels.qb and the level select.

    node tools/thp8_levels/thp8_levels.ts selfcheck
    node tools/thp8_levels/thp8_levels.ts list

`selfcheck` reads every file of every zone archive (scenes and their strips, textures' base levels untiled,
collision, qb / node arrays) and round-trips THUG2's LA / TestLevel files; `list` shows the zones.

| Zone | THUG2 level (Load_...) | Sign | Level select | Network barriers added |
|---|---|---|---|---|
| z_houses | thp8_houses | SUBURBS | 110 | z_online_suburbs |
| z_outschool | thp8_outschool | SCHOOL | 111 | z_online_school |
| z_inschool | thp8_inschool | SCHOOL INSIDE | 112 | |
| z_shops | thp8_shops | SHOPS | 113 | z_online_shops |
| z_ghetto | thp8_ghetto | SLUMS | 114 | z_online_ghetto |
| z_factory | thp8_factory | FACTORY | 115 | z_online_factory |
| z_center | thp8_center | CAPITOL | 116 | z_online_capitol |
| z_citypark | thp8_citypark | CITY PARK | 117 | |
| z_cretepark | thp8_cretepark | SKATE PARK | 118 | z_online_skatepark |
| z_funpark | thp8_funpark | FUN PARK | 119 | z_online_funpark |
| z_parking | thp8_parking | PARKING | 120 | |
| z_training | thp8_training | TRAINING | 121 | |
| z_cartoon | thp8_cartoon | CARTOON | 122 | |
| z_skateshop | thp8_skateshop | SKATE SHOP | 123 | |
| z_bedroom | thp8_bedroom | BEDROOM | 124 | |

The sign names are ours (THP8's English level names are in its localised text, not decoded yet).

## The world and how it's cut

THP8's world is one city streamed in zones: each `ZONES\z_<zone>.pak.xen` has a far-away stand-in `z_<zone>_lod`, and
the zones share one frame (metres, y up). The eleven city zones' collision boxes overlap at their edges; together the
city is ~800 x 1000 m with ~550,000 drawn triangles, ~180,000 collision triangles and ~150 MB of textures - far past
what THUG2's memory takes, so each zone is a level of its own (as THAW's areas are), moved so the middle of its
collision is at the origin. THP8's network areas (`z_online_<x>`: suburbs, school, shops, ghetto, factory, capitol,
skatepark, funpark, central, industrial, funskatepark) are zones (or groups of zones) closed off by barrier walls; the
barriers of the ones that are a single zone are added to it, as collision only. The separate zones: training,
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
  structures) or a flag. Script bodies: LZSS'd THUG2 tokens, little-endian. Checksum names: `PAK\dbg.pak.xen`.
- **Scenes** (`.geom`): THAW's layout grown: 0x134-byte materials (a list of up to 16 texture checksums, a parameter
  block naming the shader template, the texture count and the terrain), then 0xBABEFACE and the scene: 0x50-byte
  sectors (the node names), 0x60-byte sector nodes (box, first mesh, mesh count), 0x80-byte meshes (sphere, material,
  vertex layout, colour / uv offsets, counts, the vertex and index blocks). Vertices: float3 position, packed normal,
  0-2 tangent words, ARGB colour, 16-bit float uv pairs (one per 2-bit field of the layout word). Indices: u16
  triangle strips separated by 0x7FFF.
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

## What's converted (phase 1)

- Scene: every sector of the zone, the instances baked in; each material one pass with its first non-normal-map
  texture (THP8's shaders add normal, specular, ambient and shadow maps - not drawn); textures with alpha blended
  (double-sided). Vertex colours as THP8's (255 = full as THUG2's 128); opaque materials' vertex alpha full (THP8 uses
  it to blend layers).
- Textures: DXT1 / DXT5 kept compressed (base level untiled, mip levels made from it), DXT3 and 8888 as 32-bit, DXN
  normal maps left out.
- Collision: every body, primitives as triangles (boxes; cylinders, capsules and spheres as 8-sided prisms), named
  after its sector (or its primitives' node); face flags as THP8's (trigger faces only on kill zones), terrains mapped
  to THUG2's nearest.
- Nodes (level_port's static path): restarts, rails (with their Links and TerrainTypes), level geometry, kill zones
  where THP8's trigger script calls a kill-skater script by name; positions in inches.
- Sky: THP8's default sky dome. Lighting: THUG2's default (THP8 lights with ambient / shadow maps and its time of day).

Not yet (phase 2 - docs/pending/thp8.md): gaps, the kill zones THP8 writes as `Skater:KillSkater` member calls,
scripts, peds, vehicles, game objects (the zone's own .mdl / .skin), particles, sub-zones (_gfx / _sfx), lighting from
THP8's ambient / shadow maps, multi-layer (blended terrain) materials, the network areas of several zones.
