# Simulation rate and display

The game simulates at exactly the original's rate: one step every 1/60 s, each step the original's - physics, scripts,
timers, collisions, input, the network - computed with the original's constants (the step `slomo x (1.0F / 60)`, 16 ms
timer ticks, frame-counted waits and windows). Nothing in the simulation knows how often the picture is drawn. The
display runs at whatever rate the machine and `[Graphics] max_fps` / `present_mode` / `frame_generation` allow, by
drawing between the simulation's states (render-only frames), never by changing them.

So two players on any hardware, at any frame rate, run the same physics: online games and score competitions are fair,
and a demo (a recorded run's input) replays the same run on any machine at any display rate.

## Why exactly 60 (the decision, 2026-11-15)

Between 2026-11-14 and 2026-11-15 the game had an experimental `[Graphics] tick_rate` (60 - 240) that ran the
simulation itself in shorter steps. It showed what a faster simulation costs:

- The original's per-frame update (semi-implicit Euler, a straight move then a snap to the face found, events seen at
  the step's end) is not rate-independent. Run once a step at 1/120 or 1/240 s it parts from the 60 Hz run by inches
  within a second or two (a roll to a stop: +7 to +11 in of 660; an ollie's landing speed off by 9 units a second; a
  quarter pipe's air 1 - 2 in lower), and a 10 s line ends somewhere else entirely (another landing frame, a missed
  grind).
- Making the shorter steps reproduce the 60 Hz run ("the 60 Hz grid": forces, checks, timers, script waits and the
  network all kept once each 1/60 s inside the faster steps) got 120 and 240 to within 0.13 in / 8 in of the 60 Hz
  run over 10 s - but that is 60 Hz physics drawn faster, not faster physics, and 144 (2.4 steps a 1/60 s) never
  matched.
- Physics solved exactly (each force integrated in closed form, faces, lips and landings crossed at their own times)
  is the same at every rate to 1e-4 - but its 60 Hz run is not the original's either (the roll 14 in further, the
  quarter pipe's air 1.1 in lower, the manual balance leaning 9% more). "The same at every rate" and "the original
  at 60" cannot both hold once the simulation steps faster.

Physics must be exactly the original's (online play against the original game and other builds, competitions, the
recorded runs), so the faster simulation was removed rather than finished. What it gave that is still right at 60
stayed: the network's send decision as one function (`engine/net/net_cadence.h`, checked against the original's rule),
the demo format's reading of the RATE chunk (below) - and the measurements above, which are why.

