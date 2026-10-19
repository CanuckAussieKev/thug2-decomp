# tools/game - running the game for tests

Scripts for testing builds and converted levels in the game without disturbing the user. Read CLAUDE.md first: the
game is only launched when the user allows it, C:\Games\THUG2 is the main install (merged work only, installed by the
lead; the user's own for manual testing - self-tests off, no test runs there), and parallel work uses the workspace copies C:\Games\THUG2-1 ... THUG2-4 (one agent each). Every script takes
`-Game <folder>` (default C:\Games\THUG2).

- `make_workspace.ps1 -Number <n>` - makes or refreshes C:\Games\THUG2-<n> as a mirror of the main install (1280x720
  window, allow_multiple_instances=1, debug_collision=0). Refuses while its lock is held or a game runs. Refresh a
  workspace before giving it to a new agent, so it starts from main.
- `game_lock.ps1 -Action acquire|release|status -Owner <name> -Game <folder>` - one game per folder. Acquire waits
  (default 8 min; prints BUSY and exits 1 if still held), refuses while a game from that folder runs, and backs up
  the deployed dinput8.dll and thug2-decomp.ini into `<folder>\.game-lock`; release puts them back and never closes a
  game. Under the lock you may copy your build's DLL in, edit the ini, launch and close the game you started. To
  install something permanently (the lead installing main), also copy it over the `.backup` file before releasing.
- `run_game.ps1 -Game <folder> -Seconds N [-Keys "down,enter,shot,..."] [-Shot name] [-ShotDir dir]` - starts the game,
  waits, takes a picture of its window, sends the keys, closes the game it started (refuses if one is already running
  from that folder). In a workspace it never takes the foreground: keys are posted to the window (KEYS_POST +
  KEYS_NO_FOCUS) and pictures taken with PrintWindow, so it runs while the user uses the PC. Pictures go to
  %TEMP%\thug2-shots unless -ShotDir. Don't pipe it into `Select-Object -First` (that stops it before it closes the game).
- `keys.ps1 -ProcessId <pid> -Keys ...` - the key sender run_game uses (scan codes posted to the window: SendInput
  doesn't reach SDL). Keys: down, up, left, right, enter, esc, `pause` (4 s), `shot`, `hold:<key>:<ms>`,
  `now:<key>`, `ctrl:<key>` (the mouse commands only work with the game in front - not in workspaces).
- `window_shot.ps1 -ProcessId <pid> -Path <png>` - one game window with PrintWindow, also in the background.
  `game_shot.ps1 -Path <png>` - the game's window while it's in front. **Never capture the whole screen.**
- `run_game.ps1 ... -Level <name>` - the game started with `-level <name>`: straight into free skate in that level
  (`thps3_foun`, `thaw_dt`, `BO`, `Load_TR`...; src/game/front/start_level.h - also `[features] start_level=` in the
  ini). No keys needed, the overlay untouched. Keys: F8 restart tester; with `debug_mode=1` F9-F12, Home, Tab, T
  (src/game/sys/debug_mode.h).
- `boot_level.py <level> <folder>` - TEST ONLY, superseded by `-Level`: patches the installed overlay's SKATE NOW to
  load a level in free skate. `make_menu_level.py --game <folder>` puts the real one back.
- `load_check.ps1 -Game <folder> -Levels a, b, ...` - run_game -Level for each level in turn, then reads the log:
  `<level> : running after 50 s; responding=True loaded=True` or the errors.

Reference sources the agents read (re-fetch anonymously if needed; none of their code is in the repo except where
THIRD_PARTY_NOTICES.md says): https://github.com/Morten1337/thps-formats (MIT), https://github.com/denetii/io_thps_scene
(no licence: documentation only), https://github.com/Morten1337/thugpro-tools (public; looked at, nothing used).
Morten1337's private repositories are never used (CLAUDE.md).

After every run read `<folder>\thug2-decomp.log` (locked while the game runs): replacements installed, self-test
stages passed, no EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT REPORT.

Anything a player has to judge (does a script fire when skated into, does a menu feel right) isn't verified with
scripted keys: hand it to the user with a list of what to try.
