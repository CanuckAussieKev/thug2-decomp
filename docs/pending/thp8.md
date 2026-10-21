# thp8 - Tony Hawk's Project 8 (Xbox 360 data) levels in THUG2 (tools/thp8_levels) - phase 1

Agents "thp8" (stopped by accident on 2026-10-21 with the converter done) and its continuation (2026-10-22). How to run
it: tools/thp8_levels/README.md. Source data: the user's own THP8 disc data as the fan-made recompilation installs it,
`C:\Games\THP8\game\DATA` - **only the game data is read**, never the recompilation's programs, DLLs or code. Nothing
of THP8 is in the repo. No public format notes for THP8's scenes, collision or textures were found (thps-formats has no
THP8 reader); everything was worked out from the data plus the public descriptions of the Xbox 360's texture fetch
constant / tiling and of Havok's packfile layout. No leaked source, THUGPRO not involved.

## Result

- `node tools/thp8_levels/thp8_levels.ts convert [zones] --thug2 <game> --thp8 <thp8>` converts the fifteen zones
  (~30 s for all): eleven city zones (Suburbs, School, School inside, Shops, Slums, Factory, Capitol, City park, Skate
  park, Fun park, Parking), Training, the Cartoon world, the skate shop, the bedroom, plus THP8's default sky
  (`thp8_sky`). `--thp8` / `--thug2` go through tools/lib/install.ts's resolveInstall (THP8 marker:
  `DATA\COMPRESSED\compress.toc.xen`). A partial convert remakes only the zones named and updates their levels.json
  entries (ported_levels.writeManifest merges; the staging folder was wiped whole before 2026-10-22 - fixed).
- `selfcheck`: every file of every zone archive, the skies and qb.pak read (1132 files, 0 failures); THUG2's LA /
  TestLevel files round-trip.
- Level select: a "TONY HAWK'S PROJECT 8" section after THAW's (tools/menu_level/level_select.ts; numbers 110-124;
  placeholder signs - THUG2's training sign; no globe stars: THP8's city is fictional). make_menu_level.ts picks up
  `thug2-decomp-thp8` like the other games.
- Tests (tools/thp8_levels/thp8_levels.test.ts, synthetic data only): pak, toc, big-endian qb items (with includes),
  scene strips and uv layouts, Xenos tiling / untiling, DXT encode / decode, node transforms, terrains, and (2026-10-22)
  a Havok packfile built in the test - classes from `__types__`, members by name, local / global / virtual fixups
  (mutation-checked: the member offset read from the wrong field fails it).

## Formats (tools/thp8_levels/*.ts header comments have the layouts)

- `COMPRESSED\*`: one raw deflate stream each; `compress.toc.xen` (TOC1) / `.lst.xen` list them.
- `.pak.xen` + `.pab.xen`: THAW's pak, big-endian. A zone: the zone (flags 0), `_gfx` / `_sfx` sub-zones (2 / 4), models.
- `.qb` / `.nqb`: THAW's item format with big-endian words; structure entry type byte 0x80 | type; unnamed type 0x1A
  = include (the `<zone>_NodeArray_compressed_node_n` structures) or a flag. Script bodies LZSS'd THUG2 tokens,
  little-endian. Names: `PAK\dbg.pak.xen`.
- `.geom`: THAW's layout grown - 0x134-byte materials (up to 16 texture checksums, a parameter block naming the shader
  template, texture count, terrain), 0xBABEFACE, 0x50-byte sectors, 0x60-byte sector nodes, 0x80-byte meshes; vertices
  float3 + packed normal + 0-2 tangent words + ARGB + half-float uv pairs; u16 strips split by 0x7FFF.
- `.tex`: 0xFACECAA7, 0x28-byte records -> base level, packed mips, D3D header whose GPU fetch constant gives format
  (DXT1 / DXT2-3 / DXT4-5 / DXN / 8888), 16-bit swap, 32x32-block tiling.
