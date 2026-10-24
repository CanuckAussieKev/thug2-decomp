# thp8-edges - the missing wall at SKATE & FUN PARKS' east edge (2026-10-24)

The user: "Look at the latest 2 collision events. Seems like there is a missing wall or something here." Level
thp8_net_skatefunparks, events 11 / 12 (uber-frig, paths=no-ground) at (-1032.2, 346.4, -8310.5) and
(-1034.6, 346.4, -8116.1), the floor object's box ending at x = -1026.8. Screenshots 22-02-27 / 22-02-29: out from
under the DGK shelter toward a flat grey area (a stand-in) and bounced straight back.

## Cause (evidence from THP8's data)

- The floor there is z_cretepark's parkEntrance ground (box x -2241..-1027, y 346, z -8616..-7612). East of it
  (x > -1027) is **z_outschool**'s ground: in the whole town (thp8_net_beavervalley) the same strip is continuous ground
  (outschool body 545218a8 starts exactly at the park floor's edge). SKATE & FUN PARKS is made of cretepark + funpark +
  cartoon (`spawn_in`), so outschool's collision is not in the level.
- THP8's z_online_funskatepark barrier stands 600 inches (15 m) further east (x -428 at z -8200, a wall of two layers: a
  collidable 0x4 wall and an invisible, non-collidable 0x1090 veil) - **it encloses outschool's strip**; THP8 streams the
  city, so its skater rides on over it. Ours ended in the void between the floor edge and the barrier.
- And at the floor's edge in THP8 there is a real wall: z_outschool body 141 (a 1.2 m thick fence, x -1037..-988, y
  303..490, z -8989..-7288, face flags 0x4). Not in our level (outschool missing) - so no wall was hit, the floor
  ended, and the uber frig (nothing within uber_frig_current_height feet under the skater) turned him round. Its
  scene sector isn't drawn either (the other zone is only its `_lod` stand-in there: the flat grey area), hence "no visible wall".
- Case (b) of the brief, plus the missing wall: ground (and a wall) of a zone that isn't in the level, inside its barrier.
  Not (a) - the barrier itself was converted and is where THP8 put it - and not a seam strip (c).
- Checked: `render_audit.ts spot thp8_net_skatefunparks (-900,346,-8200)`: drawn there = the park ground, the
  snack hut, outschool's `_lod` stand-in (Sergio_Ground, a grey 256x256 texture), the barrier's veil. Collision
  (thug2_col read) there: the floor ends at -1027, nothing until the barrier walls (x -1740..-73).

## Fix (tools/thp8_levels/edges.ts, convertLevel's borderZones)

In a level with a network barrier, the collision of the city's *other* zones is added where the skater can roll to:
- from the level's own ground, cell by cell (1 m; onto ground cells within 2.5 m of height - ramps, small ledges),
  stopped by the barrier's collidable wall cells, no further than the barrier's furthest wall from the ground + 16
  cells (`barrierReach`: 4-97 m; the barrier has gaps where THP8's own scenery closes the world, so without a cap the
  walk leaks over the whole town);
- every collision body of another zone that has a collidable triangle over those cells (and 2 cells round them) is added
  **whole** (a wall box cut to its part over the cells was a few loose faces: first try, the outschool fence became a
  3-inch plate) - ground, walls, props' collision, but not trigger / non-collidable faces;
- their seam walls (`seamWalls` run with the border zones as further zones) are left out as in the town, and the own
  zones' seam walls now also see the border ground beyond them;
