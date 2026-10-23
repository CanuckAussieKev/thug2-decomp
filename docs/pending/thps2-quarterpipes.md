# THPS2 ports: quarter pipes that stop the skater (2026-10-23)

The user: in the THPS2 level ports "we often clip and can't ride up" quarter pipes - three screenshots (NY at
(-26052.4, -311.4, -7782.2) heading 4.1, Skate Heaven (360.7, 65.4, -263.5) heading 124.4, Chopper Drop (-463.0,
86.4, -475.2) heading 172.9).

## What was wrong (confirmed)

THUG2's skater (src/game/skater/core_physics_feelers.cpp CheckFeeler) takes a hit face as ground when it is flagged
skatable 0x1 or vert 0x8, as a wall when flagged not skatable 0x2 or wallridable 0x4, and an unflagged face as a wall
when it is steeper than Wall_Non_Skatable_Angle (25: normal y < sin 25 = 0.42, sloped more than ~65 degrees). The
THPS2 converter flagged surface 0x20 faces vert, other faces with |normal y| < 0.3 wallridable, the rest nothing.
THPS2 has steep non-vert ramp faces that it rides:

1. Skate Heaven: a quarter pipe 26.9 -> 45.7 -> 67.6 (unflagged) -> 90 (vert) degrees. The 67.6 face was a wall:
   in the game the skater rode up to the 45.7 / 67.6 crease (y 75), left the ground, hit the face and was thrown
   back (trace: frame 242, the horizontal velocity reversed in the air; picture: flailing against the face).
2. Chopper Drop: 14.5 -> 33.8 -> 57.8 -> 73.0 degrees, the 73.0 face (normal y 0.29) flagged wallridable: the skater
   went into the air at the top of the 57.8 face (y 155) against it, top y 205, and came back.
3. NY: a vert ramp 22.4 -> 67.6 -> 90 (all vert), with a decal model (THPS2 object 793, surface 0x1, collision object
   762) lying 0.4 inch in front of its upper faces, unflagged - the 67.9-degree part a wall: the skater stopped dead at
   the crease (speed 431 -> 19 in a frame, at y -306).

Not a cause: the concave steps over Ground_stick_angle (30). CorePhysicsComponent::CheckForGround
(src/game/skater/core_physics.cpp ~305-340) drops a new ground face past that angle only when the facing goes into it
(`facingIntoHit > 0` - a convex edge, going over a lip or bump); going up into a steeper face the facing is against its
normal, and CheckWallAhead (core_physics_walls.cpp) steps onto any skatable face ahead that isn't edge-on (|n . ground|
>= 0.01). In the game NY's 45-degree step (22.4 -> 67.6) is crossed without a stop after the fix (frames 212 -> 218:
up y 0.92 -> 0.38, still on the ground). So no bevel strips: the collision geometry is the original's.

THPS2's other surface bits don't mark ramps: among steep (65 - 84 degree) up-facing non-vert faces, the ones that
continue a ramp are mostly 0x000 (140 of 409), the walls mostly 0x001 / 0x180 / 0x100 / 0x110 - but each of those also
appears on ramp faces (0x001 on NY's decal, 0x180 on 66-degree bevels). The geometry decides.

## The rule (tools/level_port/ramp_collision.ts, used by tools/thps2_levels convertWorld - rampCollision)

- Ramps: a face with no skatable / vert / not-skatable flag, facing up (normal y >= 0.1, sloped up to ~84 degrees)
  but too steep to be ground by itself (normal y < sin 25), that is joined along an edge (collinear within 0.5 inch,
  sharing at least an inch - T-junctions and other objects too) to a rideable face below it, the join concave and the
  bend no more than 40 degrees - or to the other half of its own quad - is flagged skatable (wallridable cleared) and
  becomes ground for the faces above it. A wall standing on a floor (a 65 - 90 degree bend) stays a wall, and so do
  faces past 84 degrees. Skatable, not vert: these are THPS2's non-vert quarter pipes - the skater flies out forwards
  (Chopper Drop lands on the deck), as in THPS2. Not THPS2's trigger planes (canRamp); only the world objects (one
  space - LevelObjects' models are each in their own).
- Overlays: a face lying on a larger one (normals within 8 degrees, within an inch of its plane, its centre inside it)
  takes that face's flags when they are more rideable (vert > skatable > nothing said > wall): the skater meets the
  decal first. NY's decals over vert walls became vert.
- Only face flags change: the .scn.xbx and .tex.xbx come out byte-identical to main's converter (checked: all ten
  levels, `--out` stagings compared); the .col.xbx's geometry the same. (The .qb differs between any two runs - not this.)

