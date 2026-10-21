# Ported levels: shimmer, second pass - Foundry's ramps (2026-10-21)

User report (after the first fix, docs/pending/ported-shimmer.md): Foundry's ramps still shimmer - screenshot
thug2_2026-10-21_18-47-29 (skater (-4087.3, -390.0, 2386.6) heading -177.3, camera (-4080.9, -293.7, 2523.3) looking
along (-0.044, -0.311, -0.949): the quarter pipe with the two vents over the grated floor). "Go to the position here
and screenshot every frame and you'll see what I mean. Maybe add an engine function that lets you teleport to a given
position, while standing..." Branch `worktree-agent-ae7953c92860793b4`; workspace C:\Games\THUG2-4.

## The tool (C++, NOT run by the user yet)

- `src/game/front/test_view.{h,cpp}` (new): `Teleport` - the local skater put at a point standing: the skater's own
  custom-restart code (CSkater::SkipToCustomRestart, walking: SkipToRestart walking -> the walk state, momentum gone,
  the camera reset), the custom restart's fields put back afterwards; snapped to the ground below with the game's
  collision ray (Nx::CFeeler, 12 above to 4000 below). Heading as the screenshot sidecar (0 = +z, 90 = +x; none: as
  it faced). `LockCamera` / `UnlockCamera` / `PlaceCamera`: viewport 0's camera drawn at an exact pose (position +
  view direction, level horizon), optionally drifting by a vector every game frame (`Mlp::FramesRun` since the lock -
  exact, frame-rate independent). Called from game/gfx/render.cpp after the level editor's PlaceCamera (wins over it).
  Script command `TeleportSkaterStanding pos=(x, y, z) [heading=<degrees>]` (registered in sys/main.cpp after
  CREATE 100% CAS's; selftests_misc's main-function test stubs the call as it does the other two).
- Automation commands (game/front/automation.*): `teleport <x> <y> <z> [heading]` (in a level with the skater only),
  `camera <x> <y> <z> <dx> <dy> <dz> [<vx> <vy> <vz>]` / `camera off`, `capture <frames 1-600> [folder]` (finishes
  when every frame is written; a folder without `..`). Same safety as the rest: refused while online / in a network
  game.
