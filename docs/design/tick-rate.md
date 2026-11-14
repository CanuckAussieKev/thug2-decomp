# Tick rates above 60 Hz

The simulation (physics, scripts, the logic tasks) steps in fixed ticks; render-only frames are drawn between ticks
(`engine/sys/frame_pacing.h`, `frame_blend.h`). The original's tick is 1/60 s. `[Graphics] tick_rate=` 120 / 144 / 240
(60 - 240, `engine/sys/tick_rate.h`) runs the simulation itself in shorter ticks.

The goal: physics independent of the rate, timers right at any rate, a run at 120 / 144 / 240 Hz matching the 60 Hz run
to within a tiny tolerance ("virtually exact"), online play with 60 Hz peers, demos that know their rate. The 60 Hz path
stays byte-identical to the original's.

Status (2026-11-14): phases 1 and 2 below are done - the plumbing and the mechanical sites, the demo format's rate, and
the two yardsticks that measure how far another rate is from 60. Phases 3 - 5 remain; the yardsticks' numbers below
are where they start from.

## How the clock works

- The main loop (`frame_loop::Hub::get_main_loop`, `engine/sys/mlp.cpp`) waits `QueryPerformanceFrequency / rate` per
  loop and runs exactly one tick each loop: one `note_refresh` and one `once_every_draw`, so the frame-length history is
  always one vblank and `frame_clock::get_frame_length()` is always 1 (late frames are drawn less, never caught up).