- `.hkc` (collision): Havok 4.0 packfiles (32-bit, big-endian), read by member name from the class descriptions in the
  file. Fixed rigid bodies of MOPP trees over storage meshes, or list shapes of transformed boxes / cylinders /
  capsules / spheres. Material word = terrain << 16 | THUG2's face flags. Bodies unnamed: matched to the scene sector
  with the same box.
- Instances: a node with `MasterName` places a `__MASTERNODE_` sector at Pos / Angles (radians, R = Ry Rx Rz) / scale.

## The world split

THP8's city is one streamed world (~800 x 1000 m, ~550,000 drawn triangles, ~180,000 collision triangles, ~150 MB of
textures together) - far beyond THUG2's memory, so each zone is its own level (as THAW's areas), moved so the middle of
its collision is at the origin, metres x 39.37. The network areas `z_online_<x>` that are a single zone add their
barrier walls as collision only. Per zone: 10,000-165,000 triangles (the Cartoon world largest), 42-167 textures.
Not converted: z_mainmenu, z_mocap, z_navmesh, z_peds, z_anim, z_world, z_viewer, z_testlevel*.

## In the game (workspace C:\Games\THUG2-4, under the lock; DLL built from this branch = main's C++)

2026-10-22 (continuation; DLL = this branch's verify build, i.e. main's C++ at 5413a11; `NOT run by a person yet`):

- All fifteen zones convert (~26 s for all, `convert --thug2 C:\Games\THUG2-4`), make_menu_level installs them (the
  PROJECT 8 section's fifteen signs listed in its output), and **all fifteen load into free skate** with
  `run_game.ps1 -Level thp8_<zone> -Commands "waitfor level thp8_<zone> 170;wait 180"`: 300 frames each, no crash, no
  hang, no dialog, nothing in the log (no debug-CRT / run-time check reports, no script asserts); pools held (no
  pool ran dry - the largest, the Cartoon world with 165,000 triangles and 825 instances, and the Capitol with
  178,000, load as the small ones do). One loop's parking run printed nothing (the funpark game before it was slow to
  close); both again on their own: fine.
- Captures (%TEMP%\thp8shots, not kept): the skater standing at each zone's first restart, textures, rails and
  collision right (skater on the ground, no falling through), the city zones recognisable (the skate park's half pipe
  and shelters, the school's library steps, the shops' bunting, the slums' bowl, the factory's conveyor, the cartoon
  world's flower beds and painted roads, the bedroom, the skate shop's shoe wall).
- Fixed on the way: (1) **grey spikes in the sky** behind every zone: the cloud layers were cut at the horizon by their
  triangles' middles, which left the atmosphere dome showing through the gaps as tall grey triangles (confirmed by
  leaving the clouds out: the spikes went with them) - now only the triangles wholly below the horizon are left out
  and the clouds' vertex alpha fades to nothing at the horizon (cloudFade, full from sin 0.35 up). (2) **white haze in
  the skate shop / bedroom / light shafts**: THP8's glows (textures on black that its shaders add) were alpha-blended
  at full strength - now drawn adding (in the transparent bucket: without PASS_TRANSPARENT a floor halo's black
  square showed) at the strength of their material colour: the four floats before the first texture's checksum in
  the parameter block (R, G, B, A; the skate shop's lamp shafts A = 0.09). Glows are picked by sector name
  (`GLOW_SECTOR`: 1-26 sectors per zone where they exist); the blend mode itself isn't decoded (a word at +0x24 of
  the parameter block is 8 on most glows but also on foliage-like values: not a clean field).
