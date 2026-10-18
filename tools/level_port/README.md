# level_port - shared code of the level converters

Used by `tools/thps4_levels` (THPS4) and `tools/thug1_levels` (THUG1); `ported_levels.py` also by
`tools/menu_level/make_menu_level.py`, which installs what they convert.

- `binio.py` - little-endian reading / writing.
- `prx.py` - THUG2 `.prx` / THUG1 `.pre` archives (same format), LZSS.
- `thug2_scene.py`, `thug2_col.py`, `tex.py` - THUG2's scene, collision (version 10; reads THUG1's 9) and texture
  files, read and written field by field: THUG2's own files come out byte-identical (each tool's `selfcheck`).
- `geometry.py` - scenes with one vertex array per sector (THPS4, THUG1) to THUG2's per-mesh vertex buffers;
  collision objects to THUG2's (fixed-point vertices, BSP trees, reaches) for THPS4.
- `qb.py` - THPS4 / THUG1 qb files: globals, names, script statements; values as NeverScript text.
- `nodes.py` - node arrays: what's kept, kill zones, THUG1's included structures.
- `common.py` - NeverScript compiling, writing a level's files, lighting keys, THUG2 round trips.
- `ported_levels.py` - the staging folders' `levels.json` (format at its top), the Levels.qb structures, the level
  select signs, copying into the overlay.
