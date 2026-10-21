# Ported levels: shimmering surfaces (2026-10-21)

User report: "shimmering on a lot of assets in the ported levels ... on the ramps in Foundry; in many levels from many
ported games" (screenshot thug2_2026-10-21_18-10-40.png: Foundry, a quarter pipe with vent grilles over a grated
floor). Branch `worktree-agent-a1e50ea5d28baa054`; tools only, no C++ changed.

## Causes (from the data)

1. **Layers z-fight with the surface under them (THPS3 and THPS4 ports) - the main cause.** THUG2 draws a material
   with its z bias as the depth bias: 0x004D1F51 and 0x004D2420 push the material's byte +0x11 to render state 0x15
   (Nx::SetState -> D3DRS_DEPTHBIAS = bias x -2e-6, about 33 steps of the D24 depth buffer per unit). THUG2's own
   levels give it to the surfaces laid on others (decals, dirt and light layers, stripes): of the surface area of one
   material lying on another's (coplanar within 1 unit, same facing, overlapping - tools/level_port/layer_check.ts),
   40-88% has a z bias between (LA 40, SE 66, AU 77, NO 86, SC 88%); THUG1 / THAW ports keep their source's (fl 19%,
   z_sm 99%). THPS3's and THPS4's materials have no z bias and every converted material had 0: **0%** in foun, rio,
   sub, alc, sf2, cnv... Foundry: 7.6 M square units of layers on other surfaces, all fighting; e.g. its ramp layer
   0x0B6E0288 (decal + transparent, blend 1, 540 triangles) lies 0.1-1 unit above the ramp 0xD80720D5 (1387
   triangles), no shared vertices - at a distance the depth buffer can't tell them apart and the layer flickers in
   and out as the camera moves.
2. **THPS3's mip chains stopped early.** THPS3 ships one level per texture; the converter made at most 4 levels, down
   to 8 x 8 (128 x 128 -> 16 x 16). THUG2's own go down to a shorter side of 4 (128 x 128 has 6 levels). Every
   THPS3 texture (foun 208/208, rio 361/361, sub 401/405) stopped short: tiled floors / grates / ramps far away sample
   a level that's still too detailed and alias (sparkle). THPS4, THUG1 and THAW keep their originals' chains (THAW's
   stop at 8 x 8 - one level short, as THAW shipped them; left alone).
- Checked and not a cause: the material passes' filtering (0x10004) and mip fields (1, 4, -8, -8) are the same as
  THUG2's own levels' in every port; the sampler setup (0x004D80D0) is the game's own; single-level textures in
  THUG1 / THAW ports are the originals' (THUG2's own levels have them too, e.g. TR 52).
- Not fixable per material: the same material twice on the same surface in two meshes (alternative / duplicated
  geometry, e.g. THPS3 LA's earthquake states, transparent layers duplicated across sectors) - left as they are.

## Fix (shared converter code)

- `tools/level_port/overlay_bias.ts` (new): `biasOverlays(scene)` finds the layers from the geometry (a triangle whose
  centre lies on another material's triangle: same facing, within 1 unit of its plane, inside it) and gives the top
  one a z bias one above the one under it. Top = the later draw order; at the same draw order the decal / transparent
  one; else the one more of whose surface is covered (>= 50%, by 10% more than the other) - two opaque surfaces
  covering each other alike (alternative geometry) and same-looking materials are left alone. Loops broken (surest
  first), z bias at most 6, a bigger existing one kept. Called by tools/thps3_levels (convertLevel) and
  tools/thps4_levels (convertGeometry), the two sources without z bias. Foundry: 38 of 345 materials (36 at 1, 2 at
  2); the overlap with a z bias between goes 0% -> 79% (rio 90, sub 86, alc 98, sf2 91, cnv 88%).
- `tools/thps3_levels/thps3_levels.ts` convertTextures: mip levels (2 x 2 averages) down to the one whose shorter
  side is 4, as THUG2's own (the loader 0x004CAE90 creates the texture with the file's level count and copies each
  level as it is - smallest level 4 rows, the height it clamps to).
- `tools/level_port/layer_check.ts` (new CLI): per level, mip chains that stop short / single-level textures, and the
  share of layer area with a z bias between - to compare a port with THUG2's own.
- Tests: tools/level_port/overlay_bias.test.ts (6: a decal on a floor; a layer on a layer; draw order wins; back to
  back; left alone - alike / same look / bigger bias kept; THPS3's mip chain 128 x 64 -> 8 x 4 and its 2 x 2 average);
  mutations of the plane tolerance, the draw-order rule and the coverage rule each fail them. typecheck / lint / test
  pass (162 tests).
- Docs: tools/level_port/README.md, tools/thps3_levels/README.md, tools/thps4_levels/README.md,
  docs/level-port-compromises.md (two rows in "Across all four games").

## Game runs (C:\Games\THUG2-2, under the lock)

Reconverted every THPS3 (14) and THPS4 (11) level into the workspace, make_menu_level.ts; `-level foun` and
`-level alc` load and play (log clean). Before / after pictures are from the start spot only: a still can't show
shimmer - **the user should judge it moving**: Foundry's quarter pipes with the vents and the grated floors from a
distance, and any THPS3 / THPS4 level's painted floors, stripes and dirt layers.

## For the lead

Reconvert every THPS3 level (foun, can, rio, sub, ap, si, la, tok, shp, ware, burn, ros, tut, skateshop + the skies)
and every THPS4 level (alc, cnv, hof, jnk, kon, lon, sf2, sch, zoo, thps4_skateshop, motox + skies), then
make_menu_level.ts. THUG1 and THAW ports are unchanged (their sources' z bias and mips kept).
