# tools/game - running the game for tests

Scripts for testing builds of the game (thug2-decomp.exe, built from the decomp's source and copied into a game
folder with its DLLs - the decomp's README, "Running") and converted levels in it. Test runs go to workspace copies of an
install (C:\Games\THUG2-1 ... THUG2-4, made by make_workspace.ps1), not the install you play. Every script takes
`-Game <folder>` (default C:\Games\THUG2 - a stock install: the scripts step into its Game folder; a folder with the
game at the top works too; tools/game/game_exe.ps1's Resolve-GameFolder - the
TypeScript tools' --game / --thug2 the same, thug2-level-converter-core/lib/install.ts).

- `make_workspace.ps1 -Number <n>` - makes or refreshes C:\Games\THUG2-<n> as a mirror of the main install (1280x720
  window, allow_multiple_instances=1, debug_collision=0). Refuses while its lock is held or a game runs. Refresh a
  workspace before giving it to a new agent, so it starts from main.
- `game_lock.ps1 -Action acquire|release|status -Owner <name> -Game <folder>` - one game per folder. Acquire waits
  (default 8 min; prints BUSY and exits 1 if still held), refuses while a game from that folder runs, and backs up
  the deployed thug2-decomp.exe (its PDB, d3dx9_43.dll, an earlier build's dinput8.dll - those that are there) and
  thug2-decomp.ini into `<folder>\.game-lock`; release puts them back as they were - in a workspace it
  first closes a game still running from that folder (crashed, stuck or left open; recorded - "Crashes" below) and
  waits for it to go (`-ExitWaitSeconds`, default 60, then a kill), in the main install it never closes one; while a
  game from the folder still runs it refuses (exit 1: the lock kept, nothing put back - release again once it has
  exited). Under the lock you may copy your build's exe (with its PDB and d3dx9_43.dll) in - or run
  `deploy_build.ps1`, below - edit the ini, launch and close the game you started. To
  install something permanently (the lead installing main), also copy it over the `.backup` file before releasing.
- `deploy_build.ps1 -Game <folder> -Arch x86|x64 [-BuildDir <cmake build folder>] [-EditorTools <folder> | -NoEditorTools]` - puts a build into a game folder
  under its lock (refused without the lock, or while a game runs from it): thug2-decomp.exe, its PDB, the editor, and the
  DLLs the build copied beside them (d3dx9_43.dll, SDL3.dll, FFmpeg's four, gamecontrollerdb.txt) - those the build has -
  and the editor's tools folder `editor-tools\` (Import Level's Node, converters and the retail-limits DLL: `node tools/editor_tools.ts` makes
  it in `out\editor-tools`, run first unless `-EditorTools` names one; copied when its bundle.json differs; the lock
  backs the folder up whole - `$DeployedFolders`).
  **A 32-bit and a 64-bit build have the same file names, so a build goes IN PLACE of the one in the folder** (the
  folder is never both); the lock backs the same file list up (game_exe.ps1 `$DeployedFiles`), so `release` brings the
  folder back as acquired, whichever machine was deployed. Every exe / DLL is checked to be `-Arch`'s machine (default
  build folder `out\build\<Arch>-debug`). It warns when FFmpeg's DLLs for the build's machine
  (`node tools/ffmpeg/build_ffmpeg.ts --arch x86|x64`) aren't in the build folder. `run_game.ps1 -Arch x86|x64` (and
  load_check.ps1) starts nothing when the folder's exe is the other machine; without it either runs (a 64-bit one is
  announced). The TypeScript launchers (edit_level.ts, try_spot.ts...) start whatever thug2-decomp.exe is there.
- **Workspace games are muted:** `game_lock.ps1` acquire, `run_game.ps1` (so load_check and the scenic captures),
  `edit_level.ts` and `try_spot.ts` set `[features] mute_audio=1` in a workspace's ini (C:\Games\THUG2-n only -
  game_exe.ps1 Set-WorkspaceMuted / workspace_ini.ts; never the main install's). The user is at the machine.
- `run_game.ps1 -Game <folder> -Seconds N [-Keys "down,enter,shot,..."] [-Shot name] [-ShotDir dir]` - starts the game
  (the folder's thug2-decomp.exe),
  waits, takes a picture of its window, sends the keys, closes the game it started (refuses if one is already running
  from that folder). In a workspace it never takes the foreground: keys are posted to the window (KEYS_POST +
  KEYS_NO_FOCUS) and pictures taken with PrintWindow, so it runs while the user uses the PC. Pictures go to
  %TEMP%\thug2-shots unless -ShotDir. Don't pipe it into `Select-Object -First` (that stops it before it closes the game).
  `-Renderer d3d9|vulkan` writes `[Graphics] renderer=` in a workspace's ini first (Vulkan is the game's default: a
  Direct3D 9 test run says `-Renderer d3d9`; load_check.ps1 passes its own `-Renderer` on).
