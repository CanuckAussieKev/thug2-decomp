# thps4_levels - Tony Hawk's Pro Skater 4 levels in THUG2

Converts the levels of your own THPS4 (PC) install to THUG2's formats and puts them in THUG2's level select
(THPS4 ALCATRAZ, THPS4 CARNIVAL, ... after SKATE SHOP). Nothing of either game is in the repo: the files are
made from your copies each time.

## Running it

Needs Python 3 and NeverScript (`tools\build_neverscript.ps1` -> `out\neverscript\ns.exe`).

    python -I tools/thps4_levels/thps4_levels.py convert [--thps4 C:\Games\THPS4] [--thug2 C:\Games\THUG2]
    python -I tools/menu_level/make_menu_level.py

`convert` reads `<thps4>\Game\data\levels` and writes `<thug2>\thug2-decomp-thps4\` (not read by the game):
`levels\thps4_<lvl>\thps4_<lvl>.scn.xbx / .tex.xbx / .col.xbx / .qb`, the skies the same way, and `levels.json`.
Name levels to convert only some (`convert Alc Zoo`); the default is every THPS4 level with a scene: Alc, Cnv, Hof,
Jnk, Kon, Lon, SF2, Sch, Zoo, SkateShop, Motox (Foo has no scene; the park editor sets are left out).

`make_menu_level.py` (which makes the data overlay `<thug2>\thug2-decomp-data` from scratch on each run) then copies
the converted levels into the overlay and adds them to `Levels.qb` and the level select. Run it again after every
`convert`. Delete `thug2-decomp-thps4` and run it again to take the levels out.

The THPS4 skate shop is also the front end's backdrop (`[features] menu_backdrop`, on by default: see
src/game/front/menu_backdrop.h): with it installed, the front end shows it behind the menus and the overlay's
standard main menu; without it the game's own front end is used.

    python -I tools/thps4_levels/thps4_levels.py selfcheck

reads THUG2's LA and TestLevel scene / collision / textures (and any `--extra Data\pre` archives) and writes them
back - they must come out byte-identical - and parses every THPS4 level file.

## What's converted

- Scene: materials (up to 4 passes) and sectors; THPS4's per-sector vertex arrays become THUG2's per-mesh vertex
  buffers (position, normal if any, colour, a uv set per pass), strips kept. Skies' vertex colours are halved.
- Textures: the same format (DXT1/2/5); THPS4's few 8-bit paletted ones (skies, London) become 32-bit, unswizzled.
- Collision: version 8 -> 10, with THUG2's BSP trees, fixed-point vertices, small faces and the per-face "reach"
  bytes THUG2's ray test needs. Face flags: THPS4's camera flag inverted, flags THUG2 doesn't use dropped.
- Nodes: restarts (the Player1 restart first), rails, level geometry (THPS4's scripted LevelObjects as static
  geometry), KOTH crowns. Peds, vehicles, game objects (THPS4 models), goals and scripts are left out; trigger
  faces are kept only for kill zones (water, out of bounds), which get THUG2's SK3_KillSkater(_Water).
- Level structure: THPS4's name, sky and lighting (from its Levels.qb); no music, no goals.

Format reference: the io_thps_scene Blender add-on (denetii) - read as documentation only (it has no licence); the
formats were checked against THUG2's loaders (see docs/pending/thps4.md).