- Not fixed, seen: a long striped band across the skate park's start (a cable or tape - not looked into); the
  horizon flat (THP8's periphery hills are there, as `SF_Perif_*` sectors, but low); levels look flat-lit and some
  surfaces pale (THP8's material colours - tints in the same float block, e.g. trees 0.6 / 0.68 / 0.58 - not applied
  except on glows: in some blocks those floats are 0, 0, 0 and mean something else).

## Left for phase 2

- Gaps; kill zones written as `Skater:KillSkater` member calls; scripts, peds, vehicles, game objects (the zones' own
  .mdl / .skin), particles, the `_gfx` / `_sfx` sub-zones, sounds, music, goals.
- Materials: multi-layer (blended terrain) materials show only their first layer; normal / specular / ambient /
  shadow maps not drawn; lighting from THP8's ambient / shadow maps instead of THUG2's default (the levels look flat).
- Billboards (camera-facing quads) left out, as THAW's.
- The network areas of several zones (central, industrial, funskatepark) - a combined level if the pools / memory take
  it (Skate park + Fun park is the likely first pair).
- THP8's English level names (localised text not decoded): sign names are ours; real level-select signs.
- `NOT run by a person yet`: agents' load checks only.
- Material tints: THP8's material colour (the float block before the first texture) on more than glows, once it's
  known when those floats are a tint (not 0, 0, 0 houses).
- The skate park's striped band; the skate shop's made restart puts the skater by the counter of a room open on one
  side (a menu set).

## Phase-2 plan

1. Scripts / triggers: THP8's node array trigger scripts through the THAW script translator (tools/thaw_levels) with
   the big-endian qb reader - kill zones via KillSkater, then gaps (THP8's gap scripts in the zone qbs).
2. Look: the second material layer (blend by vertex alpha, as THAW's multi-pass materials), vertex lighting baked from
   THP8's ambient maps.
3. Game objects: the zones' .mdl models as level objects (THAW's model path), then peds / cars.
4. Merged areas: try Skate park + Fun park (and the other `z_online_*` groups) as one level and watch the pools.

# thp8 phase 2 (2026-10-22, agent "thp8 phase 2", worktree branch; workspace C:\Games\THUG2-4)

Data only from `C:\Games\THP8\game\DATA` (never the recompilation's programs). No leaked source; THUGPRO not involved.
DLL in the workspace for the runs: this branch's verify build (main's C++ at 55de9dd - no C++ changed).

## Step 1 - the look (materials, layers, light)

- **Material library decoded** (`FXFILES\MaterialLibrary.bin`, "MATL"): templates by name checksum (62:
  DiffuseTerrain_3Pass, NormalMapTerrain_3/4Pass, DiffuseSpecTerrain_3Pass, NormalMap_SeperateChannels, Decal,
  VertexColorOnly, Billboard...), each with its techniques (0x140-byte descriptions, +8 the name checksum:
  "_ENGINE_Default_Pass0_Pass1_InteriorLighting"...). A material's parameter block: +0x18 float4 count, +0x20 its
  technique, then the float4s, then the textures (materials.ts has the layout and the roles).
- **Texture roles**: pass 0 diffuse (+ normal DXN + specular per template), each further pass a diffuse map and a
  blend mask, last two the zone's **ambient map** (sky light, RGB; 1024 atlas) and **shadow map** (sun, grey; 2048
  atlas) - shared by every lit material, read through the vertex's last two uv fields. Found from texture use across
  materials (a texture that is nearly always second to last / last), checked on the decoded atlases.
- **Layers** as THUG2 passes (blend mode 5: by the pass texture's alpha x 2 x vertex alpha - THUG2's generated pixel
  shader, src/game/gfx/pixel_shaders.cpp), the mask baked into a texture made per (diffuse, mask, opacity) or into the
  vertex alpha when it has its own uv field; THUG2's PassAlphaFlags (0x004D1D90) = per pass PASS_IGNORE_VERTEX_ALPHA
  (0x1000), checked in the exe. Pass colours (tints + opacity) from the float4s (which float4 is which per template:
  read from how they vary over seven zones). The skate park's wave got its red paint stripes (the "striped band" -
  seen black / white before), the ledges their red edges, posters / stickers / RENTALS letters their layers.