Removed with it (2026-11-15): `[Graphics] tick_rate`; `engine/sys/tick_rate.h / .cpp / _settings.cpp`; the 60 Hz grid
in the clock (`grid_frames`, `grid_frame_ends`, `grid_frame_of_tick`, `anchor_grid`, `get_tick_time`,
`ms_since_grid_frame_end`, `script_frame_end_pass`, `get_slomo_grid_seconds`) and every site that read it - goal,
objective, trick-goal, king-of-the-hill and front-end timers, time of day, the special meter, tick counters (wallplant,
throw, tagging, cameras, vehicles, projectiles, THAW BMX), rigid-body and cutscene per-step amounts, pedestrians' decay
and turn, the script waits, the skater's split frame (forces once a frame, the end-of-frame checks, held ticks, the rail
overshoot), the network's gating and the remote skaters' tick-time playback clock; the demo playback at another rate
(resampling, the RATE chunk's writing); `thug2_tick_rate_check` and its integration harness; `tools/game/tick_compare.ts`;
`tools/qbjs/frame_audit.ts` (the scripts' frame-assumption audit). All of it is back to the code before 2026-11-14 - the
original's.

## How the loop runs

`frame_loop::Hub::get_main_loop` (`engine/sys/mlp.cpp`), each loop:

1. SDL's pending events handled, the typed characters cleared, the network serviced.
2. Waits until the step is due (`QueryPerformanceFrequency / 60` after the last one; mostly slept, the last 2 ms spun).
3. The frame's start for NVIDIA Reflex (`GpuContext::latency_marker` kSimulationStart: with Reflex on, its wait), then
   the step: the system tasks - among them the ports' poll (`sio::InputPort::work_on`), which handles SDL's events again
   and reads the keyboard's and the pads' state - and the logic tasks (objects, physics, scripts).
4. The step's frame drawn and presented - with `max_fps` blended between the step before and this one (below).
5. With `max_fps`: render-only frames until the next step is due (`draw_extra_frames`).

A late step is drawn less, never caught up (more than half a step late: not drawn, at most four in a row) - as the
original. `get_frame_length()` is always 1 vblank. Whether a step is drawn changes nothing in the simulation
("Determinism under load", below).

## Display at any rate

`[Graphics] max_fps` (`engine/sys/frame_pacing.h`, `frame_blend.h`, `render_pc/frame_interp.cpp`): each step's frame and
the render-only frames after it draw every instance's transform, its bone palette and the cameras blended between the
step before and this one, by `alpha` = the time since this step was due / 1/60 s. Before the draw the blended values are
written into the instances and cameras; after it every one is put back (`frame_interp::end_frame`), and a render-only
frame runs no task, counts no vblank, reads no input and puts the game's random streams and the C runtime's `rand` back
as it found them. Frame generation (DLSS-G, FSR 3) makes its frames from the drawn ones, after the fact.

The proof that none of it reaches the simulation is the check below: the same demos at max_fps 0, 120, 144 and 240 give
the same state on every frame, to the bit.

## The proof

`tools/game/sim_check.ts` plays the demos in `tools/game/sim_reference` and compares every frame's simulation state with
the reference, bit for bit. The state (`devtools/demo.cpp state_line`, the playback's `<demo>.play.state.txt`): FNV-1a
hashes of the skater's position, velocity, whole matrix and state; of every object with a motion or rigid-body
component (its id, position, velocity, matrix); of the game's two random streams; and the clock's milliseconds. The
reference was recorded from main before the change (5c403296), at max_fps=0.

| demo | level | what it does | frames |
|---|---|---|---:|
| sim_grind | a ported level | pushes, five ollies, a grind | 693 |
| sim_se | Pro Skater | pushes, ollies, a manual, bank air with a spin, flip tricks, a bail | 1177 |
| sim_vert | Training | the drop-in, a turn, quarter-pipe vert air with a spin | 713 |
| sim_wall | TestLevel | ollies into a wall, a wallplant | 624 |

`thug2_sim_check` (src/tests) checks the part that needs no game: the skater's online update goes out on the original's
frames and clock stamps (`net_cadence::skater_update_due` against the original's rule over 10 s of frames, for a client
and for the server).

(A wallride wasn't reached by the scripted runs - the test level's wall gave a wallplant; the other demos cover the rest.)

### Results

After the removal (the build of this change, a workspace, 2026-11-15) - every frame of every demo equal to the reference at every display rate (late frames: the playback's steps not drawn, below):

| demo | display rate | frames equal | missing | first differs | late frames | result |
|---|---|---|---|---|---|---|
| sim_grind | 60 (max_fps=0) | 693 / 693 | 0 | none | 6 | ok |
| sim_grind | 120 | 693 / 693 | 0 | none | 5 | ok |
| sim_grind | 144 | 693 / 693 | 0 | none | 5 | ok |
| sim_grind | 240 | 693 / 693 | 0 | none | 5 | ok |
| sim_se | 60 (max_fps=0) | 1177 / 1177 | 0 | none | 12 | ok |
| sim_se | 120 | 1177 / 1177 | 0 | none | 22 | ok |
| sim_se | 144 | 1177 / 1177 | 0 | none | 18 | ok |
| sim_se | 240 | 1177 / 1177 | 0 | none | 15 | ok |
| sim_vert | 60 (max_fps=0) | 713 / 713 | 0 | none | 18 | ok |
| sim_vert | 120 | 713 / 713 | 0 | none | 16 | ok |
| sim_vert | 144 | 713 / 713 | 0 | none | 9 | ok |
| sim_vert | 240 | 713 / 713 | 0 | none | 7 | ok |
| sim_wall | 60 (max_fps=0) | 624 / 624 | 0 | none | 4 | ok |
| sim_wall | 120 | 624 / 624 | 0 | none | 4 | ok |
| sim_wall | 144 | 624 / 624 | 0 | none | 40 | ok |
| sim_wall | 240 | 624 / 624 | 0 | none | 38 | ok |

Run again with the latency probe's build: every run equal but sim_se at 240, which had 67 late steps (against 9 - 15
in the other sim_se runs) and parted at frame 874 in the random streams - the late-frames case below (the same frame an
earlier sim_se run at 120 with 22 late steps parted at).

(Those parted runs were the original's dependence on drawing, below - fixed since.)

## Determinism under load

Every step is simulated as if its frame were drawn (the decision of 2026-11-15): a machine that can't keep up - a late
step isn't drawn - runs the same game, step for step, as one that draws every frame. Competitions are fair and a
replay is exact under any load.

### What depended on drawing

The original's simulation read whether a step's frame would be drawn (a step more than half a step late isn't, `mlp.cpp`;
the window inactive, the device lost), and its drawing (`draw_world`) changed the game's state. So a late step changed the
game: shown on purpose with `[debug] load_test=7` (a 30 ms stall every 7th loop), sim_grind parted at frame 552 (an
object) and sim_se at 837 (the random streams). Found, by reading the code and tracing every draw of the game's random
streams (the first call that differs between a run with and without load, and the script making it):

| what | where (the original's) | what it fed |
|---|---|---|
| the animation blend skipped its pose on an undrawn step | `BlendChannel::blend_pose` | the skeleton's bones: bone-attached objects, the bone positions scripts ask for (sim_grind's object) |
| a vehicle skipped its bones | `DrivableModule::refresh_bone_rig` | what rides on the vehicle |
| the particle systems' bursts begun, aged and ended only when drawn | `PcParticleSystem::draw` | `IsEmitting`: a script waiting for a system to end and then making the next (sim_se: a robot's sparks - `alloc_fast_particle`'s object, with its lock's random wobble, 13 frames later) |
| "is it in the camera's view" asked of the renderer's view - the one the last drawn frame left | `sphere_visible_in_one_viewport` | objects suspended out of sight (`DormancyModule`), the filming goal's target, the shadow projections and flexible particles that cull |
| a rain splash added when drawn | `PcWeather::render_splashes` | two of the game's random numbers a splash |
| rain and snow drops moved on only when drawn | `PcWeather::render` | which drops the next step may start (`tick_frame` draws two random numbers for each) |
| a scene's mesh triangle lists made the first time it was drawn | `draw_viewport` | blocks taken from the game's heap |

### What moved

One rule (`engine::render_frame`, `engine/gfx/engine.cpp`): what `draw_world` did to the game's state runs on every step,
drawn or not, at the point of the step where `draw_world` would run - and the drawing only reads. On a step that is drawn
the order and the values are the original's, so a run without late steps is the original's to the bit (the reference
below, recorded before the change).

- The pose and the vehicle's bones: set on every step (`suppress_draw` no longer read).
- `Device::step_frame` (render_pc `draw_frame.cpp step_frame`): the simulation's view (`occlusion.h SimulationView`) - the
  view the original's renderer was left with (the last viewport with a camera, its world pass), worked out from the
  game's own camera as the step left it - which `sphere_visible_in_one_viewport` now asks; and a new scene's mesh lists.
  The view doesn't change with what only the drawing changes: an extension's camera (the demo viewer's free camera, the
  level editor's), the fisheye lens, `max_fps`'s blend, a split view, the upscaler's jitter.
- `EffectsDirector::step_particles` (`Sparkle::step` -> `PcParticleSystem::advance_bursts`) once for each viewport with a
  camera, as the original's drawing advanced them (twice in split screen, as the original); `draw` draws.
- `WeatherSystem::step` (a splash added, before the frame) and `after_step` (the splashes aged, the drops moved on, after
  it), once for each viewport with a camera; `render` draws.
- `tools/sim_draw_independence.test.ts` keeps it so: `suppress_draw()` is read only by the main loop, `render_frame` and
  the drawing (render_pc, gpu_vulkan).

Left to the drawing, as presentation that the simulation doesn't read: the lens flare (its screen elements and the
scripts that show and hide them run from the drawing, timed by the GPU's occlusion query for the sun - already outside
the game's state on any machine), the glows, the projected-texture batches, the occluders' choice of what hides what (the
renderer's own culling; the game's code asks the simulation's view, which doesn't use them), and the C runtime's `rand`
draws of the drawing (particle seeds, primitives' sort keys) - the presentation's own random numbers, apart from the
game's two streams. Not the original's either, and not tied to the step: `[Graphics] tod` with a cycle (`day_clock`) runs
the level's time-of-day scripts by the wall clock - off by default. And kept as the original's: the simulation's view
takes the viewport's aspect, so a wider window sees - and wakes - objects further to the side than a 4:3 one; a fixed
view for the simulation would part from the original's on drawn frames.

### The music's random numbers

Not drawing, but the same kind of dependency: the music player chooses its next track when the last one's stream ends -
in real time - and the original drew that choice from the game's random streams (a reshuffle: 4,000 numbers - the
first level of a session draws a new order; and one to keep the last track from coming first). Where those draws fell
in the game's sequence depended on the wall clock, so a stall, a slower disk or a slower machine moved every later random
number of the game: a different pedestrian, a different idle, a different wait. Demos kept the music apart while they
ran (since 2026-11-13); since 2026-11-15 the engine always does (`audio/music_player.h`): the music draws from streams of
its own, seeded with a fixed seed, swapped in for the game's over the scopes that draw (`MusicDraws`), and
the game's streams are never touched by it. One path, no switch. PartyMod's shuffle order comes from the process's C
runtime `rand` (`std::rand`, seeded from the time at start-up) - not the game's C runtime `rand` (`crt::game_rand`,
which demos seed and the game's effects draw), so it touches neither of the game's sequences. The track-title script it
runs (`spawn_update_music_track_text`) only restyles screen elements. `tools/music_random.test.ts` keeps it so: every
function of the music player that draws does it inside `MusicDraws`, and nothing else in the audio draws the game's
streams. The sound effects' random choices (`surface_kind.cpp`, a sound action by its chance) stay on the game's streams:
they're made by the step, not by the clock.

A demo recorded before 2026-11-13 (no `RAND` chunk) let the music draw the game's numbers; its playback now keeps them
apart like every other, so it can part from its recording where the music changed track (the playback says so in the
log). sim_grind was such a demo (the reshuffle at frame 64 of its playback); its reference is to be recorded again
from a build with this change.

### Proof

`node tools/game/sim_check.ts --load 0,7` (`[debug] load_test=7`, `README`): each demo at each display rate, with and
without the stalls, against the same reference (recorded from main before the change, 5c403296). Late frames: the
playback's steps not drawn. Without the change, the load runs part (sim_grind at 552, sim_se at 837); with it:

The build of this change (2026-11-15, THUG2-2) - 32 of 32 runs equal to the reference on every frame:

| demo | display rate | frames equal | missing | first differs | late frames | result |
|---|---|---|---|---|---|---|
| sim_grind | 60 (max_fps=0) | 693 / 693 | 0 | none | 3 | ok |
| sim_grind | 120 | 693 / 693 | 0 | none | 3 | ok |
| sim_grind | 144 | 693 / 693 | 0 | none | 3 | ok |
| sim_grind | 240 | 693 / 693 | 0 | none | 3 | ok |
| sim_grind | 60 (max_fps=0), load 7 | 693 / 693 | 0 | none | 103 | ok |
| sim_grind | 120, load 7 | 693 / 693 | 0 | none | 110 | ok |
| sim_grind | 144, load 7 | 693 / 693 | 0 | none | 200 | ok |
| sim_grind | 240, load 7 | 693 / 693 | 0 | none | 202 | ok |
| sim_se | 60 (max_fps=0) | 1177 / 1177 | 0 | none | 2 | ok |
| sim_se | 120 | 1177 / 1177 | 0 | none | 2 | ok |
| sim_se | 144 | 1177 / 1177 | 0 | none | 2 | ok |
| sim_se | 240 | 1177 / 1177 | 0 | none | 2 | ok |
| sim_se | 60 (max_fps=0), load 7 | 1177 / 1177 | 0 | none | 182 | ok |
| sim_se | 120, load 7 | 1177 / 1177 | 0 | none | 237 | ok |
| sim_se | 144, load 7 | 1177 / 1177 | 0 | none | 336 | ok |
| sim_se | 240, load 7 | 1177 / 1177 | 0 | none | 338 | ok |
| sim_vert | 60 (max_fps=0) | 713 / 713 | 0 | none | 2 | ok |
| sim_vert | 120 | 713 / 713 | 0 | none | 2 | ok |
| sim_vert | 144 | 713 / 713 | 0 | none | 2 | ok |
| sim_vert | 240 | 713 / 713 | 0 | none | 2 | ok |
| sim_vert | 60 (max_fps=0), load 7 | 713 / 713 | 0 | none | 105 | ok |
| sim_vert | 120, load 7 | 713 / 713 | 0 | none | 110 | ok |
| sim_vert | 144, load 7 | 713 / 713 | 0 | none | 203 | ok |
| sim_vert | 240, load 7 | 713 / 713 | 0 | none | 205 | ok |
| sim_wall | 60 (max_fps=0) | 624 / 624 | 0 | none | 2 | ok |
| sim_wall | 120 | 624 / 624 | 0 | none | 2 | ok |
| sim_wall | 144 | 624 / 624 | 0 | none | 2 | ok |
| sim_wall | 240 | 624 / 624 | 0 | none | 2 | ok |
| sim_wall | 60 (max_fps=0), load 7 | 624 / 624 | 0 | none | 91 | ok |
| sim_wall | 120, load 7 | 624 / 624 | 0 | none | 93 | ok |
| sim_wall | 144, load 7 | 624 / 624 | 0 | none | 179 | ok |
| sim_wall | 240, load 7 | 624 / 624 | 0 | none | 180 | ok |

`thug2_sim_check`'s online checks still pass; the source rule above runs in `npm test`.

One run of sim_grind at 240 with load parted once in an earlier full pass (frames 65 - 80 in the random streams only -
the skater and the objects equal throughout - and equal again from 81); eight runs of the same again were equal. That
run's log shows a stall of about a second (the playback at 0.92 of real time against 1.00). sim_grind was then a demo
from before the music was kept apart, so its playback let the music's reshuffle draw on the game's streams at a moment
set by the audio stream's (wall-clock) state: the cause - not drawing. The music now never draws the game's numbers
("The music's random numbers" above); sim_grind's reference is to be recorded again.

## Input latency

What sits between a press and the picture, at 60 steps a second:

- **Sampling.** The step reads the pads and the keyboard as it starts: the ports' poll, in the step's system tasks,
  handles SDL's pending events first and then reads SDL's keyboard and controller state. So a press waits for the next
  step - 0 to 16.7 ms, 8.3 on average - and nothing earlier in the loop holds it longer. (Tried 2026-11-15: the wait for
  the step moved before the loop's own event handling as well - no difference measured, 8.48 against 8.53 ms, so the
  loop keeps the original's order.)
- **Interpolation (`max_fps` only).** A step's result is blended in over the following 1/60 s: the first frame after
  the step shows a little of it, the frame at the next step all of it. That is the price of drawing between states
  without guessing - up to one step more before a press is fully on screen. Showing the newest state at once when a
  step lands and interpolating on from it would need the step after it, which doesn't exist yet (only an extrapolation
  could, and that draws the skater where the game didn't put it); `max_fps=0` shows each step's state as soon as it is
  drawn, as the original.
- **The GPU's queue.** With vsync, presents can queue frames ahead of the display. `[Graphics] reflex=on` (NVIDIA)
  waits at the frame's start, before the step reads its input, so the frame starts as late as it can and still be
  ready. It stays off by default: it loads Streamline's interposer into every Vulkan call, and here (a light scene,
  the GPU far from busy) it changed nothing measurable.

`[debug] latency_probe=1` (`platform/latency_probe.h`) measures it inside the game: a thread pushes a stand-in input event
into SDL's queue at random moments; every 300 the log gets a `latency:` line - from the event's time to the step that
read it, to the first present showing any of that step, and to the present showing all of it (presents timed when the
present call returns: the display's scan-out comes on top). Probes that waited more than 100 ms (a load) are left out.

Measured 2026-11-15 (RTX 5070 Ti, Pro Skater, a 1280x720 window, 300 probes each; average / 95th percentile, ms):

| display | event to step | to first shown | to fully shown |
|---|---|---|---|
| max_fps=0, vsync | 8.5 / 15.8 | 10.4 / 18.3 | 10.4 / 18.3 |
| max_fps=0, vsync, reflex=on | 8.2 / 15.8 | 10.1 / 17.7 | 10.1 / 17.7 |
| max_fps=144, immediate | 8.0 - 8.6 / 15.7 - 16.0 | 10.1 - 10.7 / 18.1 | 26.8 - 27.4 / 34.8 - 35.0 |
| max_fps=144, immediate, reflex=on | 8.1 / 15.4 | 10.2 / 18.0 | 26.9 / 34.4 |
| max_fps=240, immediate | 9.0 - 10.5 / 16.3 - 16.5 | 11.7 - 14.2 / 19.9 - 25.3 | 28.8 - 30.3 / 36.2 - 37.2 |

So: the sampling delay is the 60 Hz step's own (half a step on average), drawing and presenting add about 2 ms, and
`max_fps` adds one step (16.5 ms) before a press is fully on screen while starting to show it as soon as without it.

## Demos

Every demo is a version 1 file without a RATE chunk. A version 2 demo (written 2026-11-14 - 15 by the builds with a
faster simulation: only those recorded at a rate other than 60) is read: a RATE of 60 plays as any demo, another rate is
refused - its input frames are that rate's steps (`formats/demo_format.h`).
