# thps3_levels - Tony Hawk's Pro Skater 3 levels in THUG2

Converts the levels of your own THPS3 (PC) install to THUG2's formats and puts them in THUG2's level select (its
THPS3 section: Up / Down in the level select switches games; signs and globe stars from
tools/menu_level/level_select.ts's `THPS3_LEVELS`). Nothing of either game is in the repo: the files are made from
your copies each time.

## Running it

Needs Node 24+ (`npm install` once - docs/typescript.md), NeverScript (`tools\build_neverscript.ps1` ->
`out\neverscript\ns.exe`) and, to install the levels, tools/menu_level/make_menu_level.ts.

    node tools/thps3_levels/thps3_levels.ts convert [--thps3 C:\Games\THPS3] [--thug2 C:\Games\THUG2]
    node tools/menu_level/make_menu_level.ts

`convert` reads `<thps3>\Data\Levels\<Lvl>\` (`<lvl>.bsp`, `.tdx`, `.qb`) and `<thps3>\Data\Scripts\` (levels.qb,
lights.qb and the global scripts, for the level structures and kill zones) and writes `<thug2>\thug2-decomp-thps3\`
(not read by the game): `levels\<level>\<level>.scn.xbx / .tex.xbx / .col.xbx / .qb`, the skies (`<level>_sky`:
scene and textures, no collision - as THUG2's own skies) and `levels.json`. A level is named as in THPS3 (`foun`:
Load_foun, Level_foun - "Names" below; `--legacy-names`: every level `thps3_<lvl>`, as before 2026-10-20). Name levels to convert
only some (`convert Foun Can`). The default is the career's nine (Foun, Can, Rio, Sub, Ap, SI, LA, Tok, Shp), the
classics (Ware, Burn, Ros), Tut and SkateShop; the park editor's sets (Sk3Ed_Bch, Sk3Ed_Indr, Sk3Ed_Schl) convert
when named. `--no-mips` skips making mip levels (faster). The table `LEVELS` in `thps3_levels.ts` is the list (folder,
sign text, place, a THUG2 sign texture, level select number 60-76, default or not).

`make_menu_level.ts` (which makes the data overlay `<thug2>\thug2-decomp-data` from scratch on each run) then copies
the converted levels of every staging folder (thug2-decomp-thps4, -thug1, -thps3) into the overlay and adds them to
`Levels.qb` and the level select. Run it again after every `convert`. Delete `thug2-decomp-thps3` and run it again to
take the levels out.

    node tools/thps3_levels/thps3_levels.ts selfcheck

reads THUG2's LA and TestLevel scene / collision / textures (and any `--extra Data\pre` archives) and writes them
back - they must come out byte-identical - and parses every THPS3 level file (worlds, clumps, texture dictionaries,
node arrays) and every script file: 323 files.

## THPS3's formats

RenderWare 3.1 binary streams (chunks: u32 type, u32 size, u32 version 0x310) with Neversoft's plugin chunks
(0x0294AF01...) - `thps3.ts` has the layouts:

- `.bsp` - an RpWorld: the material list, then the plane-section tree whose leaves (atomic sectors) are Neversoft's
  objects: each has its vertices (positions, prelit colours 255 = full, two uv sets), triangles (u16 material, 3 x u16
  vertex), a bin mesh (strips or lists per material) and Neversoft's extension: a u16 face flag per triangle (THUG2's
  collision flag meanings: 0x10 non-collidable, 0x40 trigger...; but 0x80 is the reverse of THUG2's - camera collides,
  where THUG2's is camera passes through: inverted) and the object's checksum
  name - the EnvironmentObject node of the same name. The same triangles are drawn and collided with.
- Materials: RW's material and texture (name, addressing), and Neversoft's extension: flags (0x02 invisible - the
  collision-only and trigger planes, 0x08 transparent, 0x10 decal), the terrain type (THUG2's numbering), the
  colour (0x80 = neutral), the blend as the PS2's alpha register ((A - B) * C + D, which THUG2's blend modes are named
  after), the fixed alpha, the uv flags (0x01 the uv wibble; 0x80 environment-mapped - the *_reflect textures:
  THUG2's env pass, their "wibble" numbers not used as a scroll - docs/pending/shimmer-2.md).
- `.tdx` - a texture dictionary of D3D8 texture natives: every texture THPS3 PC ships is 32-bit BGRA with one level
  (THUG2's 32-bit textures exactly; mip levels are made by the converter).
- `.dff` - clumps (the GameObjects: fans, doors, rubble, pumpkins...): only the ones there at the start are converted
  (static scene sectors, no collision; `--no-objects` leaves them out) - almost all are made by goal scripts. Each clump's declared size leaves out 12 bytes of its Neversoft extension.
- `.qb` - THPS4's token format (tools/level_port/qb.ts; Cruise Ship's node array has a `<...>`). Nodes use
  `Position`, z negated from the geometry's - as THPS4's, which THUG2 reads the same way: the geometry is kept as is.

## Names

Online a level is known only by its load script's checksum, so a level is named after THPS3's own load script
(levels.qb: Load_Foun -> `foun`: Level_foun, `levels\foun\`, foun_NodeArray) - the names THUGPRO's ports of these
levels use too, so the same level loads for both. Kept as `thps3_<lvl>`: the levels THPS3 doesn't offer in network
games (no `regular_level` in its master_level_list: Tut, SkateShop; the park editor's sets) and names THUG2 already
has (its own Load_AP / Load_LA: THUG2's classic Airport and Los Angeles, which THUGPRO plays instead of THPS3's). The
made objects' and materials' checksums keep the old prefix: the scenes, textures and collision are byte for byte as
before. Saved level-editor layouts under the old names: `node tools/thps3_levels/migrate_layouts.ts --game <folder>`.
Comparison with THUGPRO's own ports (read only): `node tools/thps3_levels/thugpro_compare.ts --thug2 <folder>
[--details]` - docs/pending/thps3-thugpro.md.

## What's converted

- Scene: a THUG2 sector per atomic sector with its meshes per material (the bin mesh's strips; triangle lists joined
  into strips), invisible materials left out, vertex colours halved (THUG2's 128 = full), the first uv set (RenderWare stores the sets one after
  another; THPS3's second is never written).
- Textures: 32-bit as they are, with mip levels (each 2 x 2 averaged) down to the one whose shorter side is 4, as
  THUG2's own (before 2026-10-21: at most 4 levels, down to 8 x 8 - distant tiled floors and ramps shimmered).
- Materials: no z bias in THPS3; the layers lying on other surfaces (decals, dirt and light layers, vents on ramps) get
  one from the geometry (tools/level_port/overlay_bias.ts), or they fight the surface under them for the depth buffer.
- Collision: a THUG2 object per atomic sector, every face with its flags and its material's terrain; THUG2's BSP
  trees, fixed-point vertices and reaches made (tools/level_port/geometry.convertCol).
- Nodes: EnvironmentObjects become LevelGeometry (their Links, to restarts for THPS3's scripts, dropped); restarts, rails, KOTH crowns kept.
- Capture the flag: THPS3's two flag bases (GenericNode Type CTF_1 / CTF_2 - gamemode.qb: CTF_1 is the blue flag,
  CTF_2 the red) become THUG2's TRG_CTF_Blue / TRG_CTF_Red flags and their _Base objects (THUG2's own flag models, a
  trigger script calling THUG2's Team_Flag), and each team gets TRG_CTF_Restart_<colour> at THPS3's restart of its
  team's type (CTF_1 / CTF_2) nearest to its base - the names THUG2's net\ctf.qb, SK4_Scripts and skater code look
  for. THPS3 has two teams: no Green / Yellow flags, no lobby team flags (TRG_Flag_*), no TRG_Team_Restart.
  With the compatibility profile `thugpro` (the default, `--compat none` leaves it out - tools/compat/thugpro, approved
  by the user): TP-10 puts the Red / Blue flags and bases where THUGPRO has them in Rio, Sub, SI, Tok, Shp and Ware
  (the team restart then THPS3's CTF restart nearest to the flag), and TP-11 adds THUGPRO's Green / Yellow flags (their
  restarts THPS3's multiplayer restarts nearest to them), the lobby's TRG_Flag_* and TRG_Team_Restart in the 9 levels
  THUGPRO shares; TP-12 then makes every network spawn (multiplayer, horse, team, CTF restarts) as THUGPRO has it -
  place, angles, types and their order in the node array (THUG2 starts player n at the n-th Multiplayer restart);
  added ones appended, ones THUGPRO dropped no longer spawns. Only places, angles, types and
  colours come from the data; the nodes are ours as above. THUGPRO's other hand edits that are data (the user's
  decision, 2026-10-20: everything THUGPRO changed, under the flag): TP-13 nodes it moved (peds, waypoints, particles,
  objects), TP-14 flags (CreatedAtStart, AbsentInNetGames, NetEnabled, TrickObject), TP-15 terrains, TP-16 nodes it
  took out (left out as hidden), TP-17 renames, TP-18 the rails it added (from node data; a cluster we don't have is
  left out), TP-19 collision edits (duplicate faces it took out, three decals without collision) - applied to THPS3's
  node array before conversion and to the collision; `thugpro_inventory.ts` lists all THUGPRO's edits and `--applied`
  checks these in our output. The data is read from
  THUGPRO's install by `thugpro_compare.ts --extract-thugpro tools/compat/thugpro/thps3_levels.json`.
- Scripts, peds, vehicles, objects (`--no-scripts` leaves them out): the level's scripts translated to THUG2's
  (tools/level_port/script_port.ts) and run from a setup script; peds and crowds as THUG2's ped AI, resting birds and
  sharks as THUG2's animals, traffic as THUG2's cars (Car_Generic01), other vehicles and moving game objects as
  LevelObjects of their THPS3 models, proximity nodes as ghosts watching their radius - what's brought and what's
  left out per level: docs/pending/thps3-scripts.md. Obj_RotX / Y / Z with `deceleration` (a drag in THPS3, "slow
  down to `speed`" in THUG2 - as is, the turn runs away) are rewritten to what THPS3 does: thps3_rotation.ts. Kill zones: trigger scripts that call SK3_KillSkater(_Water) or a
  level script that does (CJR_Foun_Kill_Skater_Molten...) become THUG2's SK3_KillSkater(_Water) with the call's
  messages and colour and a restart (tools/level_port/nodes.ts).
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
