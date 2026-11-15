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
3. The step: the system tasks - among them the ports' poll (`sio::InputPort::work_on`), which handles SDL's events again
   and reads the keyboard's and the pads' state - and the logic tasks (objects, physics, scripts).
4. The step's frame drawn and presented - with `max_fps` blended between the step before and this one (below).
5. With `max_fps`: render-only frames until the next step is due (`draw_extra_frames`).

A late step is drawn less, never caught up (more than half a step late: not drawn, at most four in a row) - as the
original. `get_frame_length()` is always 1 vblank.

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

### Late frames

The original's simulation reads whether a step's frame will be drawn: a step more than half a step late isn't drawn
(`mlp.cpp`), and then the animation blend skips its pose (`BlendChannel::blend_pose`), the vehicles skip their bones
(`DrivableModule::refresh_bone_rig`) and nothing in `draw_world` runs. What those feed - bone-attached objects, script
timings - then differs, so a run with late steps parts from one without: the original is not deterministic under load.
Shown on purpose (2026-11-15: a build that slept 30 ms every 7th loop): sim_grind parted at frame 552 (an object), sim_se
at 837 (the random streams: a script's object attached later); with the animation blend run on undrawn steps too
sim_grind no longer parted, sim_se still did (something in `draw_world`). Render-only frames add load, so they make late
steps likelier (one run of sim_se at 120: 22 late steps against 9, parted at frame 874) - not by feeding the
simulation, but through the original's own dependence on drawing. `sim_check.ts` shows each run's late steps; a run
without any is the comparison that counts. Making the simulation independent of drawing (every step simulated as if its
frame were drawn - the original's result on a machine that keeps up) is a separate change: every reader of
`suppress_draw` on the simulation's side and every effect of `draw_world` on game state.

## Demos

Every demo is a version 1 file without a RATE chunk. A version 2 demo (written 2026-11-14 - 15 by the builds with a
faster simulation: only those recorded at a rate other than 60) is read: a RATE of 60 plays as any demo, another rate is
refused - its input frames are that rate's steps (`formats/demo_format.h`).
