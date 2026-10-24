# THP8: the memorial's vert wall and the tree trunk (BEAVER VALLEY collision events, 2026-10-24)

The user (BEAVER VALLEY, `[debug] collision_events=1`): two `slope` events on a big concrete quarter pipe with a
coping rail "where I believe it should be a vert ramp", and a third, `wall-ahead-corner`, "stuck in a tree".
Screenshots C:\Games\THUG2\Game\Screenshots\thug2_2026-10-24_19-01-22 / -25 / 19-01-59 (.png + .txt); the full event
lines were read from the main install's log (readable while the game runs).

## 1. The vert wall: Z_Center_g_grounds_memorial_03 (the Capitol grounds' memorial)

**The vert flag is there.** Collision object 891 (its sector's body): a 6.4 m wall, floor at y 196.5, deck at 448 -
a 7, 24, 40, 54, 69, 83 degree transition up to y 261, then 4.7 m of vertical wall (y 261 - 444) and a 3-inch
bevelled lip. Every face from 69 degrees up is flagged 0x8 (vert, THP8's own flag, kept by `faceFlags`); the 40 and
54 degree faces are unflagged ground. THUG2 reads 0x8 as skatable vert (CheckFeeler, core_physics_feelers.cpp) - the
same as THUG2's own quarter pipes (`ramp_check.ts --flags`: BO / BA / SC have vert faces from 40 degrees up to
vertical). So the faces themselves behave as vert.

**What was wrong: the gap filler's shelves in the transition.** `thp8_net_beavervalley_GapFill_61` had flat 20-inch
squares at y 215.6, 217.3, 221.1, 222.8 inside the ramp's transition, standing up to ~5 inches out of its 54-degree
face (at z 10690: the ramp's face at y 221.1 is at x -3749.9, the fill square reaches -3745.1). gapFills took the
cells under the ramp's steep (> 60 degrees, normal y < 0.5) part for void - "ground one side (the transition), a
wall the other (the vert wall)" - and floored them at the transition's height, half a cell wider all round. The
user's event 2 shows the result: its `approach` has the skater on the ground at the floor, then **in the air at
(-3746, 226)** - on the transition, thrown up by a shelf - and landing on the vert wall sideways (y 267), where
0x004FF590 put his velocity along his facing (`flipped=turned>along-facing-4FF590`: z -139 -> +39, the "slope"
event). Event 1 is an ordinary roll-back: approach y 363 -> 403 -> 426 -> 422 -> 384 -> 309, all on the ground,
riding diagonally up the vert wall and turning back 18 inches under the lip (444) - THUG2's ground gravity (-1000)
and friction against a 6.4 m wall; reaching the lip from the floor takes ~700 - 900 inches/s at its foot.

**Fix (tools/thp8_levels/thp8_levels.ts, gapFills, collision only):** a cell a steep face still facing up passes
through (normal y at least `GAP_RAMP_UP` = 0.05, ~87 degrees) is a ramp, not void: never filled, and for its
neighbours' search it counts as ground at the ramp's lowest point there (a slit at a ramp's foot is still floored).
Vertical walls (kerbs' faces, invisible shells) are unchanged. Test: thp8_levels.test.ts "narrow holes ... under
eaves" - a quarter pipe (floor, 54-degree transition, 65-degree face, vert wall) gets no fill; proved with the rule
off (GAP_RAMP_UP = 2: the test fails, a fill at y 6.3 from x 205 to 225).

### Counts (all fifteen levels, converted into C:\Games\THUG2-4's staging before / after)
Gap-fill faces, and those standing proud of a sloped face (20 - 87 degrees, facing up) 1 - 30 inches below one of
their points (out\scripts\shelf_audit.ts, scratch):

| Level | fill faces before -> after | standing proud of a ramp before -> after |
|---|---|---|
| thp8_net_beavervalley | 34,634 -> 19,164 | 11,370 -> 546 |
| thp8_net_capitol | 9,752 -> 4,082 | 4,672 -> 188 |
| thp8_net_skatecentral | 17,736 -> 9,692 | 5,716 -> 222 |
| thp8_net_skatefunparks | 13,476 -> 10,936 | 3,432 -> 406 |
| thp8_net_skatepark | 7,258 -> 5,692 | 2,678 -> 212 |
| thp8_net_school | 4,150 -> 1,934 | 1,664 -> 0 |
| thp8_net_suburbia | 7,282 -> 6,910 | 1,044 -> 174 |
| thp8_net_factory | 1,978 -> 620 | 972 -> 32 |
| thp8_net_funpark | 7,354 -> 6,546 | 694 -> 236 |
| thp8_net_mainstreet | 4,484 -> 3,144 | 682 -> 8 |
| thp8_net_industrial | 5,162 -> 4,590 | 648 -> 80 |
| thp8_net_slums | 3,150 -> 3,164 | 320 -> 40 |
| thp8_training / skateshop / bedroom | 1,282 / 32 / 6 -> 1,282 / 32 / 36 | 0 |

(The metric is loose: what's left "proud" is mostly a fill over a gentle slope under it, not a ramp - e.g. BEAVER
VALLEY's (-11610, 1079, 9813), there before and after.) BEAVER VALLEY's fill cells: 45,680 -> 23,724; of the 23,116
gone, 20,294 had a steep up-facing face in the cell, the rest (sampled: (-11680, 2995), (-4880, 995), (-330, 7305))
sat at quarter pipes' feet or at the floor's own height. The known slits are floored as before: the suburb's kerbs
(-6789, 3098), the flower ring (-6519, 3453), (-6628, 3226) - the same fill faces within 400 inches. The holes audit
(drawn ground with no collision) is unchanged (it doesn't count fills).

### Vert flags across THP8's levels (ramp_check.ts)
THP8's own flags are kept; by `ramp_check.ts` (the THPS2 converter's rules, level_port/ramp_collision.ts) the THP8
levels have no more unflagged steep ramp faces than THUG2's own: "ramp walls" (steep faces left walls inside a ramp)
BEAVER VALLEY 333 of 214,136 faces (0.16 %; THUG2's SE 233 of 34,335, BA 144 of 32,462, AU 179 of 33,541), capitol
86, skatecentral 117, skatefunparks 151, funpark 100, school 52, skatepark 51, suburbia 30, industrial 12, slums 7,
factory 5, mainstreet 1, skateshop 30, bedroom 2; training 315 of 7,705 - all rounded ledge edges 1.5 - 3 ft high
(x 1437 - 1495, y 18 - 39), not ramps. So no vert flags were added: THP8 marks its vert as THUG2 does.

## 2. Stuck in a tree: Z_Center_I_JP_new_center_super_small_tree_30 (event 3)

- The trunk is THP8's own collision: a cylinder primitive in the planter's body (object 895,
  Z_Center_g_OG_grounds_plantergrind_03, faces 0 - 27), radius 8.7 inches (an 8-sided prism, flags 0x80 - the camera
  passes; solid for the skater) from y 104 to 323. The drawn trunk is 6 - 8 inches from its axis: the collision
  isn't bigger than the tree. No neighbour leaves a gap round it. THP8 makes it solid (its own primitive).
- The skater was **inside** the trunk: pos (-2718.2, 238.1, 12457.6) and pos_before are 4 and 3 inches from its axis.
  The event's `approach`: off a rail at (-3034, 231, 11973), in the air (-2941, 232) -> (-2866, 285) -> (-2790, 300)
  -> **(-2712, 278, 12456) still in the air, already inside the trunk's footprint**, then on the ground at (-2718, 238).
  So he went in through the trunk's side while in the air; inside, the planter's soil (faces 28 - 35, a low mound
  whose top vertex (-2721.0, 238.3, 12457.9) is inside the trunk) is ground, and every way out hits the trunk's faces
  from inside (the feelers are two-sided: the event's ray starts inside and hits face 10) - the `wall-ahead-corner`
  bounces.
- **Engine, not changed:** the likely way in is CheckAirForwardCollision (0x00508780, core_collision.cpp): on a hit
  it tries the skater at the feeler's end - the new position plus Skater_First_Forward_Collision_Length (10 inches)
  along the move - through 0x004FFDB0 (not rewritten), and keeps him there when that succeeds and the wall is steep
  (n.y < 0.1): a 17-inch pole is narrower than a frame's move (~14 inches) plus 10, so the end lands inside or past
  it; with the soil continuing under the trunk there is ground to land on there. Not confirmed (0x004FFDB0 isn't
  decompiled); no converter change. A data-side guard, if wanted: leave out ground faces inside closed primitives
  (the soil under a trunk) - so a landing inside finds nothing.

## The confirming run (C:\Games\THUG2-4, this branch's DLL, `[debug] collision_events=1`)
`run_game.ps1 -Level thp8_net_beavervalley -Commands "waitfor level ...;teleport -3420 200 10767 -90 board;trace 900;
press up 240;wait 500"` (loaded in 79 s): the skater pushed off towards the wall at 425 inches/s and rode the
transition **on the ground** (frames 198 - 210: y 198 -> 221 -> 259, up_y 0.99 -> 0.58 -> 0.12, no air), onto the vert
faces (0x0008) up to y 409, turned along the wall and rode off down the memorial's other faces - no collision event
in the log (only the two "logging on" lines). Not reaching the lip at that speed is THUG2's physics (above).

## Workspace / checks
- npm run typecheck / lint / test pass (356). Converted all fifteen levels into C:\Games\THUG2-4's staging and
  installed them there (make_menu_level.ts --game C:\Games\THUG2-4, --no-mips).
- Shared files: none (tools/thp8_levels only).

## Needs regenerating (the lead)
All THP8 levels (`node tools/thp8_levels/thp8_levels.ts convert`, then make_menu_level.ts).