- scene unchanged: the border zones stay `_lod` stand-ins (collision only; the grey stand-in is the surface the skater
  rides over, the fence has no drawn model). NOT done: drawing the real zone parts inside the barrier (material /
  blend side is another agent's).
- Bug found on the way (own code, same session): `t.material & ~(0x10|0x40)` treats a flag-less face (material 0) as not
  collidable; the filter is now `!(material & (0x10|0x40))` (a test: groundCells of material 0).

Tool: `node tools/thp8_levels/edge_audit.ts [levels] [--staging DIR] [--top N]` (tests: edges.test.ts, 9; mutation: the
flag-less filter back to the old form - 5 fail): the converted collision's floor edges. A "floor edge short of a barrier"
= a big (>= 1500 cells of 24 inches, one surface) ground cell with nothing beyond within 2 cells, no wall in the way (the
edge's own cell included), and the network barrier's veil (0x1090 faces) further on within 50 m with no ground between - the
user's case. "void" = nothing below within 12 m, "drop" = ground to land on within 12 m (both include every ledge and roof of
big surfaces: noisy, they say where to look, not where a bug is).

## Audit: floor edges short of a barrier (count, metres), before (the main install's conversion) -> after

| Level | Before | After |
|---|---|---|
| thp8_net_skatefunparks | 13 (34 m; the user's: x -1056..-1032, z -8592..-8160) | 12 (23 m; the user's gone) |
| thp8_net_skatepark | 12 (37 m) | 13 (54 m) |
| thp8_net_suburbia | 2 (5 m) | 9 (23 m) |
| thp8_net_capitol | 13 (38 m) | 11 (34 m) |
| thp8_net_skatecentral | 12 (37 m) | 11 (32 m) |
| thp8_net_factory, _funpark, _industrial, _mainstreet, _school, _slums | 0 | 0 |
| thp8_net_beavervalley (no barrier; veil-like faces of its own) | 22 (49 m) | 22 (unchanged) |

(The main install's conversion predates the audit's wall-in-own-cell rule only in so far as it was audited by the same
tool; counts are of one tool on both.) The "void" / "drop" counts rise in the levels that got border zones (the added
areas' own ledges: e.g. skatefunparks void 94 -> 169): not a finding.
Remaining gaps are small (most 1-3 m: kerbs, ledges at the cap) with larger ones left to look at, in THUG2 inches:
- skatefunparks: (572, 169, -8304) 15 m (x 336..768, z -8592..-8040); (-2148, -76, -5364) 3 m;
- skatepark: (792, 946, 4866) 18 m (a high roof / ledge, y 946); (2175, 163, -3600) 13 m;
- suburbia: (3684, -66, -720) 9 m, (-4315, -118, -348) 7 m;
- capitol: (900, -147, -1716) 7 m, (900, -147, -1152) 6 m, (-3672, 179, -1068) 6 m;
- skatecentral: (-2520, 181, 852) 6 m, (2052, -147, 192) 6 m, (2052, -147, 756) 5 m.
Not looked at in the game (no pictures asked).

## Run (workspace THUG2-4, lock held and released; the lead build out\build\lead\dinput8.dll, [selftests] enabled=0,
[debug] collision_events=1)

`-Level thp8_net_skatefunparks`, `teleport -1200 347 -8300 90 board; trace 900; press up 600`: the skater rolled from x -1200
to x -1031.9 and stopped dead there (velocity 0, ground object 0xC32E81DA the wall) - **no collision-event line** (only the
two "logging on" lines), no uber-frig / no-ground in the log. Before the fix the same spot gave the user's two uber-frigs.
try_spot for the user: `node tools/game/try_spot.ts thp8_net_skatefunparks -1200,347,-8300,90` (push east, +x).

## For the user
- Install: `node tools/thp8_levels/thp8_levels.ts convert` (the 11 barrier levels; ~3 min) + make_menu_level. Other
  zones' collision is now in every network level with a barrier (more faces: skatefunparks 764 -> 910 objects) - a
  wall of one's own zone may now be where the other zone's scenery isn't drawn (an invisible wall: the picture shows the
  stand-in). If those show up as complaints, the drawn part of the zone is the next step.

## Drawn border zones (round 2, 2026-10-24)

The user: "physics is good here now, but still no visible fence" - the added collision stood on the grey `_lod` stand-in.
Now the other zones' real scene is drawn where their collision was brought in (draw-only; every level's .col.xbx is
byte-identical to the installed one, checked with cmp; the .qb differs run to run in one spot - nondeterministic before
this change too).

- `drawnBorder` (thp8_levels.ts): per border zone with kept collision (`readPart`, so notMade / flags / props as for own
  zones), a sector is drawn when a triangle of it has a point over the same reached cells (`edges.triangleInCells`, 2 m
  grid). Small sectors (<= 40 m on x and z, `BORDER_WHOLE`) whole - a house or fence is not cut in half - big ones (ground)
  only their triangles over the cells (`SceneParts.window`). Instances placed in the cells are drawn, the zone's
  moving objects / bouncy props are not. Only the materials / textures those sectors use are made (`SceneParts.used`).
  The parts go through the whole pipeline: zoneDuplicates, repeatedSurfaces, oneSidedMeshes, drawsAlpha, alpha-test
  texture copies, z bias. No node-array entries or scripts of the other zones (sectors draw without nodes, as stand-ins do).
- Border triangles are not light-refined (`refine.depth = 0`) and not in the refine budget (`counts.border`), so the level's
  own lighting detail is unchanged.
- Stand-ins: `standInClear` drops a stand-in triangle with a point (2 m) over a drawn border ground triangle unless the
  stand-in lies more than 1.5 m under it (far-under backdrops stay). The old filters (own ground, drawn surfaces) stay.
- Tests: edges.test.ts (triangleInCells), thp8_levels.test.ts (drawnBorder window / used / hidden, standInClear).

