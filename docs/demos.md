# Demos

A demo is a recorded run: the player's input from a level's load, enough to play the same run again. Played back
with different settings (renderer, MSAA, anisotropy, ray tracing...), it gives before / after pictures of the same
moments; or saved as a video. The code is `src/devtools/demo.h` (recording, playback, capture, the menus' commands),
`src/devtools/demo_overlay.h` (the REC / REPLAY tag), `src/devtools/demo_viewer.h` (the viewer), `src/devtools/compare_split.h` (the split comparison), `src/formats/demo_format.h` (the file),
`src/engine/media/video_writer.h` + `src/media_ffmpeg/ffmpeg_video.h` (the video), `tools/pipeline/menu_level/demo_menu.ts`
(the menus, in the data overlay) and `tools/game/demo_compare.ts` (several settings, side by side).

## In the game: the pause menu's DEMOS

In free skate the pause menu has **DEMOS** (single player, after YOUR OPTIONS - the data overlay's
`gamemenu_pause.qb`, made by `make_menu_level.ts`; both front ends show it, the new front end in its own look):

| Item | What it does |
|---|---|
| RECORD DEMO | The level starts again and records (REC and the time in the top-right corner). The pause menu then shows **STOP RECORDING**: it saves `demo_<level>_<date>_<time>` (the frames in the pause menu are left out). F6 does the same. |
| PLAY DEMO | The saved demos, newest first (level, length, date; the twelve newest): the chosen one's level loads and the run is played by the game itself - the recorded input drives the skater, physics and all, with the game's camera. REPLAY and the time are shown. **Any button or stick press hands the controls back** (the run stops there; Start also pauses as ever). |
| WATCH REPLAY | The same list: the chosen demo plays in **the viewer** (below) - a timeline, cameras, keyframes, the split comparison. |
| SAVE AS VIDEO | The same list: the chosen demo plays and is saved as `thug2-decomp-videos\<demo>.mp4` (SAVING VIDEO and the percentage are shown, not in the video), then VIDEO SAVED and the file for a few seconds. A button press stops it (VIDEO STOPPED: the file holds what was made). |

A playback started from the pause menu is as exact as one from the command line (the same drift check: 0 in every test).

## The viewer

A demo played in the viewer (DEMOS > WATCH REPLAY, `-demo <name> -viewer`, the console's `demo view <name>`) is the same
playback - the recorded input drives the skater - with a timeline, cameras and keyframes over it: the panels are Dear
ImGui's (`src/devtools/demo_viewer.h`, `demo_viewer_ui.cpp`), drawn after a video's picture is taken, never in it. While
it is up the game reads no key or pad (they are the viewer's); **Esc** leaves it - the controls are then the player's
from the frame shown (as a button press does in PLAY DEMO).

| Keys | |
|---|---|
| Space | play / pause (at the end: from the start again) |
| Left / Right | a frame back / forward (Shift: a second) |
| Home / End | the level up / the last frame |
| `[` / `]` | slower / faster: 0.1x, 0.25x, 0.5x, 0.75x, 1x, 1.5x, 2x, 4x |
| C | the next camera: game, free, orbit, fixed, path |
| K, Delete | a key at the frame shown from the camera drawn; the selected key deleted |
| I / O, L | the loop region's in / out at the frame shown; the loop on / off |
| U | the HUD (the game's 2D) on / off |
| V | the split comparison on / off (below) |
| H | the panels hidden / shown |
| WASD, Q / E, the right mouse button, the wheel | the free camera (Shift faster, Ctrl slower; a pad's sticks and triggers too); the orbit camera turns with the right button, the wheel sets its distance |
| A pad: A / Start, the shoulders | play / pause; a frame back / forward |
| Esc | leave the viewer (during SAVE AS VIDEO: stop the video) |

The bar at the bottom has the same buttons, the speed, the time and frame, Loop with In / Out, HUD, Split, + KEY, SAVE
AS VIDEO and EXIT, and the timeline: the frames across (the level's loading dimmed, the loop region shaded, a tick a
second), the keys as diamonds and the frame shown. Click or drag on it to seek (when let go); drag a key to move it;
click a key to select it and go to its frame. The panel on the right: **Camera** (the cameras, the field of view, the
fixed cameras - "Place one here", their names; a name starting with "track" keeps the skater in view), **Keys** (the
keys, the selected one's frame, position, yaw / pitch / roll, field of view and how it moves to the next: smooth,
linear, ease or hold; "Set from the view") and **Compare** (the split comparison's sides).

- **Playing, pausing, slow motion.** The main loop asks the viewer each loop how to run it (`ext::LoopPace`,
  `engine/sys/mlp.cpp`): paused, a loop runs no logic frame - no clock counted, no input read for the game, no task run -
  and the frame is drawn again as the last logic frame left it (the cameras still move); slow motion is a logic frame
  every few loops, every loop drawn (a camera path moves on between the logic frames); 1.5x - 4x are 2 - 4 logic frames
  a drawn one. The logic is the same fixed 1/60 s a frame however the loops are paced, so none of this changes the run.
- **Seeking.** The game's logic only runs forward. A frame after the one shown is reached by running on flat out,
  undrawn (a frame drawn every 0.1 s shows SEEKING and the percentage); a frame before it by starting the demo again as
  `-demo` starts it - back to the main menu (the pause menu's QUIT), the menu up a moment, the demo's level loaded with
  its random numbers, clocks and input - and running on to it. (Loading the level again from inside itself kept the
  skater's state at the level up: a demo recorded from a fresh start parted at frame 5.) No snapshot of the game is kept
  for a faster way back: its state is spread over the script heap, the objects, the physics, the sound and the screen
  elements, and nothing puts all of it back exactly - a checkpoint that missed one piece would part from the recording
  without the drift check seeing why. Every pass after a seek back is a second playback in the same session: exact
  for a demo with the `GLOB` and `RAND` chunks (below, "What a session leaves behind").
- **Cameras.** The game's (as recorded); **free** (it starts where the camera is); **orbit** round the skater (from
  behind it while "follow" is on); **fixed** cameras placed in the level; **path** - the keyframes: a position, a
  rotation (yaw, pitch, roll) and a field of view at a frame; between keys the camera moves smooth (a curve through the
  keys whose speed carries on through each - Catmull-Rom slopes, spaced in time), linear, ease (slowing into and out of
  both keys) or hold (it stays, then cuts); before the first key and after the last, those keys'. The yaw turns the
  short way (170 to -170 passes 180). The game's camera keeps running underneath: the one drawn is put back once the
  frame is presented, and the check track records the game's, so the run and its check are the same whichever camera is
  drawn. The field of view (0: the game's) applies to every camera but a key's own.
- **The sidecar.** The keys, the fixed cameras and the loop region are kept beside the demo as
  `thug2-decomp-demos\<demo>.camera.txt`, saved half a second after a change: a text file, `thug2-demo-camera 1` then a
  line each - `key <frame> <x> <y> <z> <yaw> <pitch> <roll> <fov> <smooth|linear|ease|hold>`, `fixed <x> <y> <z> <yaw>
  <pitch> <roll> <fov> <name>`, `loop <in> <out>` - numbers written to read back exactly. A newer version is refused, a
  line of a kind it doesn't know skipped (`src/devtools/demo_camera_path.h`; `thug2_viewer_check` tests the curves and
  round-trips the file).
- **SAVE AS VIDEO** renders the loop region if one is set, else the demo from the level up, at `[demo] video_fps` with
  the camera path (with no keys, the camera chosen), the graphics as they are set (the split too, its divider and
  labels) and the HUD as set - into `thug2-decomp-videos\<demo>_viewer.mp4` (the encoder of "Videos" below). The frames
  before the range run flat out (the level loaded again if the frame shown is past its start), then every frame of it is
  drawn and taken; the viewer is paused at the range's end afterwards. `-demo <name> -viewer -demo-video <file.mp4>`
  does it from the command line with the demo's sidecar, then quits.
- No depth of field (left out: a pass of its own for the viewer only).

The console (also the automation channel's `console ...`, which runs while the viewer is paused): `viewer status`,
`viewer play | pause | seek <frame | seconds s> | step [n] | speed <x>`, `viewer camera game | free | orbit | fixed [n] |
path`, `viewer free <x> <y> <z> <yaw> <pitch>`, `viewer fov <degrees>`, `viewer hud 0|1`, `viewer ui 0|1`, `viewer key add
| delete <n> | clear`, `viewer loop <in> <out> | off`, `viewer export [file]`, `viewer save`, `viewer exit`.

## The split comparison

The screen-space effects with two sets of settings at once: side A (the ini's `[Graphics]`) left of a divider, side B
(the same with `[Compare]`'s values put over it) right of it - in normal play, in a demo playback and in the viewer
(`src/devtools/compare_split.h`).

| Setting | Default | |
|---|---|---|
| `[Graphics] compare_split` | 0 | 1: on from the start (the console's `compare on` / `off`, the viewer's V or Split) |
| `[Graphics] compare_divider` | 0.5 | where the divider is, 0.05 - 0.95 of the width; dragged with the mouse (the left button on it), Alt+Left / Alt+Right move it 1% |
| `[Graphics] compare_labels` | 1 | A and B (with B's settings) in a box each side of the divider's top; 0: none (pictures to compare) |
| `[Compare] <key>=<value>` | none | side B's `[Graphics]` values, e.g. `rt_ao=1`, `rt_shadows=0`, `rt_exposure=1.5` - the viewer's Compare panel lists the usual ones (A from the ini, B typed) and takes any other |

How: the first viewport is drawn twice in the frame - the level editor's split views' mechanism (`ext::view_count`) - the
left part with A's settings, the right with B's. Each view's camera is the whole viewport's projection cropped to its
part (`ext::ViewPass::crop`, `rt::crop_clip_space`), so every pixel is where an unsplit frame has it, and each effect
sees only its side's rectangle (`rt::ViewDrawn::rect`). The effects keep their slots and histories per rectangle (the
occlusion, the bounce light, the fog, the sky and its clouds, the sun's rays, the reflections), so the temporal ones
keep a history per side; the settings are swapped between the two views without starting any history again
(`rt::settings_swap`; `graphics_options::compare_*`, Vulkan's in `app/renderer_choice.cpp`). The sky's match to the
level's own sky is made once, with the side drawn when it was due.

What can't differ per side is drawn as `[Graphics]` has it on both, and named in the Compare panel: the renderer,
`ray_tracing`, `rt_hdr`, the upscaler (off while the split is on: both sides at the native resolution - it upscales a
whole view), `rt_tessellation`, the relit mode (it changes how the world itself is drawn and keeps a cache), `msaa`,
`anisotropy` and the texture sets. A `[Compare]` key among them is logged and left out. Direct3D 9 has no effects to
split: both sides are the same there. An effect that B turns on and A has off needs the depth targets made samplable for it:
with `compare_split=1` from the start they are (`rt::depth_sampling_wanted`); turned on later in the game, only once the
targets are made again (a window resize, a restart). The objects' own shadows (the skater's projected one, the peds'
blobs) are shown or hidden once a frame, by the game's logic: as side A's `rt_shadows` / `rt_shadow_objects` say, on
both sides. The world is drawn twice, so a frame costs about twice as much. The effects that blur or look at neighbours
(the bloom, the shafts, the denoisers, the reflections' blur) stop at the divider instead of reaching across it, and
those that look at their whole view (the sky's and the fog's light, the bounce) see only their side.

Measured (the demo above, `-demo-capture` every 100 frames, 9 pictures, the halves 12 pixels from the divider left out;
mean |difference| per channel, 0 - 255, and the share of pixels more than 24 apart):

| Effects | A, B | split left vs A only | split right vs B only | A only vs B only (left / right) |
|---|---|---|---|---|
| only the occlusion and the shadows | A: rt_ao=0 rt_shadows=0; B: both 1 | 0.03 (0.1%) | 0.50 (1.6%) | 8.2 / 4.6 |
| every effect on (GI, fog, sun, bloom, shafts, sky, clouds, reflections, relit) | the same | 0.13 (0.2%) | 1.28 (1.6%) | 8.1 / 4.3 |

(Runs with a frame not drawn - "the capture is a frame behind" in the log - were taken again: a picture a frame off
isn't comparable. Two A-only runs with every other effect on were identical.) Every split run played identically to the recording.
A demo saved as a video with the split on shows it, its divider and labels included.

What was measured (2026-11-13, workspace THUG2-7, x64 RelWithDebInfo, Vulkan at 1280x720 with ray tracing and every
`rt_*` effect on, MSAA 8; the demo `viewtest`: BO, 13.8 s, 828 frames, recorded with `-level BO -record` and the
automation channel's presses):

| Run | Drift against the recording |
|---|---|
| plain `-demo viewtest` | identical (822 check points) |
| the viewer straight through | identical (822) |
| the viewer paused 3 s at frame 123, then played on | identical (822) |
| the viewer with the free and orbit cameras, 0.25x, frame steps, 2x - from a seek back | as the next row |
| after a seek back (to 300, to 600, then forward to 700, back to the start; every pass) | the same as each other, and the same as the recording until frame 784 (13 s in), where the standing skater pushes off 8 frames later |
| a plain `-demo viewtest` run to its end, QUIT to the main menu, `demo play viewtest` again - no viewer | the same: frame 784, 8 frames later |

So the cameras, pausing, slow motion, fast play and the split comparison don't touch the run, and a seek back plays as a
second playback in one session does. `viewtest` was made before the `GLOB` / `RAND` chunks: what parted it is below.

### What a session leaves behind

A second playback in one session parted from the recording because the session before left state that the level's
scripts read and a level load doesn't reset. Found by tracing every draw of the game's random numbers (the call stack
of each) and the scripts and objects frame by frame, first playback against second (2026-11-13):

- **The music's shuffles.** A new track is chosen when the last one ends - in real time - and the shuffle drew 4,000 of
  the game's random numbers (the session's first level: at its load). Every later number moved with it: in `viewtest` the
  pedestrian preselection then drew 266 numbers instead of 386, a script's random wait came out different and the
  braking skater's idles were other ones, so it pushed off 8 frames later at frame 784. Now while a demo records or plays
  the music draws from streams of its own (`audio::set_music_random_apart`); a recording notes it (`RAND`) and its
  playback does the same. The game's own draws are untouched.
- **RandomNoRepeat / RandomPermute.** Their state (the last choice, the shuffled order) is kept per script site and
  outlives the script that used it: a level's scripts found the state of the last pass's. A demo's start drops them all
  (`script::forget_all_stored_randoms`, recording and playback alike).
- **Script globals.** `Car_CheckForSkater_Pause` and `Car_CheckForOtherCar_Pause` count on with each car a level makes
  (1, 5, 9...: they stagger the cars' checks), and a car stopped for the skater in one pass and not in the next (a car's
  frantic stop at frame 440 of a 33 s demo; its playbacks alternated). The first level of a session also changes a few
  others (`DrumPartialNumber`, `select_shift`, the music's temporary volume). A recording keeps every global with a
  simple value (integers, floats, names: about 1,560) as they were at its start (`GLOB`); its playback puts back those
  that differ and logs them ("demo: N script global(s) put back as recorded").

Measured (THUG2-7, the same build and settings; `drift2`: BO, 32 s, 1934 frames - pushes, an ollie, a grind try, a
crouch, a pause, a long stand braking, a push off, a square - recorded with the automation channel's presses):

| Run | Drift against the recording |
|---|---|
| `-demo drift2`, then QUIT to the main menu and `demo play drift2` three times in the same session | identical, every pass (1928 check points; the skater's fields identical to the last digit) |
| `-demo drift2`, then `demo play drift2` from inside the level twice | identical, every pass |
| the viewer: straight through, then seek back to 300, to 0, to 600, playing to the end each time | identical, every pass (1928 check points; seeks back took 0.5 - 2.3 s) |
| `viewtest` (made before the chunks): first playback, then a second | identical; parts (frame 727) - as before, record it again |

Seek speed, with all of those effects on: forward 100 frames in 0.04 - 0.06 s (1700 - 2300 frames a second, 28 - 39x
real time); back to the start 0.8 s (the main menu and the level loaded again), to frame 300 4.4 s, to frame 600 2.2 s.
SAVE AS VIDEO of frames 100 - 500 with a keyframed path and the split on: 401 frames at 60 a second (thug2_video_check),
the run identical to the recording.

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

- **A second playback in one session** (a demo played to its end, QUIT, played again - and every pass of the viewer
  after a seek back) is exact for a demo made since 2026-11-13 ("What a session leaves behind" above). A demo without
  the `GLOB` and `RAND` chunks (made before) is exact only as the first playback after the game starts: its music drew
  from the game's random numbers and its globals weren't kept. Record it again.
- What a session leaves that isn't a simple script global, a stored random or the music (a structure or array global
  changed by a script, the career's flags, a goal's state) isn't put back: a level that reads one could still part on a
  second playback - the drift check shows where.
- **Restarting in a level (F6, RECORD DEMO, `demo record`).** Measured 2026-11-13 with the clocks put back (the `CLCK`
  chunk): demos recorded in a level after 30 s of skating (BO, foun) play back identically from the command line and
  from the pause menu (drift 0 over 1371 check points). The earlier report of an in-level recording parting after a
  bail was made before the clocks were recorded; a demo without `CLCK` (made before then) still parts when it is played
  from anywhere but a fresh start (seen: an ollie from a held X lost - the button's press time came from the run before).
  Record again to get one that plays the same from the menu.
- A frame one run didn't draw (no camera noted) isn't counted as a difference: only the skater is compared there.
- While a demo records or plays the pause menu freezes the game as the original did, whatever `[features] live_pause`
  says (`src/game/front_end/live_pause.h`): a run recorded with one pause and played back with the other would part.
- Keys our own tools read directly aren't in the demo (F7 BMX, F8 restart tester, F9 free camera, the console, mouse
  clicks on menus): using them while recording makes the playback differ.
- Only port 0 is recorded (single player).
- A playback puts the recorded skater back in memory only (not saved), and only where the current one differs.
- The level's files must be the same as when it was recorded (a re-converted ported level can play differently).

## The file (`.demo`, version 1)

Little-endian: `THUG2DEM`, u32 version, then chunks `{4-character tag, u32 size, body}`. A reader skips chunks it
doesn't know and refuses a newer version. `INFO` (names, seeds, the frame the level was up, the random state there),
`SETS` (settings), `PROF` / `DETL` (the skater), `INPT` (port 0's 32 bytes a frame, run-length coded), `CHEK` (the
check points, 84 bytes each), `CLCK` (the clocks when the level was up; optional), `RAND` (u32 flags: bit 0, the
music's random numbers apart; optional), `GLOB` (u32 count, then each simple script global at the recording's start:
u32 name, u8 kind - 1 integer, 2 float, 13 name -, u32 value; optional). `formats/demo_format.h` has the
fields; `thug2_format_check` round-trips it and checks a later chunk is skipped and a newer or broken file refused.
