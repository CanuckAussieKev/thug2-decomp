# How THUGPRO's Foundry bucket avoids the rotation bug - 2026-10-21

Research only (follow-up to foun-bucket-spin.md). Sources, read only: THUGPRO's installed
`%LOCALAPPDATA%\THUG Pro\data\pre\FOUN_scripts.prx` / `TOK_scripts.prx` (extracted with tools/level_port/prx.ts,
decompiled with ns.exe -d), Morten1337's thugpro repo (`source/levels/foun/foun_Scripts.q`, `tok/tok_Scripts.q`;
commit 361b2c1), and our converted foun.qb / tok.qb (C:\Games\THUG2-2, this fix's conversion).

## Answer: (a) - THUGPRO hand-edited the script; the engine is stock

THUGPRO's `CJR_Foun_Bucket_MoveOut` (installed data and repo source agree):

    Obj_RotX Angle=-90 speed=30          (tip; was Angle=90 Speed=30 Acceleration=75 Deceleration=20 FLAG_MAX_COORDS)
    Obj_WaitRotate
    Obj_StopRotating
    ... pour (8 s) ...
    Obj_RotX Angle=90 speed=30           (back; was Angle=-90 ... FLAG_MAX_COORDS)
    Obj_WaitRotate
    Obj_StopRotating

- No acceleration, no deceleration, and FLAG_MAX_COORDS dropped with the angle negated - the same direction as THPS3
  (the flag negates X: THPS3's 90 with the flag = -90 without it). A constant 30 deg/s.
- (b) ruled out: the thugpro repo has scripts and assets only (no DLL source), and the edit itself shows the engine
  wasn't changed - THUGPRO's `CJR_Foun_Intro_Bucket` still has the THPS3 `Angle=90 speed=30 Acceleration=75
  Deceleration=20 FLAG_MAX_COORDS` untouched. That object (Obj_Intro_Bucket, AbsentInNetGames, not created at start)
  is never created in THUGPRO's scripts, so its bug never shows there.
- (c) ruled out: the bucket's motion is still Obj_MoveToNode + Obj_RotX (Obj_Bucket.qb holds only two collision/kill
  nodes, no animation).
- Other THUGPRO pour changes, not motion: Obj_WaitRotate + Obj_StopRotating before the pour starts (THPS3: `Wait 3.25
  seconds`), its own sounds (TramEngineLoop2, foun_pour stream, SewagePipeLoop), a kill trigger and a gap during the
  pour, getting a hanging / grinding skater off before tipping back.

The other statements our rewrite touches:

| Statement | THUGPRO | Ours (after the rewrite) |
|---|---|---|
| Foundry intro bucket | unchanged (latent bug, object never created) | Acceleration=55 |
| Foundry soaker chain (3 RotY) | CJR_Foun_Soaker_* scripts emptied (sequence removed) | accel 25 / 40 / 50 |
| Tokyo smasher, 1st turn `angle=65 Speed=100 Acceleration=100` | `angle=65 speed=80` (constant) | as THPS3 |
| Tokyo smasher, 2nd turn `angle=-20 speed=80 deceleration=200` | deleted | `angle=-16 speed=40` |

(THUGPRO also replaced Tokyo's smasher cutscene with its own animated one, play_thugpro_cutscene_anim.)

## Motion compared

Bucket tip (and back), 90 degrees:
- THUGPRO: constant 30 deg/s from the first frame -> 3.00 s, linear (a hard start and stop); the pour begins as the turn
  ends (~3.0 s).
- Ours: from 0 at 55 deg/s^2 to 30 deg/s (0.55 s, 8.2 degrees), then 81.8 degrees at 30 -> 3.27 s, a soft start, hard
  stop; the pour begins at THPS3's 3.25 s. This is THPS3's own motion (THPS3: +75 -20 drag, the same 3.27 s; its
  script's 3.25 s wait was timed to it).
- Same direction, same end angles, same top speed: the difference is 0.27 s and the eased start. Hard to tell apart.

Tokyo smasher: THUGPRO's swing is 65 degrees in 0.81 s (constant 80), ours / THPS3's 1.15 s (accelerating to 100), plus
our 0.4 s, 16-degree recoil THUGPRO doesn't have.

## Recommendation

Keep ours: it reproduces THPS3's motion and timing, it's within a quarter second of THUGPRO's, and the bucket is
local (not synced online), so nothing needs to agree. If the user prefers THUGPRO's feel: a linear turn = drop
acceleration as well (`Angle=90 Speed=30 FLAG_MAX_COORDS`, 3.0 s; pour then starts 0.25 s after the turn ends) - one
line in thps3_rotation.ts (a > d: drop both instead of acceleration = a - d), or a Foundry-only TP entry; Tokyo's would
be `angle=65 speed=80` and deleting the recoil. Not recommended: THPS3's eased start is the original.
