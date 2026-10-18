# thps3_levels - Tony Hawk's Pro Skater 3 levels in THUG2

Converts the levels of your own THPS3 (PC) install to THUG2's formats and puts them in THUG2's level select (its
THPS3 section: Up / Down in the level select switches games; signs and globe stars from
tools/menu_level/level_select.py's `THPS3_LEVELS`). Nothing of either game is in the repo: the files are made from
your copies each time.

## Running it

Needs Python 3 and NeverScript (`tools\build_neverscript.ps1` -> `out\neverscript\ns.exe`).

    python -I tools/thps3_levels/thps3_levels.py convert [--thps3 C:\Games\THPS3] [--thug2 C:\Games\THUG2]
    python -I tools/menu_level/make_menu_level.py

`convert` reads `<thps3>\Data\Levels\<Lvl>\` (`<lvl>.bsp`, `.tdx`, `.qb`) and `<thps3>\Data\Scripts\` (levels.qb,
lights.qb and the global scripts, for the level structures and kill zones) and writes `<thug2>\thug2-decomp-thps3\`
(not read by the game): `levels\thps3_<lvl>\thps3_<lvl>.scn.xbx / .tex.xbx / .col.xbx / .qb`, the skies
(`thps3_<lvl>_sky`: scene and textures, no collision - as THUG2's own skies) and `levels.json`. Name levels to convert
only some (`convert Foun Can`). The default is the career's nine (Foun, Can, Rio, Sub, Ap, SI, LA, Tok, Shp), the
classics (Ware, Burn, Ros), Tut and SkateShop; the park editor's sets (Sk3Ed_Bch, Sk3Ed_Indr, Sk3Ed_Schl) convert
when named. `--no-mips` skips making mip levels (faster). The table `LEVELS` in `thps3_levels.py` is the list (folder,
sign text, place, a THUG2 sign texture, level select number 60-76, default or not).

`make_menu_level.py` (which makes the data overlay `<thug2>\thug2-decomp-data` from scratch on each run) then copies
the converted levels of every staging folder (thug2-decomp-thps4, -thug1, -thps3) into the overlay and adds them to
`Levels.qb` and the level select. Run it again after every `convert`. Delete `thug2-decomp-thps3` and run it again to
take the levels out.

    python -I tools/thps3_levels/thps3_levels.py selfcheck

reads THUG2's LA and TestLevel scene / collision / textures (and any `--extra Data\pre` archives) and writes them
back - they must come out byte-identical - and parses every THPS3 level file (worlds, clumps, texture dictionaries,
node arrays) and every script file: 323 files.

## THPS3's formats

RenderWare 3.1 binary streams (chunks: u32 type, u32 size, u32 version 0x310) with Neversoft's plugin chunks
(0x0294AF01...) - `thps3.py` has the layouts:

- `.bsp` - an RpWorld: the material list, then the plane-section tree whose leaves (atomic sectors) are Neversoft's
  objects: each has its vertices (positions, prelit colours 255 = full, two uv sets), triangles (u16 material, 3 x u16
  vertex), a bin mesh (strips or lists per material) and Neversoft's extension: a u16 face flag per triangle (THUG2's
  collision flag meanings: 0x10 non-collidable, 0x40 trigger, 0x80 camera collides...) and the object's checksum
  name - the EnvironmentObject node of the same name. The same triangles are drawn and collided with.
- Materials: RW's material and texture (name, addressing), and Neversoft's extension: flags (0x02 invisible - the
  collision-only and trigger planes, 0x08 transparent, 0x10 decal), the terrain type (THUG2's numbering), the
  colour (0x80 = neutral), the blend as the PS2's alpha register ((A - B) * C + D, which THUG2's blend modes are named
  after), the fixed alpha, the uv wibble.
- `.tdx` - a texture dictionary of D3D8 texture natives: every texture THPS3 PC ships is 32-bit BGRA with one level
  (THUG2's 32-bit textures exactly; mip levels are made by the converter).
- `.dff` - clumps (the GameObjects: fans, doors, rubble, pumpkins...): only the ones there at the start are converted
  (static scene sectors, no collision; `--no-objects` leaves them out) - almost all are made by goal scripts. Each clump's declared size leaves out 12 bytes of its Neversoft extension.
- `.qb` - THPS4's token format (tools/level_port/qb.py; Cruise Ship's node array has a `<...>`). Nodes use
  `Position`, z negated from the geometry's - as THPS4's, which THUG2 reads the same way: the geometry is kept as is.

## What's converted

- Scene: a THUG2 sector per atomic sector with its meshes per material (the bin mesh's strips; triangle lists joined
  into strips), invisible materials left out, vertex colours halved (THUG2's 128 = full), the first uv set (RenderWare stores the sets one after
  another; THPS3's second is never written).
- Textures: 32-bit as they are, with mip levels down to 8 x 8.
- Collision: a THUG2 object per atomic sector, every face with its flags and its material's terrain; THUG2's BSP
  trees, fixed-point vertices and reaches made (tools/level_port/geometry.convert_col).
- Nodes: EnvironmentObjects become LevelGeometry (their Links, to restarts for THPS3's scripts, dropped); restarts, rails, KOTH crowns kept.
- Scripts, peds, vehicles, objects (`--no-scripts` leaves them out): the level's scripts translated to THUG2's
  (tools/level_port/script_port.py) and run from a setup script; peds and crowds as THUG2's ped AI, resting birds and
  sharks as THUG2's animals, traffic as THUG2's cars (Car_Generic01), other vehicles and moving game objects as
  LevelObjects of their THPS3 models, proximity nodes as ghosts watching their radius - what's brought and what's
  left out per level: docs/pending/thps3-scripts.md. Kill zones: trigger scripts that call SK3_KillSkater(_Water) or a
  level script that does (CJR_Foun_Kill_Skater_Molten...) become THUG2's SK3_KillSkater(_Water) with the call's
  messages and colour and a restart (tools/level_port/nodes.py).
- Objects not there at the start: an EnvironmentObject without CreatedAtStart is kept only when the level's start
  scripts create it (Create name= / prefix=, followed from Load_<lvl>, its once_on_startup, the Player1 restart's
  trigger script and the goal parameters' Goal_Start_Script - Airport's areas are made that way); the others (a
  press's other position, open doors, LA's earthquake rubble, the tutorials' lesson pieces) are left out with their
  sectors.
- Level structure: THPS3's name, sky (Load_<lvl>'s loadlevelgeometry Sky=) and lighting (its SetUp...Lights: ambient,
  lights 0 and 1, modulation factors); no music, no goals.

The THUG2 writers, the geometry conversion, the node arrays and the installing are shared with tools/thps4_levels and
tools/thug1_levels (tools/level_port).

Format reference: thps-formats (Morten1337, MIT - see THIRD_PARTY_NOTICES.md) has the start of a RenderWare reader
for THPS3 (`thps_formats/experimental/chunky.py`); the layouts here were worked out from the files themselves.