Per level, triangles changed (ramp faces made skatable / faces over a ramp given its flags): Hangar 25 / 34, School II
17 / 90, Marseille 33 / 8, NY 14 / 14, Venice 19 / 114, Skatestreet 22 / 29, Philadelphia 7 / 4, Bullring 41 / 12,
Chopper Drop 24 / 5, Skate Heaven 156 / 0.

## Numbers before / after (`node tools/level_port/ramp_check.ts <staging>\levels`)

"ramp walls": steep faces left walls inside a ramp - what ramp_collision would still change; "overlays": faces on a
more rideable face with other flags; ">30": concave joins between rideable faces bending more than 30 degrees (now
counted only between faces THUG2 rides, so it grows a little as the steep faces join in - not a problem, above).

| Level | ramp walls before -> after | overlays before -> after | >30 deg joins before -> after |
|---|---|---|---|
| Bullring | 45 -> 4 | 12 -> 0 | 91 (13.2%) -> 93 (12.9%) |
| Chopper Drop | 24 -> 0 | 5 -> 0 | 3 (6.5%) -> 3 (4.9%) |
| Hangar | 33 -> 11 | 34 -> 0 | 74 (17.6%) -> 74 (17.2%) |
| Marseille | 35 -> 3 | 8 -> 0 | 155 (13.2%) -> 159 (13.3%) |
| NY | 14 -> 0 | 14 -> 0 | 96 (17.2%) -> 99 (17.3%) |
| Philadelphia | 6 -> 1 | 4 -> 0 | 156 (20.4%) -> 159 (20.6%) |
| School II | 19 -> 4 | 90 -> 0 | 79 (22.1%) -> 81 (21.8%) |
| Skate Heaven | 156 -> 0 | 0 -> 0 | 214 (15.5%) -> 248 (16.4%) |
| Skatestreet | 23 -> 1 | 29 -> 0 | 99 (16.0%) -> 103 (16.4%) |
| Venice | 24 -> 5 | 114 -> 0 | 151 (20.1%) -> 151 (20.0%) |

The ramp walls left after are LevelObjects' faces (each model in its own space, which the check reads as if in the
level's - Hangar's obj 382 / 407 near the origin) and trigger-bit faces; the converter leaves those alone.

For comparison the THPS3 ports (their own Neversoft flags, not changed here) also have steep unflagged faces inside
ramps by this measure - Canada 163, Shipyard 110, Suburbia 162, Airport 55, LA 86. Worth a look if the user reports
the same there (not done: THPS3's flags are the original game's).

## Tested in the game (C:\Games\THUG2-1, under its lock)

The workspace's THPS2 levels converted with main's converter (before) and this branch's (after), installed with
make_menu_level.ts. This branch's dinput8.dll (it adds the automation command `trace`, below) copied in for the runs;
the lock's release put the workspace's own DLL and ini back - the workspace keeps the converted levels.
Each run: `-level <level>`, `teleport <x> <y> <z> <heading> board` ~400 inches before the user's spot along their
heading, `trace 600`, `press up 60` (push), `capture 90`, `wait 200` (out\scripts\ride.ps1 in the worktree). Traces
and frame series in the worktree's out\qp\runs\<level>_<before|after>; one picture each in out\qp\compare.

