# thps4_levels - Tony Hawk's Pro Skater 4 levels in THUG2

Converts the levels of your own THPS4 (PC) install to THUG2's formats and puts them in THUG2's level select
(its THPS4 section: Up / Down in the level select switches games; signs and globe stars from
tools/menu_level/level_select.ts). Nothing of either game is in the repo: the files are
made from your copies each time.

## Running it

Needs Node 24+ (`npm install` once - docs/typescript.md), NeverScript (`tools\build_neverscript.ps1` ->
`out\neverscript\ns.exe`) and, to install the levels, tools/menu_level/make_menu_level.ts.

    node tools/thps4_levels/thps4_levels.ts convert [--thps4 C:\Games\THPS4] [--thug2 C:\Games\THUG2]
    node tools/menu_level/make_menu_level.ts

`convert` reads `<thps4>\Game\data\levels` and writes `<thug2>\thug2-decomp-thps4\` (not read by the game):
`levels\<lvl>\<lvl>.scn.xbx / .tex.xbx / .col.xbx / .qb`, the skies the same way (`alc_sky`), and `levels.json`.
Name levels to convert only some (`convert Alc Zoo`); the default is every THPS4 level with a scene: Alc, Cnv, Hof,
Jnk, Kon, Lon, SF2, Sch, Zoo, SkateShop, Motox (Foo has no scene; the park editor sets are left out).

Names (online play with THUGPRO: a level is known online only by its load script's checksum): each level is named as
THPS4 names it - `alc`, `cnv`, `hof`, `jnk`, `kon`, `lon`, `sf2`, `sch`, `zoo`, `motox` (Load_alc, Level_alc,
alc_NodeArray), as THUGPRO's THPS4 ports are; the skate shop stays `thps4_skateshop` on disk (THUG2's own skate shop
is `skateshop`, and the front end's backdrop loads it by that name) and its load script is `Load_practice` - THUGPRO's
name for it, from the compatibility profile (`tools/compat/thugpro/thps4_levels.json`, TP-20). The profile also
gives the skate shop THUGPRO's King of the Hill crown and 8 multiplayer spawns (TP-21: places only, extracted with
`thugpro_compare.ts --extract-thugpro`; the nodes are made in our form by `thps4_levels.addNetNodes`). `--compat none` leaves
the profile out (Load_thps4_skateshop); `--legacy-names` names every level `thps4_<lvl>` as before. Saved level editor
layouts of the old names: `node tools/thps4_levels/migrate_layouts.ts --game <folder>`. Comparison with THUGPRO's
install (read only): `node tools/thps4_levels/thugpro_compare.ts --thug2 <folder>` (docs/pending/thps4-thugpro.md).

`make_menu_level.ts` (which makes the data overlay `<thug2>\thug2-decomp-data` from scratch on each run) then copies
the converted levels into the overlay and adds them to `Levels.qb` and the level select. Run it again after every
`convert`. Delete `thug2-decomp-thps4` and run it again to take the levels out.

The THPS4 skate shop is also the front end's backdrop (`[features] menu_backdrop`, on by default: see
src/game/front/menu_backdrop.h): with it installed, the front end shows it behind the menus and the overlay's
standard main menu; without it the game's own front end is used.

    node tools/thps4_levels/thps4_levels.ts selfcheck

reads THUG2's LA and TestLevel scene / collision / textures (and any `--extra Data\pre` archives) and writes them
back - they must come out byte-identical - and parses every THPS4 level file.

## What's converted

- Scene: materials (up to 4 passes) and sectors; THPS4's per-sector vertex arrays become THUG2's per-mesh vertex
  buffers (position, normal if any, colour, a uv set per pass), strips kept. Vertex colours (levels and skies) have
  their rgb halved: THPS4's PC files light rgb at 255 = full (alpha at 128 = full, kept), THUG2 takes 128 = full -
  unhalved, every level drew exactly twice as bright, light colours clipped to white (washed out). THPS4's materials
  have no z bias: the layers lying on other surfaces get one from the geometry (tools/level_port/overlay_bias.ts), or
  they z-fight (shimmer) with the surface under them.
- Textures: the same format (DXT1/2/5); THPS4's few 8-bit paletted ones (skies, London) become 32-bit, unswizzled.
- Collision: version 8 -> 10, with THUG2's BSP trees, fixed-point vertices, small faces and the per-face "reach"
  bytes THUG2's ray test needs. Face flags: THPS4's camera flag inverted, flags THUG2 doesn't use dropped.
- Nodes: restarts (the Player1 restart first), rails, level geometry, KOTH crowns; kill zones (water, out of bounds)
  get THUG2's SK3_KillSkater(_Water). The restarts keep THPS4's trigger scripts (MakeSkaterGoto DropIn: the skater
  drops in from the ramp's lip). The game modes' flags (TRG_CTF_Red / _Base..., TRG_Flag_Red...: capture the flag and
  the lobby's team flags - THUG2's own flag models and Team_Flag scripts) are kept for THUG2's net scripts.
- Scripts and moving objects (`thps4_scripts.ts`; `--no-scripts` leaves them out): LevelObjects, ghost game objects
  and waypoints kept, run by THPS4's own scripts translated to THUG2's (tools/level_port/script_port.ts: the
  sectors' / rails' trigger scripts - gaps -, the objects', the level's setup / startup scripts); the vehicles moved
  by scripts (trams, rides, trains, cranes...) LevelObjects of their THPS4 models; peds that don't walk stand as THUG2
  peds; pigeons as THUG2's own; goals, goal objects, the other animals, particles and proximity nodes left out. See
  docs/pending/thps4-scripts.md.
- Peds and cars (every level: `PEDS_CARS` in thps4_levels.ts; `tools/level_port/peds_cars.ts`): made again
  as THUG2's own, run by THUG2's scripts, added after the converted nodes. The cars (Car_Generic01 in THPS4) keep
  their model when THUG2 has it (`Data\models\veh\...`, else a sedan) and drive the same waypoints with THUG2's
  Car_Generic01; the people walking by (THPS4's Obj_FollowPathLinked peds) walk their paths with THUG2's ped AI
  (Ped_InitAI, PedAI walk waypoints, random_male_profile); the pros on the halfpipe (Skateshop_PedProTest's two lines)
  that THUG2 has (Burnquist, Koston, Margera, Mullen, Hawk: PedPro_<name>) skate it with THUG2's ped AI (the two lines
  one loop, each lip a Vert_Flip, as THUG2's LA vert skaters) from their places on the deck; the other pros' places
  get random skateboarders standing there. `make_menu_level.ts` copies them into the front end too (seen after SKATE
  NOW).
- Level structure: THPS4's name, sky and lighting (from its Levels.qb); no music, no goals.

The THUG2 writers, the geometry conversion, the node arrays and the installing are shared with `tools/thug1_levels`
(`tools/level_port`); this folder has THPS4's own reader (`thps4.ts`) and `thps4_levels.ts`. `levels.json` also
carries `game`, `location` and `icon` per level for the level select (`tools/level_port/ported_levels.ts`).

Format reference: the io_thps_scene Blender add-on (denetii) - read as documentation only (it has no licence); the
formats were checked against THUG2's loaders (see docs/pending/thps4.md).
