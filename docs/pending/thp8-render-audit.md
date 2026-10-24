# thp8-render-audit - z-fighting and missing textures in the Project 8 levels (2026-10-24)

The user: "There is a lot of z-fighting in Project 8 levels. Especially in Beaver Valley. In addition to that, Project
8 is also missing a lot of textures. Can you build two tools, one is a z-fighting detection tool, and the other is a
missing texture tool?" - with a screenshot of BEAVER VALLEY (thug2_2026-10-24_17-52-34: skater at (-5187.9, 36.3,
3826.1)): the yard between the fence and the kerb flat untextured grey.

This round builds the tools and reports; **the converter is not changed** (the only edits to it: `baseTexture` and
`xenos.unconverted` pulled out of convertScenes / toThug2 so the audit uses the converter's own choice - the same
expressions, no change to what it makes; `materials.LAYERED` exported). Offline only: no game run. Data read: the
staging folder `C:\Games\THUG2-2\thug2-decomp-thp8` (byte-identical to what is installed in C:\Games\THUG2: same
sizes, same convert) and THP8's own `C:\Games\THP8\game\DATA`. No leaked source, nothing of Morten1337's / THUGPRO.

## The tools

Generic (any port - tools/level_port, documented in its README):

- `node tools/level_port/zfight.ts <levels folder> [<level> ...] [--top N] [--tolerance IN] [--view M] [--near IN]
  [--min-area M2] [--csv FILE]` - pairs of meshes (different sectors, or different materials of one sector) whose
  triangles lie in one plane within the tolerance (2 inches) and overlap (triangle-triangle clipping in the plane),
  aggregated per mesh pair: overlap area, the gap (min-max over the overlap), the camera distance from which they
  fight at THUG2's depth precision (D3DFMT_D24X8, near plane 12 - src/game/gfx/device.cpp / camera.cpp; a few depth
  steps: `fightDistance`), the area fighting within the view distance (100 m) - the ranking -, a `try_spot` spot
  (ground: its middle; a wall: 3 m out, facing it). Counted apart: pairs whose materials' z biases differ (the bias
  decides), opposite-facing pairs (only when the one seen from behind isn't culled), pairs made of the same triangles
  (equal depths: with LESSEQUAL the later one wins - little or no shimmer; two see-through copies blend twice).
  `findZFights(scene)` takes THUG2's Scene - read from the .scn.xbx or straight from a converter's output before it's
  written (`convertScenes(...).scene`). BEAVER VALLEY (1.26 M triangles) takes ~25 s.
- `node tools/level_port/texture_check.ts <levels folder> [<level> ...] [--top N]` - in the game's terms: a pass naming
  a checksum the .tex.xbx doesn't hold, a pass without a texture, a texture that is one flat colour, a tiny
  see-through dither (THP8's 16 x 16 placeholders: texels alternating see-through / opaque - a cut-out such as a
  distant tree line is not one). By cause and texture: materials, meshes, triangles, area, a spot.

THP8 (tools/thp8_levels/render_audit.ts, README "Render audit"):

- `node tools/thp8_levels/render_audit.ts textures [levels...]` - every material of the level's zones and stand-ins
  as convertScenes draws it, against THP8's data; then texture_check over the converted files.
- `node tools/thp8_levels/render_audit.ts zfight [levels...] [--csv FILE]` - zfight.ts with THP8's sector names
  (dbg.pak), the zone each comes from, the shader template, and the cause. Objects in their own space (LevelObjects,
  game objects - drawn at the origin in the scene file, placed by the game) are left out: before that 2,400 false
  pairs stacked at the origin.
- `node tools/thp8_levels/render_audit.ts spot <level> "(x, y, z)" [radius]` - the drawn meshes at a place, every
  texture THP8's material lists, what pass 0 is drawn with and the verdict.

Options: `--thp8` (C:\Games\THP8), `--thug2` (C:\Games\THUG2) or `--staging <folder>`. Tests:
tools/level_port/zfight.test.ts (clipping, depth precision, fights / non-fights / flagged pairs on built scenes, walls'
spots, texture looks, missing / untextured / flat), tools/thp8_levels/render_audit.test.ts (causes, baseTexture,
blended base's layers). Mutations checked: the gap test, the back-face rule, the same-corner order, the flat and dither
thresholds - each caught. npm test 353 pass, typecheck, lint, thp8 selfcheck (1132 files, 0 failures).

## The screenshot's grey ground

`render_audit.ts spot beavervalley "(-5187.9, 36.3, 3826.1)" 80`: the yard left of the road is
**Z_Houses_Street_JG_Lawn05** (z_houses, material BF735712, shader template **NormalMapBlended**). THP8 lists
73782B21 (normal map), 886E6FC9 (normal map), **2511D697 (32 x 32, a flat mid-grey: 129, 128, 129)**, 7A0195ED x3
(256 x 256, the grass: average 90, 124, 78), 0A18ADED x2 (64 x 64, grass detail), then the light maps. NormalMapBlended
isn't one of the templates materials.ts reads passes of (LAYERED), so baseTexture falls back to "the first texture that
isn't a normal map" - the flat grey, a neutral stand-in in THP8's list - and the lawn is drawn grey. The same for its
neighbours: Lawn03, dt_asianYard02, Grecian_Tm_GrecianYard05 (4 materials, z_houses / z_shops, 775 triangles,
3,246 m2 - the suburb's lawns and yards). The grass texture is in the zone's .tex (converted fine) - just not picked.

## Missing textures - what the audit finds

Nothing is missing in the game's sense: in every level each texture checksum the .scn.xbx names is in its .tex.xbx and
every material has one (0 missing, 0 untextured but the barrier veil - untextured on purpose - and the bedroom's one
material with only a normal map). No texture of a picked material is absent from its zone's .tex, none fails to
decode. What's "missing" is textures **converted but not drawn**, or a blank drawn instead:

BEAVER VALLEY (THP8's materials as converted; m2 of THP8's surface):

| Cause | Materials | Triangles | m2 | Largest |
|---|---|---|---|---|
| blank picked, real texture listed | 104 | 15,060 | 16,338 | inschool curved walls BD9DFC57 for 20295205 (3,982 m2); capitol god rays (TheWibbler); **the suburb lawns 2511D697 for 7A0195ED (3,246)**; flowing water 02A5970C; school pool, gym track |
| only blanks listed | 29 | 2,449 | 11,675 | outschool light shafts (VertexColorOnly, CD015FE5 / F51E5450 a dither - 7,928 m2), periphery strips |
| layer drawn with a stipple placeholder | 115 | 7,060 | 20,928 | 778B446B in 76 materials (capitol top deck, ...), 176BD3E9 in 34, BF6BCB1B (shops street04) - **a dotted overlay** |
| layer drawn with a flat colour | 52 | 4,039 | 9,526 | mostly white 281566A3 / 18997782 tinted by the pass colour: THP8's paint layers - intended, not a bug |
| layers dropped | 75 | 7,599 | 30,322 | a second mask with uvs of its own (only one fits the vertex alpha): city park ground 9F06D8EA (7,278 m2), capitol terraces; base texture with alpha (layers left out) |
| other textures not drawn (passes not read) | 371 | 60,291 | 441,661 | environment / refraction / normal-map shaders: the capitol's lake (WaterCubeRefractionInteractive, 362,515 m2) drawn with its dark 64 x 64; factory floors' env maps |

As THUG2 draws it (texture_check over the files): flat-colour textures on 102 materials (27,462 m2; the lawns'
2511D697 among them), dither placeholders on 52 materials (5,250 m2).

The other levels (same columns; the city areas' figures include their stand-ins):

| Level | blank picked | only blanks | stipple layer | flat layer | layers dropped | passes not read |
|---|---|---|---|---|---|---|
| SKATE PARK | 22 / 3,837 | 19 / 3,855 | 19 / 19,061 | 6 / 968 | 15 / 3,304 | 4 / 93 |
| FACTORY | 33 / 4,038 | 19 / 3,855 | 17 / 18,916 | 7 / 977 | 19 / 5,895 | 80 / 40,312 |
| SLUMS | 23 / 3,945 | 21 / 4,298 | 36 / 22,048 | 31 / 4,057 | 28 / 11,360 | 7 / 3,689 |
| SUBURBIA | 41 / 7,580 | 19 / 3,855 | 18 / 19,584 | 16 / 1,940 | 19 / 3,333 | 24 / 2,730 |
| MAIN STREET | 30 / 4,036 | 23 / 3,893 | 54 / 23,777 | 9 / 3,518 | 22 / 3,467 | 35 / 1,104 |
| FUN PARK | 21 / 3,230 | 16 / 3,348 | 17 / 18,916 | 6 / 968 | 15 / 3,782 | 53 / 4,671 |
| SCHOOL | 54 / 10,015 | 22 / 12,232 | 27 / 17,644 | 5 / 951 | 20 / 3,622 | 82 / 11,743 |
| INDUSTRIAL ZONE | 34 / 4,146 | 21 / 4,298 | 36 / 22,048 | 32 / 4,065 | 32 / 13,951 | 87 / 44,001 |
| CAPITOL BUILDING | 34 / 6,515 | 23 / 3,324 | 46 / 13,395 | 14 / 2,925 | 42 / 18,685 | 86 / 377,319 |
| SKATE & FUN PARKS | 21 / 3,230 | 16 / 3,348 | 19 / 19,061 | 6 / 968 | 15 / 3,782 | 57 / 4,764 |
| SKATE CENTRAL | 61 / 10,457 | 27 / 3,362 | 84 / 18,924 | 27 / 6,446 | 53 / 18,878 | 145 / 381,154 |
| TRAINING | 8 / 357 | 3 / 69,047 * | - | - | 9 / 512 | 48 / 4,109 |
| SKATE SHOP | 6 / 13 | 2 / 22 | - | 2 / 34 | - | 8 / 14 |
| BEDROOM | 1 / 18 | 4 / 114 | - | - | - | 16 / 154 |

(materials / m2.) \* TRAINING's is its skybox: VertexColorOnly with THP8's white 281566A3 - vertex colours only, as
THP8 draws it: harmless. White / grey flat textures on VertexColorOnly materials are harmless in general; a dither or a
flat grey on a material that lists a real texture is not.

### Proposed fixes (textures)

1. **Blank picked while a real texture is listed** (the screenshot): in baseTexture (or planMaterial's pass 0), pass
   over blanks - a texture whose texels are one colour or a dither (texture_check.lookOf: `flat || stipple`) - when
   the material lists a real one (not a normal map, not a light map); the biggest real one is what the audit names.
   That alone makes the lawns grass (7A0195ED). Better for NormalMapBlended (4 materials, the lawns): read its list
   as passes (normal, normal, neutral grey, then the grass maps blended by the vertex alpha - its meshes have vertex
   alpha 0-255) - a LAYERED entry with its own layout. Not a one-line fix, but small (~10 lines: a `look` per texture
   is already computed by the light-map code's decode).
2. **Layers drawn with a dither** (778B446B / 176BD3E9 / BF6BCB1B... - THP8's 16 x 16 placeholders standing in a
   pass's diffuse slot): leave such a layer out in convertScenes' layer loop (`continue` when the layer's diffuse is a
   dither). 115 materials in BEAVER VALLEY; a dotted overlay today.
3. **Only blanks listed**: draw a dither-only material untextured (texture 0 - its vertex colours, as VertexColorOnly
   means) instead of the dither; keep flat white / grey (harmless).
4. **Layers dropped - a second mask with uvs of its own** (75 materials, 30,322 m2 in BEAVER VALLEY: the city park's
   ground, the capitol's terraces): only one mask fits the vertex alpha. Bake the second layer x its mask into a
   texture in the layer's uv space when the mask's uvs are an affine function of the layer's (check per mesh), else
   refine the triangles and put the mask into a second THUG2 pass's vertex alpha... (needs a design; the largest
   single item after the passes not read).
5. **Passes not read** (environment, refraction, normal-map-only shaders: 371 materials) - expected compromises
   (README "Not yet"); the capitol lake (one material, 362,515 m2, drawn with its dark 64 x 64 refraction texture) is
   the one worth a look: a water colour / THP8's other listed texture (02360408) instead.

## Z-fighting - what the audit finds

THUG2 has 24-bit depth and a 12-inch near plane: surfaces fight only when practically coplanar (0.02 inch apart from
~40 m, 0.25 inch from ~90 m). Found per level (`render_audit.ts zfight`, m2 of overlap; "shimmering" = fighting within
100 m, not the same triangles, not opposite-facing):

| Level | Mesh pairs | Shimmering m2 | Opposite-facing two-sided m2 | Same triangles m2 | Largest shimmering causes (m2) |
|---|---|---|---|---|---|
| BEAVER VALLEY | 4,836 | 3,585 | 2,208 | 183,897 | overlapping objects in one zone 1,161; duplicates in one zone 949; see-through layers 861; THP8 decals 516 |
| SKATE PARK | 285 | 1,725 | 471 | 193 | **stand-ins over each other (ours) 1,696** |
| FACTORY | 832 | 985 | 1,525 | 283 | stand-ins over each other (ours) 823 |
| SLUMS | 766 | 1,056 | 602 | 116 | stand-ins over each other (ours) 828 |
| SUBURBIA | 677 | 532 | 475 | 116 | stand-ins over each other (ours) 265; see-through layers 114; barrier veil on the level (ours) 109 |
| MAIN STREET | 586 | 622 | 475 | 691 | stand-ins over each other (ours) 483 |
| FUN PARK | 763 | 1,635 | 471 | 181,594 | stand-ins over each other (ours) 805; duplicates 403; see-through layers 308 |
| SCHOOL | 858 | 2,658 | 513 | 355 | stand-ins over each other (ours) 1,842; THP8 decals 279 |
| INDUSTRIAL ZONE | 1,475 | 868 | 1,640 | 283 | stand-ins over each other (ours) 479 |
| CAPITOL BUILDING | 923 | 2,551 | 982 | 244,357 | duplicates in one zone 1,161; overlapping objects 768; THP8 decals 342 |
| SKATE & FUN PARKS | 916 | 1,486 | 471 | 181,665 | stand-ins over each other (ours) 618 |
| SKATE CENTRAL | 1,951 | 1,698 | 994 | 245,865 | overlapping objects 804; duplicates 419 |
| TRAINING / SKATE SHOP / BEDROOM | 80 / 36 / 41 | 4 / 1 / 6 | 0 | 0 | - |

### BEAVER VALLEY in detail

- **The whole fun-park periphery twice**: Z_Funpark_g_funpark_perif_peninsula and z_cartoon_g_funpark_perif_peninsula
  - 181,472 m2, the same strips (identical vertex sequence), both see-through (texture D2CF3594 has alpha), lit by
  different zones (materials 104C43CA / 74F83533). The cartoon world's archive carries a copy of the fun park's
  surroundings; BEAVER VALLEY (and FUN PARK, SKATE & FUN PARKS) merge both zones. Equal depths, so not a per-pixel
  shimmer - but drawn twice and blended twice (denser / darker than THP8's), and if THUG2's order of the two flips
  with the camera the whole periphery would switch lighting. Spot: `try_spot thp8_net_beavervalley -22435,-268,1709,0`.
  Also z_inschool / z_outschool both draw the library windows (Z_Inschool / Z_Outschool_g_out_school_lib_win_19: 10
  pairs, 401 m2, same name).
- **Opposite-facing two-sided** (535 pairs, 2,208 m2 seen): see-through materials are made two-sided
  (noBackfaceCulling for every blended material); where THP8 has an inside and an outside shell back to back they
  fight from both sides - the factory's paint shells (Z_Factory_G_RMM_paint_shell_outside / _inside, 552 + 402 m2,
  NormalMapEnvMap_Decal, `try_spot thp8_net_beavervalley 7221,-121,8863,0`), the capitol roof pools over
  int_topdecks (4 x 160 m2).
- **Duplicates in one zone** (164 pairs, 949 m2 shimmering + 39 pairs, 1,465 m2 same triangles): THP8's own
  doubles, both CreatedAtStart in THP8 - VC_ / G_ twins (z_cartoon_VC_G_RM_MAINST_PIRATE_INSIDE /
  z_cartoon_G_VC_RM_MAINST_PIRATE_INSIDE 242 + 159 m2, Z_Shops_G_JG_BuckstersINT / Z_Shops_VC_G_JG_BuckstersINT 169 m2,
  the cheese shop's interior), the parking awning (Z_Parking_levelgeo_63 / z_parking_G_SG_Park_Awning 365 + 240 m2),
  numbered copies (castle pieces, crates, doors).
- **Overlapping objects in one zone** (1,454 pairs, 1,161 m2 seen): the capitol's eternal-flame water over its top
  (0.14-0.3 inch apart: fights from ~70 m), the city park's afterscene pieces under the porkrind boxes (0.25 inch),
  the slums' streets.
- **See-through layers on a surface, no z bias** (1,397 pairs, 861 m2) and **THP8 decals** (461 pairs, 516 m2 + 439
  m2 same triangles): the gym's basketball lines on its floor (Z_Inschool_g_gym_floor01 / _Bball_Lines_01, 248 m2,
  0.008-0.05 inch: fights from ~16 m - `try_spot thp8_net_beavervalley -5739,0,-377,0`), the parking's lighting wall,
  bushes' shadows. THP8 draws its decals with a depth bias of its own (not decoded); our materials all have z bias 0
  and the converter doesn't run level_port/overlay_bias.ts (THPS3 / THPS4 do).
- Our own additions in BEAVER VALLEY: none fight - it has no stand-ins or barriers; the gap fills, trimmed shells and
  seam walls are collision only (not drawn).

### The single-area levels: our stand-ins

- **Stand-ins over each other** - the largest shimmering cause in 9 of the 11 areas: the stand-ins' `__MASTERNODE_`
  sectors. A `_lod` archive holds instance masters too (z_center_lod 9 / 2,039 triangles, z_cretepark_lod 8,
  z_cartoon_lod 5, z_houses_lod 2, z_citypark_lod 2, z_factory_lod 1, z_funpark_lod 1: 28 sectors, 2,754 triangles);
  convertLevel gives the stand-in parts `placed: new Map()`, so the masters are drawn where they are defined - all at
  THP8's origin, on top of each other (the small trees Z_Center_LOD__MASTERNODE_I_JP_new_center_super_small_tree_00 /
  _shopsbleed_super_small_tree_01..., 642 m2 in SKATE PARK at `try_spot thp8_net_skatepark 5519,282,2820,185`) - and
  their instances aren't drawn at all.
- **Stand-in over the level, same triangles** (CAPITOL, SKATE CENTRAL: 244,061 m2): z_factory_lod's
  Z_Factory_LOD_TK_G_Industrial_Map_01 is the same backdrop as z_citypark's TK_G_Industrial_Map_01 / _05, with another
  texture (C8058E75 vs 177AB13F); standInOutside keeps it (it only drops stand-in triangles over the level's
  collision ground - the backdrop has none). Same corners but different strips, so it may shimmer.
- **Barrier veil on the level** (SUBURBIA 109 m2): the veil lies on a wall of the level.

### Proposed fixes (z-fighting)

1. **Stand-ins' masters** - near one-line: in convertLevel's stand-in loop give each part its node array's instances,
   as readPart does for a zone: `placed: lod.nqb ? instances(nodeArrayOf(lod.nqb, <the lod zone's name>)) : new
   Map()` (the masters then aren't drawn at the origin and the instances are). Removes most "stand-ins over each
   other" in all 11 areas.
2. **Run overlay_bias on THP8's scene** - one line after convertScenes in convertLevel:
   `overlayBias.biasOverlays(done.scene)` (as THPS3 / THPS4 do): gives decals and see-through layers lying on a
   surface a z bias over it (THP8's own depth bias isn't decoded). The tool's "THP8 decal" + "see-through layer" pairs
   (BEAVER VALLEY 1,858 pairs) should then read "decided by a z bias".
3. **Zone duplicates**: when a level merges zones, leave out a later zone's sector that is an earlier zone's under the
   other zone's prefix with the same box (z_cartoon_g_funpark_perif_peninsula = Z_Funpark_..., Z_Outschool_g_out_
   school_lib_win_19 = Z_Inschool_...). Removes the 181,472 m2 double periphery.
4. **Stand-ins over the level's drawn surfaces**: extend standInOutside to drop a stand-in triangle lying on one of
   the level's own drawn triangles (zfight's overlapOf against the level's scene) - not only over its collision.
5. **Two-sided see-through back to back**: give blended materials noBackfaceCulling only where THP8's material is
   double-sided (its flags / render state - to find), or leave out the back-to-back twin (the factory's _inside shell
   behind _outside).
6. THP8's own in-zone duplicates (VC_ / G_ twins, the parking awning, crates): both are made at start in THP8 too; a
   z bias (fix 2 by coverage) or leaving out one of an exact pair (same box, same texture) - the lead's call.

Verify after a fix round: `render_audit.ts zfight` / `textures` again (the counts above are the baseline), then the
user in the game. Run in the game: none in this round (NOT run in the game).
