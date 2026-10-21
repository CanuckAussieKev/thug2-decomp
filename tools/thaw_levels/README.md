# thaw_levels - Tony Hawk's American Wasteland levels in THUG2

Converts the levels of your own THAW (PC, 1.01) install to THUG2's formats and puts them in THUG2's level select
(a "TONY HAWK'S AMERICAN WASTELAND" section after THPS3's). Nothing of either game is in the repo: the files are made
from your copies each time.

## Running it

Needs Node 24+ (`npm install` once; docs/typescript.md) and NeverScript (`tools\build_neverscript.ps1` -> `out\neverscript\ns.exe`).

    node tools/thaw_levels/thaw_levels.ts convert [--thaw C:\Games\THAW] [--thug2 C:\Games\THUG2]
    node tools/menu_level/make_menu_level.ts

Switches (tools/compat/thugpro/README.md): `--compat none` (no THUGPRO data), `--no-thugpro-new` (THUGPRO's hand edits, not its own content), `--thugpro <dir>` (THUGPRO's install, read for `thugpro_new` - its pieces are referenced, never copied into the repo; default `%LOCALAPPDATA%THUG Pro`; missing: a warning, those pieces skipped), `--no-port-changes` (our own changes off but the ones a level needs to load - `port_changes`, PC-THAW-n).

`convert` reads each level's "net" archive (`Game\data\worlds\worldzones\<zone>\<zone>_net.pak.wpc`: the area on its
own, as THAW loads it for classic and online games - no story props, vehicles or sounds), the sky
(`data\skies\TestLevel_Sky`), `data\pak\qb.pak.wpc`'s levels.qb and `data\pak\dbg.pak.wpc` (the names of the
checksums), and writes `<thug2>\thug2-decomp-thaw\`: `levels\z_<xx>\z_<xx>.scn.xbx / .tex.xbx / .col.xbx /
.qb` (the level named after its zone, as THUGPRO's are - four with THUGPRO's own names, from the compatibility profile `tools/compat/thugpro` (`--compat none`: none): online the load script's name is the level), `levels\thaw_sky` and `levels.json`. The default is all 15 levels of the table `LEVELS` in `thaw_levels.ts`:
the classic ones (Z_CH Chicago, Z_MA The Mall, Z_DN Minneapolis, Z_JA Kyoto, Z_SZ Santa Cruz, Z_SV2 The Ruins) and
the story's areas (Z_SV Vans Park, Z_HO Hollywood, Z_BH Beverly Hills, Z_DT Downtown, Z_EL East LA, Z_SM Santa
Monica, Z_SR Skate Ranch, Z_LV Casino, Z_OI Oil Rig). Name some to convert only those (`convert Z_CH MA`). Level
select numbers 80-94. (Converting some levels empties the output folder first: the others go - finish with a
full `convert`.)

`make_menu_level.ts` copies every converted game's levels into the data overlay `<thug2>\thug2-decomp-data`; run it
after every `convert`. Delete `thug2-decomp-thaw` and run it again to take them out.

    node tools/thaw_levels/thaw_levels.ts selfcheck

reads THUG2's LA scene / collision / textures and writes them back (byte-identical), then every file of every THAW
`_net` archive, the skies and qb.pak: qb files, textures, collision (written back byte-identical once the grid
header is off) and scenes (read and converted).

## THAW's formats (thaw.ts has the details)

- Archives `.pak.wpc` (+ `.pab.wpc`): the GH3-family pak - 32-byte entries (extension, offset, size, name
  checksums, flags; flags & 0x20: a 160-byte path follows), uncompressed. Flags & 2: the zone's `_sfx` sub-zone.
- Textures `.tex`: a 0xABADD00D container of `.img` records that map field for field onto THUG2's `.tex.xbx`
  (DXT1/2/5; a few 32-bit ones, whose levels have a {u16 pitch, u16 rows} size).
- Collision `.col`: THUG2's version 10 plus 48 bytes after the header (a supersector grid). Face flag 0x2000 (not
  taggable) is THUG2's too: kept; 0x4000 / 0x8000 dropped (their THUG2 meaning not known yet). Terrains are THUG2's numbers.
- QB: item-based (GH3 family): typed top-level items (int, float, string, local string, pair, vector, script,
  structure, array, checksum, $reference), structures as linked 16-byte entries. Scripts are LZSS'd THUG2 token
  streams with weighted random tokens and one more token, 0x4A (an inline structure). All 851 qb files read; every
  script's token walk ends at its end.
- Scene `.geom` (THAW.exe's loader 0x005083B0, materials 0x00530470): a memory image. 0x120-byte materials (pass
  flags, blend modes, fixed alpha, addressing and filtering use THUG2's values; up to 4 passes), then 0xBABEFACE
  and the scene: sector table (0x30 bytes: checksum, flags), a 0x68-byte node per sector (box, mesh count), 0xE0-byte
  mesh records (sphere, material checksum, stride, vertex / index counts, Direct3D FVF), then the vertex and index
  data in mesh order (FVF vertices - THUG2's own layout -, one u16 triangle strip per mesh). Colour wibble sectors
  (flag 0x800) have a byte per vertex and a second vertex buffer.

## What's converted

- Scene and textures (THAW's FVF vertices straight into THUG2's mesh buffers), collision. Left out: LOD copies
  (a node's LOD_Name), billboards (glows: every vertex sits at the pivot, spread by THAW's shader), night-time
  sectors (NightOn_*, *_tod_light_*: lit at night only by THAW's time of day).
- Node flags: THAW's story areas make much of their geometry, rails and objects from node flags
  (`CreatedFromVariable=NODEFLAG_...`, set by scripts). Nodes whose flag is on in THAW's non-story games
  (chapter_info.qb's `game_progress_set_non_story_mode_nodeflags`, read from qb.pak) get CreatedAtStart; the others stay
  not made, as in THAW (Skate Ranch: every piece built, its `_DEF` stand-ins gone).
- Nodes: restarts, rails, level geometry (LevelObjects as static geometry), KOTH crowns; kill zones (a trigger script
  calling a "killskater" script) become THUG2's SK3_KillSkater naming the nearest restart.
- Areas with a `<zone>_Zone_Origin` node are moved by minus its position (all axes, in sixteenths), as THUGPRO's
  toolchain does, so that skater positions agree online (Vans Park excepted: THUGPRO left it in THAW's frame).
  `--frame recentre` gives the older frame (areas reaching past `--recentre` moved to the origin, x / z);
  `--legacy-names` the older `thaw_<xx>` names. Layouts saved in the older levels: `node tools/thaw_levels/migrate_layouts.ts --game <folder>`.
- Game-mode nodes (TRG_CTF_*, TRG_Flag_*, TRG_KOTH*, TRG_Team_Restart) lose the zone prefix and the CTF / team flags
  are kept (THUG2's own flag models): capture the flag works with THUG2's net\ctf scripts.
- `node tools/thaw_levels/thugpro_compare.ts` compares the converted levels with an installed THUGPRO's (read only): node positions, crowns,
  multiplayer restarts, game-mode nodes, collision faces unreachable through their BSP trees
  (`tools/level_port/col_check.ts`).
- Level structure: THAW's name, the TestLevel_Sky sky, THUG2's default lighting (THAW lights by its time of day
  manager), no music, no goals.
