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

## Fix round 1 (2026-10-24)

Branch thp8-fix-round-1: the audit's proposed fixes 1-3 (textures) and 1-3 (z-fighting), draw only. All 15 levels
reconverted into C:\Games\THUG2-2 (`thp8_levels.ts convert --thug2 C:\Games\THUG2-2`, `make_menu_level.ts --game` after)
and the audit tools rerun on that staging folder. **The 15 `.col.xbx` files are byte-identical before / after
(SHA-256); `thp8_levels.ts selfcheck`: 1132 files, 0 failures; npm test 356 pass, typecheck and lint clean.** Before =
the staging folder as the audit found it, run with the same tool; after = the new convert.

What changed (tools/thp8_levels; tests in render_audit.test.ts / thp8_levels.test.ts):

1. **Stand-in masters** - `standInPlaced`: a `_lod` archive's node array (CreatedAtStart instances, as readPart takes a
   zone's) is the stand-in part's `placed`, so its `__MASTERNODE_` sectors are no longer drawn stacked at THP8's origin
   and their instances are drawn. Stand-ins over each other (m2 seen): SKATE PARK 1,696 -> 95, FACTORY 823 -> 84, SLUMS
   828 -> 83, SUBURBIA 265 -> 95, MAIN STREET 483 -> 81, FUN PARK 805 -> 91, SCHOOL 1,843 -> 56, INDUSTRIAL 479 -> 83,
   CAPITOL 244 -> 57, SKATE & FUN 618 -> 91, SKATE CENTRAL 44 -> 44. (What is left is the instances standing where THP8
   puts them, some over each other or the backdrop - not looked at.)