| Spot | before | after |
|---|---|---|
| Skate Heaven (30.7, 65, -37.5) 124.4 | ground to y 72 (the 45.7 face), air from y 77, top y 137, bounced back off the 67.6 face (frame 242) | rides the 67.6 face (up y 0.38) and the vert face (up y 0) to y 225, vert air to y 236, lands on the ramp, rides down and away |
| Chopper Drop (-512.6, 86, -78) 172.9 | ground to y 151, air at the 73.0 face, top y 205, back down | rides the 73.0 face to its top (y 246), flies out, lands on the deck at y 247 |
| NY (-26081, -311, -8181) 4.1 | stopped dead at the crease (y -306, speed 431 -> 19) | rides the 22.4 / 67.6 faces (no stop at the 45-degree step) and the vert face to y -100 |

thug2-decomp.log after each run: no exception, run-time check, CRT report or other fault.

NY, what's left (not collision flags): that vert wall is 141 inches of 90-degree vert above the 67.6 face (y -154 to
-13; THPS2's scale is large - 2.25 units an inch). Pushing alone (top speed ~433) the skater runs out of speed at
y ~-100, on the vertical face; THUG2's ground physics has no sideways gravity, so he then rolls along the wall
horizontally (as THUG2 would anywhere on a tall vertical vert face) until it ends and he drops off. In THPS2 the
skater probably went into vert air at the 67.6 face's top; making THPS2's tall vertical vert faces behave like that
(e.g. the lip at the 67.6 / 90 crease) would be a separate change.

## New tools

