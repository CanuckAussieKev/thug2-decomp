# Tick rates above 60 Hz

The simulation (physics, scripts, the logic tasks) steps in ticks of 1/60 s; render-only frames are drawn between
ticks (`engine/sys/frame_pacing.h`, `frame_blend.h`). This is what would have to change to run the simulation itself at
120 / 144 / 240 Hz - an audit (2026-11-14), nothing changed yet.

## How the clock works

- The main loop (`frame_loop::Hub::get_main_loop`, `engine/sys/mlp.cpp`) waits `QueryPerformanceFrequency / 60` per
  loop and runs exactly one tick each loop: one `note_refresh` and one `once_every_draw`, so the frame-length history is
  always one vblank and `frame_clock::get_frame_length()` is always 1 (late frames are drawn less, never caught up).
- The step is `frame_clock::get_slomo_frame_seconds()` = slow motion x `1.0F / 60` - a constant (76 call sites in 48
  files: the skater's `frame_.length`, rigid bodies, cars, cameras, animation, particles, goal timers, the script
  timer).
- Milliseconds come from `frame_clock::get_time()` = ticks x 1000 / `vblanks_per_second` (60): trick windows, screen
  element morphs, the game clock, the network (about 200 call sites).

So the engine is already fixed-step: the loop's `/ 60`, the step's `1.0F / 60` and `vblanks_per_second` define the
tick, and most code multiplies by the step and follows them.

## Scripts

`Wait` (`cfunc_wait`): `seconds` and bare milliseconds are time; `frame(s)` is 16.666666 ms (truncated) of time;
`gameframe(s)` / `game frame` counts Updates (ticks); `one_per_frame` lets one script a frame go on. Timed waits count
down by the step each Update, so with the step following the tick they are right at any rate.

`node tools/qbjs/frame_audit.ts --game <folder> [--overlays] [--out report.md] [--json out.json] [--all]` lists every
wait, frame loop, tick counter, per-tick increment and frame-named argument in the translated scripts with its
file:line and a class - **a** mechanical (ticks meant as time, N -> N/60 s), **b** judgement, **c** fine. The game's
Data (470 files): 2,859 sites - a 163, b 320, c 2,376. Most of a and b are `wait N gameframes` (N >= 2), `repeat N`
loops and counters around a one-tick wait, and fades / scrolls that add a fixed amount each tick. `frames:` keys in data
(camera shots, rotations) are a 1/60 s unit of time the C++ converts with a fixed 60 - they stay as they are.

## C++

| class | sites (approx.) | examples |
|---|---|---|
| scales already | ~76 step + ~200 milliseconds | skater integrator, rigid bodies, cars, animation, trick windows |
| mechanical: a hard 1/60 step or 60 ticks a second | ~20 | the loop and clock, the skater camera's distance lerps (`one_frame()`), the cutscene step, flexible particle emission, pedestrian turn frames |
| judgement | ~35 | tick counters (late wallplant window, throw wait, camera `instant_frames_`, settle / sleep after 3 ticks, tag countdown, THAW BMX counters), per-tick damping and decay without the step (rigid body `damp`, pedestrian bias decay, 3 degrees a frame), milliseconds truncated each tick (goal and objective timers, king of the hill, effects, time of day: 16 ms a tick at 60 Hz, 6 at 144 Hz) |
| determinism / compatibility | ~8 + demos | network sends every N ticks and timeouts in ticks; demos without a rate |
| unit conversions that must stay 60 | ~25 | the animations' 60 fps clock, script and data "frames" (1/60 s), cutscenes' 30 fps |

Smaller steps don't make collision tunnel more (the probes are the step's movement); the risk is thresholds tuned to a
1/60 step.

## Demos and the script check

A demo records input per tick and a check track; it has no rate, so it only replays at the rate it was made. The
script check compares QB and TypeScript on the same interpreter and ends waits at the next frame: a tick rate change
doesn't affect what it compares.

## Recommended: a fixed, configurable tick rate

A fixed tick (60 by default; 120, 144, 240) with render-only frames still blended above it - not a variable step: the
engine is fixed-step already, demos and network rely on it, and truncation and tick counters behave the same every
tick at a fixed rate.

1. **Plumbing, 60 Hz identical**: one tick rate read by the loop, the step (`1.0F / 60` kept exactly at 60) and
   `vblanks_per_second`; frame pacing and interpolation take it too. The network's and PAL's `game_config::frame_rate`
   stays separate. Proof: recorded demos replay with no drift at 60.
2. **Mechanical sites**: hard 1/60 steps become the step; unit conversions stay 60.
3. **Judgement sites**: tick counters become time, per-tick rates scale with the step (`pow(x, step * 60)`), truncated
   milliseconds keep their fraction (only away from 60); network sends and timeouts in milliseconds. Proof: a 60 Hz
   demo played at 120 Hz (each input held two ticks) stays within a tolerance of its check track.
4. **Scripts**: `gameframe` waits become N/60 s away from 60 (one place in `cfunc_wait`); then review the polls that
   must see every tick with frame_audit.ts. Proof: the script check unchanged at 60.
5. **Demos and online**: the demo format records the rate; online stays at 60 until the network changes are tested
   against 60 Hz players.