Cost per level (main install's conversion -> now; scn / tex MB; border = sectors, triangles in window, materials):
see the table in the report; largest: thp8_net_slums scn 9.9 -> 34.1, tex 12.7 -> 43.6 (827 sectors, 79k tris);
suburbia tex 8.8 -> 25.4; mainstreet scn 11.2 -> 29.4, tex 12.8 -> 38.0; skatecentral 41.8 -> 50.0 / 33.8 -> 55.7.
(BEAVER VALLEY has no barrier: unchanged.) The costs follow the reach of the collision brought in (4-97 m round the own
ground); if too much, narrow `REACH_SLACK` / the cap in edges.ts (collision and drawing share the cells).

Load check (THUG2-2, lead build, selftests off): thp8_net_skatefunparks loaded, running, CComponent peak 103146 / 360000,
CStruct peak 19945 / 64000; thp8_net_skatepark loaded, peak 66008 / 360000, 15026 / 64000. Other levels not load-checked.
Screenshot: C:\Games\THUG2-2\Screenshots\thp8_border_drawn.png (outschool's asphalt, building and the kerb at the skater's feet
drawn instead of the grey plane). try_spot: `node tools/game/try_spot.ts thp8_net_skatefunparks -1032,346,-8407,79`.

## The fence you hear but don't see (round 3, 2026-10-24)

The user: "there is still a missing fence. I can hear the fence, but don't see it." (thug2_2026-10-24_23-34-51, skater
(-1083, 346, -8457) heading +x, thp8_net_skatefunparks.)

- **What the fence is**: z_outschool collision body 141 = the collision of node `Z_Outschool_g_jeffedit_68`
  (LevelGeometry, `CreatedFromVariable = NODEFLAG_Z_SHOPS_UNLOCKSCHOOL_PRE`; its siblings g_jeffedit_70 / _73 / _74 / _76 /
  _71 and the sponsor banners 01 / 02 on the same line, x ~ -124.5 m THP8). It is the school's fence *before the school
  is unlocked*: THP8's non-story / network games set that flag false (game_progress_set_non_story_mode_nodeflags), so the
  node is never made - **neither drawn nor collided**. Its scene sectors exist in the zone's scene, but notMade hides them
  (own zones: `hidden`), and own zones' collision drops its bodies too (`convertCollision` `hidden`). The town
  (thp8_net_beavervalley) therefore has no fence there - the ground is continuous, as the first round found.
- **Why we had it**: edges.ts's border-zone collision took the zone's *raw* hkc bodies (`ZoneBodies.of`) and passed
  `hidden: new Set()` - the not-made filter that own zones get was never applied to the other zones. So collision of nodes
  THP8 does not create stood in the level with nothing to draw (drawnBorder correctly skipped the hidden sectors). Not a
  sampling / window miss, not an instance, not a cut-out material: the fence does not exist in THP8's net games.
  (Round 1's "case (b) plus a wall at the floor's edge" was wrong about the wall: it was this body, an artefact of reading
  raw bodies.)
- **Fix** (collision changes - the invisible wall goes; scenes byte-identical, checked with cmp on all 11): `ZoneBodies`
  takes `unmade(zone, file)` = `unmadeNodes` (nodes of the zone's array after the non-story flags, notMade) and drops
  bodies whose every triangle is owned by one (`edges.withoutUnmade`); the reached cells / ground are computed without
  them too. Border zones' report line gives how many bodies of the zone are of such nodes.
- **Across the 11 barrier levels**: bodies left out - z_outschool 3 (jeffedit_68 and two more), z_inschool 2, z_houses 1,
  z_ghetto 1, z_citypark 1, z_cretepark 1 (zone totals; only those inside the reached cells matter). Collision
  differs in 9 of 11 levels (funpark, capitol: same), e.g. factory 634202 -> 553990 bytes, skatepark 559194 -> 550874,
  mainstreet 2021594 -> 1999118, skatefunparks 1662788 -> 1661516; .scn / .tex identical in all 11.
- **Collision brought in with nothing drawn** (new report: `undrawnBodies`: under 25 % of the body's triangles within 1 m of a
  triangle drawnBorder draws, and the body's owner sector not drawn): per level 0-15 bodies, nearly all tiny or at the
  zone's origin (0,0: moving objects / props in their own space, e.g. z_ghetto 198 / 209 / 330-333, z_inschool 205 / 206,
  z_funpark 0, z_cartoon 0 / 11, z_citypark 8), plus a few large ones that are not fences: z_outschool body 191 (63 m x 43 m
  high, material 0x1080 = THP8's own invisible wall, no owner - same in THP8), z_cretepark Z_CretePark_g_hillside_locked1
  (19 x 134 m, a locked-area slope), z_inschool body 1 (71 x 11 m), z_parking construction / Entrance_Grounds00 (ground),
  z_shops body 235 (35 x 164 m), z_factory body 69 / 199, z_citypark Z_Citypark_TRG_killcollide (a trigger). The first
  version of this audit (sector name / box match only) listed 5-70 per zone and was mostly noise (a body is often many
  sectors' collision). Not looked at one by one: hillside_locked1 and the few big ground ones are the candidates if
  another invisible wall is reported. Nothing else fence-like (thin tall body with a sector of the same name undrawn)
  was found.
- Tests: edges.test.ts withoutUnmade (mutation `every` -> `some`: 1 fails), thp8_levels.test.ts undrawnBodies.
- Run (THUG2-2, lock held and released; lead build, selftests off): thp8_net_skatefunparks loaded and ran (CComponent peak
  103146 / 360000, CStruct peak 19941 / 64000); the user's spot: C:\Games\THUG2-2\Screenshots\thp8_fence_gone.png (open
  outschool asphalt, the Beavers building; the skater can roll on east past x -1037).
- Install: `node tools/thp8_levels/thp8_levels.ts convert` (the 11 barrier levels) + make_menu_level.
