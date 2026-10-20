# Collision face flag 0x80 (settled 2026-10-21)

**THUG2's 0x80 = the camera passes through the face.** THPS3's and THPS4's 0x80 meant the reverse (the camera
collides). THUG1 and THAW use THUG2's meaning, but THUG1 shipped four old levels with the THPS3 / THPS4 one.

## Code (THUG2PM.exe, Ghidra snapshot)
- SetIgnore 0x004170F0 (`this` in ECX, ret 8): writes its first argument to feeler +0x4C and its second to +0x4E. The
  face test 0x004131F0 (ESI = the feeler's hit record at +0x24, so +0x28 / +0x2A = feeler +0x4C / +0x4E) skips a face
  when `flags & [+0x28]` (the ignore mask: the first argument) or `~flags & [+0x2A]` (the require mask: the second).
- Every SetIgnore call (19, by cross-reference): ignore 0x10 (most), 0x810 (0x004F8698), **0x90 (0x004E4425 and
  0x004FB380)**, 0x3000 (graffiti 0x0051DF71 / 0x0051E74B / 0x0051E564), require 0x20 (3 callers) / 0x50 (3), and
  0x005003CF passing its own arguments. Only the two 0x90 calls name 0x80.
- 0x004E4425 is in 0x004E4360 (`Obj::AvoidCameraCollisions`, src/game/obj/camera_util.cpp): the line from the
  camera's target to 2 beyond the camera; on a hit the camera is pulled in to the hit. Its callers: 0x004FA860 (5.4 KB,
  a vtable method, not rewritten yet; it makes its own feeler at 0x004FB380 with SetIgnore(0x90, 0) too), 0x00524940,
  0x005251D0. So both camera feelers pass through faces flagged 0x80 (or 0x10).
- Nothing else in the feeler masks reads 0x80.

## Data (face-flag census of the shipped levels)
| Game | faces with 0x80 | where |
|---|---|---|
| THUG2 (Data\pre\*col.prx) | 21,034 of 460,190 (4.6%) | lamps, street lights, poles, trees, signs, fences, glass, railings, invisible walls (TRG_Invis_Collision), kill planes, trigger walls; 798 objects all 0x80 (median 12 faces), big buildings rarely |
| THAW | 21,484 of 433,782 (5%) | as THUG2 |
| THUG1 | 44,285 of 476,641 | its own levels 0.1-7% (as THUG2); but DJ 6,596 of 7,120, PH 8,596 of 10,120, SC 10,692 of 13,114, skateshop 6,141 of 19,724 (THPS4's skate shop, the same numbers) |
| THPS4 | 324,196 of 488,023 (66%) | buildings, walls, floors, stairs, ramps, rivers; not on trees, signs, railings, light poles, trash cans, triggers, occlusion planes, non-collidable faces |
| THPS3 | 225,343 of 577,634 (39%) | buildings, floors, walls, ramps; Foundry's collidable faces without it: lamps, furnaces, pillars, booths, air ducts, invisible rails |

A flag on two thirds of THPS4's world (its buildings and floors) can't be "the camera passes through", and THUG2's
code ignores it in the camera's feeler: THPS3 / THPS4's 0x80 is "the camera collides", THUG2's its reverse. THUG2
re-flagged its own copies of the old levels (DJ 0, PH 316, SC 328 faces with 0x80). THUGPRO (reference only) inverts
THPS4's too (its Hof: 18,515 = 59,859 - 41,344).

## What was wrong, what changed
- The editor agent's reading (docs/pending/editor-view.md, src/game/front/level_editor_view.h / level_editor_ui.cpp)
  was right; nothing changed there but a pointer in its pending record.
- THPS4 converter (tools/thps4_levels/thps4.ts): its code already inverted 0x80 - right - but both comments had the
  meanings swapped (THPS4 "camera goes through", THUG2 "camera collides"). Comments fixed; output unchanged.
- **THPS3 converter** (tools/thps3_levels/thps3_levels.ts `faceFlags`): kept 0x80 as THPS3's ("as THUG2's") - so in
  the ports the camera passed through 39% of the world (floors, walls, buildings) and stopped at lamps. Now inverted
  as THPS4's: `(flags ^ 0x80) & 0x1FFF`.
- **THUG1 converter** (tools/thug1_levels/thug1_levels.ts): `faceFlagsOf(code)` inverts 0x80 in `OLD_CAMERA_FLAG`
  (dj, ph, sc, skateshop); the other levels keep THUG1's flags as before.
- `FACE_CAMERA_COLLIDABLE` in tools/level_port/thug2_col.ts (unused) renamed `FACE_CAMERA_PASSES`; comments / docs
  corrected: tools/thps3_levels/README.md, tools/thug1_levels/README.md, tools/thaw_levels/thaw_levels.ts (the
  breakables' comment), tools/compat/thugpro/README.md (PC-THAW-4's text), docs/level-port-compromises.md (a row in
  section 2), docs/pending/thps3.md, docs/pending/thug1.md.
- Checks: `npm run typecheck`, `lint`, `test` (100 pass; no test pins face flags; the converters' parity cases are
  off since their Python twins were deleted). Converted into this worktree's out\ (read THUG2 data from
  C:\Games\THUG2-4, wrote nothing into any game folder): THPS3 Foun (now 30,981 of 41,055 faces 0x80 - every
  non-collidable face plus 7,031 collidable props), THUG1 SC (2,158 of 12,850 collidable) and NJ (unchanged 1,764),
  THPS4 Alc (8,101 of 44,549 collidable, as before) - all ran clean.
- The census scripts were ad hoc (this worktree's out\ff_*.ts, not committed).

## Needs regenerating (the lead)
- **THPS3 levels** (all) and **THUG1's DJ / PH / SC / skateshop** - then installed with make_menu_level.ts.
- THPS4 and THAW levels and THUG1's others: unchanged, no need.

## In-game checklist (NOT run in the game yet)
1. A THPS3 level (Foundry): skate next to a wall or under a roof and turn - the camera stops at walls (doesn't go
   behind them); it passes through lamps and pillars.
2. THUG1 Old School (SC) / Old Downhill Jam: the same - the camera stays out of the canyon walls / school walls.
3. A THPS4 level: as before (the camera already behaved).
