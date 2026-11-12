# Demos

A demo is a recorded run: the player's input from a level's load, enough to play the same run again. Played back
with different settings (renderer, MSAA, anisotropy, ray tracing...), it gives before / after pictures of the same
moments. The code is `src/devtools/demo.h` (recording, playback, capture), `src/formats/demo_format.h` (the file) and
`tools/game/demo_compare.ts` (several settings, side by side).

## Using it

| What | How |
|---|---|
| Record from a level's start | `thug2-decomp.exe -level BO -record mydemo`; skate; F6 (or quitting the game) saves |
| Record in a level | F6 restarts the level you are in and records; F6 again saves `demo_<level>_<date>_<time>` |
| From the developer console | `demo record [name]`, `demo stop`, `demo play <name>`, `demo status` |
| Play back | `thug2-decomp.exe -demo mydemo` - the demo's level loads, the run plays, then the controls are yours |
| Render every frame | `-demo mydemo -demo-capture <folder> [-demo-every N]` - PNGs from the level's first frame, then the game quits |
| Compare settings | `node tools/game/demo_compare.ts --demo mydemo --game <folder> --set A:Graphics.msaa=0 --set B:Graphics.msaa=8` |

`[Keybinds] DemoRecord=` changes F6 (an SDL scan code or key name). Recording and playback are refused online and in
network games.

Files, in `thug2-decomp-demos\` next to the exe:
- `<name>.demo` - the demo (below); `<name>.json` - its level, skater, date, length and the `[Graphics]` and
  `[features]` settings it was recorded with.
- After a playback: `<name>.play.track.txt` and `<name>.play.run.json` (with `-demo-capture`: `track.txt` and
  `run.json` in the capture folder) - the run's check track and a summary: frames, the first frame that differed from
  the recording and the largest drift, the settings it was played with.

`demo_compare.ts` writes `<out>\<set>\` (each run's frames, track and summary), `<out>\compare\frame_0001.png...` (the
pictures put together: `--layout side` all sets side by side, `split` half and half, `wipe` a line moving across over
the run, `diff` the difference, brightened), `report.md` / `report.json`, and an .mp4 when `ffmpeg` is on the PATH
(or `--ffmpeg <exe>`; the game's own FFmpeg DLLs only decode Bink, they can't encode). Without FFmpeg the report gives
the command that makes the video from the PNGs. It exits 1 if the runs differ from each other. Pictures are about
1.5 MB each at 1280x720: `--every 2` (the default, 30 a second) or more keeps a 30 s demo to a few hundred MB a set.

## How a run is made the same

The main loop's logic already runs a fixed 1/60 s a frame on PC (one vblank counted per loop - `engine/sys/mlp.cpp`);
real time only decides whether a frame is drawn or skipped to catch up. A demo pins down everything else:

1. **The start.** Recording and playback both start the level the way `-level` does (`begin_level::read_in_load`, the
   level select's SKATE NOW in free skate). At that call (`begin_level::set_load_listener`) both random streams
   (`maths::seed_random`) and the C runtime's `rand()` are seeded from the demo, and the frame count starts.
2. **The input.** Port 0's control data each frame, after every input source has added to it (keyboard, controller,
   the automation channel), is recorded; a playback puts it back in place of the live input. The demo's input source
   is the last one (`app/extensions.cpp`).
3. **The level up.** When the level is up (the skater there, no loading screen) the random streams and the clocks
   (the stopwatch's vblanks and the game clock - `frame_clock::set_vblanks_since_reset`, `set_game_clock_snapshot`) are
   noted; a playback puts them back at the same frame, so anything that reads the time or draws a random number sees
   the same values.
4. **Every frame drawn.** While a demo records or plays, no frame is skipped and the game keeps drawing in the
   background (`keeps_every_frame`, `draws_unfocused`; the main loop then draws a late frame too -
   `engine/sys/mlp.cpp`): the same frames run the same draw-side code, and a capture has a picture for every frame.
   (Before that last change a ray-traced run left ~10 frames of 1192 undrawn - the skater's track was still identical,
   only the camera went unnoted on those frames.)
5. **The skater.** The profile (appearance, trick style, specials, its info and stats) is saved with the demo and put
   back before a playback if the current one differs.

The **check track** - the skater's position, velocity, matrix rows and state, and the camera, every frame from the level
up - is recorded with the input. A playback compares its own with it and logs the first frame that differs (more than
0.01 apart) and the largest drift (`demo: ... drift: ...` in the log, and `run.json`). `demo_compare.ts` compares the
runs' tracks with each other the same way.

## What was measured (2026-11-13, workspace runs, x64 RelWithDebInfo)

| Demo | Made by | Played | Result |
|---|---|---|---|
| BO, 19.9 s, pushes, ollies, flips, grinds | `-level BO -record` | 3 times: Vulkan, Direct3D 9, Vulkan | identical to the recording and to each other (1186 check points, drift 0.000) |
| foun (THPS3 port), 19.1 s | `-level foun -record` | MSAA 0 / MSAA 8 | identical to the recording and to each other |
| the same BO demo | | `ray_tracing=1`, `rt_shadows` 0 / 1 (Vulkan) | identical to the recording and to each other; the pictures differ in the shadows (23% of the pixels, mean 6.2 / 255) |
| foun, 19.6 s | F6-style restart in the level (`demo record`) | MSAA 0 / MSAA 8 (+ `ray_tracing=1`) | the playbacks identical to each other; they differ from the recording after a bail (frame 858 of 1173 - the skater gets up 40 frames sooner) |

Graphics settings didn't change a run: Vulkan against Direct3D 9, MSAA 0 against 8 and `ray_tracing` 0 against 1 gave
identical check tracks over every frame. What could make them differ, and so would show in the check track:
anything the game updates only when it is drawn (animation or particles advanced by the renderer), culling or LOD that
feeds back into the logic, and frames left undrawn (a device reset when the window comes back to the front logs
"wasn't drawn" during a capture).

## Limits

- **Restarting in a level (F6, `demo record`).** The skater object lives through the level's restart, and some of
  its state (seen: how long getting up after a bail takes) carries over from before. The playback - which starts from
  the main menu - then plays the run identically every time, under any settings, but may part from what was recorded
  after such a moment. For a playback that matches the recording, record from the start: `-level <name> -record <demo>`.
- Keys our own tools read directly aren't in the demo (F7 BMX, F8 restart tester, F9 free camera, the console, mouse
  clicks on menus): using them while recording makes the playback differ.
- Only port 0 is recorded (single player).
- A playback puts the recorded skater back in memory only (not saved), and only where the current one differs.
- The level's files must be the same as when it was recorded (a re-converted ported level can play differently).

## The file (`.demo`, version 1)

Little-endian: `THUG2DEM`, u32 version, then chunks `{4-character tag, u32 size, body}`. A reader skips chunks it
doesn't know and refuses a newer version. `INFO` (names, seeds, the frame the level was up, the random state there),
`SETS` (settings), `PROF` / `DETL` (the skater), `INPT` (port 0's 32 bytes a frame, run-length coded), `CHEK` (the
check points, 84 bytes each), `CLCK` (the clocks when the level was up; optional). `formats/demo_format.h` has the
fields; `thug2_format_check` round-trips it and checks a later chunk is skipped and a newer or broken file refused.