- **Commands instead of keys (prefer these):** the game's test-automation channel (src/devtools/automation.h; on in
  the workspaces - `[features] automation=1`, set by make_workspace.ps1; never on in the main install). The game reads
  `<folder>\thug2-decomp-commands.txt` once a frame and writes `<folder>\thug2-decomp-status.json` (readable while it
  runs - the log isn't). Nothing is dropped and nothing waits a fixed time: a cold start to the free-skate level select
  takes ~8 s (posted keys: ~65 s).
  - `run_game.ps1 -Game C:\Games\THUG2-4 -Commands "waitfor ready","choose menu_singlesess","choose select_skater_hmenu","choose ss_play_level","waitfor focus level_menu"`
    writes the commands before the game starts, waits until they're done (`-Seconds` is then the most it waits), takes
    the picture, closes the game. With `powershell -File` an array arrives as one string: separate commands with `;`.
    `-WaitReady`: wait for the main menu instead of `-Seconds`. `-Run <script>`: the game started with `-run <script>`.
    `-Keys` still works (after the commands).
  - `game_cmd.ps1 -Game <folder> -Commands ... [-Timeout 120] [-Shot <png>]` - more commands to a game that runs (one
    you started, under the lock); prints the result and the status. `-Status` prints only the status. Exit code 0 done,
    1 failed / refused, 2 timed out.
  - Commands (one a line, `#` comments, an id is a name or `0x<hex>`): `run <script> [name=value | flag]...`,
    `choose <id> [s]` (a menu item: its menu stepped there with pad_up / pad_down as the pad does, then pad_choose -
    waits for the item, the menu taking pad events, menu input unblocked and no loading screen; a menu id chooses its
    selected item), `focus <id> [s]`, `press <button> [frames]` (through Sio's control data: up down left right x/enter
    circle/esc square triangle start select l1 r1 l2 r2 l3 r3), `hold <button> [frames]` (the same, held for up to
    3600 frames without waiting: the next commands - a capture, a trace - run while it's down), `wait <frames>`, `waitfor ready | frontend | ingame |
    level <name> | menu <id> | focus <id> [s]`, `level <name>` (as -level), `teleport`, `camera`, `capture` (below),
    `logtricks [count]` (a test: one trick by skater 0 over the level's trick objects - all, or the first `count` - through
    the trick object manager's log_trick_objects; the result says how many objects / clusters and changed hands: THUG2's
    own change mask crashed past 128), `rtprobe` / `colprobe` (below), `echo <text>`, `quit`. A failure or refusal
    skips the rest of the batch. After a `quit` the scripts wait for the process to exit and report its exit code
    ("the game exited (code 0) after its quit"; ok only with 0), whichever came first - the game's last status or
    its exit.
  - **Looking at one spot frame by frame** (src/game/rider/test_view.h, src/devtools/screenshot.h start_series):
    `teleport <x> <y> <z> [heading]` puts the skater there standing (walking, no momentum, on the ground below;
    heading in degrees as the screenshot sidecar gives it), `camera <x> <y> <z> <dx> <dy> <dz> [<vx> <vy> <vz>]` holds
    viewport 0's camera at that pose (a screenshot sidecar's [camera] lines), moved by (vx, vy, vz) every game frame
    when given (`camera off` lets go), and `capture <frames> [folder]` saves the next 1-600 frames, every one, as
    `Screenshots\<folder>\frame_0001.png ...` with one `series.txt` (the sidecar, and each frame's game frame / vblank /
    clock). While a series runs the main loop doesn't count the capture's time, so no frame is skipped: the series is
    the game's frames 1/60 s of game time apart. `capture <frames> [folder] afterload` starts at the first frame drawn
    after the next level's loading screen (not the front end's) and is done at once (armed): with `-Level`, a level's
    first frame onward (a `camera` given before the load holds through it). Script command too:
    `TeleportSkaterStanding pos=(x, y, z) heading=f`.
    E.g. the user's screenshot spot, still and drifting sideways:
    `-Level foun -Commands "waitfor level foun 150;teleport -4087.3 -390 2386.6 -177.3;camera -4080.9 -293.7 2523.3 -0.044 -0.311 -0.949;wait 60;capture 120 still;camera -4080.9 -293.7 2523.3 -0.044 -0.311 -0.949 0.05 0 0;capture 120 side"`.
  - **One ray, two ways** (ray tracing's check against the game's collision): `rtprobe <x> <y> <z> <dx> <dy> <dz> [max]`
    traces the ray from (x, y, z) along (dx, dy, dz) up to `max` (default 100000) through the ray-traced scene's last
    TLAS (renderer=vulkan with `[Graphics] ray_tracing=1`; src/gpu_vulkan/vulkan_rt_scene.h) - the result is
    `hit <distance> instance <i> mesh <id> material <id> primitive <p> point <x y z>`, `miss`, or `no scene` (no ray
    tracing, or no frame built yet). `colprobe` takes the same arguments and casts the game's own collision feeler
    (static collision and movable objects, src/engine/render/probe_ray.h): `hit <distance> point <x y z> normal <x y z>
    face <f> object <checksum>` or `miss`; in a level only. Both are read from the status file's `result`.
  - **Following the skater:** `trace <frames>` writes `<folder>\thug2-decomp-trace.txt` - a line every game frame
    for the next 1-36000 frames: frame, position, velocity, the up row's y, the SkaterState state (0 ground, 1 air,
    2 wall, 3 lip, 4 rail, 5 wallplant) and physics (0 skating, 1 walking), bailing, doing a trick, the combo so far
    and its multiplier, the score's +0x20, and the last ground face's flags and collision object; each `teleport`
    while it runs adds a `# teleport ...` line. `trace <frames> objects` also writes
    `<folder>\thug2-decomp-trace-objects.txt`: each frame a line for every object with a motion or rigid body
    component (kind m / r / b both) - frame, id, position, velocity, the at row of its matrix. It's done at once and runs on under the next commands, so
    `teleport <x> <y> <z> <heading> board;trace 600;press up 60;wait 300` pushes off and records the ride (a manual:
    `press up 2;press down 2`; `run client_toggle_cheat cheat_flag=CHEAT_PERFECT_MANUAL on` holds its balance).
    `node tools/game/trace_summary.ts <trace.txt> [--every N | --trials]` gives the runs of one state (where, the
    highest point, the top speed), the frames where the skater bounced off something, stopped, bailed, ended or lost
    a combo, was in the air for 3 frames or less, or crossed onto another collision object; `--trials` a line per
    ride between teleports (riding at quarter pipes).
  - `node tools/pipeline/run.ts lib/frame_diff.ts <series folder> [--threshold N] [--region name:x,y,w,h]... [--heatmap out.png]`:
    how much a series flickers - per pixel the frame-to-frame changes over the threshold (summed |dRGB|, default 24),
    the share of pixels that flicker (>= 2% of the steps) per region, the mean difference, and a heat map. With the
    camera still only animation changes pixels; with a slow drift surfaces change a little and z-fighting / crawling
    ones a lot.
  - Status: frames, ready, main_menu, where (front_end / level), paused (the pause menu up), live_paused (the world running behind it), loading, level, game_mode, focus (the focused
    elements' ids), selected (the focused menus' items), lines_read / lines_done, current, last, result, errors,
    refusals. Unknown ids show as 0x<hex>: name them in a `waitfor` once and they're shown by name.
  - **Never online:** `run` refuses the network / online scripts (every script of the game's net files, and names with
    net / online / internet / lobby / login / buddy / homie / gamespy / network / server / join / host / lan), `choose` /
    `focus` / `press` refuse items whose ids or event handlers lead there (MULTIPLAYER, Network / Online Play...), and
    nothing but wait / echo / quit runs while the game is online or in a network game mode. The tables are generated:
    `node tools/game/make_automation_tables.ts` (reads the game's loose scripts) -> src/devtools/automation_tables.inc.
  - With the channel on, the game also keeps drawing while its window is in the background (PrintWindow pictures show
    the current frame; without it the main loop stops drawing when the window loses the focus).
- `node tools/game/renderer_parity.ts run <plan.json> --game C:\Games\THUG2-<n> --out <folder> [--runs N]
  [--renderers d3d9,vulkan] [--groups a,b] [--build <folder>] [--exp-build <folder>] [--owner <name>]
  [--set Section.key=value]... [--pictures]` - the two renderers side by side: for each run, renderer and group of the
  plan (`renderer_parity.json`: the five smoke levels' spots, the new front end's menus, the level editor, a frame-time
  run per level), the workspace's ini written with `[Graphics] renderer=` and the plan's / --set's values, the game started
  through run_game.ps1 and the group's spots captured with the automation channel (`teleport`, `camera`, `capture 1` -
  the frame the screenshot key would save) into `<out>/<renderer>/run<k>/<spot>.png`. The whole run holds the
  workspace's lock (game_lock.ps1 with `--owner`; --build's thug2-decomp.exe, .pdb and d3dx9_43.dll copied in, all put
  back at the release). Workspaces only - it refuses any other folder. Groups: `spots` (each its commands, then a
  capture), `commands` (with `capture <n> <name>` lines of their own: the front end), `window` (the window's PrintWindow
  picture at full size after `seconds` - the level editor, where the channel stops), `collect` (the .png / .ppm files a
  measuring build writes into the `{dir}` its `env` names: movie and loading-screen frames; `exp` groups run
  `--exp-build`'s exe), `frametime: <frames>` (that many frames timed from the status file, polled every 100 ms, and the
  Vulkan log's "ms a frame" line; the status counts the game's frames, not the presented ones - a measuring build's "exp-frames" lines time those). Then `compare`:
  `node tools/game/renderer_parity.ts compare <folder> [--threshold 8] [--grow 1] [--pictures]` - per picture name, each
  renderer's noise (the pixels that differ by more than the threshold between any two of its runs), the union grown by
  `--grow` pixels, and run 1 of each renderer compared outside it: the mean channel difference, the mean signed one
  (Vulkan minus Direct3D 9), the largest, the pixels over the threshold, their bounding rectangle and the three 80x80
  cells holding most of them; also the same over every pixel. `report.txt` / `report.json`; `--pictures` writes
  `sbs/<name>.png` (Direct3D 9 | Vulkan | the difference x4, the noise mask blue).
- `node tools/game/first_frame_check.ts run --game C:\Games\THUG2-<n> --out <folder> --build <x64 build folder>
  [--settings <ini>] [--levels BO,AU,z_dt,foun] [--set Graphics.key=value]...` - the first frame after a load
  (docs/design/vulkan.md "The first frame after a load"): under the workspace's lock, the build deployed, each level
  started with `-Level` and a camera held through the load, `capture 61 ... afterload`; `on` the [Graphics] of
  `--settings` (default the main install's ini, read only; the window's keys left as the workspace's), `off` the same
  with ray tracing and every `rt_` effect off. Then `compare <folder> [--threshold 12] [--grow 2]`: frame 1 against
  frame 60 outside the noise (`off`'s frame 1 / 60 and `on`'s frames 50 - 60): at most 0.5% of the pixels changed and a
  mean difference of at most 0.6; the log's new-world line before the series' first frame; the times reported.
- `node tools/game/rt_shadows_check.ts run --game C:\Games\THUG2-<n> --out <folder> --build <x64 build folder>
  [--baseline-build <folder>] [--groups foun,BO] [--runs 2] [--owner <name>] [--set Graphics.rt_shadow_strength=0.5]...
  [--expect shadows|none]` - ray-traced sun shadows (`[Graphics] rt_shadows`) in the game: under the workspace's lock,
  the x64 build deployed (deploy_build.ps1 -Arch x64), the spots of `rt_shadows.json` (renderer_parity's five levels)
  captured with `ray_tracing=1` and `rt_shadows=0` / `1` (and `--baseline-build`'s exe as the workspace's ini has it)
  into `<out>/<off|on|baseline>/run<k>/`, then compared outside each configuration's run-to-run noise. `--expect shadows`:
  the shadows only darken (at most 0.1% of the pixels lighter), at most 80% of a picture darkened and at least 0.5% in
  some spot, the darkened pixels' median brightness ratio from 1 - strength - 0.2 to 0.9 (where 1% or more is
  darkened), the sun lined up with the level's bake (over every spot, the pixels `off` shows darker - under its Otsu
  threshold - darkened at least 1.5 times as often as the brighter ones), a level whose bake shows no sun (the log's
  "rt: the baked sun: none") unchanged, the `on` logs showing the pass (or that "none") and no skip; the share of the
  darkened pixels in the picture's lower half is reported. `--expect none` (no
  acceleration structures yet): on and off the same, the pass logged as skipped. The baseline: off and baseline the
  same. `compare <folder>` does the comparison again; report.txt / report.json, exit 1 on a failure.
  `--effect ao` (both commands): ray-traced ambient occlusion (`[Graphics] rt_ao`) instead - `rt_ao=0` / `1` with
  `rt_shadows=0`; `--expect drawn` (= `shadows`): the occlusion only darkens (at most 0.1% lighter), at most 60% of a
  picture darkened and at least 0.5% somewhere, at most 0.1% darker than full occlusion leaves (1 - `rt_ao_strength` -
  0.05: a NaN or runaway value shows as black), the darkened pixels' median ratio no lower than 1 - strength - 0.2, the
  `on` logs showing "rt_ao: the first ambient occlusion pass" and no skip. After R2: `node tools/game/rt_shadows_check.ts
  run --effect ao --expect drawn --game C:\Games\THUG2-<n> --out <folder> --build <x64 build> --baseline-build <main's
  x64 build> --groups foun,BO`.
  `--effect reflections` (both commands): ray-traced reflections (`[Graphics] rt_reflections`) - `rt_reflections=0` /
  `1` with `rt_shadows=0`, `rt_ao=0`; `--expect drawn`: per spot by its group's `on` log - where the pass ran ("rt_reflections:
  the first reflection pass"), at most 60% of the picture changed (lighter or darker) and at most 0.1% turned black (a
  NaN, a missing texture); where the level has nothing reflective ("nothing reflective in the scene"), on and off the
  same; at least one group drew reflections and some spot changed by 0.1% or more; no "couldn't" line.
  `--effect hdr` (both commands): HDR rendering and tone mapping (`[Graphics] rt_hdr`) - `rt_hdr=0` / `1`, the rest of
  the ini as the workspace has it (`--set` reaches both: `--set Graphics.rt_exposure=1` changes only `on`,
  `--set Graphics.ray_tracing=1 --set Graphics.rt_shadows=1 ...` puts the ray-traced effects on float targets in both);
  `--expect drawn` (the defaults): per spot at most 0.1% of the pixels changed by more than 12 in a channel and the
  mean at most 1.0 (the shoulder takes white to ~247; a material pass above 1 no longer clipped before the next
  multiplies it changes a few pixels more), the largest and the mean reported; `--expect brighter` (exposure raised): at most 0.1% darker or turned
  black, pure-white pixels growing by at most 10 points of the picture, some spot +2 or more on average. Both: the `on`
  logs show "rt_hdr: the first tone-mapping pass", the `off` ones none; each configuration's frame times (the Vulkan
  "ms a frame" lines) reported. With `--baseline-build <main's x64 build>`, off against main's build: at most 0.1%
  changed. E.g. `node tools/game/rt_shadows_check.ts run --effect hdr --game C:\Games\THUG2-<n> --out <folder> --build
  <x64 build> --baseline-build <main's> --groups foun,z_dt,BO`.
  `--effect fog` (both commands): volumetric fog and light shafts (`[Graphics] rt_fog`) - `rt_fog=0` / `1` with
  `rt_shadows=0`, `rt_ao=0`, `rt_reflections=0` (`--set Graphics.rt_fog_mode=1` etc. for the other settings);
  `--expect drawn`: the picture's mean luma change at most 40 (the share changed is reported), at most 0.1% turned black (a NaN, garbage) and 0.5% turned white
  (the fog or the sky blown out); every `on` log shows "rt_fog: the first fog pass" (or "the level has no fog"), at
  least one group drew the fog, no "couldn't" line; the passes' device time from the logs ("rt_fog: the passes take")
  in the report.
  `--effect gi` (both commands): one-bounce indirect light (`[Graphics] rt_gi`) - `rt_gi=0` / `1` with `rt_shadows`,
  `rt_ao`, `rt_reflections` 0; `--expect drawn`: the bounce only adds light (at most 0.1% darker, 0.1% turned black, 0.1%
  lighter by more than 112 in luma - a NaN or runaway value), at most 80% of a picture brightened and at least 0.5%
  somewhere; a level whose bake shows no sun ("rt: the baked sun: none") unchanged; over every spot the baked-dark pixels
  brightened more often than the baked-bright ones (more in the shade); the `on` logs showing "rt_gi: the first indirect
  light pass" and no skip. `--set Graphics.rt_gi_rays=4` etc. try the other settings; the passes' device time is in the
  logs ("rt_gi: <x> ms a pass on the device").
  `--effect tess` (both commands): smoothed curves (`[Graphics] rt_tessellation`) - 0 / 1, the rest as the ini and --set
  have it; the plan's `qpsch` and `qpBO` groups (`--groups qpsch,qpBO`: sch's quarter pipe still and with the skater
  riding up it, fences, barriers, a taxi; BO's start, cannons, statues). `--expect drawn`: the mean luma change at most 6,
  at most 0.1% turned black (a hole) or white (the sky through a crack), some spot changed by 0.05% or more; the `on`
  logs show "rt_tessellation: on" and a "meshes smoothed" line, the `off` ones neither; the levels' smoothing totals and
  the frame times reported.
  `--effect relit` (both commands): the relit mode (`[Graphics] rt_relit` - the bake taken apart and lit again,
  engine/gfx/rt_relit.h) - 0 / 1 with `ray_tracing=1`, the other effects as the ini and --set have them; `--set` reaches
  the baseline too (main's build with the same look: `off` must be it). `--expect drawn`: the mean luma change at most
  20 and the mean (signed) within 6 levels, at most 0.1% turned black and 0.5% white; a level whose bake shows no sun
  changed at most 0.1%; the `on` logs show "rt_relit: ... meshes written" (or "the bake shows no sun"), the `off` ones
  no `rt_relit:` line; the analysis's lines and the frame times reported.
  `--effect pbr` (both commands): per-pixel lighting and surface maps (`[Graphics] rt_pbr`) - `rt_pbr=0` / `1`, the rest
  of the ini as the workspace has it with `--set`'s values (the user's look: `--set Graphics.ray_tracing=1 ...`); make
  the workspace's maps first (`node tools/pbr_maps/pbr_maps.ts make --game <workspace> --levels foun,z_dt,bo,...`,
  under its lock; `remove` after). `--expect drawn`: the mean luma change at most 25, at most 0.1% turned black and 2%
  white, some spot changed by 0.5% or more; the `on` logs showing "rt_pbr: per-pixel lighting on" and maps attached,
  the `off` ones neither; each configuration's frame times reported.
  `--effect upscale [--upscaler dlss|fsr|auto[:dlaa|quality|balanced|performance|ultra]]` (both commands): upscaling
  (`[Graphics] upscaler`) - `off` upscaler=off (the window's size, the workspace's msaa), `on` the `--upscaler` given
  (fsr:quality by default), the rest as the ini and --set have it. compare: per spot, `on` against `off` - PSNR (at
  least 20 dB) and SSIM (luma, 8 x 8 windows), at most 0.2% turned black; each configuration's frame time; the `on`
  logs must show "upscaler: the first frame upscaled by" and no failure, the `off` ones no upscaled frame.
