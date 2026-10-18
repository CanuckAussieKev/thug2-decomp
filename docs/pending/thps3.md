# thps3 - THPS3 levels in THUG2 (tools/thps3_levels, shared code in tools/level_port)

Agent "thps3". How to run it and THPS3's formats: tools/thps3_levels/README.md.

## Result

- `python -I tools/thps3_levels/thps3_levels.py convert` converts 14 levels by default: the career's nine (Foundry,
  Canada, Rio, Suburbia, Airport, Skater Island, Los Angeles, Tokyo, Cruise Ship), the classics (Warehouse,
  Burnside, Roswell), the Tutorials and the skate shop, with their skies (Can, Rio, Sub, Ap, SI, LA, Tok, Shp, Burn,
  Ros; Tut uses the park editor's beach sky; Foundry, Warehouse and the shop have none). The park editor's sets
  (Sk3Ed_Bch / Indr / Schl) convert when named. All 14 in ~15 s (with mips).
- Kill zones: Foun 3, Can 27, Rio 2, Sub 41, SI 20, LA 7, Tok 162, Shp 15, Ros 6 (Ap, Ware, Burn, Tut, shop none).
- `selfcheck`: THUG2's LA / TestLevel round trips byte-identical; 323 THPS3 files parse (every .bsp, .dff, .tdx,
  level .qb and Data\Scripts .qb). THPS4's (139 files) and THUG1's (333) self-checks still pass; THPS4's Alcatraz
  converts byte-identical to before (scene, collision).
- Level select: a fourth section, TONY HAWK'S PRO SKATER 3 (level_select.THPS3_LEVELS: THPS3's order), numbers
  60-73; new pictograms gear (Foundry), pine (Canada), redeemer (Rio), torii (Tokyo), ship (Cruise Ship), crate
  (Warehouse), ufo (Roswell); stars for Canada (BC, inferred), Rio, LA, Tokyo, Warehouse (Woodland Hills), Burnside
  (Portland), Roswell; the fictional places spin the globe.
- Game runs: NOT run in the game yet.

## Changes to shared code

- level_port/qb.py: the `<...>` token (0x2C) as a value (Cruise Ship's node array has one) - only files that failed
  before are affected.
- level_port/ported_levels.py: STAGINGS + thug2-decomp-thps3.
- menu_level/level_select.py: THPS3_LEVELS, sections(thps3=); level_select_icons.py: 7 pictograms;
  make_menu_level.py: one line (passes the THPS3 section).

## Findings (format)

- RenderWare 3.1 (chunk version 0x310) with Neversoft plugin chunks 0x0294AF01/02/04. Atomic sectors = Neversoft
  objects (checksum = the EnvironmentObject's name); triangles are (u16 material, 3 x u16 vertex) in world sectors,
  (v2, v1, material, v3) in clump geometry.
- Face flags (u16 per triangle in the sector's Neversoft extension) have THUG2's meanings: e.g. Foundry's floors 0x80
  (camera collides), walls 0x480 / 0x484 (no-shadow wall, wall ride), quarter pipes 0x88 / 0xC8 (vert, trigger),
  railings and decorations 0x10 / 0x110 (non-collidable), water 0x50 (trigger + non-collidable), the invisible
  collision planes 0x100 / 0x180.
- Material extension: per pass flags (0x02 invisible, 0x08 transparent, 0x10 decal), terrain (THPS3's terrain.qb
  numbering = THUG2's), 4 RGBA colours (the first used, 0x80 neutral), blend = the PS2 ALPHA register (0x44 blend,
  0x48 add, 0x68 add fixed, 0x42 subtract, 0x64 blend fixed...), fixed alpha, has texture + an embedded RW texture
  chunk, uv flags (0x01 uv wibble, 0x80 ?) + 8 floats as THUG2's uv wibble, then vertex colour wibble keys.
  23 materials have a second pass, never textured: not used.
- Textures: every one 32-bit, one level, D3D8 natives with 128-byte names (RW 3.1's layout).
- .dff clumps: their size field is 12 bytes short (the Neversoft extension's chunk header); geometry in a Z-up model
  space (frame rotation (x, z, -y)) at the GameObject node's position.
- Nodes: Position with z negated from the geometry (as THPS4). EnvironmentObject = sector node.
- Reference (thps-formats experimental/chunky.py): it reads the atomic sector extension's padding as 6 bytes after
  the checksum - the field is a u32 version 6 at the start, and the trailer is 3 bytes (u8 flags, u8, u8 has vertex
  data + 2 bytes per vertex when set); its material extension isn't decoded. Not ours to fix; noted.

## Left

- Game runs (load, skate, grind, kill zones, textures, sky) - waiting for the game lock.
- GameObjects (.dff: fans, doors, presses, pumpkins) not converted - almost all are made by goal scripts; the
  CreatedAtStart ones could be added as static scene sectors.
- Vertex colour wibbles, env-mapped passes (uv flag 0x80), the second passes, music, goals, peds, vehicles.
