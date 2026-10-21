# thug1_levels - Tony Hawk's Underground levels in THUG2

Converts the levels of your own THUG1 (PC) install to THUG2's formats and puts them in THUG2's level select
(THUG1 NEW JERSEY, THUG1 MANHATTAN, ... after SKATE SHOP and the THPS4 levels). Nothing of either game is in the
repo: the files are made from your copies each time.

## Running it

Needs Node 24+ (`npm install` once - docs/typescript.md), NeverScript (`tools\build_neverscript.ps1` ->
`out\neverscript\ns.exe`) and, to install the levels, tools/menu_level/make_menu_level.ts.

    node tools/thug1_levels/thug1_levels.ts convert [--thug1 C:\Games\THUG1] [--thug2 C:\Games\THUG2] [--static]
         [--compat thugpro|none] [--legacy-names]
    node tools/menu_level/make_menu_level.ts

`convert` reads `<thug1>\Game\Data\pre` (`<LV>scn.pre`, `<LV>col.pre`, `<LV>.pre`, and `qb.pre`'s Levels.qb) and
writes `<thug2>\thug2-decomp-thug1\` (not read by the game): `levels\<name>\<name>.scn.xbx / .tex.xbx / .col.xbx /
.qb`, the skies (`<name>_sky`, scene and textures - no collision, as THUG2's own skies) and `levels.json`.

`<name>` is the level's name in THUG2 (its folder, `Load_<name>` / `Level_<name>`, `<name>_NodeArray`). Online a
level is known only by its load script's checksum, so it's THUG1's own name for the level (its Levels.qb's `level=`
loaded by `Load_<level>`): `nj`, `ny`, `fl`, `sd`, `hi`, `vc`, `sj`, `ru`, `vn`, `hn`, `sc2` - the names THUGPRO's
ports load too. A name THUG2 already uses keeps the `thug1_` prefix (`thug1_sc`, `thug1_dj`, `thug1_ph`,
`thug1_testlevel`: THUG2 has SC, DJ, PH, TestLevel of its own), as do the levels THUG1 doesn't load by their own
name (`thug1_nj_skateshop`, `thug1_skateshop`, `thug1_cas_bedroom`). Hotter Than Hell (THUG1's SE - THUG2's SE is
its own) is `hh`, THUGPRO's choice: TP-30 in the compatibility register (tools/compat/thugpro). `--compat none`
leaves the register out (`thug1_se`); `--legacy-names` names every level `thug1_<code>` as before. Name levels to convert only some (`convert NJ SE`). The default is the 9 main levels (NJ, NY, FL, SD,
HI, VC, SJ, RU, SE = Hotter Than Hell), the 6 classic ones (SC Old School, DJ Old Downhill Jam, PH Old Philly, VN Old
Venice, HN Old Hangar, SC2 School II) and NJ_Skateshop; `skateshop` (THPS4's shop again), `CAS_BEDROOM` and
`TestLevel` convert when named.

The table `LEVELS` in `thug1_levels.ts` is the list: code, sign text, real-world location, a THUG2 sign texture that
suits it, the level select number (40-58), default or not. `levels.json` carries the same per level (`game`,
`location`, `icon`, `level_num`, ...: see `tools/level_port/ported_levels.ts`).

`make_menu_level.ts` (which makes the data overlay `<thug2>\thug2-decomp-data` from scratch on each run) then copies
the converted levels of both `thug2-decomp-thps4` and `thug2-decomp-thug1` into the overlay and adds them to
`Levels.qb` and the level select. Run it again after every `convert`. Delete `thug2-decomp-thug1` and run it again to
take the levels out.

    node tools/thug1_levels/thug1_levels.ts selfcheck

reads THUG2's LA and TestLevel scene / collision / textures (and any `--extra Data\pre` archives) and writes them
back - they must come out byte-identical - and parses every level file in every THUG1 archive (scenes, collision with
its BSP trees, textures written back byte-identical, qb files): 333 files.

## THUG1's formats (the differences from THUG2's)

- Archives: `.pre` is THUG2's `.prx` format (version 0xABCD0003, LZSS) - `tools/level_port/prx.ts`.
- Textures (`.tex.xbx`): THUG2's format. The classic levels' skies are 8-bit paletted and Xbox-swizzled (as THPS4's):
  made 32-bit, unswizzled.
- Scene (`.scn.xbx`): THUG2's materials byte for byte; the sectors are THPS4's layout (one vertex array per sector:
  positions, normals, packed weights + bone indices, uv sets, colours, wibble indices) with THUG2-style meshes in
  front of each index list (sphere, box, flags, material, LOD count, per LOD a u32 count + u16 strip indices - every
  shipped mesh has one LOD). Vertex colours are 128 = full, as THUG2's (no halving, unlike THPS4's skies).
- Collision (`.col.xbx`): version 9 = THUG2's version 10 without the faces' reaches (no byte per large face after
  the faces, the small faces' 4th byte 0, the objects' reach scale 0). The vertices (fixed point included), faces and
  BSP trees are THUG2's: kept; the reaches are computed (zeros make faces all but uncollidable in THUG2's ray test).
  Face flags have THUG2's meanings (0x80 = the camera passes through; THPS3 / THPS4's is the reverse, and so is
  THUG1's DJ / PH / SC / skate shop data: inverted there); 0x2000 (every face), 0x4000 and
  0x8000 are dropped.
- QB: THUG2's token format exactly (the random tokens have their weights). Node arrays share common parts as global
  structures named in the nodes (`ncomp_RailNode_574`): expanded into the nodes. Nodes use `Pos`.

## What's converted

- Scene, textures, collision as above; sectors with weights (skinned) are left out.
- Nodes: restarts (the Player1 restart first), rails, level geometry, KOTH crowns; kill zones: a trigger script
  calling `SK3_Killskater(_Water)`, a level's own `xx_KillSkater` or a level script that calls one (NY_KillWater)
  becomes THUG2's `SK3_KillSkater(_Water)` with the call's restart (`nodename=`), messages, colour and `Bail` /
  `NoMessage`.
- The level's life (`thug1_port.ts`; `--static` leaves it out): THUG1's nodes are THUG2's predecessors, so they are
  kept nearly whole - waypoints, ledges / ladders (ClimbingNode), proximity nodes, sound emitters; LevelObjects as
  LevelObjects (moving and bouncy: cones, trash cans, chairs, with their rigid body settings); peds as THUG1's own
  people (`thug1_peds.ts`: their THUG1 profile's models, on THUG1's THPS5_human / Ped_Female skeletons, with THUG1's
  animations and its Ped_InitAI walking lists - below; `--thug2-peds`: THUG2's THPS6_human people, their THUG1
  profile when THUG2 has it, else a random one); vehicles and game objects whose model THUG2 has (Data\models - THUG2
  ships most of THUG1's cars); particle objects whose texture THUG2 has; the dynamic lights (LevelLight nodes, as
  THUG2's own levels have them). Goal nodes are left out.
- THUG1's people (`thug1_peds.ts`; docs/pending/ped-models.md): the skins of the profiles' CAS parts (THUG1's
  `<LV>ped.pre`) to `models\THUG1_Peds\...` (thug1.toThug2Skin: THUG2's skinned meshes, `thug2_scene.skinnedMesh`),
  their THUG1 profiles / parts added to THUG2's profile_ped.qb / cas_ped_m.qb / cas_ped_f.qb as `THUG1_<name>`
  (`<staging>\peds.json`, applied by make_menu_level), random people as up to 6 fixed variants per profile and level;
  THUG1's animations to `anims\THUG1\...`, loaded by the level's load_level_anims under THPS5_human (men) /
  THUG1_Ped_Female (women: their nodes' ReferenceChecksum), with every animation THUG2's ped AI may play (THUG1's of
  that name, else one of the same role).
- Scripts: from the nodes' trigger scripts and the level structure's startup_script / setup_script, through
  everything they call, from the level's .qb / _scripts.qb / _sfx.qb and THUG1's level designers' shared files - by
  `tools/level_port/script_port.ts` (THUG1's long if / else / switch made THUG2's jump forms, `object:command` calls
  checked against the object's components and run only while it is alive). Left out: the goal manager, panels,
  cutscenes, the skater's state, commands THUG2 doesn't have, animations / sounds THUG2 doesn't load. The classic
  levels' `DuplicateTrigger` gap sectors get the trigger script of the node they link to.
- Level structure: THUG1's name, sky, lighting, startup_script and setup_script (from its Levels.qb); no music, no
  goals.
- Drivable cars (PC-THUG1-22): THUG1's driving scripts (`scripts\game\player_controlled_vehicle.qb`,
  `scripts\game\misc\car_scripts.qb` - THUG2 ships both empty) go to `<out>\scripts\` in their THUG2 form (`driving.ts`)
  and make_menu_level installs them in the overlay; VC's limo is parked by the SJ restart and drivable ("Press ... to drive"), its goal counted as won.
  The other levels' drivable cars stay parked or absent (docs/level-port-compromises.md, THUG1).

The shared code (THUG2 writers, geometry conversion, nodes, installing) is in `tools/level_port`, used by
`tools/thps4_levels` too.

## THUGPRO (online play with THUGPRO's own THUG1 ports)

    node tools/thug1_levels/thugpro_compare.ts [levels...] [--thug2 C:\Games\THUG2-3] [--details] [--json out]
    node tools/thug1_levels/migrate_layouts.ts --game C:\Games\THUG2-3 [--dry-run]

`thugpro_compare.ts` reads THUGPRO's install (read only: `%LOCALAPPDATA%\THUG Pro\data\pre`) and THUG1's node arrays
and compares each converted level three ways: the load script's checksum (what the network carries), every node
shared by name at the same place, the King of the Hill crowns, the multiplayer restarts (and which differ by name), the
CTF / flag / team game-mode nodes, collision faces unreachable through their BSP trees; `--details` adds the node
classes, the trigger scripts only THUGPRO's nodes run and the nodes it moved. `migrate_layouts.ts` renames saved
level-editor layouts (`thug2-decomp-layouts`) from the `thug1_<code>` names to the current ones (no record moves: the
frame is THUG1's own on both sides). The findings: docs/pending/thug1.md, "THUGPRO".

`thugpro_compare.ts --details` also lists THUGPRO's hand edits against THUG1's own data, by kind (`thugpro_edits.ts`
`inventory`: nodes moved / renamed / reused / added / removed / changed with the keys and flags that changed, level
scripts, scene sectors, collision objects and face flags). `thugpro_compare.ts --extract-thugpro` writes THUGPRO's
spawn edits into the compatibility profile (TP-31: `tools/compat/thugpro/thug1_levels.json` `spawns`), which
`convert` applies (`applySpawns`); the comparison's `spawns` column checks the result (names, places, order).
