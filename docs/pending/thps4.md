# thps4 - THPS4 levels in THUG2 (tools/thps4_levels)

Agent "thps4", branch worktree-agent-a581fc09e82f1f3e3. How to run it: tools/thps4_levels/README.md.

## Result

All 11 THPS4 levels with a scene convert and were run in the game from the level select (signs after SKATE SHOP):
Alcatraz, Carnival, Chicago (Hoffman), Shipyard (Jnk), Kona, London, San Francisco, College (Sch), Zoo, the
THPS4 skate shop, and Motox, each with its sky (Motox uses Hoffman's). For each: the level loads with no errors in
thug2-decomp.log, the skater starts at THPS4's Player1 restart, skates (screenshots after a few pushes), textures
right, skies right. The game-run check is short (load, push a few times). Grinding is NOT confirmed: two tries
with scripted key taps (a test restart moved next to a Kona rail, ollie + grind key) didn't land a grind - with
300 ms taps that proves little either way; it needs a person with a pad. Kill zones not tried. The overlay keeps the
converted levels installed (screenshots: scratchpad t4\shots\<lvl>-step*.png).

## Formats (checked against THUG2's loaders and its files)

### THUG2 scene (.scn.xbx) - loader 0x004B36D0, sectors 0x004C6B10, meshes 0x004D6100
- u32 x3 versions (1, 1, 1), materials (0x004D3390), sectors, i32 hierarchy count + 0x50 bytes each.
- Per mesh: sphere, box, u32 flags (0x400 -> no skater shadow, 0x20000 on every shipped mesh), material,
  u32 LOD count + per LOD {u32 n, u16 x n} - **read and thrown away** (0x004C6B10 frees them); then
  **8 x {u16 n, u16 x n}** index buffers (the "14 bytes of padding" in io_thps_scene are the 7 empty ones);
  u8 stride, u16 vertex count, **u8** buffer count, per buffer {u8 flag (nonzero: 0x004D4350 converts it),
  u32 size, data}; 2 x u32 vertex shader (0..6 built-in, else an FVF); 3 bytes (normal / colour / uv offsets);
  u8 colour wibble flag (+ a byte per vertex); u32 index sets (> 1: a u32 each); u32 pixel shader:
  1 = {u32, u32 size, data} **skipped** (0x004DA6D0) and the shader made from the material at run time
  (0x004DDC00) - used for 2+ passes; 2..18 = built-in shaders (0x004D3ED0; 2 textured with vertex alpha, 3 textured
  with the pass's fixed alpha (IGNORE_VERTEX_ALPHA), 4 untextured - the ps.1.1 sources are in the exe).
- Vertex colours: 128 = full (the shaders multiply by 4 / 2).

### THUG2 textures (.tex.xbx) - 0x004CAE90
u32 version 1, count; per texture checksum, w, h, levels, texel depth, palette depth, DXT (1/2 -> DXT1, 5 -> DXT5;
0: 8 -> P8, 16 -> A1R5G5B5, 32 -> A8R8G8B8), palette size (non-zero: a separate path, made 32-bit), per level
{u32 size, data}. THPS4 PC's tex.dat is the same format; its 8-bit paletted textures are **Xbox-swizzled**
(Morton order) - converted to 32-bit, unswizzled.

### THUG2 collision (.col.xbx, version 10) - 0x0049CBB0, 0x004169C0, 0x00410BC0
Layout in tools/thps4_levels/thug2_col.py. Findings beyond io_thps_scene:
- Float vertices are all stored before the fixed-point ones (offsets per object say where).
- **The byte after a small face's 3 indices, and the array of a byte per large face after the faces, are not
  padding**: they are each face's "reach" - ceil(distance of its farthest vertex from the object's box minimum x the
  object's scale), where the scale (the object's last u32, a float: io_thps_scene writes 0 "padding") is
  ~254 / the farthest vertex. The ray test 0x00413B50 skips a face whose reach is below the ray's distance from the
  box minimum x scale. With zeros (what io_thps_scene's exporter writes) faces are found by the skater's downward
  test but miss the others: the skater lands in an "in air" pose, can't push. Verified by rebuilding LA's collision
  with this writer (own BSP, reaches): LA plays normally.
- Empty objects (no faces) are kept, scale 1.0, an empty leaf.
- BSP: a split word = axis | (point in sixteenths << 2), absolute coordinates; children side by side, written depth
  first; the leaves' face indices in node order (per object, from 0). Rebuilt for the THPS4 geometry.

### THPS4 scene (scn.dat), collision (col.dat v8), qb
- Scene: as io_thps_scene describes (per-sector vertex arrays: positions, normals, weights, uv sets, colours,
  wibble indices; meshes = flags, material, strip); all levels parse to the end.
- Collision v8: vertices 16 bytes (3f + 4 intensity bytes), first vertex is an index; faces 12 bytes; 20-byte BSP
  nodes (not used). Face flag 0x80 is the reverse of THUG2's (set on 80% of THPS4's faces); 0x2000 is on every face
  and 0x4000 / 0x8000 on some - none of the three in THUG2's files: dropped.
- QB: the same tokens as THUG2's except the random tokens (0x2F/0x37/0x40/0x41 = u32 count + u32 offsets, no u16
  weights) - why NeverScript stops on THPS4 files ("Byte not recognised ... 0x2f"). qb4.py reads the globals,
  checksum names and script statements; node arrays are written out as NeverScript text and compiled for THUG2.