- `tools/level_port/ramp_collision.ts` (+ test) - the rule above, shared (any port whose flags are ours to make).
- `tools/level_port/ramp_check.ts` - the measurement; `--list` face by face, `--spot <col.xbx> <x,y,z> [radius]` the
  faces near a sidecar position (replaces the lead's scratch qp_spot.ts / qp_angles.ts).
- The automation command `trace <frames>` (src/game/front/automation.cpp, a parse self-test in
  selftests_automation.cpp): the skater's position, velocity, up y, state and physics each frame into
  thug2-decomp-trace.txt, running on under the next commands; `tools/game/trace_summary.ts` reads it (state runs,
  bounces, stops). tools/game/README.md documents both.
- Checks: npm run typecheck / lint / test (327 pass), the five converters' selfcheck, tools\verify_build.cmd +
  thug2_check (early self-tests 771 pass).

## For the user to try by hand

Install (the lead): convert the THPS2 levels and make_menu_level.ts into the main install. Then:

1. Skate Heaven, `node tools/game/try_spot.ts thps2_skateheaven 360.7,65.4,-263.5,124.4`: turn round, push back a
   bit and ride the quarter pipe in front - up its steep face onto the vertical top and into the air, no wall bonk.
   Also the bowls' corners (a few corner pieces bend more than 40 degrees from the face below and are still walls).
2. Chopper Drop, `try_spot.ts thps2_chopperdrop -463.0,86.4,-475.2,172.9`: ride up the steep ramp; you should fly off
   its top (onto the deck) instead of stopping at the last face.
3. NY, `try_spot.ts thps2_ny -26052.4,-311.4,-7782.2,4.1`: back off and ride the vert wall - no stop at the bottom
   crease or on the graffiti. Its vertical part is tall: say whether the skater should go into vert air earlier.
4. Anywhere: walls that should stay walls (slanted building fronts, kerb bevels) - say if one became rideable.

## Follow-up (2026-10-24): bails at some angles, combos breaking

The user, after the first fix was installed: the quarter pipes are skatable now, "but at certain angles, it causes
bails. Also, around 50% of the time, it fails to keep a combo going (e.g. doing a manual up the ramp). I think the
reason is because it doesn't consider it a single piece like most quarter pipes."

### How it was looked at

- The automation `trace` now also records bailing, doing a trick, the combo so far (score +0x14) and its multiplier,
  the score's +0x20, and the last ground hit's face flags and collision object (core physics' feeler copy at
  +0x158). Each `teleport` while a trace runs adds a `# teleport` line, and `tools/game/trace_summary.ts --trials`
  gives a line per ride: bail (where, in what state, up y), how the combo ended, air blips (in the air for 3 frames or
  less between ground), seams (the ground's collision object changing).
- The rides in THUG2-1 were at the three spots, 5 approach angles (0, +-25/30, +-45/50 degrees), and these modes:
  plain push, manual (up, down; perfect manual on - `run client_toggle_cheat cheat_flag=CHEAT_PERFECT_MANUAL on` - so
  the balance doesn't decide), square presses, manual + square, an early / late ollie. About 270 rides in all, plus
  the same set at a native THUG2 halfpipe (Kona, SC obj 107). The scripts are the worktree's out\scripts\trials.ps1
  and ride2.ps1 (one-off, not committed).
- The native levels' flags, offline: `node tools/level_port/ramp_check.ts --flags C:\Games\THUG2-1\Data\pre\BOcol.prx`
  (also SC, BA, TR, Sk5Ed). Up-facing faces by slope: vert almost only from 40 degrees up (BO 0-40: 34 of 7,490 faces,
  40-90: 775). No face is ever flagged skatable (0x1). Steep (65+) unflagged faces are plentiful, and they are walls.
  Kona's halfpipe, one object of 184 faces, goes 19.3 (unflagged) -> 37.7 (unflagged) -> 52.8 vert -> 83.9 vert.

### Causes

1. **Bail, confirmed: vert on low transitions.** THPS2 sets its vert bit (0x20) on whole vert ramps, down to their
   low transition (NY's 22.4-degree faces; 0x20 appears from 10 degrees up in the data). THUG2 sends a skater leaving
   any vert face into vert air: locked over the ramp, upright (up y 0), the velocity across the ramp taken away. An
   ollie on NY's transition at 25 / -25 / 45 degrees across it went straight into vert air, and he bailed when he
   came down on the transition (trace: up y 0.00 in the air, bail at landing, height ~60). THUG2's own levels never
   have vert there. That's also a likely way for a manual to end on a ramp - any hop on the transition is a vert air,
   and landing from it ends the manual.
2. **Not native: steep faces flagged skatable.** The first fix's promoted faces (67-84 degrees) were skatable, which
   THUG2's own levels never use: a steep face rides there only with vert. As skatable, leaving them is ordinary air
   with the skater pitched over. Made vert, with the ramp faces of 45 degrees and more under them, each ramp now has
   one vert band, as native ones do. This is what wasn't "one piece": the flags along a ramp - skatable next to vert
   next to unflagged, and vert on low transitions.
3. **Ruled out: seams between objects / unshared edges.** Over all the rides: 0 air blips. The ground's collision
   object changed 1-13 times a ride (ramp panels, NY's decal models over its vert faces) without ever ending a manual
   or a combo. In THUG2's physics nothing is per object for static collision; the feelers just hit the nearest face.
4. **Manual breaks:** with the balance held (perfect manual), manuals up the ramps held in every ride - up the
   ramp, back down, through the seams. They ended only where the skater hit a real wall: NY's ramp-end side walls at
   -45 degrees, the same in THPS2. Without the cheat the automation can't balance a manual, so the user's ~50% wasn't
   reproduced directly; cause 1 is the likely one.
5. **Left alone (THUG2 physics on THPS2's geometry, not flags):**
   - Chopper Drop's drop-in ramp is 448 inches tall with a 170-inch vert part at 75 degrees. Pushing up it the
     skater stalls; THUG2's TurnDownSlope (core_physics_turning.cpp, Skater_Slow_Turn_on_slopes) turns him sideways
     on the near-vertical face, and he rolls along it, off its edge onto the deck, and bails. Same before and after
     the vert change.
   - On Chopper Drop's west ramp, approached from the side at -25 / -45 degrees, the skater reaches its vert face
     right at the ramp's end (x -528). Vert air carries him along the lip past the end, and he lands on the floor
     upright and bails. That's the ramp's shape - THUG2's own vert ramps do the same off an open end.

### The rule now (tools/level_port/ramp_collision.ts rampFlags, in this order)

0. Vert off faces flatter than kMinVertSlope (40 degrees) - not THPS2's trigger planes (canRamp).
1. Faces lying on a more rideable one take its flags (unchanged).
2. Steep faces continuing a ramp (unchanged rule) are flagged **vert** (was skatable).
3. The vert band: unflagged faces of kVertBandSlope (45 degrees) or more, concave under a vert face with a bend of 40
   or less (or coplanar with one), become vert, and so on down.

Converter counts (triangles; steep made vert / band / low vert off / overlays):

| Level | Steep made vert | Band | Low vert off | Overlays |
|---|---|---|---|---|
| Hangar | 25 | 12 | 48 | 34 |
| School II | 17 | 33 | 21 | 90 |
| Marseille | 33 | 31 | 45 | 8 |
| NY | 14 | 17 | 53 | 14 |
| Venice | 19 | 65 | 0 | 114 |
| Skatestreet | 22 | 32 | 48 | 29 |
| Philadelphia | 7 | 30 | 17 | 4 |
| Bullring | 41 | 161 | 2 | 12 |
| Chopper Drop | 24 | 13 | 0 | 5 |
| Skate Heaven | 156 | 164 | 52 | 0 |

NY's flags by slope (`ramp_check.ts --flags`) now look like a native level's: vert 0 below 40 degrees (was 53
triangles), and from 40 up 22 / 16 / 63 / 31 per 10 degrees. The drawn scene and the geometry are unchanged.

### Checked in the game (one short run, THUG2-1, under its lock)

NY, the three ollie approaches that bailed (teleports at headings 25, -25 and 45, 380 inches before the user's spot):
3 of 3 bailed before, 0 of 3 after. The ollie is ordinary air now (up y 0.93), onto the 67.6-degree vert face, and he
rides on. The log was clean, and the lock's release put the workspace DLL and ini back. Not run again after that, by
the lead's instruction - the user judges the feel by hand.

### Tests

ramp_collision.test.ts: vert on the steep faces, the band down to 45 degrees (Skate Heaven's 45.7, Chopper Drop's
57.8; 33.8 not), low vert taken off (22.4), canRamp keeping a trigger plane's flags. thps2_levels.test.ts:
rampCollision across two objects (the 48-degree transition becomes the band), and convertWorld's flat vert quad no
longer vert. npm test 327 pass, lint / typecheck clean, thug2_check 771 early self-tests pass.

### For the user to try by hand

1. NY's vert ramp, `node tools/game/try_spot.ts thps2_ny -26052.4,-311.4,-7782.2,4.1`: ollie on the low transition
   at an angle, and manual up the ramp and back, several times - no bails from the transition, combos kept.
2. Skate Heaven's and Chopper Drop's quarter pipes: air out of them and land back in - it should feel like THUG2's own
   vert ramps (vert from the steep part up).
3. Say whether faces between 45 and 65 degrees going vert feels wrong anywhere (an ollie there is a vert air now, as
   on THUG2's own ramps).
4. Known: on Chopper Drop's giant drop-in ramp, stalling high up rolls the skater sideways off its edge; and leaving
   the west ramp's vert at its open end lands on the floor and bails.

## Tested in the game (2026-10-24, the user)
Both fixes as installed at 0388943: the user played the THPS2 quarter pipes (NY, Skate Heaven, Chopper Drop) - "all of
those areas feel good". Steep ramp faces ride, no bails on angled ollies, manuals / combos up the ramps hold.
