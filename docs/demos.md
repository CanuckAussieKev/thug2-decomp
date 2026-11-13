# Demos

A demo is a recorded run: the player's input from a level's load, enough to play the same run again. Played back
with different settings (renderer, MSAA, anisotropy, ray tracing...), it gives before / after pictures of the same
moments; or saved as a video. The code is `src/devtools/demo.h` (recording, playback, capture, the menus' commands),
`src/devtools/demo_overlay.h` (the REC / REPLAY tag), `src/formats/demo_format.h` (the file),
`src/engine/media/video_writer.h` + `src/media_ffmpeg/ffmpeg_video.h` (the video), `tools/pipeline/menu_level/demo_menu.ts`
(the menus, in the data overlay) and `tools/game/demo_compare.ts` (several settings, side by side).

## In the game: the pause menu's DEMOS

In free skate the pause menu has **DEMOS** (single player, after YOUR OPTIONS - the data overlay's
`gamemenu_pause.qb`, made by `make_menu_level.ts`; both front ends show it, the new front end in its own look):

| Item | What it does |
|---|---|
| RECORD DEMO | The level starts again and records (REC and the time in the top-right corner). The pause menu then shows **STOP RECORDING**: it saves `demo_<level>_<date>_<time>` (the frames in the pause menu are left out). F6 does the same. |
| PLAY DEMO | The saved demos, newest first (level, length, date; the twelve newest): the chosen one's level loads and the run is played by the game itself - the recorded input drives the skater, physics and all, with the game's camera. REPLAY and the time are shown. **Any button or stick press hands the controls back** (the run stops there; Start also pauses as ever). |
| SAVE AS VIDEO | The same list: the chosen demo plays and is saved as `thug2-decomp-videos\<demo>.mp4` (SAVING VIDEO and the percentage are shown, not in the video), then VIDEO SAVED and the file for a few seconds. A button press stops it (VIDEO STOPPED: the file holds what was made). |

A playback started from the pause menu is as exact as one from the command line (the same drift check: 0 in every test).

## Videos

A demo saved as a video is rendered, not filmed: every frame the video takes is drawn and handed to the encoder
however long that takes (the game's logic is a fixed 1/60 s a frame, so a slow export plays the same run). The picture
is taken over the frame before our tag, the menus and the console are drawn. Settings, `[demo]` in `thug2-decomp.ini`:

| Key | Default | |
|---|---|---|
| `video_fps` | 60 | 1-60: the frames taken (30: every other one) |
| `video_quality` | 80 | 1-100 |
| `video_codec` | auto | `auto`: NVIDIA's H.264 (h264_nvenc), else Windows' Media Foundation H.264 (h264_mf), else FFmpeg's MPEG-4 part 2 (mpeg4); `h264`; `mpeg4`; or an FFmpeg encoder's name |
| `video_scale` | 100 | the size in % of the game's picture (10-400; rounded to even) |

The encoders are in the game's FFmpeg DLLs (LGPL; `tools/ffmpeg/build_ffmpeg.ts` - no x264 / x265): MP4, BT.709, a key
frame every 2 s. **No sound**: the game's audio plays as the run is rendered (slower than real time) and isn't
captured. Sizes for the 16.9 s BO demo at 1280x720 (quality 80): h264_nvenc 60 fps 31 MB, h264_mf 60 fps 22 MB, mpeg4 30
fps 16 MB; h264_mf 640x360 30 fps 2.8 MB; h264_nvenc 1920x1080 (scale 150, quality 60) 37 MB. Each video's frames match
the game's own PNG capture of the same frames to within the codec's loss (mean 2.4-4.2 / 255).

`thug2-decomp.exe -encode-video <file.mp4> -encode-size WxH [-encode-fps N] [-encode-quality Q] [-encode-codec C]` is the
same encoder without the game: raw RGB frames on its standard input (`src/app/encode_video.h`) - `demo_compare.ts` makes
its side-by-side video with it. `thug2_video_check <file.mp4> [--frame N <out.png>]...` (a check program) reads a video
back with the same DLLs: codec, size, frame rate, frame count, frames as PNG.

## From the command line

| What | How |
|---|---|
| Record from a level's start | `thug2-decomp.exe -level BO -record mydemo`; skate; F6 (or quitting the game) saves |
| Record in a level | F6 restarts the level you are in and records; F6 again saves `demo_<level>_<date>_<time>` |
| From the developer console | `demo record [name]`, `demo stop`, `demo play <name>`, `demo status` |
| Play back | `thug2-decomp.exe -demo mydemo` - the demo's level loads, the run plays, then the controls are yours (any button takes them sooner) |
| Save as a video | `-demo mydemo -demo-video <file.mp4>` (relative: to the game folder) - then the game quits; `[demo]` settings above |
| Render every frame | `-demo mydemo -demo-capture <folder> [-demo-every N]` - PNGs from the level's first frame, then the game quits |
| Compare settings | `node tools/game/demo_compare.ts --demo mydemo --game <folder> --set A:Graphics.msaa=0 --set B:Graphics.msaa=8` |

`[Keybinds] DemoRecord=` changes F6 (an SDL scan code or key name). Recording and playback are refused online and in
network games. A tool's playback (`-demo-capture`, `-demo-video`) can't be taken over: the player's input is ignored.

Files, in `thug2-decomp-demos\` next to the exe:
- `<name>.demo` - the demo (below); `<name>.json` - its level, skater, date, length and the `[Graphics]` and
  `[features]` settings it was recorded with.
- After a playback: `<name>.play.track.txt` and `<name>.play.run.json` (with `-demo-capture`: `track.txt` and
  `run.json` in the capture folder) - the run's check track and a summary: frames, the first frame that differed from
  the recording and the largest drift, the settings it was played with.

`demo_compare.ts` writes `<out>\<set>\` (each run's frames, track and summary), `<out>\compare\frame_0001.png...` (the
pictures put together: `--layout side` all sets side by side, `split` half and half, `wipe` a line moving across over
the run, `diff` the difference, brightened), `report.md` / `report.json`, and an .mp4 made by the game's own encoder
(`-encode-video`, above). `--ffmpeg <exe>` makes it with that FFmpeg instead (also tried on the PATH if the game's
encoder fails); without either the report gives the command that makes the video from the PNGs. It exits 1 if the runs differ from each other. Pictures are about
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
4. **Real speed live, every frame when rendered.** A live recording or playback (RECORD DEMO, F6, PLAY DEMO,
   `-record`, `-demo`) runs at the game's own pace like any play: a frame that is late isn't drawn, so the game keeps
   up with real time however long the picture takes. That changes nothing in the run - the logic is one fixed step a
   loop whether the frame is drawn or not. A tool's playback (`-demo-capture`, `-demo-video`, SAVE AS VIDEO) renders
   the run instead: no frame is skipped (`keeps_every_frame`; the main loop then draws a late frame too -
   `engine/sys/mlp.cpp`) and a capture or a video has a picture for every frame - slower than real time when the
   frames are heavy, but the video plays at the recorded game time. The game keeps drawing in the background while a
   demo runs (`draws_unfocused`). When a recording or playback ends the log gives its pace (`demo: ... pace - game s
   in real s`, and the frames not drawn).
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

Live recording and playback at real speed (2026-11-13, BO, 2560x1440 asked - a 1920x1080 window -, Vulkan, `ray_tracing=1`
and every `rt_*` effect, MSAA 8): before, every frame was drawn and the game ran slow - 0.61 game s a real second while
recording, 0.52 while playing back (normal play with the same settings: 0.97). Now: recording 0.97 (481 of 1283 frames
not drawn), playback 0.89 / 0.95 (effects on: 230 / 561 not drawn) and 1.00 (effects off: 533 not drawn), the demo made
before the change 0.99 - every playback identical to the recording (1283 check points). Saved as a video it took 102 s
(every frame drawn) and the video has 1284 frames at 60 a second: the run's 21.4 s.

## Limits

- **Restarting in a level (F6, RECORD DEMO, `demo record`).** Measured 2026-11-13 with the clocks put back (the `CLCK`
  chunk): demos recorded in a level after 30 s of skating (BO, foun) play back identically from the command line and
  from the pause menu (drift 0 over 1371 check points). The earlier report of an in-level recording parting after a
  bail was made before the clocks were recorded; a demo without `CLCK` (made before then) still parts when it is played
  from anywhere but a fresh start (seen: an ollie from a held X lost - the button's press time came from the run before).
  Record again to get one that plays the same from the menu.
- A frame one run didn't draw (no camera noted) isn't counted as a difference: only the skater is compared there.
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