2. **Z bias** - `overlayBias.biasOverlays(done.scene)` after the refine loop in convertLevel (report line "n layer
   materials given a z bias over the surfaces under them": BEAVER VALLEY 318). The audit now reads those pairs as
   "decided by a z bias": BEAVER VALLEY 2,424 m2 seen (0 before); decals + see-through layers without a bias 1,377 ->
   348 m2. Remaining in BEAVER VALLEY: 189 THP8-decal pairs (10 m2 seen) and 624 see-through pairs (338 m2 seen) the
   bias didn't take (overlay_bias needs a layer lying on a surface within its 1-inch plane test and an overlap of 1 m2).
3. **Blank base textures** - `baseTexture` (+ `materials.blankKind`: texture_check's flat / stipple look, cached per
   texture; a texture whose data doesn't decode counts as not blank): a blank pick is replaced by the material's biggest
   real texture (not a normal map, not a light map, not blank). The suburb lawns: Z_Houses_Street_JG_Lawn05 (BF735712)
   is drawn with 7A0195ED (the grass, RGB 90,123,78) instead of the flat grey 2511D697 (`render_audit.ts spot
   thp8_net_beavervalley "(-5187.9, 36.3, 3826.1)" 30`). "Blank picked, real texture listed": BEAVER VALLEY 104
   materials / 16,338 m2 -> 0, and 0 in every other level. **Not done: NormalMapBlended's own layer layout** (2-3 grass
   maps blended by the vertex alpha, 4 materials): the lawns are the first grass map only now; the blend needs a LAYERED
   entry with its own layout and the mask read from the parameter block - left for a later round.
4. **Dither layers** - the layer loop in convertScenes leaves out a layer whose diffuse is a stipple placeholder (opaque
   passes; a blended base keeps no layers anyway). "Layer drawn with a stipple placeholder": BEAVER VALLEY 115 materials
   / 20,928 m2 -> 0 (0 in all levels). As THUG2 draws it, stipple placeholders: 52 materials / 5,250 m2 -> 1 (B9583299,
   pass 2 of Z_Parking_G_SG_Park_Enterance23, 75 m2 - a diffuse x mask layer texture that itself looks like a dither;
   left).
5. **Only blanks listed** - a material whose textures are all dithers is drawn untextured (texture 0, its vertex
   colours); a flat colour stays (white / grey paint layers tinted by the pass colour). The audit counts the dither-only
   ones as resolved. "Only blanks listed" in BEAVER VALLEY 29 / 11,675 m2 -> 16 / 10,577 (all flat now: outschool light
   shafts, periphery strips). As THUG2 draws it: untextured 0 -> 14 materials / 1,099 m2 in BEAVER VALLEY (the other
   levels' count also holds the barrier veil), flat-colour textures 102 materials / 27,462 m2 -> 27 / 12,065.
6. **Duplicates across merged zones** - `zoneDuplicates`: a later zone's sector with the same name under another zone's
   prefix (the leading `z_<zone>_`) and the same bounding box (0.01 m) as an earlier zone's is not drawn (it goes into
   the scene part's hidden set only: collision and node arrays untouched). Left out: BEAVER VALLEY 6 sectors, FUN PARK /
   SCHOOL / CAPITOL / SKATE & FUN PARKS / SKATE CENTRAL 1 each (convert report line "n sectors a later zone repeats from
   an earlier one left out of the scene"). The doubled fun park periphery is gone: BEAVER VALLEY mesh area fighting
   within view 189,689 m2 -> 7,790. What THP8 ships twice inside one zone (same box and same index count: VC_ / G_
   twins, crates, castle pieces) is only counted and kept, in the convert report ("n sectors THP8 ships twice in one
   zone (same box and size) kept": BEAVER VALLEY 146, SKATE PARK 59, SCHOOL 79, CAPITOL 31, ...).

### Z-fight audit, before -> after (m2 seen within 100 m; pairs)

"Shimmering" = seen m2 without the same-triangles, opposite-facing and decided-by-z-bias pairs.

| Level | pairs | within view m2 | shimmering m2 | stand-ins over each other | decals + see-through, no bias | decided by a z bias | same triangles |
|---|---|---|---|---|---|---|---|
| thp8_net_beavervalley | 4836 -> 4807 | 189689 -> 7790 | 3585 -> 2082 | 0 -> 0 | 1377 -> 348 | 0 -> 2424 | 183896 -> 1619 |
| thp8_net_skatepark | 285 -> 283 | 2389 -> 850 | 1726 -> 160 | 1696 -> 95 | 28 -> 63 | 0 -> 85 | 193 -> 135 |
| thp8_net_factory | 832 -> 840 | 2793 -> 2116 | 986 -> 280 | 823 -> 84 | 35 -> 68 | 0 -> 473 | 283 -> 244 |
| thp8_net_slums | 766 -> 774 | 1774 -> 1092 | 1057 -> 254 | 828 -> 83 | 107 -> 76 | 0 -> 270 | 116 -> 77 |
| thp8_net_suburbia | 677 -> 683 | 1123 -> 1011 | 531 -> 210 | 265 -> 95 | 115 -> 77 | 0 -> 250 | 116 -> 77 |
| thp8_net_mainstreet | 586 -> 597 | 1787 -> 1445 | 621 -> 185 | 483 -> 81 | 96 -> 70 | 0 -> 147 | 690 -> 643 |
| thp8_net_funpark | 763 -> 770 | 183700 -> 1577 | 1635 -> 852 | 805 -> 91 | 314 -> 340 | 0 -> 172 | 181594 -> 83 |
| thp8_net_school | 858 -> 829 | 3525 -> 1391 | 2660 -> 270 | 1843 -> 56 | 494 -> 94 | 0 -> 572 | 355 -> 47 |
| thp8_net_industrial | 1475 -> 1483 | 2791 -> 2458 | 868 -> 384 | 479 -> 83 | 141 -> 81 | 0 -> 705 | 283 -> 244 |
| thp8_net_capitol | 923 -> 924 | 247890 -> 248404 | 2551 -> 2452 | 244 -> 57 | 376 -> 249 | 0 -> 785 | 244357 -> 244196 |
| thp8_net_skatefunparks | 916 -> 925 | 183622 -> 1686 | 1485 -> 860 | 618 -> 91 | 343 -> 340 | 0 -> 219 | 181665 -> 135 |
| thp8_net_skatecentral | 1951 -> 1950 | 248556 -> 249254 | 1698 -> 1604 | 44 -> 44 | 407 -> 103 | 0 -> 969 | 245865 -> 245703 |
| thp8_training | 80 -> 80 | 4 -> 4 | 4 -> 3 | 0 -> 0 | 2 -> 1 | 0 -> 1 | 0 -> 0 |
| thp8_skateshop | 36 -> 36 | 1 -> 1 | 0 -> 0 | 0 -> 0 | 0 -> 0 | 0 -> 1 | 0 -> 0 |
| thp8_bedroom | 41 -> 41 | 6 -> 6 | 6 -> 4 | 0 -> 0 | 0 -> 0 | 0 -> 2 | 0 -> 0 |

The same-triangles column falling in BEAVER VALLEY / FUN PARK / SKATE & FUN PARKS (about 184,000 / 181,600 / 181,700 ->
1,619 / 83 / 135) is the periphery. CAPITOL and SKATE CENTRAL keep their 244,000 m2: the z_factory_lod backdrop over
the level's TK_G_Industrial_Map pieces (z-fight fix 4 of the list above, not in this round). The opposite-facing
two-sided shells (the factory's paint shells) are untouched (fix 5 there). BEAVER VALLEY's top now: the paint shells
(552 + 402 m2, `try_spot thp8_net_beavervalley 7221,-121,8863,360`), overlapping objects in one zone 735 m2, in-zone
duplicates 949 m2.

### Texture audit, before -> after (materials / m2 of THP8's surface)

### THP8's materials as converted (materials / m2)
| Level | blank picked, real texture listed | layer drawn with a flat colour | layer drawn with a stipple placeholder | layers dropped | no texture | only blanks listed | other textures not drawn (passes not read) |
|---|---|---|---|---|---|---|---|
| thp8_net_beavervalley | 104 / 16338 -> 0 / 0 | 52 / 9526 -> 57 / 10466 | 115 / 20928 -> 0 / 0 | 75 / 30322 -> 78 / 30553 | - | 29 / 11675 -> 16 / 10577 | 371 / 441661 -> 403 / 445953 |
| thp8_net_skatepark | 22 / 3837 -> 0 / 0 | 6 / 968 -> 14 / 2258 | 19 / 19061 -> 0 / 0 | 15 / 3304 -> 19 / 3334 | - | 19 / 3855 -> 14 / 2292 | 4 / 93 -> 4 / 93 |
| thp8_net_factory | 33 / 4038 -> 0 / 0 | 7 / 977 -> 15 / 2267 | 17 / 18916 -> 0 / 0 | 19 / 5895 -> 23 / 5925 | - | 19 / 3855 -> 14 / 2292 | 80 / 40312 -> 81 / 40316 |
| thp8_net_slums | 23 / 3945 -> 0 / 0 | 31 / 4057 -> 39 / 5347 | 36 / 22048 -> 0 / 0 | 28 / 11360 -> 32 / 11391 | - | 21 / 4298 -> 15 / 2704 | 7 / 3689 -> 7 / 3689 |
| thp8_net_suburbia | 41 / 7580 -> 0 / 0 | 16 / 1940 -> 24 / 3231 | 18 / 19584 -> 0 / 0 | 19 / 3333 -> 23 / 3364 | - | 19 / 3855 -> 14 / 2292 | 24 / 2730 -> 40 / 6308 |
| thp8_net_mainstreet | 30 / 4036 -> 0 / 0 | 9 / 3518 -> 15 / 4613 | 54 / 23777 -> 0 / 0 | 22 / 3467 -> 23 / 3502 | - | 23 / 3893 -> 14 / 2311 | 35 / 1104 -> 41 / 1539 |
| thp8_net_funpark | 21 / 3230 -> 0 / 0 | 6 / 968 -> 12 / 1515 | 17 / 18916 -> 0 / 0 | 15 / 3782 -> 19 / 3813 | - | 16 / 3348 -> 11 / 1786 | 53 / 4671 -> 53 / 4671 |
| thp8_net_school | 54 / 10015 -> 0 / 0 | 5 / 951 -> 16 / 3136 | 27 / 17644 -> 0 / 0 | 20 / 3622 -> 26 / 3849 | - | 22 / 12232 -> 16 / 11190 | 82 / 11743 -> 87 / 11906 |
| thp8_net_industrial | 34 / 4146 -> 0 / 0 | 32 / 4065 -> 40 / 5355 | 36 / 22048 -> 0 / 0 | 32 / 13951 -> 36 / 13982 | - | 21 / 4298 -> 15 / 2704 | 87 / 44001 -> 88 / 44005 |
| thp8_net_capitol | 34 / 6515 -> 0 / 0 | 14 / 2925 -> 20 / 3907 | 46 / 13395 -> 0 / 0 | 42 / 18685 -> 46 / 18716 | - | 23 / 3324 -> 16 / 1755 | 86 / 377319 -> 90 / 377432 |
| thp8_net_skatefunparks | 21 / 3230 -> 0 / 0 | 6 / 968 -> 12 / 1515 | 19 / 19061 -> 0 / 0 | 15 / 3782 -> 19 / 3813 | - | 16 / 3348 -> 11 / 1786 | 57 / 4764 -> 57 / 4764 |
| thp8_net_skatecentral | 61 / 10457 -> 0 / 0 | 27 / 6446 -> 31 / 7234 | 84 / 18924 -> 0 / 0 | 53 / 18878 -> 54 / 18913 | - | 27 / 3362 -> 16 / 1774 | 145 / 381154 -> 171 / 385279 |
| thp8_training | 8 / 357 -> 0 / 0 | - | - | 9 / 512 -> 9 / 512 | - | 3 / 69047 -> 3 / 69047 | 48 / 4109 -> 55 / 4129 |
| thp8_skateshop | 6 / 13 -> 0 / 0 | 2 / 34 -> 2 / 34 | - | - | - | 2 / 22 -> 2 / 22 | 8 / 14 -> 8 / 14 |
| thp8_bedroom | 1 / 18 -> 0 / 0 | - | - | - | 1 / 157 -> 1 / 157 | 4 / 114 -> 2 / 113 | 16 / 154 -> 16 / 154 |

### As THUG2 draws it (materials / m2)
| Level | flat colour | stipple placeholder | untextured |
|---|---|---|---|
| thp8_net_beavervalley | 102 / 27462 -> 27 / 12065 | 52 / 5250 -> 1 / 75 | 0 / 0 -> 14 / 1099 |
| thp8_net_skatepark | 33 / 6321 -> 16 / 2884 | 15 / 4805 -> 0 / 0 | 1 / 22662 -> 6 / 24224 |
| thp8_net_factory | 44 / 7419 -> 16 / 2884 | 15 / 4805 -> 0 / 0 | 1 / 13329 -> 6 / 14892 |
| thp8_net_slums | 42 / 7561 -> 26 / 3936 | 18 / 4991 -> 0 / 0 | 1 / 32338 -> 7 / 33931 |
| thp8_net_suburbia | 48 / 10017 -> 16 / 2884 | 20 / 5088 -> 0 / 0 | 1 / 68152 -> 7 / 69715 |
| thp8_net_mainstreet | 34 / 6140 -> 14 / 2479 | 27 / 5272 -> 0 / 0 | 1 / 17862 -> 10 / 19444 |
| thp8_net_funpark | 29 / 5282 -> 13 / 2377 | 15 / 4805 -> 0 / 0 | 1 / 32031 -> 6 / 33593 |
| thp8_net_school | 53 / 20835 -> 16 / 11782 | 29 / 4736 -> 0 / 0 | 1 / 109039 -> 7 / 110080 |
| thp8_net_industrial | 53 / 8424 -> 26 / 3936 | 18 / 4991 -> 0 / 0 | 1 / 46298 -> 7 / 47892 |
| thp8_net_capitol | 49 / 8980 -> 21 / 2625 | 19 / 4371 -> 1 / 75 | 1 / 164918 -> 8 / 166486 |
| thp8_net_skatefunparks | 29 / 5282 -> 13 / 2377 | 15 / 4805 -> 0 / 0 | 1 / 47050 -> 6 / 48613 |
| thp8_net_skatecentral | 66 / 12426 -> 20 / 2621 | 35 / 5120 -> 1 / 75 | 1 / 129383 -> 13 / 130971 |
| thp8_training | 10 / 69384 -> 2 / 69026 | - | - |
| thp8_skateshop | 7 / 29 -> 3 / 23 | 2 / 7 -> 0 / 0 | - |
| thp8_bedroom | 3 / 131 -> 2 / 113 | 2 / 1 -> 0 / 0 | 1 / 157 -> 3 / 158 |


The "layer drawn with a flat colour", "layers dropped" and "other textures not drawn" figures grow a little because
materials that were classed "blank picked" are now picked right and their layers and further textures get looked at;
the white paint layers are intended. Nothing is missing in the game's sense (as before).

### Load check

One load check (`load_check.ps1 -Game C:\Games\THUG2-2 -Levels thp8_net_beavervalley`, the workspace's own dinput8.dll of
2026-10-19, not replaced as the workspace had one): **crashed** - EXCEPTION 0xC0000005 (write at 0x00000004) right after
`levels\thp8_net_beavervalley\thp8_net_beavervalley.qb` was read, before the collision loaded (in the script / node
loading path, not in drawing). The DLL predates this project's bigger limits for BEAVER VALLEY (README: node name hash,
rail table, script heap, pools), so this is probably the stale DLL and not these draw-only fixes (collision and nodes are
byte-identical in the .col; the .qb is regenerated, its node list now differs only by the zone-duplicate sectors not being
in `done.sectors`). Not diagnosed further (one check allowed): rerun with a current DLL
(out\build\lead\dinput8.dll, 2026-10-24) to settle it.`

### For the user, BEAVER VALLEY (try_spot positions)

- The suburb lawns: `node tools/game/try_spot.ts thp8_net_beavervalley -5187.9,36.3,3826.1,0` (the screenshot's yard:
  grass now, not flat grey).
- The fun park periphery (was doubled and blended twice): `try_spot thp8_net_beavervalley -22435,-268,1709,0`.
- The gym's basketball lines (z bias): `try_spot thp8_net_beavervalley -5739,0,-377,0`.
- Stand-in trees (SKATE PARK, was stacked at the origin): `try_spot thp8_net_skatepark 5519,282,2820,185`.
- Dotted overlays gone, e.g. the capitol top deck (778B446B layers) and shops street04.

## Fix round 2 (2026-10-24)

The user after round 1: "Still getting some z fighting" (BEAVER VALLEY, the shops alley, skater at (-2221,-197,3207)
looking -x: the corrugated sheet at the feet flickers in dark patches, the "NO DUMPING" sign on the wooden ramp half
eaten by a black triangle). Branch of this round: draw only, `.col.xbx` of all 15 levels byte-identical before / after
(SHA-256), `thp8_levels.ts selfcheck` 1132 files / 0 failures, npm test 358 pass, typecheck and lint clean. Offline plus
one load check; NOT looked at in the game.

### What the twin prefixes are (evidence)

- **`F_` = floater.** The names: Z_Shops_F_G_TM_BlendedAsphalt, _GrassBlendnew, _grassbits, _BlendFloaterDustyPath,
  _FloaterSignFence, _CheeseFloaterSign, _ZumiezFloaters, _StereoBanner, _ZumiezStencils, _Trash, F_JAD_pile_of_leaves_nn,
  F_JAD_stains (z_shops 43 sectors, z_center 27 ...): geometry laid over a surface - sign cells, blend patches, leaves.
  They are not LODs: the node arrays give both twins the same `Class=LevelGeometry CreatedAtStart` (+ TrickObject /
  Cluster / CollisionMode=Geometry) with the same Pos / Angles; the sector flags (0x80042007) and mesh flags are equal; no
  script names any of them (searched every .qb / .nqb of the install for their checksums: only the node arrays do), no
  LOD distance is on them. So THP8 draws both.
- **What differs between `Z_Shops_g_TM_Shops830` and `Z_Shops_F_G_TM_Shops830`** (the sheet): the same 4 vertices, same
  material 09E400E6, same colours, same diffuse uv; only the last two uv fields (the ambient / shadow light map
  coordinates) differ - the floater has its own light map bake. THP8 draws the two with the same vertices through the
  same shader (equal depth, the later wins); THUG2 triangulates and lights each copy on its own (vertex colours, refined
  triangles), the depths round apart: gap 0.0000 inch and still a flicker (the audit's "same triangles: little or no
  shimmer" was wrong).
- **The sign**: `Z_Shops_g_TM_Shops808_to` (2 meshes: base 9A9D958F, layer 1E24B995) and `Z_Shops_F_G_TM_Shops808_to`
  (only the layer 1E24B995). Same layer positions; the g_ one's diffuse uv is the whole 512 x 512 sign atlas (0..1) -
  that is the half-eaten look - the floater's is the 1/8 x 1/8 cell of the NO DUMPING sign (u 0.376-0.499, v 0.625-0.75;
  looked at the decoded atlas). The floater's is the intended one; the g_ sector kept the layer it was cut from.
- `VC_` = vertex colours, `G_` = geometry (`g_`), `I_` = instance, `TK_` / `RMM_` / `JG_` ... = artists' initials, `_to` =
  trick object, `_LOD` stand-ins are other archives. The VC_G_ / G_VC_ pairs (z_cartoon_..._PIRATE_INSIDE) are one wall
  tessellated twice (46 vs 121 vertices), both CreatedAtStart.

### What changed (tools/thp8_levels; tests in thp8_levels.test.ts, zfight.test.ts)

1. **`repeatedSurfaces`** (new): a mesh is not drawn when 95% of its area (7 sample points per triangle) lies in the plane
   (3 mm), facing the same way, and inside triangles of meshes of the same material drawn before it - a floater's
   first, then the larger meshes, then the earlier sectors (the floater is kept: it has the real uv). Skips instance
   masters / instances, local objects, not-made and zone-duplicate sectors. Collision, node arrays untouched (a per-mesh
   hide set in the scene part). Convert report line: "n meshes drawing a surface another sector draws too ... left out,
   m of them for a floater's". Left out per level (net_...): BEAVER VALLEY 27 (3 for floaters: Shops829, Shops830,
   Shops808_to); the other levels 2-18 each (10 levels report some). Replaces the round-1 "twins only
   counted" line.
2. **Audit** (`tools/level_port/zfight.ts`, `render_audit.ts`): the same-triangles pairs no longer read "little or no
   shimmer": they flicker. `flickers(f)` (no z bias, not back to back, seen > 0) and a per-level line `FLICKER (no z
   bias, facing one way): n pairs, x m2 seen; of them coplanar copies (gap under 0.05 inch): n pairs, x m2 seen`; the top
   list marks `[COPLANAR COPIES: flickers]`. Re-baselined below with the same definition on both sides.
3. **Fix 7** `standInOnDrawn`: a `_lod` stand-in's triangle lying on a triangle the level draws (any material, either
   way round, coplanar within 3 mm; a `SurfaceIndex` of the world drawn so far, three grids 4 / 64 / 1024 m) is left out,
   not only over collision. This was the CAPITOL / SKATE CENTRAL 244,000 m2 (z_factory_lod's TK_G_Industrial_Map_01 /
   _BG_02 over z_citypark's) - and it is see-through, so it also blended twice.
4. **Fix 8 (blended materials two-sided only where THP8 says so): not done.** Histogram of every material of the
   install (template, `flags`, parameter block `state` at +0x3c, texture alpha): `flags` is 0x200 for all, `state` takes
   ~40 values (0, 3, 7, 0x300, 0x30300, 0x7000700 ... - layer / stencil-like per pass bits) with no pattern against
   alpha or template; the cull mode is not in the material, it would be in the shader technique (MaterialLibrary.bin's
   0x140-byte technique records) - not decoded. Left as is (the factory paint shells stay).

### Z-fight audit, before -> after (no z bias, not back to back, seen within 100 m)

| Level | flicker pairs | flicker m2 seen | of them coplanar (gap < 0.05 in): pairs | m2 seen |
|---|---|---|---|---|
| thp8_net_beavervalley | 596 -> 574 | 3317 -> 1331 | 116 -> 95 | 2455 -> 480 |
| thp8_net_skatepark | 37 -> 37 | 232 -> 232 | 12 -> 12 | 145 -> 145 |
| thp8_net_factory | 105 -> 101 | 460 -> 394 | 27 -> 23 | 273 -> 207 |
| thp8_net_slums | 125 -> 125 | 256 -> 256 | 16 -> 16 | 91 -> 91 |
| thp8_net_suburbia | 98 -> 98 | 211 -> 211 | 13 -> 13 | 88 -> 88 |
| thp8_net_mainstreet | 84 -> 75 | 759 -> 186 | 17 -> 9 | 649 -> 87 |
| thp8_net_funpark | 128 -> 126 | 597 -> 353 | 37 -> 35 | 497 -> 254 |
| thp8_net_school | 128 -> 127 | 180 -> 140 | 22 -> 21 | 65 -> 25 |
| thp8_net_industrial | 195 -> 191 | 553 -> 487 | 33 -> 29 | 276 -> 210 |
| thp8_net_capitol | 140 -> 126 | 246643 -> 1201 | 50 -> 36 | 246258 -> 816 |
| thp8_net_skatefunparks | 134 -> 132 | 657 -> 414 | 39 -> 37 | 555 -> 311 |
| thp8_net_skatecentral | 258 -> 235 | 247272 -> 1257 | 60 -> 38 | 246820 -> 816 |
| thp8_training | 7 -> 7 | 3 -> 3 | 0 -> 0 | 0 -> 0 |
| thp8_skateshop | 9 -> 9 | 1 -> 1 | 0 -> 0 | 0 -> 0 |
| thp8_bedroom | 4 -> 2 | 4 -> 2 | 2 -> 0 | 2 -> 0 |

(before = the round-1 staging, run with the new tool's definition.) The shops alley's three pairs (Shops829, Shops830,
Shops808_to) are gone from the list. CAPITOL / SKATE CENTRAL 246,000 m2 -> 1,200 (the backdrop). What is left in BEAVER
VALLEY (95 coplanar pairs, 480 m2): the pirate shop interior's wall (159 m2: the two tessellations share the material but
the finer one covers less than 95% of it), Z_Citypark_g_OutsideWall_low / vo_jad_park_periph_reflection (96 m2), the
factory elevator panels over the shaft (80 + 15 m2: other materials, same diffuse texture - the rule keys on the
material), the crete park school hills over its ground (52 m2), floor grates (21 m2). A next round could key the rule on
the converted pass-0 texture instead of the material.

### Load check

Under the lock on C:\Games\THUG2-2 with out\build\lead\dinput8.dll (2026-10-24 17:35) copied in and `[selftests]
enabled=0`: `load_check.ps1 -Game C:\Games\THUG2-2 -Levels thp8_net_beavervalley`: loaded=True, responding after 56 s,
CComponent 256058 / 360000, CStruct 43340 / 64000; no error or exception in thug2-decomp.log. (Round 1's crash was the
stale DLL, as suspected.) Lock released (the old dll and ini put back).

### For the user (try_spot)

- The alley: `node tools/game/try_spot.ts thp8_net_beavervalley -2221,-197,3207,-92` (the sheet at the feet, the sign
  on the wooden ramp ahead at about -2359,-161,3095).
- The sign closer: `... -2340,-170,3120,275`; the second sheet: `... -2703,-161,3159,91`.
- Capitol backdrop (was drawn twice): `node tools/game/try_spot.ts thp8_net_capitol 20964,1346,-20020,149`.

## Fix round 3 (2026-10-24)

Converter only (tools/thp8_levels, tools/level_port/overlay_bias.ts). `.col.xbx` of all 15 levels byte-identical before /
after (SHA-256), `thp8_levels.ts selfcheck` 1132 files / 0 failures, npm test 363 pass, typecheck and lint clean. Offline
plus one load check; NOT looked at in the game.

### What changed

1. **One-sided copies, per mesh (`oneSidedMeshes`).** A mesh of a blended material (base DXT5 / DXT1 with alpha) gets its
   own one-sided copy of the material (`thp8_onesided_<material>`, noBackfaceCulling 0) when >= 90% of its area (7 sample
   points per triangle) lies against an opposite-facing surface of any other mesh (normals opposite, plane gap <= 1 mm =
   0.04 in, inside its triangles). Every other mesh of the material stays two-sided. Absolute exclusions (never changed,
   never a partner): a material whose base or any layer texture has binary alpha (`textureAlpha`: > 2% clear texels and
   < 25% partial = cut-out: leaves, grass, fences, wire), and any material / sector / texture name matching `FOLIAGE_NAME`
   (leaf, leaves, tree, grass, bush, plant, ivy, hedge, foliage, vine, flower, shrub, palm, branch, weed, fern, cactus,
   bamboo). Instances, local objects, not-made and zone-duplicate sectors and hidden meshes are not looked at. Stand-ins
   (_lod) get the same rule among themselves. Convert report: "n meshes lying back to back ... made one-sided" and one line
   per mesh (level, sector, material, base texture, area, share), `REVIEW:` for a soft-alpha texture or vertex alpha < 240.
2. **`repeatedSurfaces` keyed on the converted pass-0 texture** (+ blended or not) instead of the material: the factory
   elevator panels over their shaft are gone. A mesh covered only in part now loses just the triangles lying wholly (7/7
   samples) on earlier ones ("n triangles lying wholly on a surface of the same texture"): the pirate shop wall
   (VC_G_ / G_VC_, 159 m2) is gone (BEAVER VALLEY 680 triangles).
3. **`overlay_bias.biasOverlays(scene, lookAlikes)`**, on for THP8: materials that look the same (so far skipped) and pairs
   that cover each other alike (so far undecided) get a z bias too, the one more of whose surface is covered over the other
   (else the second). This resolves Z_Citypark_g_OutsideWall_low / vo_jad_park_periph_reflection (partial overlap, large
   triangles) and the crete park school hills over their ground. Side effect to watch: BEAVER VALLEY "layer materials given
   a z bias" is 595 (far fewer before; "decided by a z bias" pairs 1301 -> 2629 in the audit); a bias is at most 6 steps.

### Audit, before -> after (zfight, seen within 100 m)

| Level | flicker pairs | flicker m2 seen | coplanar pairs | coplanar m2 | opposite-facing pairs | opposite m2 seen | meshes made one-sided (to review) |
|---|---|---|---|---|---|---|---|
| thp8_net_beavervalley | 817 -> 469 | 1667 -> 496 | 109 -> 73 | 672 -> 231 | 396 -> 338 | 1663 -> 441 | 13 (13) |
| thp8_net_skatepark | 44 -> 34 | 296 -> 140 | 12 -> 9 | 145 -> 10 | 14 -> 12 | 468 -> 388 | 2 (1) |
| thp8_net_factory | 134 -> 66 | 458 -> 214 | 23 -> 16 | 207 -> 33 | 66 -> 32 | 1119 -> 459 | 9 (8) |
| thp8_net_slums | 152 -> 92 | 331 -> 190 | 18 -> 15 | 93 -> 16 | 38 -> 34 | 491 -> 411 | 2 (1) |
| thp8_net_suburbia | 123 -> 62 | 288 -> 146 | 14 -> 10 | 94 -> 9 | 38 -> 33 | 474 -> 394 | 2 (1) |
| thp8_net_mainstreet | 98 -> 54 | 253 -> 137 | 10 -> 4 | 87 -> 1 | 31 -> 29 | 468 -> 389 | 2 (1) |
| thp8_net_funpark | 186 -> 147 | 693 -> 438 | 44 -> 40 | 437 -> 192 | 88 -> 70 | 470 -> 390 | 2 (1) |
| thp8_net_school | 161 -> 108 | 231 -> 137 | 22 -> 13 | 25 -> 4 | 51 -> 42 | 501 -> 421 | 2 (1) |
| thp8_net_industrial | 249 -> 124 | 562 -> 261 | 31 -> 22 | 213 -> 38 | 89 -> 53 | 1124 -> 464 | 9 (8) |
| thp8_net_capitol | 180 -> 114 | 1206 -> 71 | 36 -> 21 | 816 -> 13 | 138 -> 130 | 971 -> 330 | 6 (6) |
| thp8_net_skatefunparks | 202 -> 157 | 753 -> 441 | 46 -> 40 | 494 -> 192 | 91 -> 73 | 470 -> 390 | 2 (1) |
| thp8_net_skatecentral | 346 -> 187 | 1289 -> 80 | 40 -> 18 | 823 -> 10 | 203 -> 186 | 978 -> 336 | 6 (6) |
| thp8_training | 8 -> 4 | 3 -> 2 | 0 -> 0 | 0 -> 0 | 22 -> 22 | 0 -> 0 | 0 (0) |
| thp8_skateshop | 15 -> 6 | 1 -> 0 | 0 -> 0 | 0 -> 0 | 0 -> 0 | 0 -> 0 | 0 (0) |
| thp8_bedroom | 4 -> 2 | 2 -> 0 | 0 -> 0 | 0 -> 0 | 0 -> 0 | 0 -> 0 | 0 (0) |

BEAVER VALLEY's one-sided list (13, all flagged "soft alpha texture": the paint shells' base A2EDA0BA and the capitol decks'
7F0113CF are soft-alpha paint / glass / water, none foliage-named, none binary alpha): capitol roof_pools00-03 (4 x 160 m2)
and int_topdecks (641 m2); factory paint_shell_outside / _inside (552 m2 each), _outside_01 / _inside_01 (402 m2 each),
fact_breakroom_inside_glass (50 m2), turbine_case / turbine_case_inside (29 m2 each); Z_Inschool_g_back_periph05 (285 m2,
base CFEA1FFB - a periphery backdrop; its partner mesh in the same sector stays two-sided, being bigger than the 90% rule
allows; worth a look). The other levels: 2-9 each, mostly the same factory / capitol sets plus two stand-in meshes
(Z_Inschool_LOD_g_back_periph05, Z_Parking_LOD_g_contruction_01 type); full lists in the convert report. Left as is: fences /
lines (cut-out), glass against a Decal-template wall (Level_Geo_106, share < 90%), meshes bigger than their partner.

### Load check

Under the lock on C:\Games\THUG2-2 with out\build\lead\dinput8.dll (+.pdb), `[selftests] enabled=0`:
`load_check.ps1 -Levels thp8_net_beavervalley`: loaded=True, responding after 55.9 s, CComponent 256058 / 360000, CStruct
43340 / 64000; no error or exception in the log (the search only hit function names). Lock released (old dll / ini back).

### For the user (try_spot)

- Factory paint shells: `node tools/game/try_spot.ts thp8_net_beavervalley 7221,-121,8863,360` and `... 8109,-53,9034,0`.
- Pirate shop interior wall: `... -7280,977,10418,0` (and `-7823,1061,10418,0`).
- Elevator panels: `... 6210,1248,3429,270`.
- Capitol roof pools: `... -401,1007,10033,0`. Park wall / reflection: `... 4943,-387,13676,180`.
- Crete school hills: `... -14280,231,3299,0`.
