# thaw - THAW (Tony Hawk's American Wasteland, PC 1.01) levels in THUG2 (tools/thaw_levels)

Agent "thaw". How to run it: tools/thaw_levels/README.md.

## Result

- `python -I tools/thaw_levels/thaw_levels.py convert` converts all 15 levels from their `_net` archives: Chicago,
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

- Pak / tex / col / qb: see the README and thaw.py. qb: 851 of 851 files read, and every script's token walk ends
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