- `node tools/game/tess_gap.ts run --game C:\Games\THUG2-<n> --build <x64 build> --level <name> --at x,y,z,heading --out
  <folder> [--set S.key=v]...` - how far the drawn surfaces are from the collision with `rt_tessellation` 0 and 1: under
  the workspace's lock, the build deployed, the level loaded twice (`ray_tracing=1`), the skater stood at --at and a fan
  of rays (every 10 degrees round, four pitches down, from 150 units over him) cast with `colprobe` and `rtprobe`; per
  ray hitting a slope (the collision's normal 0.2 - 0.97 up) on the same surface both ways, the gap along its normal
  (positive: the drawn surface behind the collision - the wheels float; negative: they sink). Fails when the smoothing
  moved a surface more than --cap (2). `report <folder>` reads the logs again. E.g. sch's quarter pipe: `--level sch
  --at 960,-440,2550,90`.
- `node tools/game/frame_bench.ts run --game C:\Games\THUG2-<n> --out <folder> [--build <x64 build>] [--settings <ini>]
  [--levels BO,AU,z_dt,sch,foun] [--seconds 30] [--runs 1] [--samples] [--no-lock] [--owner <name>] [--set S.key=v]...` -
  frame times in the game (`[debug] frame_profile`, src/platform/frame_profile.h): under the workspace's lock (`--no-lock`:
  you hold it), --build deployed (without it the folder's exe), the [Graphics] of --settings (default the main install's
  ini, read only; the window's keys the workspace's own - `--set Graphics.ResolutionX=1920` etc. for another size) and
  `--set`'s. Each level is started with `-level` and driven by the automation channel - pushing forward, an ollie and a
  turn every five seconds for --seconds - then quit (the logic is a fixed step a loop: each run is the same). Per run
  `<out>/<level>/run<k>/`: frames.jsonl (a line a loop: drawn, loading, its time, its work - less the pacing wait - and
  each phase's milliseconds), game.log, and with `--samples` samples.txt and samples-named.txt (the main thread's
  samples by function, named with the PDB: over every loop, each busy phase, and the hitches - loops whose work passed
  33 ms - alone). The report (also `report <folder>`): per level and run the drawn frames a second, their spacing (average,
  99th percentile - the "1% low" -, the worst, the hitches over 33.4 ms) and a drawn loop's work and phases, from 120
  loops after the level's loading screen. `compare <before> <after>` puts two folders' median runs side by side.
  Several games on the machine (other workspaces) share its processor and GPU: take `--runs 2` or more and compare
  medians.
- `node tools/game/rt_contract.ts run --game C:\Games\THUG2-<n> [--build <folder>] [--arch x64] [--renderers vulkan,d3d9]
  [--scenarios levels,anteara] [--owner <name>] [--out <folder>]` - ray tracing's contract checked in the game
  (src/devtools/rt_check.h, `[debug] rt_contract_check=1`): under the workspace's lock, --build deployed
  (deploy_build.ps1), then per renderer `levels` (BO with the skater pushed off and riding, then LA, SE and foun loaded in
  turn, riding in foun; the frame record dumped as JSON) and `anteara` (Anteara Mod's BO, loaded from the main menu - with `-level` its start-up never finished - riding). Each run's log and
  `rt-frame-<renderer>.json` go to --out (default `out\rt-contract`); a run fails on any `rt-check: VIOLATION`, no
  frame or draw checked, or a crash. `node tools/game/rt_contract.ts read <log>` reads one log the same way.
- `keys.ps1 -ProcessId <pid> -Keys ...` - the key sender run_game uses (scan codes posted to the window: SendInput
  doesn't reach SDL). Keys: down, up, left, right, enter, esc, `pause` (4 s), `shot`, `hold:<key>:<ms>`,
  `now:<key>`, `ctrl:<key>` (the mouse commands only work with the game in front - not in workspaces).
- `window_shot.ps1 -ProcessId <pid> -Path <png>` - one game window with PrintWindow, also in the background.
  `game_shot.ps1 -Path <png>` - the game's window while it's in front. **Never capture the whole screen.**
- **The demo viewer from a test** (docs/demos.md "The viewer"): `run_game.ps1 ... -Extra -demo,<name>,-viewer` (or
  `-Commands "console demo view <name>"` in a level), then `game_cmd.ps1 -Commands "console viewer seek 300"`,
  `"console viewer status"` (frame, paused / playing / seeking, camera, keys, the last seek's time and rate), `console
  viewer pause | play | step [n] | speed <x> | camera free | free <x> <y> <z> <yaw> <pitch> | key add | export [file] |
  exit`. While the viewer is paused the game's logic doesn't run - and with it the automation channel's own frame - so
  only `console` and `echo` lines run then (the others wait for a frame that runs; `wait <frames>` counts logic frames:
  don't wait while it's paused). A playback's drift is in `thug2-decomp-demos\<demo>.play.run.json` once it reaches the
  demo's end (each seek back starts the run's check again). `console compare on | off | set <key> <value> | status` is
  the split comparison (`[Graphics] compare_split`, `[Compare]`).
- `run_game.ps1 ... -Level <name>` - the game started with `-level <name>`: straight into free skate in that level
  (`foun`, `z_dt`, `BO`, `Load_TR`...; src/game/front_end/start_level.h - also `[features] start_level=` in the
  ini). No keys needed, the overlay untouched. Keys: F8 restart tester; with `debug_mode=1` F9-F12, Home, Tab, T
  (src/engine/sys/debug_mode.h).
- `node tools/game/edit_level.ts [<level>] [--game <folder>] [--list]` - for the user: no level lists the installed
  levels by game (load names and names); a level starts the game with `-edit <level>` (free skate there, intro movies
  skipped, the level editor opened once it's up - also `[features] edit_level=`). Refuses while a game runs from that
  folder; never closes one. Not for agents' test runs (no lock, no workspace handling).
- `node tools/game/try_spot.ts <level> <x,y,z[,heading]> [--game <folder>]` - for the user: the game started with
  `-level <level> -at <spot>`: free skate there, and once the level is up the skater stands still at the spot (on the
  ground below it, facing `heading` degrees: 0 = +z, 90 = +x; src/game/front_end/start_level.h - also
  `[features] start_at=`). `--game` goes before a spot that starts with a minus. `--list collfix` prints the spots in
  `spots.json`'s list `collfix` (in front of THUGPRO's collision overlays in the THPS3 ports)
  with each one's command; `--list collfix <n>` starts the n-th. Refuses while a game
  runs from that folder. In a workspace run, `run_game.ps1 -Level` plus the automation channel's `teleport` does the
  same.
- `node tools/game/apply_layout.ts <layout.txt> | --level <name> [--game <folder>] [--out <folder>] [--dry-run]
  [--install [--allow-main]]` - bakes a level editor layout into a copy of the level's .scn.xbx / .col.xbx / .qb
  (default out\baked\<level>; `--install` writes the game folder's overlay - a workspace; the main install only with
  `--allow-main`, the user's own runs). Lists what isn't baked (level properties, trigger records) and every edit that
  breaks THUGPRO online parity. Header of apply_layout.ts has the record-by-record meaning.
  `--install` first backs up every file it replaces into `<game>\thug2-decomp-bake-backups\<level>\<time>`;
  `--uninstall --level <name>` puts the newest backup back (deleting files that install made) and removes it, so
  repeating it undoes the installs one by one (tools/game/bake_backup.ts).
- `node tools/game/boot_level.ts <level> <folder>` - TEST ONLY, superseded by `-Level`: patches the installed overlay's SKATE NOW to
  load a level in free skate. `make_menu_level.ts --game <folder>` puts the real one back.
- `load_check.ps1 -Game <folder> -Levels a, b, ...` - run_game -Level for each level in turn, then reads the log:
  `<level> : running after 50 s; responding=True loaded=True CComponent=<in use>/peak <peak>/<count> CStruct=...` or
  the errors (the pools: the log's `script pools in use in <level>` line, written once the level is up -
  src/game/front_end/start_level.cpp; and any `pool ... full` / `80% full` line).
- `start_level_check.ps1 -Game <folder> [-Levels BO,bo,LA] [-Anteara 0,1] [-Runs 1] [-Seconds 90]` - the game started
  with `-level <name>` for each level name (as typed: BO and bo both) and each `[features] anteara_mod` value: each run
  passes when the automation status is in that level, not loading, within -Seconds; else the log's tail is printed and
  the game closed. The scripts as the folder has them (unpacked by tools/qbjs/unpack_game.ts: set `ts_scripts=1`
  yourself). Exit code: the runs that failed.
- A game that hangs (its window stops responding, the loading screen drawn on and on):
  `python -I tools/standalone/first_fault.py <exe> <folder> --seconds 60 --stacks-at-end -- -level <name>` runs it
  under the debugger and, when the time is up, prints every thread's calls (the main thread first) - a 64-bit build.
- `node tools/game/heap_report.ts <log> [<second log>]` - the memory manager's heaps once each level was up (the log's
  `memory heap in <level>:` lines: region, use, peak and blocks of every heap, src/engine/memory/memory_report.h) as a
  table; with two logs (a 32-bit and an x64 build's) side by side - each heap's region and peak in both, the growth
  and the room left; a peak within 10% of its region is marked TIGHT.

Reference sources the agents read (re-fetch anonymously if needed; none of their code is in the repo except where
the THIRD_PARTY_NOTICES.md files - the decomp's, the content pipeline's and the converters' - say): https://github.com/Morten1337/thps-formats (MIT), https://github.com/denetii/io_thps_scene
(no licence: documentation only), https://github.com/Morten1337/thugpro-tools (public; looked at, nothing used).

After every run read `<folder>\thug2-decomp.log` (locked while the game runs): no EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT REPORT / INVALID PARAMETER / PURE VIRTUAL CALL /
ABORT / BUFFER OVERRUN DETECTED / GAME CRT MESSAGE.

## Crashes
- **The game never shows a dialog** (src/platform/crash.h): every fault - an exception, a Debug run-time check or CRT
  error, an invalid CRT parameter, a pure virtual call, abort() - is logged with the call stack / return addresses and ends
  the process (exit code 3, 0xC0000409 for an overrun, the exception's code when unhandled). Windows Error Reporting
  shows nothing. Not catchable: our own build's /GS failures (fast fail - exit code 0xC0000409, nothing logged; WER may
  still write a dump).
- **The scripts notice a game in trouble** (game_health.ps1): it exited with a code other than 0, a WerFault runs for
  it, it shows a dialog, or its window hasn't responded for `-HangSeconds` (default 45) while idle (a long load works
  the CPU and isn't counted). run_game.ps1 and game_cmd.ps1 then close it (WM_CLOSE, then a kill), print
  `GAME IN TROUBLE: ...` with the log's last lines, append it to `<folder>\thug2-decomp-crashes.txt` and exit 5;
  load_check.ps1 reports it per level and goes on; `game_lock.ps1 -Action release` closes a game still running from a
  workspace first (never in the main install).
- **Then:** read the log, `node tools/symbolize.ts --pdb-dir <the build that crashed> <folder>\thug2-decomp.log`
  (the last report of any kind; a 64-bit build's report the same way), record it in your pending doc, carry on.
- **A dump** (`%LOCALAPPDATA%\CrashDumps\THUG2*.dmp`, written by WER when LocalDumps is on - a crash from before the
  game logged it, or a fast fail): `node tools/game/read_dump.ts <dmp> [--image <build>\thug2-decomp.exe] [--threads]`
  gives the exception, registers and return addresses (checked against the module files: with --image, the build's
  exe whose PE timestamp the dump names - each agent's build dir has one; `?` = not checked); save it to a file and
  `node tools/symbolize.ts --pdb-dir <that build> <file>` names our functions.
- **Testing the handling:** the automation command `crash <kind>` (workspaces only): access, rtc, crterror,
  invalidparam, purecall, abort, fastfail, hang (overrun and crtbox went through the original's C runtime: not caused
  any more).

Anything a player has to judge (does a script fire when skated into, does a menu feel right) isn't verified with
scripted keys: hand it to the user with a list of what to try.

## install_manifest.ts - which version of the game is this?
`node tools/game/install_manifest.ts make` writes `notes/data/install_manifest.json` (the private notes repo): the size and SHA-256 of every file
of the reference install (made from C:\Games\THUG2 when it held the reference build; our files,
Backup, Save and Faces left out). `... check --game <folder>`
compares another install with it - identical / changed / missing / extra, by kind (exe, partymod, game) - and names
the executables by hash: the reference build (the one the source's addresses are for), or the retail
THUG2.exe most installs have (the game built from source needs only the data files). A stock install's Game subfolder is found by itself. Exit code 1 when anything differs or is missing.