- **Lighting** baked into the vertex colours (formula and scales: README / compromises section 10), triangles refined
  where the maps vary (materials.refine, crack-free inside a mesh), triangle budget per zone (MAX_TRIANGLES 190,000).
  Interiors by technique name. THUG2's level lights (skater) from each zone's `<zone>_tod_manager`.
- **Fixed**: 8888 textures had their alpha as blue (every 8888 - the interiors' light maps, env maps: the factory and
  skate shop came out blue-washed).
- Captures (workspace Screenshots\p2_*, p3_*, p4_*: start view + an overview of every zone; not kept in the repo):
  skate park before (flat, shot-1 of the first run) / after (lit, red wave stripes, the bowl's target), suburbs,
  school, slums, shops, capitol, cartoon, factory and skate shop (warm interiors after the 8888 fix).

## Step 2 - gaps, kill zones, restarts

- Scripts through tools/thaw_levels/thaw_scripts.ts (shared, small additive changes: `scriptToks` takes the inline
  structures' byte order; THP8's tokens 0x4B "$" dropped and 0x4C wide strings (UTF-16BE) as strings - THAW's PC
  scripts have neither: checked over all 23,354 of them). Sources: the zone's qbs, its sub-zones', every other zone's.
  English text: qb.pak's string globals replace the names scripts use (gap names: "Over the Steps").
- Per zone 180-500 scripts brought; gaps' StartGap / EndGap kept, goals / chalk gaps / sounds / cameras left out.
- Kill zones: member calls (Skater:KillSkater) read; THP8's trigger volumes (0x20 on non-collidable faces) as THUG2
  trigger faces, the volume faces split into their own object named after the collision-only node in them (the
  Capitol's Z_Center_TRG_Water_Kill now has 64 trigger faces - it had none: phase 1's kill zone there was dead).
- Restarts: network spawns also Player1 (soloRestarts), nodes.groundRestarts / netGameNodes as the other converters.
- Moving objects (LevelObject) in their own space; grid pieces (Vehicle nodes) left out.
- Crashes found and fixed on the way: the slums **hung** loading (Z_Ghetto_Pipe_Box_Launch: an endless loop whose
  wait the translator left under a never-true `if` - such scripts are now emptied, endlessLoop); the fun park
  **crashed** (RunScriptOnObject {...} of a script not brought: ParseArguments on nothing - struct arguments
  flattened, such statements made GotParam, unrunnable).
- Not verifiable by automation (for the user): gaps firing (skate the skate park's front stairs - "Over the Steps",
  the slums' Hemlock rail gap...), the Capitol lake / city park drown kills, the gym fans turning in the school.

## Step 3 - objects

- THP8's node arrays hold few objects: no Pedestrian / car nodes made at the start (THP8 spawns its peds and traffic
  from its grid / ped systems at run time; the shops' two peds are story ones), its Vehicle nodes are grid pieces (left
  out, step 2), its particles are .pfx systems made by scripts (CreateGlobalFlexParticleSystem - the _gfx sub-zones'
  ProximNodes run them) and its sounds SoundEvents: none has a THUG2 counterpart - documented, not ported. The
  sub-zones' scripts are translation sources (step 2); their nodes aren't converted.
- **Props**: THP8's bouncy physics props (GO_ game objects with the Bouncy flag, made at the start, no script) as
  THUG2 bouncy LevelObjects with THP8's own models (from ZONES\global.pak or the zone's archive): 1-13 a zone (cafe
  chairs and tables, cones, cans, bottles, barrels, pallets, the cartoon world's skulls and castle pieces, the fun
  park's jewels). Seen in the game: the skate park's table and chair under the snack hut (p5_crete_props).
- Script objects (Ghost game objects), moving LevelObjects, waypoints: made by thaw_scripts.PortedNodes (step 2).
- Interiors re-tuned (ambient x 6 / (1 + 4 x)): the factory's floor was white (p5_factory).
- All 15 zones load (p4 run: every zone, start + overview captures, no exception / run-time check / pool dry in the
  log). For the user: bump a cafe chair / cone (do they bounce?), the props' look.
