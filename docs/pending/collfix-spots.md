# THUGPRO's collision overlays in the THPS3 ports - what they fix, and spots to try them

Branch worktree-agent-a7f83046be3eb7697, 2026-10-21. NOT run by the user yet: the checklist below is for them.

## What was added

- **Start at a spot, standing** (the user: "launch the game in the exact spot, standing so I don't move, and I can try
  it"): `THUG2.exe -level <name> -at x,y,z[,heading]` or `[features] start_at=` (the command line wins) - once the
  level is up (skater there, unpaused, a second settled) the skater is put there standing by the test views' teleport
  (src/game/front/test_view.h: on the ground below the point, walk state, no momentum), facing `heading` degrees
  (0 = +z, 90 = +x). Works with `-edit` too (before the editor opens). The spot is three or four numbers, commas and /
  or spaces, optionally in parentheses (the screenshot sidecar's `(x, y, z)` can be pasted). Code:
  src/game/front/start_level.h / .cpp (`ParseSpot`, `AtUpdate`); self-test in selftests_editor.cpp (TestEditorAutosave,
  "start at: ..."). Not online: only with a start level, which loads free skate.
- **Launcher**: `node tools/game/try_spot.ts <level> <x,y,z[,heading]> [--game <folder>]` (default C:\Games\THUG2;
  `--game` before a spot that starts with a minus); `--list collfix` prints the spots below with their commands,
  `--list collfix <n>` starts the n-th. Spots: tools/game/spots.json. Refuses while a game runs from that folder.
- **Comparison tool**: `node tools/thps3_levels/collfix.ts [--game <folder>] [--detail <object> [--no-faces]] [--json]`
  - each overlay's faces against the converted level's own collision (read only).
- **Converter switch** `thps3_levels.ts convert ... --no-collfix`: the 8 overlays (collision objects and their
  geometry nodes, TPN-THPS3-1 / -2) left out, and Suburbia's Inviso_PicnicCollide01 kept as THPS3 has it (its TP-14
  flag edit - CreatedAtStart removed - not applied). Default unchanged. Checked: sub 52556 -> 52296 faces (-260 = the
  5 overlays), foun 41055 -> 41043 (-12), col_check 0 unreachable both ways; the picnic node CreatedAtStart again.

Verified in the game (workspace C:\Games\THUG2-1, under its lock, never online): `try_spot.ts --game C:\Games\THUG2-1
--list collfix 6` (Foundry, command line `-at`) and `run_game.ps1 -Level sub` with `start_at=(-1803.9, 99.4,
3106.2, 46)` in the ini: both logged "test view: skater teleported standing to ... (on the ground below)" and "start
at: the skater stands at ..."; the pictures show the skater standing, board in hand, at the spot. thug2_check (THUG2PM
.exe and C:\Games\THUG2\Game\THUG2.exe): 0 MISMATCHED, 521 early self-tests pass; check_code_writes: no overlaps.

## The overlays against our own collision

Measured with collfix.ts on the main install's converted levels (default conversion: the overlays in). "In front":
rays from 48 units in front of 15 points on each overlay face, back along its normal - which surface a skater coming
from that side meets first. Our conversion's collision is complete there (col_check: every face reachable).