- The step is `frame_clock::get_slomo_frame_seconds()` = slow motion x `tick_rate::step_seconds()` (the float
  `1.0F / rate`: at 60 the original's `1.0F / 60`, bits `0x3C888889`). 76 call sites in 48 files read it: the skater's
  `frame_.length`, rigid bodies, cars, cameras, animation, particles, goal timers, the script timer.
- Milliseconds come from `frame_clock::get_time()` = ticks x 1000 / `vblanks_per_second`, which is the tick rate
  (`tick_rate::clock_rate`: at 60 the original's `game_config::frame_rate`, 60 - or 50 with the PAL command line, as
  ever). So `get_time()` stays real milliseconds at any rate: trick windows, screen element morphs, the game clock, the
  network (about 200 call sites).

## What the tick rate changes (phases 1 and 2)

| place | at 60 (unchanged) | at another rate |
|---|---|---|
| `tick_rate.h` | `[Graphics] tick_rate` read once at start-up (`app/startup.cpp`, before the clock starts); logged when not 60 | |
| the loop (`mlp.cpp`) | QPF / 60; far behind after 20 ticks; at most 4 late frames undrawn in a row; screen saver every 200 | QPF / rate; the counts scaled to the same time (`tick_rate::ticks_for`) |
| the step (`stopwatch.cpp`) | `slomo * (1.0F / 60)` | `slomo * (1.0F / rate)` |
| the clock's rate | `game_config::frame_rate` | the tick rate |
| render-only frames (`frame_pacing.cpp`, `frame_interp.cpp`) | `max_fps / 60` frames a tick; a tick of 1/60 s | `max_fps / rate`; a tick of 1/rate |
| skater camera lerps (`skater_camera_modes.cpp one_frame`) | `0x3C888889` | the step |
| cutscene fixed step, cutscene and music clocks (`cutscene_update.cpp`, `cutscene_play.cpp`) | 1/60; `frame_rate` | the step; the tick rate |
| flexible particle emission (`pc_flexible_particles.cpp`) | `emit_rate * (1.0F / 60)` a tick | `emit_rate * step` |
| pedestrian turn frames (`ped_logic_move.cpp`) | `reach / speed * 60` ticks | `reach / speed * rate` ticks |
| the editor's arc preview and camera flight's first frame | 1/60 | the step |
| demos (`devtools/demo.cpp`, `formats/demo_format.h`) | version 1 files, no RATE chunk (the same bytes as before) | version 2 with RATE; timelines, REC / REPLAY clocks and the viewer count the demo's rate |

The proof that 60 is unchanged:

- `thug2_tick_rate_check` (src/tests): at 60 the step's bits are `0x3C888889` and equal each constant it replaced
  (stopwatch's `1.0F / 60`, the camera's `0x0064BA64`, the cutscene's `0x006463B8`); `get_slomo_frame_seconds` bit for
  bit over 100,000 slow motions; the loop's tick QPF / 60 for 10,000 frequencies; `get_time` over 200,000 vblank counts;
  counts of ticks and per-tick factors unchanged; `frames_this_tick` (13 caps x 2,000 ticks) and `shown_time_ms` equal
  to the old 60-constant formulas. 39 checks.
- The other checks (maths, parse, format, frame pacing, viewer, printf, options, tess, `ff_shader_check --no-gpu`)
  print the same before and after.
- A recorded demo (11.6 s, pushes, ollies, grinds) played at 60 by the build before and after the change: every one of
  its 694 check points has the same skater (position, velocity, matrix rows, state) to the last digit, and the same
  camera on every frame both runs drew (the frames left undrawn to keep real time differ from run to run).

Unit conversions that stay 60 whatever the rate (data and code that count time in 1/60 s): script and data "frames"
(`kScriptFrameMilliseconds`, `kFramesToMs`, `kFrameMs`, `kSecondsPerFrame`), the animations' 60 frames a second
(`anim_*`, `boned_anim_*`, `viewer_object.cpp`'s frame step), particle velocities in 1/60 s units
(`flexible_particle_manager.cpp`'s `frame_time_scale`), cutscenes' 30 fps, the level editor's shot timings, the
network's transmit periods (`game_config::frame_rate`).

## Scripts

`Wait` (`cfunc_wait`): `seconds` and bare milliseconds are time; `frame(s)` is 16.666666 ms (truncated) of time;
`gameframe(s)` / `game frame` counts Updates (ticks); `one_per_frame` lets one script a frame go on. Timed waits count
down by the step each Update, so they are right at any rate now; `gameframe` waits are still ticks (phase 4).

`node tools/qbjs/frame_audit.ts --game <folder> [--overlays] [--out report.md] [--json out.json] [--all]` lists every
wait, frame loop, tick counter, per-tick increment and frame-named argument in the translated scripts with its
file:line and a class - **a** mechanical (ticks meant as time, N -> N/60 s), **b** judgement, **c** fine. The game's
Data (470 files): 2,859 sites - a 163, b 320, c 2,376. Most of a and b are `wait N gameframes` (N >= 2), `repeat N`
loops and counters around a one-tick wait, and fades / scrolls that add a fixed amount each tick. `frames:` keys in data
(camera shots, rotations) are a 1/60 s unit of time the C++ converts with a fixed 60 - they stay as they are.

## Measuring: the two yardsticks

**The kernels** - `thug2_tick_rate_check [--harness]`: the game's integration steps written out as the game computes
them (the same float rounding), stepped at 60 and at 120 / 144 / 240 over the same time; the difference from the 60 Hz
run (illustrative values in the game's units, inches and seconds):

| kernel | metric | 60 Hz | 120 - 60 | 144 - 60 | 240 - 60 |
|---|---|---:|---:|---:|---:|
| ollie, landing seen at the tick's end (as now) | apex | 49.500 in | +0.000008 | -0.001 | -0.000015 |
| | airtime | 0.600 s | +8.3 ms | +4.2 ms | 0 |
| | landing vy | -330.0 | -9.17 | -4.58 | 0 |
| ollie, landing at its own time inside the tick | airtime | 0.600 s | 0 | 0 | 0 |
| | landing vy | -330.0 | +0.00003 | +0.00001 | +0.00003 |
| roll to a stop (rolling friction + drag, as now) | distance | 660.49 in | +7.13 | +8.32 | +10.69 |
| roll, integrated in closed form | distance | 674.75 in | +0.00007 | -0.0002 | -0.0003 |
| roll, the 60 Hz scheme's modified equation (below) | distance | 660.49 in | +0.099 | +0.100 | +0.101 |
| | speed at 1 s | 284.89 | +0.068 | +0.068 | +0.068 |
| camera distance lerp over 0.3 s (as now) | end value | 160.00 | +0.0001 | +1.11 | +0.83 |
| per-tick decay 0.95 (as now) | left after 1 s | 0.0461 | -0.0439 | -0.0455 | -0.0461 |
| per-tick decay, `per_tick_factor` | left after 1 s | 0.0461 | 0 | 0 | 0 |
| goal timer, trunc(ms) a tick (as now) | ms in 60 s | 57,600 | 0 | -5,760 | 0 |
| goal timer, on the 60 Hz grid | ms in 60 s | 57,600 | 0 | 0 | 0 |
| a 10-tick window (as now) | length | 0.167 s | -0.083 | -0.097 | -0.125 |
| a 10-tick window, `ticks_for` | length | 0.167 s | 0 | 0 | 0 |

**A recorded run** - `node tools/game/tick_compare.ts --game <workspace> --demo <name> --rates 60,120,144,240`: the
demo played at each rate (a demo recorded at 60 played at another rate holds each recorded frame over the ticks of its
time, anchored to the time from the level's load; check points compared where a tick ends on a recorded frame - every
frame at 120 and 240, every 5th at 144), each run's track against the 60 Hz run's: position error by second, velocity,
camera, state, each jump's take-off, landing, airtime and apex. A workspace only, under its lock. After phases 1 and 2,
on an 11.6 s demo (pushes, five ollies, a grind) in a ported level:

| rate | max position error | mean | state differs on | first jump: apex / landing | error in the first second |
|---|---:|---:|---:|---|---:|
| 120 | 1490 in | 492 in | 68 of 693 frames | -0.035 in / one frame early | 16 in |
| 144 | 1517 in | 514 in | 15 of 139 | -0.93 in / one frame late | 2.4 in |
| 240 | 245 in | 94 in | 75 of 693 | +0.08 in / one frame early | 108 in |

Clean airborne arcs already match to a hundredth of an inch (the integrator is exact for gravity); the runs part
within the first second and then go their own ways (a different push, a missed grind - the maxima are another route,
not a drift). What they show, for phase 3: the push's timing (a few frames later at 120 - tick-counted script waits and
counters), the landing seen at the tick's end (one 60 Hz frame either way), and at 240 the skater lifted off the ground
in the level's first frames (vy +245 at the level up - a per-tick ground correction tuned to a 1/60 step).

## How "virtually exact" will be reached

The reference is the 60 Hz run itself - 60 stays the original, so "the same at any rate" means the same as the 60 Hz
*scheme*, with its own discretisation, not as the exact physics. The roll shows why: integrated exactly it goes 674.7
in, the 60 Hz scheme 660.5; an "exact" integrator would be 14 in away from 60 Hz at every rate. So:

1. **Steps that compose to the 60 Hz step.** A per-tick factor k becomes `pow(k, 60 / rate)` (`per_tick_factor`):
   R/60 ticks of it are one 60 Hz tick of k, exactly. Constant acceleration (`x += v dt + g dt^2 / 2`, the skater's move
   and the arc preview) composes exactly already. Explicit / semi-implicit Euler updates (friction, drag, the ground's
   normal blend, camera lerps that add a fraction each tick) integrate the 60 Hz scheme's *modified equation* instead:
   for `v -= f(v) h; x += v h` (h = 1/60) that is `dv/dt = f - (h/2) f' f`, `dx/dt = v + (h/2) dv/dt` - integrated finely
   at the tick (RK4 in the harness), it lands on the 60 Hz run's numbers to second order (the roll: 7.13 in -> 0.10 in at
   every rate). Linear lerps (the camera's `time` and step) end exactly at their target and time (the last step
   clamped), which also removes the overshoot at 144 / 240.
2. **Events at their own time, on the 60 Hz grid where 60 quantises them.** A landing, a grind snap, a wall hit, a lip
   or a state change is found at the time it happens inside the tick (the ollie's airtime and landing speed then match to
   1e-5), then applied the way the 60 Hz run sees it: where the 60 Hz run notices an event only at the end of its 1/60
   s tick (the landing feeler, trick windows' edges, "this frame" flags), the event's effect is held to the next 1/60 s
   boundary, so the gameplay state changes on the same 60 Hz frame. (A "physics-exact" mode could apply them at once -
   better physics, but no longer the 60 Hz run.)
3. **Counts and truncations in 60 Hz units.** Tick counters become time (`ticks_for`, or counted down by the step);
   truncated milliseconds count on the 60 Hz grid (16 ms each 1/60 s: the 60 Hz run's 57.6 s a minute at any rate - or
   real time, if timers should be right rather than the same as 60; that is a decision for phase 3, the harness has
   both rows); `gameframe` waits become N/60 s (phase 4).
4. **Input on the 60 Hz grid for comparisons.** A demo's input is held per 1/60 s (as tick_compare plays it); live play
   may sample faster - it can't match a 60 Hz run exactly anyway.
5. **Random numbers.** Systems that draw a random number each tick (particles, pedestrians, some scripts) draw R/60 as
   many; anything that then feeds back into the run (a ped in the skater's way, a random trick choice) parts it. Give
   per-tick consumers their own streams, or draw on the 60 Hz grid.

Targets for the yardsticks (each phase reports them): the kernels within 0.1 in / 1 ms of 60; the recorded run with
every check point's state equal to the 60 Hz run's and the position within 0.1 in over 10 s, jumps' apex within 0.05
in, landings on the same 60 Hz frame.

## What remains

### Phase 3: tick counters, per-tick rates, truncations (C++)

- Skater: late wallplant window (`core_physics.cpp` `late_wallplant_.frames`, from LateWallplantFrames), projectile
  throw wait (`core_physics.h` `throw_.wait`, 30), model rotation `frames_left -= slomo` (`core_physics_state.cpp`),
  tagging countdown / cooldown (`tagging.cpp`), the ladder's first 25 ticks (`walk_physics.h`), THAW BMX's counters
  (`thaw_bmx_physics.cpp` no_balance_frames_, `kSlowTurnRamp` 1/600 a tick, `thaw_bmx.h` stall_steep_frames_,
  `thaw_bmx_parked.cpp`), collision thresholds per tick ("not moved this frame" - `adjust_physics.h`, the ground
  correction that lifts the skater at 240 Hz), the landing seen at the tick's end; the friction / drag / normal blend
  integrators (`core_physics_ground.cpp`: the modified equation above).
- Cameras: `instant_frames_` (3 ticks after a cut - `skater_camera_update.cpp`, `walk_camera.cpp`),
  `wall_ride_frames_`, the distance lerps' last step clamped.
- Objects: rigid body `damp()` (speed less `damping` a tick, no step) and sleep after 3 settled ticks
  (`rigidbody_support.cpp`), vehicles asleep after 3 still ticks (`vehicle_physics.cpp`), projectile `rest_` frames, the
  cutscene's `kFadeStep` 0.02 a tick (`cutscene_update.cpp`), particle trails' history moving a step a tick
  (`particle_system_update.cpp`), `suspend_component.cpp`'s check counts (cost only).
- Pedestrians: `bias *= decay` a tick (`ped_logic_move.cpp decay_bias`), 3 degrees a frame (`ped_logic.h`).
- Truncated milliseconds a tick: `engine_effects.cpp` (`int(dt * 1000)`), `tod_manager.cpp` (and its `wait_frames_`),
  goal and objective timers (`goal_run.cpp`, `goal_types.cpp`, `objective.cpp`, `trick_goals.cpp`, `front_end.cpp`), king
  of the hill (`skate_module_skaters.cpp`), the special meter (`score_combo.cpp`, `ftol(special - dt * rate)`) and
  `pot_countdown_`, the controller-unplugged frame count (`skate_module_skaters.cpp`), the screen blur's frames.
- Input: menu repeat counts (`menu_reading.cpp`), one-tick button events reaching scripts that don't poll every tick.
- Video and captures of a demo at a rate other than 60 (`media::starts_video_frame` picks frames of a 60 a second
  run).

### Phase 4: scripts

`cfunc_wait`'s `gameframe(s)` become N/60 s of game time at rates other than 60 (one place: the 106 + 10 waits, and
through them the `repeat` loops, counters and per-tick increments around a one-tick wait); then frame_audit.ts's polls of
one-tick events (296 `wait 1 gameframe` + 19 WaitOneGameFrame) reviewed - those that must see every tick keep a one-tick
wait. Proof: the script check unchanged at 60 (it compares QB and TypeScript on the same runtime; the rate doesn't
change what it compares), frame_audit.ts showing no class a left.

### Phase 5: demos and online

Demos (done in phase 1 - 2): version 2 with a RATE chunk for a demo recorded at another rate; a 60 Hz demo is still the
version 1 file it always was (older builds read it; they refuse version 2). A plain playback of a demo at another rate
holds each recorded frame over its time (tick_compare's runs); the viewer, `-demo-capture` and videos play a demo only at
its own rate. `-demo-quit` runs are tools' runs: a pad left connected doesn't take them over.

Online - what the translation needs so 60 Hz peers (stock THUG2, THUGPRO, this game at 60) see normal play:

1. **Send cadence by time.** `local_net_logic.cpp`'s forced send every 15 ticks and `% kSendEveryFrames` count ticks: at
   120 Hz they would send twice as often. Send when the 60 Hz frame count (ticks x 60 / rate) crosses the multiple - the
   same packets a second as a 60 Hz peer. The transmit periods (`gamenet_join.cpp`, `skate_net.cpp`: two frames of
   `game_config::frame_rate`, in ms) are already time.
2. **Frame numbers on the wire in 60 Hz frames.** Whatever carries a tick count between peers (`net_app.h`
   `frame_count_`, `skate_net.cpp` `forced_frame_`, `gamenet_player_lists.cpp` `add_at_frame`) is converted to 60 Hz
   frames when written and back when read, or replaced by the app clock's milliseconds (already real time:
   `nonlocal_playback.cpp`'s states are stamped in ms). Audit every packet field that is "per frame" (a velocity in units a
   frame, a count of frames) - none may leave in local ticks.
3. **Timeouts in time.** `gamenet_newplayers.cpp`, `gamenet_player_lists.cpp` (join / ready / drop counted in ticks):
   `ticks_for` or milliseconds - else a 120 Hz host drops players in half the time.
4. **Remote skaters' interpolation.** `RiderRemoteSync::interpolate` (`nonlocal_playback.cpp`) plays the history by the
   app clock (ms) - right at any rate, and smoother at 120 (sampled twice as often) - but its per-tick corrections are in
   frames of `frame_rate`: falling back 1000 / 60 ms each tick when past the newest state, and catching up 10 frames when
   far behind. Scale them by 60 / rate (or by the tick's ms) so the correction a second is the 60 Hz one.
5. **Shared game timers.** Goal / king of the hill / game-mode timers (phase 3) count on the 60 Hz grid so a 120 Hz host
   and 60 Hz clients agree on time left; scores that depend on tick windows (trick timing) follow phase 3.
6. **Tests by hand only:** two copies on a local OpenSpy server, one at 60 and one at 120 / 240 - remote skater smooth,
   no extra packets (count them), timers in step. Until then the online code stays at 60 Hz semantics and online play at
   another tick rate is unsupported.