- Frame series (game/sys/screenshot.*: `StartSeries`, `SeriesRunning`, `SeriesResult`): every rendered frame's back
  buffer (the screenshot key's capture) PNG-encoded on worker threads (at most 6 waiting; the frame waits for one),
  `frame_0001.png ...` in `<screenshot folder>\<folder>` (default `series_<time>`), one `series.txt` (the sidecar's
  state at the first frame + a `[frames]` table: frame, game frame, vblanks, clock ms). Works with the screenshot key
  off. **sys/mlp.cpp:** while a series runs the main loop restarts its frame clock after the present, so the capture's
  time isn't counted and no frame is skipped to catch up: the series is consecutive game frames, one vblank apart
  (series.txt shows vblanks +1 every frame). Without that, the first capture had 1-4 vblanks between frames (render
  frames skipped while it encoded) and animated layers jumped erratically - a measuring artefact, not the bug.
- `tools/game/frame_diff.ts` (new, TypeScript; tests in frame_diff.test.ts): per-pixel frame-to-frame changes of a
  series (summed |dRGB| over a threshold), per region: pixels that flicker (>= 2% of the steps), the change rate, the
  mean |d|; a heat map. Its own PNG reader / writer (8-bit RGB / RGBA, all five row filters).
- Self-tests: TestAutomationParse (teleport / camera / capture lines, errors, numbers), TestTestViewRows (camera rows
  from a direction - along +z, +x, down 45 degrees; zero and straight-down refused; standing rows for headings 0 / 90 /
  -180), TestScreenshotNames (series folder / frame names). thug2_check on THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe:
  0 MISMATCHED, 513 early self-tests pass; check_code_writes: no overlaps.

## Capture method

Workspace C:\Games\THUG2-4 under the lock, 1280 x 720 (same aspect as the user's 2560 x 1440, same graphics settings
otherwise), Foundry reconverted there from this branch (it held October 19th's thps3_foun before; the converted scene
was byte-identical to the main install's). `run_game.ps1 -Level foun -Commands "waitfor level foun 150;teleport -4087.3
-390.0 2386.6 -177.3;camera -4080.9 -293.7 2523.3 -0.044 -0.311 -0.949;wait 60;capture 120 foun_lock;camera ...
0.05 0 0;capture 120 foun_side;camera ... 0 0 -0.1;capture 120 foun_fwd"`: the user's exact view (frame 1 matches
the screenshot), 120 consecutive frames still, then drifting 0.05 units a frame sideways, then 0.1 a frame forwards.
Regions (pixels, 1280 x 720): ramp_left 0,130 570x180; ramp_right 730,130 550x180; vents 440,180 115x90 and 928,180
122x90; floor 0,330 570x200.

## What flickers, and why

With the camera still, the only per-frame change on the ramp was a horizontal band of light streaks crawling across
the whole quarter pipe (36-37% of the ramp's pixels changing by more than 6 every few frames; with frames skipped -
the user's case whenever a frame is late - it jumped). The vents and the grated floor were stable apart from animated
layers meant to move (the conveyor under the last grate row, the furnace in the background).

The band is THPS3's quarter-pipe **reflection** overlay: materials 0xBC86C29C / 0x22E2573F, texture
Foun_CG_QP_Reflect_01 (64 x 64), a transparent layer over the ramp (blend 1 / 2, fixed alpha 20 / 35). In THPS3's
.bsp its uv flags are **0x81 = uv wibble + 0x80 environment-mapped** - every `*_reflect` material and only those
(Foundry 7: QP reflect x2, lamp, cannister x2, smelter booth window, window pane; also AP 2, LA 3, Ros 1, Shp 6, SI 3,
Sub 3, Tut 2, the Sk3Ed sets). The converter ignored 0x80 and kept the "wibble" ("1 0 1 1 0 0 1 1": u velocity 1) as
a THUG2 uv wibble: THUG2's 0x004D1DC0 computes u = u_vel x t + u_amp x sin(u_freq x t + u_phase) (t = Tmr::GetTime
ms x 0.001), so the reflection streaks scrolled a whole texture a second over the ramps' own uvs - all the time,
standing or not. THUG2 draws an env pass (flag 0x08) with camera-space reflection-vector texture coordinates
(0x004D1F00: D3DTSS_TEXCOORDINDEX | 0x30000, the env tiling as the texture matrix) and passes over the wibble then;
THUG2's own levels' env passes have no wibble and tile 3 x 3.

Checked and not the cause here: z-fighting (no flicker on the ramp or the vents with the sideways / forward drift once
the reflection stopped scrolling - the first pass's z biases hold), mip chains (full since the first pass), the vents'
alpha (stable).

## Fix (converter)

tools/thps3_levels/thps3_levels.ts toThug2Material: uv flag 0x80 -> PASS_ENVIRONMENT (tiling 3 x 3 as before - THUG2's
common value), no uv wibble; plain wibbles (0x01 without 0x80: water, conveyor, sludge...) as before. Test in
thps3_levels.test.ts (an env pass and a plain scroll). Docs: tools/thps3_levels/README.md,
docs/level-port-compromises.md (the "env-mapped passes not converted" lines).

## Numbers (frame_diff.ts, threshold 6; "flickering" = pixels changing in >= 2% of 119 steps)

| series            | ramp_left           | ramp_right          | vent_left          | vent_right         |
|-------------------|---------------------|---------------------|--------------------|--------------------|
| still, before     | 36.06%, mean d 1.81 | 37.40%, mean d 1.88 | 6.96%, 0.33        | 8.13%, 0.36        |
| still, after      | 0.00%, mean d 0.40  | 0.00%, mean d 0.65  | 0.00%, 0.16        | 0.00%, 0.11        |
| sideways, before  | 40.27%, 2.03        | 42.14%, 2.12        | 13.18%, 0.95       | 16.70%, 1.04       |
| sideways, after   | 1.11%, 0.37         | 1.80%, 0.41         | 8.83%, 0.64 (edges moving) | 9.01%, 0.64 |
| forwards, before  | 40.77%, 2.06        | 42.52%, 2.13        | 9.61%, 0.80        | 14.50%, 1.03       |
| forwards, after   | 0.63%, 0.42         | 1.43%, 0.41         | 4.53%, 0.53        | 10.07%, 0.74       |

(At threshold 24 the still band showed 0% only because consecutive frames move it little; with skipped frames - the
first, unpaced capture - 33-34% of the ramp flickered at 24.) The reflection is now a soft view-dependent highlight
on the lower ramp, still while standing (frame 1 before / after look alike otherwise). Series kept in
C:\Games\THUG2-4\Screenshots\foun_{lock,side,fwd}{_before,} (frames + series.txt).

## For the lead / the user

- Reconvert every THPS3 level (`node tools/thps3_levels/thps3_levels.ts convert --thug2 <game>`) and run
  make_menu_level.ts. THPS4 / THUG1 / THAW unchanged (their env passes are already THUG2-style flags).
- THUG2-4's THPS3 staging and overlay are now this branch's (the THPS4 / THUG1 / THAW staging there is still the
  older one, as it was).
- The user should judge: Foundry's quarter pipes from the screenshot spot, standing and skating - the reflection
  should now sit still and slide only with the camera; also THPS3 Airport, LA, Suburbia, Ship, Skater Island... any `*_reflect` surface
  (windows, lamps, cannisters). THPS3 sectors have no vertex normals (usableNormals), so D3D's reflection vector uses
  a zero normal: the env map follows the view direction - smooth, but not a true reflection off the curve; if it
  looks wrong, normals for the env-mapped meshes only would be next.
- Not tested in the game: the script command TeleportSkaterStanding (the automation `run` can't pass a vector).
