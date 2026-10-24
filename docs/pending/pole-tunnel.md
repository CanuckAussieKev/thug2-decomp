# Stuck inside a tree trunk: how the skater gets into a thin pole (BEAVER VALLEY, 2026-10-24)

The user's collision event 3 (thp8_net_beavervalley, `wall-ahead-corner`, sidecar
C:\Games\THUG2\Game\Screenshots\thug2_2026-10-24_19-01-59.txt): inside Z_Center_I_JP_new_center_super_small_tree_30's
trunk - a THP8 cylinder primitive in object 895 (Z_Center_g_OG_grounds_plantergrind_03, faces 0 - 27: an 8-sided
prism of radius 8.7 round (-2714.75, 12455.3), y 104.4 - 322.9, flags 0x80), reached in the air coming off the
planter's rail (approach (-2790, 300) -> (-2712, 278, 12456), 2.8 inches from the axis). Branch
`worktree-agent-a51065017737976a6`.

## Verdict: a converter bug (the trunk was inside out) - the engine is the original's, and ours matches it

**Mechanism.** THUG2's collision is one-sided and its normals come from the winding:
- 0x0040B420 (ray / triangle; ours Nx::RayHitsTriangle, coll_tests.cpp) returns no hit when the determinant
  edge1 . (dir x edge2) - that is -dir . ((v1 - v0) x (v2 - v0)) - is under 0.03 (`FCOMP [0x00645A8C]`, then
  `XOR AL,AL` at 0x0040B4D1): a face is hit only from the side its winding faces.
- 0x00413B50 (CCollStatic::CollideWithLine, coll_line.cpp) gives the hit the normal (v1 - v0) x (v2 - v0),
  normalised - whichever way the face "really" faces.
- DoAirPhysics 0x0050CA00 (core_physics.cpp): after CheckAirForwardCollision 0x00508780, the feet feeler from the
  last position to the new one (0x0050D505 CheckFeeler 0x10, 0); on a hit, unless 0x004FFDB0 puts the skater on a
  ledge (below), position = hit point + normal (0x0050D5A3 - 0x0050D625); not skatable: the velocity and facing turned
  to the plane and the skater pushed Skater_Min_Distance_To_Wall (8 in skater_physics) along the normal. The ground's
  wall checks use the same feelers.

**The data.** The converter's `cylinderTriangles` (tools/thp8_levels/havok.ts) wound a prism's ends outwards but its
sides inwards (the side triangles' corners in the wrong order): the converted file has face 0 (-2714.7, 104.4,
12446.6) (-2714.7, 322.9, 12446.6) (-2720.8, 322.9, 12449.2), normal (0.39, 0, 0.92) - on the trunk's -z side,
pointing +z, into the trunk; every side the same; the caps (faces 4, 5, ...) point out. So:
1. From outside, the near side faces away from the feeler: not hit. The skater's feelers (feet, and the forward one
   8.1 up reaching 10 past the move) pass the near side, and in one frame (~14 inches) don't reach the far one: he
   moves in unhindered.
2. Inside, every side faces him: the next feeler hits the far side from its front, normal pointing back in; hit
   point + normal + 8 along it puts him ~1 inch from the axis (the far side is 8.0 from it). That's where the event
   found him: pos (-2718.2, 238.1, 12457.6), 4.1 from the axis; the approach point 2.8 from it.
3. Every way out hits a side from the front and is pushed back in; the planter's soil continues under the trunk (its
   apex (-2721.0, 238.3, 12457.9) is inside), so there's ground to stand on: stuck, the `wall-ahead-corner` bounces.

The previous agent's hypothesis (CheckAirForwardCollision leaving the skater at the feeler's end through 0x004FFDB0)
is not it: 0x004FFDB0 (read here, not rewritten) is the ledge snap - a feeler from Physics_Air_Snap_Up (15) above the
higher of the old / new position down to the lower one, at the new position; on a face with normal y > 0.5
(`[0x006453BC]`), a second feeler from the old position (raised 15) to the hit + normal (raised 15); if that's clear
the skater is put at hit + normal + (0, 0.1, 0) (`[0x00645D74]`) and it returns 1. Inside the trunk at y ~280 its
feeler (y ~280 - 297) meets nothing (the soil is at 238, the top at 323), so it returns 0 and changes nothing. The
"two-sided feelers" in thp8-vert.md were this: the ray from inside hit face 10 because face 10 faces inwards.

**Original vs ours.** The path's functions are all rewritten and self-tested against the original: RayHitsTriangle
0x0040B420 (selftests_anim10.cpp, bit for bit on random and exact rays incl. back faces), CollideWithLine 0x00413B50
(selftests_anim12.cpp), CheckFeeler 0x005003A0, CheckAirForwardCollision 0x00508780 (selftests_skater_collision.cpp),
DoAirPhysics 0x0050CA00. Read again here against the disassembly for this path: the culling compare and the landing
branch (0x0050D505 - 0x0050D625: the 0x004FFDB0 / velocity y > 10 / steep / ride timer > 500 test, hit point + normal)
are as ours. The original would put the skater in the same place with this data.