| # | Overlay (faces) | Where | Against ours | What THUGPRO was fixing (our reading) | Ours |
|---|---|---|---|---|---|
| 1 | Sub `SUB_TRamp06_collfix` (8) | trailer-park launch ramp SUB_TRamp06 | a coarse skin 2-12 units (median 6) under the ramp's faceted surface; 2 faces run on under the ground at its foot; ours met first at all 120 points | a backstop under the ramp and its foot - against falling through the ramp or the ramp / ground joint (THUGPRO's own conversion of THPS3's .bsp) | covered: the ramp's 443 faces are there and met first; the overlay is touched only by going through them |
| 2 | Sub `SUB_TRamp04_collfix` (8) | launch ramp SUB_TRamp04 (+ SUB_TRbase07) | 2-22 units (median 13) under; 4 faces under the ground at its foot; ours first at 120/120 | as 1 | covered, as 1 |
| 3 | Sub `SUB_plywood_08_collfix` (24) | construction site's plywood bowl | a simplified bowl 5-36 units under the floor (dirt at y 23.7, overlay 8.6-12.9) and transitions; ours first at 354 of 360 points; 6 points in the open at the top of its west wall (x 1188-1214, y up to 140) | a backstop under the bowl's floor and transitions, and a lip-height extension along the west wall's top | covered but for that strip along the west wall's top: the one place where the overlay is the first thing touched |
| 4 | Sub `SUB_HHBYardWall_collfix` (192) | haunted house back yard, along the yard wall | a 35-degree slope and a flat strip under the quarter-pipe decks (SUB_HHByard02 / 05 / 07, y 208-217) and inside the foot of the thick yard wall; from the yard ours is met first (1360 points) or at the same place (21); the rest is under the decks / inside the wall (hollow in ours: nothing there) | the crack where the decks meet the wall's inner face (wallridable, its bottom edge at deck height): a skater who slips through - dropping out of a wallride, landing against the wall - lands on a slope that pushes him back out instead of falling into the wall | covered geometrically (the decks reach 1-18 units under the wall face, shared edges); whether THUG2's physics slips through that joint is the play test |
| 5 | Sub `Inviso_PicnicCollide01_collfix` (28) | trailer roof: invisible box round the picnic table (y 143.8-286.1) | the same 28 faces as THPS3's Inviso_PicnicCollide01, vertex for vertex; only the flags differ - THPS3's (ours with --no-collfix): not skatable on 26 faces, the +x side vert + trigger (an empty SubInviso_PicnicCollide01Script); THUGPRO's: plain collidable (camera passes, under ok) | THUGPRO switched THPS3's box off (TP-14) and put a copy that is skatable and not vert: landing on the box's invisible top (14 units above the table) or riding into its +x side behaves like a normal surface instead of THPS3's no-skate / vert launch | a behaviour choice, not missing geometry: with the default conversion the game has THUGPRO's box only; --no-collfix gives THPS3's |
| 6 | Foun `SouthDeck_QP04_coll` (4) | south deck: quarter pipe QP04's run-out into the sloped deck (SouthDeck_Ramp_03) | a flat 45-degree plane 5-47 units behind the curved QP where it narrows to a point over 355 units (long thin fan triangles); ours first at 60/60 | a backstop behind the QP's narrowing end - against falling through its thin fan faces | covered (all faces there, reachable) |
| 7 | Foun `SouthDeck_QP01_coll` (4) | the same on QP01 (mirror side) | 4-42 units behind; ours first at 60/60 | as 6 | covered |
| 8 | Foun `Trench_HP04_Back_coll` (4) | trench: the bank behind the half pipe | a 22-unit strip 1.1-3.5 units under the bank, over the seam x = -4060.7 between Trench_HP03_Back and Trench_HP04_Back; ours first at 60/60 | a seam bridge: against catching / falling at the join of the two bank objects | covered (the two banks share the edge's vertices exactly); the overlay is so close under the surface that it could itself be felt as a bump |

So no overlay adds a surface the skater meets in the open, apart from a strip along the top of the plywood bowl's west
wall (3) and the picnic box's flags (5). The rest are nets under or behind our surfaces: harmless if THUG2's physics
never goes through those joints, needed if it does - the play test decides. Overlays 1, 2, 6, 7, 8 lie close enough
under the surface (1-12 units) that, if anything, they could add a bump; 3 and 4 are deeper.

Online: the overlays are geometry THUGPRO players collide with. Dropping one is safe for parity only where ours covers
the same surface (all but the plywood strip and the picnic box's flags); recorded in docs/thugpro-hacks-review.json
"collfix-overlay-collision".

## For the user: try each spot (A/B)

The lead converts both variants into the main install on request: the default (THUGPRO's overlays in) and
`--no-collfix` (left out, THPS3's picnic box back): `node tools/thps3_levels/thps3_levels.ts convert sub foun
[--no-collfix]`. Try each spot in both and say which feels right. `node tools/game/try_spot.ts --list collfix` prints
this list; each command starts the game standing at the spot, facing the surface.

1. Suburbia - trailer-park launch ramp SUB_TRamp06: skate straight up it, and off its sides. Does it snag / bump /
   let you through at the foot or on the face?
   `node tools/game/try_spot.ts sub -1584,25.8,-723.9,-90`
2. Suburbia - launch ramp SUB_TRamp04: the same.
   `node tools/game/try_spot.ts sub -2243.7,26.3,-1198.5,90`
3. Suburbia - construction site's plywood bowl: ride the floor and the transitions, especially up to the top of the
   west wall (the one in front of you). Snag / bump / let you through? Anything invisible at the top of that wall?
   `node tools/game/try_spot.ts sub 1494.4,25.7,2387.3,-91`
4. Suburbia - haunted house back yard: the quarter pipes and their decks along the yard wall - land on the decks next
   to the wall, wallride the yard wall and drop onto the deck. Do you fall into the wall / under the deck, snag?
   `node tools/game/try_spot.ts sub -1803.9,99.4,3106.2,46`
5. Suburbia - trailer roof, the invisible box round the picnic table: land on top of it, ride into its far (+x) side.
   Default: THUGPRO's plain box; --no-collfix: THPS3's (no-skate top, vert far side). Which feels right?
   `node tools/game/try_spot.ts sub -3011.9,142.8,-993.4,90`
6. Foundry - south deck, where quarter pipe QP04 narrows into the sloped deck: skate up the narrowing end and along it.
   Snag / bump / let you through?
   `node tools/game/try_spot.ts foun -3568.4,-202,-959.1,105`
7. Foundry - the same on QP01 (other side).
   `node tools/game/try_spot.ts foun -4558.3,-202,-960.3,-104`
8. Foundry - trench: the bank behind the half pipe, across the join of its two halves (straight ahead): ride up and
   across the join. A bump or a snag at the join?
   `node tools/game/try_spot.ts foun -3929.2,-298,1690.7,-27`

(The spots are from `collfix.ts --json`: about 300 units out from each overlay's largest sideways face, on the floor
in front of it, facing it - the skater stands still there; the camera may still be to one side.)

## Shared files touched (small, additive)

- src/game/front/start_level.h / .cpp (`-at` / `start_at`, `ParseSpot`), src/hook/selftests_editor.cpp (its test)
- tools/thps3_levels/thps3_levels.ts (`--no-collfix`), tools/thps3_levels/collfix.ts (+ test)
- tools/game/try_spot.ts (+ test), tools/game/spots.json, tools/game/edit_level.ts (`readLevels` exported)
- README.md (4a table, [features] table), tools/game/README.md, tools/compat/thugpro/README.md (TPN-THPS3-1 row)
- CLAUDE.md's "Where things are" feature list doesn't name `start_at` / `-at` yet (left for the lead).

## How to switch it off

`-at` / `start_at` does nothing unless given. The converter's default is unchanged; `--no-collfix` only when asked.
