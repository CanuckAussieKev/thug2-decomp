# Foundry's bucket spinning away (THPS3 rotation deceleration) - 2026-10-21

Branch `worktree-agent-a4b60cc81b88f8367`. The user (main install, after PC-THPS3-15): over the molten metal the bucket
first turns the wrong way, then keeps spinning ever faster instead of pouring and going back.

## Cause

Not PC-THPS3-15 (LevelObject vs Vehicle: both have the motion component and run the same THPS3 script,
CJR_Foun_Bucket_MoveOut; the statements are unchanged). The script tips the bucket with

    Obj_RotX Angle=90 Speed=30 Acceleration=75 Deceleration=20 FLAG_MAX_COORDS   (back: Angle=-90 ..., Obj_WaitRotate)

and THPS3 and THUG2 read `deceleration` differently. Everything else agrees: degrees and degrees per second (/ s²),
the angle relative to where the object is, FLAG_MAX_COORDS negating X and swapping Y / Z, all speeds given the angle's
sign, the turn ending at the angle (THPS3.exe 0x0048C4F0 per-axis set-up, 0x0048C040 signs, 0x0048ED80 update;
THUG2PM.exe 0x00424B90 Obj_RotX, 0x00424D50 RotY, 0x00424F50 RotZ, 0x00423F10 signs, 0x00425A80 update; the member
table entries at 0x00681FC0.. set up by 0x00640E99..).

- THPS3: deceleration is a drag, read on its own: each frame speed += acceleration (clamped at `speed`), then speed -=
  deceleration; the turn ends at the angle or when the speed reaches 0.
- THUG2: deceleration means "slow the current spin down to `speed`": given it, acceleration isn't read at all and the
  current speed isn't set from `speed` (the angle reset it to 0); each frame speed -= deceleration until it reaches
  `speed`. After the signs (angle -90 internally: X negated by the flag) the speed is 0, `speed` -30, deceleration -20:
  speed - (-20 dt) grows positive - the wrong way, faster and faster, never reaching the angle and never reaching
  `speed`. The way back (`Angle=-90`) resets the speed and runs away the other way; its Obj_WaitRotate never returns,
  so the bucket never goes back.
- THUG2's own scripts never pass deceleration with an angle (AU's `Decceleration=` is a typo, ignored); THPS4's pass it
  only without one (`speed=0 deceleration=10`, spinning down) - THUG2's meaning. So only THPS3 needs it rewritten.

Logged in the game (C:\Games\THUG2-2, a temporary per-10-frames log of Obj_Bucket's matrix, not committed), main's
foun.qb: out at x -4061, then pitch -6, -32, -78, -144, +130... (60-frame samples), never back.

## Fix (tools/thps3_levels/thps3_rotation.ts, a Translator `rewrites` entry for Obj_RotX / Y / Z, THPS3 only)

THPS3's statement without deceleration, doing what THPS3 does:
- acceleration a > deceleration d: acceleration = a - d (THPS3's drag then holds it d x frame time under `speed`, a
  third of a degree per second: ignored). Bucket: `Angle=90 Speed=30 Acceleration=55`.
- a <= d: THPS3 stops on the first frame -> `angle=0` (THUG2: stop turning about that axis). None in the data.
- no acceleration: THPS3 starts at `speed` and slows at d to a stop - the same angle (or the shorter speed²/2d where it
  stops first) in the same time at its average speed. Tokyo's smasher: `angle=-20 speed=80 deceleration=200` ->
  `angle=-16 speed=40` (stops after 0.4 s, 16 degrees on, as THPS3).
- no deceleration, angle 0, or a value that isn't a plain number: kept as it is.

Tests: tools/thps3_levels/thps3_rotation.test.ts (3). typecheck, lint, npm test (156 pass), thps3_levels selfcheck
(323 files, 0 failures).

## Statements affected (all THPS3 data; every one brought along by the converter)

| Level | Script | Was | Now |
|---|---|---|---|
| Foundry | CJR_Foun_Bucket_MoveOut (tip / back) | Angle=±90 Speed=30 Acceleration=75 Deceleration=20 | Acceleration=55 |
| Foundry | CJR_Foun_Intro_Bucket | Angle=90 Speed=30 Acceleration=75 Deceleration=20 | Acceleration=55 |
| Foundry | CJR_Foun_Soaker_Chain_Move (3) | -20/45/65/40, 30/30/45/5, -10/60/60/10 (angle/speed/accel/decel) | accel 25, 40, 50 |
| Tokyo | JS_Mid_Smasher_Controller | angle=-20 speed=80 deceleration=200 | angle=-16 speed=40 |

## Verified in the game (C:\Games\THUG2-2 under its lock; this branch's convert + make_menu_level; a debug build of
the DLL with the temporary bucket log; -Level foun, 200 s)

Four full cycles: out from x -5351 to -4061 (frames 70-680), tips 0 -> 90 degrees in ~3 s (the up vector leaning to
-z, toward the molten tub), holds 90 for ~8.5 s (the pour), back 90 -> 0 in ~3 s, returns to -5351, repeats (f 2250,
4440, 6620, 8800). No exception / run-time check / CRT report in the log. The same build with main's foun.qb showed
the bug (above). The look (pour direction, timing) is for the user to confirm.

## Reconvert

THPS3's **Foun** and **Tok** change (`node tools/thps3_levels/thps3_levels.ts convert` - all levels, so levels.json stays
whole - then make_menu_level for the main install).