- THPS4's node array is plain "NodeArray"; THUG2 wants <file name>_NodeArray. Position= is kept: THUG2's
  AddNodeArray turns it into Pos with z negated, as THPS4 did.

## Decisions
- Level structures are made from THPS4's Levels.qb (name, sky, lighting; mod factors as THUG2's _lo / _hi pairs),
  level_number LevelNum_Default (as the skate shop), no pre files: the overlay serves the loose files
  (LoadScene / LoadCollision / NodeArrayManLoad read them through File / Pip, which check the overlay first).
- Signs: level numbers 19, 20, 22..29, 31 (unused; no records - the info panel skips them as it does 18 / 21).
- Kept nodes: Restart, RailNode, LevelGeometry, LevelObject (as LevelGeometry: THPS4 moves them by script), KOTH
  GenericNodes; keys Position, Angles, Name, Class, Type, RestartName, restart_types (added from Type if missing),
  TerrainType, Links (renumbered), Cluster, CollisionMode; flags CreatedAtStart, TrickObject, AbsentInNetGames,
  NetEnabled. Dropped: peds, vehicles, game objects (THPS4 models / goal pickups), waypoints, goal nodes, scripts.
- Trigger faces: THUG2's collision trigger looks the node up by name and reads its TriggerScript unchecked
  (see make_menu_level.py), so trigger flags are removed except on kill zones: a THPS4 trigger script calling
  something with "kill" in its name (ALC_KillWater, *_KillSkater...) becomes SK3_KillSkater (or _Water), THUG2's own.
- Skies: THPS4 lights them at 255 - halved for THUG2's 128 = full.
- make_menu_level.py empties the overlay on each run, so it does the install: when <thug2>\thug2-decomp-thps4\
  levels.json exists it copies the levels in and adds their structures (both Levels.qb) and signs.

## Reference bugs (io_thps_scene; nothing of it copied - it has no licence)
- Collision export: small faces' 4th byte and the large faces' byte array written as 0, the object's last float
  written 0 -> faces culled by THUG2's ray test (above). Ours computes them.
- Scene import: reads "14 bytes of padding" and a u16 buffer count (really 7 empty index buffers, a u8 count and the
  first buffer's flag byte); the per-LOD index lists are inside the vertex loop (wrong for LOD > 1; unused anyway).
- THPS4 paletted textures: its importer unswizzles (right); its exporter halves alpha for 32-bit textures (PS2 habit,
  not THUG2 PC's).
- THUGPRO (thugpro-tools; only its public tools were looked at) wasn't needed: it ships no format code we used.

## Left
- Grinding to be checked by hand (rails converted with links, TerrainType, Type, TrickObject Cluster); gaps, goals, peds, vehicles,
  moving objects (LevelObjects are static), THPS4's music and sounds (THUG2's terrain sounds come with level pres we
  don't load: check the log for missing sounds), net games (load_level uses <level>scn.pre there: not made).
- Park editor sets (Sk4Ed*) not converted (their 32-bit textures may be swizzled too).
