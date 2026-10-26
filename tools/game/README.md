# tools/pipeline/game - running the game for tests

Scripts for testing development builds and converted levels in the game. Test runs go to workspace copies of an
install (C:\Games\THUG2-1 ... THUG2-4, made by make_workspace.ps1), not the install you play. Every script takes
`-Game <folder>` (default C:\Games\THUG2 - a stock install: the scripts step into its Game folder; a folder with the
game at the top works too; tools/pipeline/game/game_exe.ps1's Resolve-GameFolder - the
TypeScript tools' --game / --thug2 the same, tools/pipeline/lib/install.ts).

- `make_workspace.ps1 -Number <n>` - makes or refreshes C:\Games\THUG2-<n> as a mirror of the main install (1280x720
  window, allow_multiple_instances=1, debug_collision=0). Refuses while its lock is held or a game runs. Refresh a
  workspace before giving it to a new agent, so it starts from main.
- `game_lock.ps1 -Action acquire|release|status -Owner <name> -Game <folder>` - one game per folder. Acquire waits
  (default 8 min; prints BUSY and exits 1 if still held), refuses while a game from that folder runs, and backs up
  the deployed dinput8.dll and thug2-decomp.ini into `<folder>\.game-lock`; release puts them back - in a workspace it
  first closes a game still running from that folder (crashed, stuck or left open; recorded - "Crashes" below), in the
  main install it never closes one. Under the lock you may copy your build's DLL in, edit the ini, launch and close the game you started. To
  install something permanently (the lead installing main), also copy it over the `.backup` file before releasing.
- **Workspace games are muted:** `game_lock.ps1` acquire, `run_game.ps1` (so load_check and the scenic captures),
  `edit_level.ts` and `try_spot.ts` set `[features] mute_audio=1` in a workspace's ini (C:\Games\THUG2-n only -
  game_exe.ps1 Set-WorkspaceMuted / workspace_ini.ts; never the main install's). The user is at the machine.
- **Faster start-up (self-tests):** workspaces skip the early self-tests in the game (`[selftests] early=0`, set by
  make_workspace.ps1 - thug2_check already runs them offline against both exes). `run_game.ps1 -Tests "anim,coll"` runs
  only the self-tests whose names contain one of those words (the work being tested); `-FullTests` runs every stage and
  test (a range's final run). In the ini: `[selftests] filter=` does what -Tests does.
- `run_game.ps1 -Game <folder> -Seconds N [-Keys "down,enter,shot,..."] [-Shot name] [-ShotDir dir]` - starts the game,
  waits, takes a picture of its window, sends the keys, closes the game it started (refuses if one is already running
  from that folder). In a workspace it never takes the foreground: keys are posted to the window (KEYS_POST +
  KEYS_NO_FOCUS) and pictures taken with PrintWindow, so it runs while the user uses the PC. Pictures go to
  %TEMP%\thug2-shots unless -ShotDir. Don't pipe it into `Select-Object -First` (that stops it before it closes the game).
- **Commands instead of keys (prefer these):** the DLL's test-automation channel (src/game/front/automation.h; on in
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
    the trick object manager's LogTrickObjects; the result says how many objects / clusters and changed hands: THUG2's
    own change mask crashed past 128), `echo <text>`, `quit`. A failure or refusal
    skips the rest of the batch.
  - **Looking at one spot frame by frame** (src/game/front/test_view.h, src/game/sys/screenshot.h StartSeries):
    `teleport <x> <y> <z> [heading]` puts the skater there standing (walking, no momentum, on the ground below;
    heading in degrees as the screenshot sidecar gives it), `camera <x> <y> <z> <dx> <dy> <dz> [<vx> <vy> <vz>]` holds
    viewport 0's camera at that pose (a screenshot sidecar's [camera] lines), moved by (vx, vy, vz) every game frame
    when given (`camera off` lets go), and `capture <frames> [folder]` saves the next 1-600 frames, every one, as
    `Screenshots\<folder>\frame_0001.png ...` with one `series.txt` (the sidecar, and each frame's game frame / vblank /
    clock). While a series runs the main loop doesn't count the capture's time, so no frame is skipped: the series is
    the game's frames 1/60 s of game time apart. Script command too: `TeleportSkaterStanding pos=(x, y, z) heading=f`.
    E.g. the user's screenshot spot, still and drifting sideways:
    `-Level foun -Commands "waitfor level foun 150;teleport -4087.3 -390 2386.6 -177.3;camera -4080.9 -293.7 2523.3 -0.044 -0.311 -0.949;wait 60;capture 120 still;camera -4080.9 -293.7 2523.3 -0.044 -0.311 -0.949 0.05 0 0;capture 120 side"`.
  - **Following the skater:** `trace <frames>` writes `<folder>\thug2-decomp-trace.txt` - a line every game frame
    for the next 1-36000 frames: frame, position, velocity, the up row's y, the SkaterState state (0 ground, 1 air,
    2 wall, 3 lip, 4 rail, 5 wallplant) and physics (0 skating, 1 walking), bailing, doing a trick, the combo so far
    and its multiplier, the score's +0x20, and the last ground face's flags and collision object; each `teleport`
    while it runs adds a `# teleport ...` line. It's done at once and runs on under the next commands, so
    `teleport <x> <y> <z> <heading> board;trace 600;press up 60;wait 300` pushes off and records the ride (a manual:
    `press up 2;press down 2`; `run client_toggle_cheat cheat_flag=CHEAT_PERFECT_MANUAL on` holds its balance).
    `node tools/pipeline/game/trace_summary.ts <trace.txt> [--every N | --trials]` gives the runs of one state (where, the
    highest point, the top speed), the frames where the skater bounced off something, stopped, bailed, ended or lost
    a combo, was in the air for 3 frames or less, or crossed onto another collision object; `--trials` a line per
    ride between teleports (riding at quarter pipes).
  - `node tools/pipeline/game/frame_diff.ts <series folder> [--threshold N] [--region name:x,y,w,h]... [--heatmap out.png]`:
    how much a series flickers - per pixel the frame-to-frame changes over the threshold (summed |dRGB|, default 24),
    the share of pixels that flicker (>= 2% of the steps) per region, the mean difference, and a heat map. With the
    camera still only animation changes pixels; with a slow drift surfaces change a little and z-fighting / crawling
    ones a lot.
  - Status: frames, ready, main_menu, where (front_end / level), paused, loading, level, game_mode, focus (the focused
    elements' ids), selected (the focused menus' items), lines_read / lines_done, current, last, result, errors,
    refusals. Unknown ids show as 0x<hex>: name them in a `waitfor` once and they're shown by name.
  - **Never online:** `run` refuses the network / online scripts (every script of the game's net files, and names with
    net / online / internet / lobby / login / buddy / homie / gamespy / network / server / join / host / lan), `choose` /
    `focus` / `press` refuse items whose ids or event handlers lead there (MULTIPLAYER, Network / Online Play...), and
    nothing but wait / echo / quit runs while the game is online or in a network game mode. The tables are generated:
    `node tools/pipeline/game/make_automation_tables.ts` (reads the game's loose scripts) -> src/game/front/automation_tables.inc.
  - With the channel on, the game also keeps drawing while its window is in the background (PrintWindow pictures show
    the current frame; without it the main loop stops drawing when the window loses the focus).
- `keys.ps1 -ProcessId <pid> -Keys ...` - the key sender run_game uses (scan codes posted to the window: SendInput
  doesn't reach SDL). Keys: down, up, left, right, enter, esc, `pause` (4 s), `shot`, `hold:<key>:<ms>`,
  `now:<key>`, `ctrl:<key>` (the mouse commands only work with the game in front - not in workspaces).
- `window_shot.ps1 -ProcessId <pid> -Path <png>` - one game window with PrintWindow, also in the background.
  `game_shot.ps1 -Path <png>` - the game's window while it's in front. **Never capture the whole screen.**
- `run_game.ps1 ... -Level <name>` - the game started with `-level <name>`: straight into free skate in that level
  (`foun`, `z_dt`, `BO`, `Load_TR`...; src/game/front/start_level.h - also `[features] start_level=` in the
  ini). No keys needed, the overlay untouched. Keys: F8 restart tester; with `debug_mode=1` F9-F12, Home, Tab, T
  (src/game/sys/debug_mode.h).
- `node tools/pipeline/game/edit_level.ts [<level>] [--game <folder>] [--list]` - for the user: no level lists the installed
  levels by game (load names and names); a level starts the game with `-edit <level>` (free skate there, intro movies
  skipped, the level editor opened once it's up - also `[features] edit_level=`). Refuses while a game runs from that
  folder; never closes one. Not for agents' test runs (no lock, no workspace handling).
- `node tools/pipeline/game/try_spot.ts <level> <x,y,z[,heading]> [--game <folder>]` - for the user: the game started with
  `-level <level> -at <spot>`: free skate there, and once the level is up the skater stands still at the spot (on the
  ground below it, facing `heading` degrees: 0 = +z, 90 = +x; src/game/front/start_level.h - also
  `[features] start_at=`). `--game` goes before a spot that starts with a minus. `--list collfix` prints the spots in
  `spots.json`'s list `collfix` (in front of THUGPRO's collision overlays in the THPS3 ports)
  with each one's command; `--list collfix <n>` starts the n-th. Refuses while a game
  runs from that folder. In a workspace run, `run_game.ps1 -Level` plus the automation channel's `teleport` does the
  same.
- `node tools/pipeline/game/apply_layout.ts <layout.txt> | --level <name> [--game <folder>] [--out <folder>] [--dry-run]
  [--install [--allow-main]]` - bakes a level editor layout into a copy of the level's .scn.xbx / .col.xbx / .qb
  (default out\baked\<level>; `--install` writes the game folder's overlay - a workspace; the main install only with
  `--allow-main`, the user's own runs). Lists what isn't baked (level properties, trigger records) and every edit that
  breaks THUGPRO online parity. Header of apply_layout.ts has the record-by-record meaning.
  `--install` first backs up every file it replaces into `<game>\thug2-decomp-bake-backups\<level>\<time>`;
  `--uninstall --level <name>` puts the newest backup back (deleting files that install made) and removes it, so
  repeating it undoes the installs one by one (tools/pipeline/game/bake_backup.ts).
- `node tools/pipeline/game/boot_level.ts <level> <folder>` - TEST ONLY, superseded by `-Level`: patches the installed overlay's SKATE NOW to
  load a level in free skate. `make_menu_level.ts --game <folder>` puts the real one back.
- `load_check.ps1 -Game <folder> -Levels a, b, ...` - run_game -Level for each level in turn, then reads the log:
  `<level> : running after 50 s; responding=True loaded=True CComponent=<in use>/peak <peak>/<count> CStruct=...` or
  the errors (the pools: the log's `script pools in use in <level>` line, written once the level is up -
  src/game/front/start_level.cpp; and any `pool ... full` / `80% full` line).

Reference sources the agents read (re-fetch anonymously if needed; none of their code is in the repo except where
THIRD_PARTY_NOTICES.md - this repository's and the decomp's - say): https://github.com/Morten1337/thps-formats (MIT), https://github.com/denetii/io_thps_scene
(no licence: documentation only), https://github.com/Morten1337/thugpro-tools (public; looked at, nothing used).

After every run read `<folder>\thug2-decomp.log` (locked while the game runs): replacements installed, self-test
stages passed, no EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT REPORT / INVALID PARAMETER / PURE VIRTUAL CALL /
ABORT / BUFFER OVERRUN DETECTED / GAME CRT MESSAGE.

## Crashes
- **The DLL never shows a dialog** (src/hook/crash.h): every fault - an exception, a Debug run-time check or CRT
  error, an invalid CRT parameter, a pure virtual call, abort(), the game's own /GS check ("Buffer overrun detected!"
  from its VC7.1 CRT) or any other box of the game's CRT - is logged with the call stack / return addresses and ends
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
  (the last report of any kind), record it in your pending doc, carry on.
- **A dump** (`%LOCALAPPDATA%\CrashDumps\THUG2*.dmp`, written by WER when LocalDumps is on - a crash from before the
  DLL logged it, or a fast fail): `node tools/pipeline/game/read_dump.ts <dmp> [--image <build>\dinput8.dll] [--threads]` gives
  the exception, registers and return addresses (checked against the module files: the exe and, with --image, the
  DLL whose PE timestamp the dump names - each agent's build dir has one; `?` = not checked); save it to a file and
  `node tools/symbolize.ts --pdb-dir <that build> <file>` names our functions.
- **Testing the handling:** the automation command `crash <kind>` (workspaces only): access, rtc, crterror,
  invalidparam, purecall, abort, overrun, crtbox, fastfail, hang. thug2_check runs eight of them offline in child
  processes ("crash handling: ...").

Anything a player has to judge (does a script fire when skated into, does a menu feel right) isn't verified with
scripted keys: hand it to the user with a list of what to try.

## install_manifest.ts - which version of the game is this?
`node tools/pipeline/game/install_manifest.ts make` writes `notes/data/install_manifest.json` (the private notes repo): the size and SHA-256 of every file
of the reference install (made from C:\Games\THUG2 when it held the reference build; our files,
Backup, Save and Faces left out). `... check --game <folder>`
compares another install with it - identical / changed / missing / extra, by kind (exe, partymod, game) - and names
the executables by hash: the reference build (the one the DLL's addresses are for), or the retail
THUG2.exe most installs have (the DLL can't run on it). A stock install's Game subfolder is found by itself. Exit code 1 when anything differs or is missing.

## exe_audit.ts - do two exes differ where our code is?
`node tools/pipeline/game/exe_audit.ts [--a <the reference build>] [--b <THUG2.exe>]`: every differing byte between two same-layout
builds, the function or data it's in, which rewritten functions' original code differs, and which functions refer to
the differing data. (The reference build vs THUG2.exe, 2026-10-20: 172 bytes in 35 runs; 4 rewritten
functions touched - SetScreenBlur, WinMain, ServiceTask, LoadFromMemoryCard - all replaced by our source.)