New self-test (src/hook/selftests_pole_tunnel.cpp, Early, `pole tunnel`): the trunk's 16 sides as converted and
turned round, the event's approach lines through them with the original 0x0040B420 and ours (identical results):
- wound inwards: the feet feeler outside -> inside: no hit; the forward feeler: no hit; inside -> past the far side:
  the far side hit, normal into the trunk, pushed to < 8 from the axis (inside);
- wound outwards: outside -> inside hits the near side, pushed out beyond the radius; inside -> out: no hit.

## The fix (converter, collision only): primitives wound outwards
- `cylinderTriangles`: the sides' corners in the outward order (the ends were right).
- `faceOutwards` (new, havok.ts): every primitive's triangles, once placed, turned to face away from the primitive's
  middle (convex shapes: boxes, cylinders, capsules, spheres) - so a mirroring transform can't turn one inside out
  either; `readCollision` counts them (`PrimitiveStats`).
- Counts over all THP8 zone archives (out\scripts\pole_prim_audit.ts, scratch): 3,982 primitives, 54,040 triangles;
  inside out before: 6,256 triangles = 391 cylinders / capsules / spheres (z_citypark 1,360, z_houses 1,136, z_center
  992, z_shops 720, z_inschool 560, z_cretepark 544, z_parking 384, z_ghetto 304, z_funpark 128, z_outschool 112,
  z_factory 16, z_cartoon 0). Boxes were right; no transform mirrors (faceOutwards turns 0 after the side fix).
- BEAVER VALLEY converted into out\pole_stage: object 895's sides now (-0.39, 0, -0.92) etc. (outwards); same file
  size (no face added or dropped).
- Mesh collision (THP8's MOPP meshes) is not touched: its windings are THP8's own; faces pointing down are 7 % of
  BEAVER VALLEY's (14,995 of 204,662), as THUG2's own levels (SE 8 %, BA 7 %, AU 5 %) - no sign of meshes inside out
  in bulk.
- Test: thp8_levels.test.ts "collision primitives: cylinders face outwards whatever their axis; faceOutwards turns an
  inside-out one" - fails with the old side order (checked).
- No engine guard is needed or proposed: with outward primitives the trunk is solid from outside like any THUG2 pole.
  (Thinness isn't the problem: the feelers cover the whole move from the last position, so a pole of any width is hit
  once its faces face out.)

## Game runs (C:\Games\THUG2-4, this branch's DLL, `[debug] collision_events=1`, self-tests off)
`run_game.ps1 -Level thp8_net_beavervalley -Commands "waitfor level thp8_net_beavervalley 200;teleport -2753.6 300
12397.1 33.7 board;trace 400;press up 200;wait 200"` - pushing off on the planter's soil 70 inches from the trunk,
straight at it (traces in out\scripts\pole_trace{A,B,C}.txt, scratch):
- A, the converted file as installed (sides inwards), our code: at frame 164 (-2719.1, 12449.8) at 265 in/s, at 174
  inside; then stuck for the remaining ~230 frames within 1.5 - 3.8 inches of the axis, a `wall-ahead-corner` event
  (paths side-wall, wall-ahead, wall-ahead-corner) - the user's event, reproduced on the ground.
- C, the same file with the path's code the original's (`[replacements] disable=coll_tests.cpp,coll_line.cpp,
  core_collision.cpp,core_physics.cpp,core_physics_feelers.cpp,core_physics_walls.cpp`: 32 replacements off): the
  identical trace, frame for frame (position and velocity), the same event. So the original does this too.
- B, the fixed file (out\pole_stage's col only), our code: turned off the trunk at frame ~160 and rode away, never
  nearer than 19.8 inches to the axis. (Later in that run an `uber-frig` at (-2393.2, 196.5, 12222.4) on the plaza
  past the planter - another place, not looked into.)
- THUG2-4's overlay keeps the fixed BEAVER VALLEY collision (the .col.xbx only).

## Checks
- thug2_check (out\build\verify, Debug) on THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe: 0 MISMATCHED, all 816
  early self-tests passed (with `pole tunnel`).
- npm run typecheck / lint / test (357) pass; thp8_levels.ts selfcheck: 1132 files, 0 failures.
- No game function rewritten (0x004FFDB0 read, not needed), so no register / call scans.

## Needs regenerating (the lead)
All THP8 levels (`node tools/thp8_levels/thp8_levels.ts convert`, then make_menu_level.ts): the 391 primitives are in
every zone but z_cartoon.
