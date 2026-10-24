# thug2-decomp

A readable C++ reimplementation of Tony Hawk's Underground 2 (PC), built up one function at a time while the
game keeps running. No game files or binaries are included; you need your own copy of the game.

## Getting started

This section gets a new contributor from an empty PC to a built DLL, a running game and converted levels. The
paths below are the project's defaults; every tool takes a flag (`--thug2`, `--thps3`, `--game` ...) if yours differ.

### 1. What you need

**Games** (your own copies; nothing from them is ever committed):

| Game | Needed for | Default folder | Recognised by (in the folder or its `Game` folder) | What the tools read |
|---|---|---|---|---|
| Tony Hawk's Underground 2 (PC) | Everything | `C:\Games\THUG2` (the game in its `Game` subfolder) | `THUG2.exe` or `THUG2PM.exe`, or `Data\pre\qb_scripts.prx` | The exe, `Data\...` |
| THPS3 (PC) | Porting THPS3's levels | `C:\Games\THPS3` | `Data\Levels` | `Data\Levels\<Lvl>\` (`.bsp`, `.tdx`, `.qb`), `Data\Scripts\` |
| THPS4 (PC) | Porting THPS4's levels, the THPS4 skate-shop menu backdrop | `C:\Games\THPS4` | `data\levels` and `data\skeletons` | `data\levels\`, `data\scripts\`, `data\models\` |
| THUG (PC) | Porting THUG1's levels | `C:\Games\THUG1` | `Data\pre\qb.pre` | `Data\pre\` (`<LV>scn.pre`, `<LV>col.pre`, `<LV>.pre`, `qb.pre`) |
| THAW (PC, version 1.01) | Porting THAW's levels | `C:\Games\THAW` | `data\worlds\worldzones` or `data\pak\qb.pak.wpc` | `data\worlds\worldzones\<zone>\<zone>_net.pak.wpc`, `data\pak\qb.pak.wpc`, `data\pak\dbg.pak.wpc`, `data\skies\` |
| THUGPRO (optional) | THUGPRO's own additions to the ported levels, and online parity with its players | `%LOCALAPPDATA%\THUG Pro` | | Read only, by reference (`tools/compat/thugpro`) |

**Layout:** every folder you give a tool (`--thug2`, `--thps3`, `--thps4`, `--thug1`, `--thaw`, `--game`, `-Game`, CMake's
`THUG2_GAME_DIR`) may be the install root - with the game in a `Game` subfolder, as the stock installs of THUG2, THPS4,
THUG and THAW have it - or the game folder itself (`Game`'s contents at the top, e.g. after deleting the launcher, docs
and readme around it). The tools look for the markers in the column above in the folder itself first, then in its
`Game` folder, without regard to case, and say what they expected when neither matches (`tools/lib/install.ts`;
`tools/game/game_exe.ps1` for the PowerShell scripts, which also need the exe).

THUG2 needs an **unwrapped exe**: a no-CD `THUG2.exe`, or PartyMod's `THUG2PM.exe` (the addresses the code is
named after; both have the same layout). The retail SafeDisc-encrypted exe can't run anything. The exe is never
modified on disk. `node tools/game/install_manifest.ts check --game <folder>` tells you which one a folder has.

**Software:**

| Tool | Version | Used for |
|---|---|---|
| Visual Studio | 2026 (MSVC 14.51) with "Desktop development with C++", x86 tools, CMake and Ninja | The DLL and `thug2_check.exe` |
| Node.js | 24 or later | Every tool in `tools/` (TypeScript, run directly: `node tools/<x>.ts`) |
| Git | any | |
| NeverScript | built from `tools/neverscript/` | Compiling / decompiling QB scripts for the level converters (`tools\build_neverscript.ps1` -> `out\neverscript\ns.exe`) |
| Ghidra | 12.1.4 at `C:\ghidra` | Decompiling (project `C:\Games\THUG2-ghidra`) |
| Python + uv | Python 3.13 via uv | The Ghidra scripts in `tools/ghidra` (pyghidra) and `tools/capstone_disasm.py` |

The build downloads its libraries itself at configure time, each pinned and checked against a SHA-256: SDL2 2.30.9
(its `SDL2.dll` is copied into the game folder), MinHook 1.3.3, Dear ImGui 1.91.9b, stb, and the Inter 4.1 and
JetBrains Mono 2.304 fonts (the level editor's UI).

**Antivirus:** the self-test runner loads the game's code into its own process and patches it in memory, which
behaviour-based antivirus can flag (Bitdefender has killed builds). Add exceptions for the repo folder, the game
folders (keep build scripts inside the repo, e.g. `out\scripts`, so the repo's exception covers them).

### 2. Folder layout on disk

The paths below use the stock layout, with the game in a `Game` subfolder; if you keep the game at the install's top
level instead, drop the `Game\` part - the tools find either.

```
C:\Dev\thug2-decomp\              this repo
  out\build\...                   build folders (git-ignored); out\neverscript\ns.exe
  data\derived\                   data extracted from the exe (git-ignored; regenerate, see step 7)
C:\Games\THUG2\                   the main install: merged work only, for playing and manual testing
  Game\THUG2.exe | THUG2PM.exe    the unwrapped exe (never modified)
  Game\dinput8.dll                our build (copied in by the post-build step)
  Game\SDL2.dll                   from the build
  Game\thug2-decomp.ini           settings (see "Tracking down a problem" and CLAUDE.md for the [features] keys)
  Game\thug2-decomp.log           read it after every run
  Game\thug2-decomp-data\         the data overlay made by tools/menu_level/make_menu_level.ts (menus, ported levels)
  Game\thug2-decomp-thps3\ ...    the converters' staging folders (-thps4, -thug1, -thaw)
  Game\thug2-decomp-layouts\      level editor layouts
  Game\Screenshots\               Print Screen captures with a .txt of the game's state
C:\Games\THUG2-1 ... THUG2-4      optional workspaces: copies of the main install for test runs and parallel work
C:\Games\THPS3, THPS4, THUG1, THAW  the source games for the level ports
C:\Games\THUG2-ghidra             the Ghidra project (+ THUG2-ghidra-snapshot, a read-only copy for scripts)
```

Never write into a backup install you keep (the project's own notes use `C:\Games\THUG2_PM_BAK` and
`C:\Games\THUG2-THPSX-ORIGINAL`) or into THUGPRO's folders.

### 3. First build

```
npm install                          (once: the TypeScript tools' dependencies)
tools\build_neverscript.ps1          (once: out\neverscript\ns.exe)
tools\verify_build.cmd               (builds out\build\verify; no copy into the game)
out\build\verify\thug2_check.exe     (argument-size checks + the early self-tests, offline, against THUG2PM.exe)
out\build\verify\thug2_check.exe "C:\Games\THUG2\Game\THUG2.exe"   (the same against a stock no-CD exe)
node tools\check_code_writes.ts      (must say "No overlaps")
npm run typecheck && npm run lint && npm test
```

Or build in Visual Studio (see **Build** below), which copies the DLL into `THUG2_GAME_DIR`.

### 4. Play it

Put the build's `dinput8.dll` and `SDL2.dll` in `C:\Games\THUG2\Game`, start the game, then read
`thug2-decomp.log`. Useful start-up switches (full list in CLAUDE.md under "Where things are"):
`-level <name>` / `[features] start_level=` starts in a level; `-edit <name>` (or `node tools/game/edit_level.ts`)
opens the level editor there; `[features] skip_intro_movies=1`.

### 4a. Boot straight into a level

| How | What it does |
|---|---|
| `THUG2.exe -level <name>` or `[features] start_level=<name>` | Skips the menus and starts that level in free skate (`src/game/front/start_level.h`). `<name>` is the level's folder name (`TR`, `BO`, `foun`, `z_ma`...). |
| `THUG2.exe -edit <name>` or `[features] edit_level=<name>` | The same, skips the intro movies and opens the level editor there. |
| `THUG2.exe -level <name> -at x,y,z[,heading]` or `[features] start_at=x,y,z[,heading]` | With `-level` / `-edit`: once the level is up, the skater stands still at that spot (on the ground below it), facing `heading` (degrees, 0 = +z, 90 = +x - the screenshot sidecar's skater position and heading). |
| `node tools/game/try_spot.ts <name> <x,y,z[,heading]>` | Starts the game that way (`--game <folder>` before the spot); `--list collfix` prints the spots in front of THUGPRO's collision overlays in the THPS3 ports, each with its command (`docs/pending/collfix-spots.md`). |
| `node tools/game/edit_level.ts` | Lists every installed level by game (THUG2's own and the ported ones); `edit_level.ts <name> [--game <folder>]` starts one in the editor. A misspelt name is refused with the list. |
| `tools\game\run_game.ps1 -Game <folder> -Level <name>` | A test run in a workspace: starts the level, waits, takes a picture, closes the game. |
| `tools\game\load_check.ps1 -Game <folder> -Levels a, b, c` | Loads each level in turn and reports whether it loaded and ran without errors (under the folder's lock). |
| `run_game.ps1 -Commands "waitfor ready", "choose ..."` | The test-automation channel (workspaces only, `[features] automation=1`): menu choices, scripts and waits as commands, the game's state in `thug2-decomp-status.json`. Never online. See `tools/game/README.md`. |

### 4b. The level editor

The editor is built into the DLL: pause menu > **LEVEL EDITOR**, or start with `-edit <name>`. It's offered in free
skate and single session only (never in career / story, classic, the park editor or network games).

- **Learning it:** F1 lists every key; **Ctrl+Space** (or F3) searches every action; the status bar shows what the
  mouse and keys do at that moment; first-run tips appear once. Every key is rebindable (Preferences > Keys, saved in
  `[editor_keys]` of the ini). The full list of controls and the editor's modules is the comment at the top of
  `src/game/front/level_editor.h`; each feature's record and test checklist is a `docs/pending/editor-*.md` file.
- **Edited copies:** edits never change a shipped level. Saving (Ctrl+S) writes the level's layout,
  `Game\thug2-decomp-layouts\<scene>.txt` (plain text, one record per line - `tools/game/layout_file.ts` reads it),
  and the level select then has a separate entry, **EDITED LEVELS (OFFLINE)** > "<LEVEL> (EDITED)", that loads the
  level with the layout on top. Layouts are never applied online. Unsaved work is autosaved every minute
  (`<scene>.autosave.txt`) and offered back after a crash; Reset level puts a level back as shipped.
- **Nodes and scripts it makes:** nodes the editor adds (spawns, crowns, flags, lights, rails, ladders, paths,
  triggers, cameras, goals...) go into an extra node array it adds to the level; trigger volumes, rail speed-ups,
  jump boosts, breakables and proximity nodes get small QB scripts the editor generates and loads at run time
  (`level_editor_triggers.cpp`, `level_editor_chains.cpp`; the same scripts in TypeScript for the bake:
  `tools/game/triggers.ts`, `tools/game/map_features.ts`). Goals use THUG2's own Create-a-Goal records
  (`level_editor_goals.cpp`).
- **Baking a layout into a level's files:** `node tools/game/apply_layout.ts --level <name> [--dry-run]` writes a
  copy of the level's `.scn.xbx` / `.col.xbx` / `.qb` (and its scripts and `Levels.qb` entry) with the layout built
  in, to `out\baked\<name>` by default. `--install --game <workspace>` puts it in that folder's overlay, backing up
  what it replaces; `--uninstall --level <name>` restores the newest backup (repeat to step back further). It warns
  about every edit that would break online parity with THUGPRO.
- **Offline helpers:** `node tools/game/pick_census.ts` (what the editor's click picking makes of each piece of a
  level), `node tools/thps4_levels/migrate_layouts.ts` (moves layouts when a ported level is renamed).

### 5. Convert the other games' levels (optional)

Each converter reads your install of that game and writes a staging folder next to THUG2; the menu tool then installs
everything into the data overlay and the level select:

```
node tools\thps3_levels\thps3_levels.ts convert --thug2 C:\Games\THUG2 --thps3 C:\Games\THPS3
node tools\thps4_levels\thps4_levels.ts convert --thug2 C:\Games\THUG2 --thps4 C:\Games\THPS4
node tools\thug1_levels\thug1_levels.ts --thug2 C:\Games\THUG2 --thug1 C:\Games\THUG1 convert
node tools\thaw_levels\thaw_levels.ts   convert --thug2 C:\Games\THUG2 --thaw  C:\Games\THAW
node tools\menu_level\make_menu_level.ts --game C:\Games\THUG2
```

THPS4 must be converted before `make_menu_level.ts` for the default menu backdrop (THPS4's skate shop). Each
converter's README (`tools/<game>_levels/README.md`) lists its switches; the THUGPRO compatibility register is
explained in `tools/compat/thugpro/README.md` (`--compat none`, `--no-thugpro-new`, `--no-port-changes`).
`make_menu_level.ts` empties and rebuilds the overlay each run. Optional: the Anteara Mod (`tools/anteara/README.md`).

### 6. Test workspaces (optional, for test runs and parallel work)

`tools\game\make_workspace.ps1 -Number 1` makes `C:\Games\THUG2-1` as a copy of the main install set up for testing
(1280x720 window, several instances allowed, the test-automation channel on). Test runs go there, never into the
main install; `tools\game\game_lock.ps1` keeps two runs out of the same folder, and `tools\game\run_game.ps1` starts
a level, takes pictures and closes the game. Everything is in `tools/game/README.md`.

### 7. Decompiling

1. Create a Ghidra project for `THUG2PM.exe` at `C:\Games\THUG2-ghidra` (base 0x400000), let it analyse, then
   `powershell tools\ghidra\snapshot_project.ps1` for the read-only snapshot the scripts use.
2. Regenerate `data\derived\`: `set GHIDRA_INSTALL_DIR=C:\ghidra` then
   `uv run --no-project --python 3.13 --with pyghidra python tools\ghidra\export_functions.py C:\Games\THUG2-ghidra-snapshot data\derived\functions.json`,
   and `node tools\extract_cfuncs.ts <path to THUG2PM.exe> data\derived\cfuncs.csv` for the script command table.
3. Follow the `decomp-batch` skill (`.claude/skills/decomp-batch/`) for every batch: read the disassembly, write the
   C++, self-test original against ours, prove the tests with `node tools/mutate.ts`, run the register and call
   scans, record the batch in `docs/decomp-progress.md`.

### 8. Where to read next

| File | What it holds |
|---|---|
| `CLAUDE.md` | The project's working rules, every ini switch, facts about the binary |
| `docs/HANDOFF.md` | Work in flight and what's next |
| `docs/decomp-progress.md` / `docs/PROGRESS.md` | Every batch, and progress by subsystem |
| `docs/subsystems.md`, `docs/library_ranges.md` | The exe's map; the libraries not decompiled (D3DX9, GameSpy, CRT) |
| `docs/typescript.md` | Tool conventions |
| `docs/level-port-compromises.md` | What the level ports do differently from the originals |
| `docs/agent-rules.md` | Rules for parallel agents |
| `src/game/front/level_editor.h` | The level editor's controls and modules |

### Project rules worth knowing first

- Our code comes only from our own decompilation of the game. Never read, reference or copy leaked Neversoft /
  Activision source code.
- Morten1337's repositories (THUGPRO) may be used for insight, never copied 1:1; credit ideas in comments.
- The exe is never modified; the ported levels are generated from the original games, with THUGPRO as reference only.
- Online: tools and automation never go online; online play is tested by hand only.

## How it works

The build produces `dinput8.dll`. Placed in the game folder, Windows loads it instead of the system DirectInput
DLL, which puts our code inside the game process at startup. On load it:

1. runs self-tests that compare our functions with the originals in the exe,
2. redirects each original function we've rewritten to our version (`REPLACE(address, function)`),
3. forwards the one real DirectInput call to the system `dinput8.dll`.

Everything not yet rewritten still runs the original code, so the game works at every step.

The fixes from [PartyMod](https://github.com/PARTYMANX/partymod-thug2) v1.0.4 by PARTYMANX (an SDL2 window and
input, controller and keyboard bindings, widescreen, the OpenSpy online service and more) are part of the game's
source here, marked "Originally from PartyMod Fixes", with their settings in `thug2-decomp.ini` (sections
`[Graphics]`, `[Miscellaneous]`, `[Keybinds]`, `[Gamepad]`, `[Logging]`) - MIT, (c) 2024 PARTYMANX, see
`THIRD_PARTY_NOTICES.md`. `SDL2.dll` (2.30.9) must be in the game folder.

## Build

Requires Visual Studio 2026 (MSVC 14.51) with "Desktop development with C++" (x86 tools, CMake, Ninja). Builds are
Debug: debug-CRT reports and run-time check failures go to `thug2-decomp.log` with a call stack.

1. In Visual Studio: **File > Open > Folder...** and pick this folder.
2. Choose the **x86 Debug** configuration in the toolbar.
3. **Build > Build All**. The DLL (with SDL2.dll and gamecontrollerdb.txt) is copied to `C:\Games\THUG2\Game`
   automatically (change `THUG2_GAME_DIR` in `CMakeLists.txt` or the CMake cache if your game is elsewhere). The game
   folder needs an unwrapped exe: a no-CD `THUG2.exe` or PartyMod's `THUG2PM.exe` (the same addresses; the exe is never
   modified) - not the retail SafeDisc-encrypted one (`node tools/game/install_manifest.ts check --game <folder>` says which).
4. Run the game and check `thug2-decomp.log` in the game folder.

Prefer a solution file? Generate one (it lands in the git-ignored `out\` folder and regenerates itself when
`CMakeLists.txt` changes; add new source files to `CMakeLists.txt`, not the solution):

```
cmake -S . -B out/vs -G "Visual Studio 18 2026" -A Win32
```

then open `out\vs\thug2_decomp.slnx` and build **Debug | Win32**.

To uninstall, delete `dinput8.dll` from the game folder.

## Tracking down a problem

Create `thug2-decomp.ini` in the game folder to switch things off without rebuilding:

```ini
[replacements]
; Leave these as the original game code. Each entry matches part of a source file name (matrix.cpp, lzss),
; part of a function name (Mth::FastSin) or an exact address (0x00402640). * disables everything.
disable=

[selftests]
; 0 skips all self-tests and shadow checks.
enabled=1
; Shadow checks: for this many of the game's own calls to each REPLACE_SHADOW function, also run the
; original and compare the results (0 = off).
shadow_calls=100
```

The log (`thug2-decomp.log`) lists every replacement as `replaced` or `disabled`, the result of each
self-test stage (early, late, engine-started), any `SELFTEST FAIL` / `SELFTEST CRASH` / `SHADOW MISMATCH`,
and on a normal quit a summary of how many shadow checks ran.

**When the game crashes or misbehaves:**

1. **Read the log.** A crash is an `EXCEPTION 0xC0000005 at DINPUT8.dll+0x...` report with registers and return
   addresses; debug-CRT reports and run-time check failures (`RUN-TIME CHECK`, `DEBUG CRT`) come with a call stack.
   `pool <name> full` means one of the game's fixed pools ran out (usually a converted level that's too big).
2. **Name the functions:** `node tools/symbolize.ts --pdb-dir <build folder> C:\Games\THUG2\Game\thug2-decomp.log`
   turns the `DINPUT8.dll+0x...` lines of the last crash into function names and source lines. The PDB must come from
   the same build as the DLL that crashed (compare the DLL's timestamp); addresses in `THUG2.exe+0x...` are the
   original game's, to look up in Ghidra.
3. **Bisect:** list suspect source files, functions or addresses under `[replacements] disable=` and the game's own
   code runs instead. If the crash goes away, our version is at fault; if not, it's elsewhere (often converted data:
   the original would crash on it too).
4. **Report it with the screenshot key:** Print Screen (F12 if Windows' Snipping Tool takes Print Screen) saves a PNG
   and a `.txt` to `Game\Screenshots` with the level, mode, skater and camera positions, the mouse position (marked
   on the picture), the editor's state and the log's last lines.

**Common snags:** the build can't replace `dinput8.dll` while the game runs (close it first); Visual Studio sometimes
needs **Rebuild All** before the deployed DLL updates (check its timestamp); an antivirus may quarantine
`thug2_check.exe` or build scripts (add exceptions - see Getting started); a level that crashes on load in only one
ported game is usually a converter problem (`tools/<game>_levels`), not the DLL.

## Settings (`thug2-decomp.ini`)

The ini sits next to the exe. Every key is optional; the default applies when it's missing. 1 = on, 0 = off.

**`[features]`** - our additions

| Key | Default | Effect |
|---|---|---|
| `skip_intro_movies` | 0 | Skips the logo and intro movies at start-up |
| `widescreen_menus` | 1 | 0 brings back PartyMod's pillarboxed 4:3 front end |
| `mouse_menus` | 1 | Mouse control of the menus (`debug_mouse=1` logs each click) |
| `menu_backdrop` | 1 | THPS4's skate shop behind a vertical main menu (needs the THPS4 levels converted); 0 = THUG2's original front end |
| `level_editor` | 1 | The pause menu's LEVEL EDITOR (`editor_ui=0`: the old on-screen HUD instead of the Dear ImGui UI; `debug_editor=1` logs its picks and moves) |
| `start_level` / `edit_level` | (none) | Start straight in a level / in the level editor (as `-level` / `-edit`) |
| `start_at` | (none) | With a start level: the skater put standing at `x,y,z[,heading]` once it's up (as `-at`) |
| `screenshots` | 1 | The screenshot key; `screenshot_dir=` changes the folder (default `Screenshots` next to the exe) |
| `restart_tester` | 1 | F8 puts the skater at the level's next unvisited restart |
| `loading_console` | 1 | A panel on the loading screen: what is loading (files, scripts, log lines), the time since the load began and "last activity N s ago" (orange after 10 s with nothing new: probably a hang); 0 = the original loading screen |
| `debug_mode` | 0 | Neversoft's DEBUG OPTIONS in the pause menu, DEVKIT_ONLY levels, the viewer's free camera (F9) |
| `anteara_mod` | 0 | Plays the Anteara Mod from the overlay (`tools/anteara/README.md`) |
| `create_100_cas` | 1 | GAME OPTIONS > CREATE 100% CAS |
| `allow_multiple_instances` | 0 | Lets a second copy run (its log is `thug2-decomp-2.log`) |
| `automation` | 0 | The test-automation channel; for workspaces only, never the main install |
| `debug_rail_speed`, `debug_collision`, `debug_sprites` | 0 | Extra log lines for those systems |

**`[debug]`** - our debugging aids

| Key | Default | Effect |
|---|---|---|
| `collision_events` | 0 | Logs a `collision-event:` line each time the skater's velocity turns round on the ground (a wall bounce, rolled back by the ground's slope...): the physics path that did it, the face, probe lines down and ahead (along the way the skater was going before it), and a `collision-event-detail:` line (state, input, the deciding ray, the face's corners and open edge, the level files, the approach, the camera) - for finding broken collision in converted levels; turns on flat ground with no collision involved (the player's own) aren't logged; each also shows `collision event <n>: <reason>` on screen for 2 s, and a level's load logs `collision-event: logging on in <level>`; `node tools/game/collision_events.ts <log> [--col <level>]` groups them by spot (src/game/skater/collision_events.h) |
| `collision_event_screenshots` | 1 | With `collision_events`: a screenshot of each event's frame too (in `Screenshots`, named in the log); 0 turns them off |

**`[Graphics]`** - from PartyMod, plus `OcclusionCulling`

| Key | Default | Effect |
|---|---|---|
| `ResolutionX` / `ResolutionY` | 0 | The game's resolution; 0 = the desktop's |
| `Windowed` | 0 | Runs in a window |
| `Borderless` | 0 | A borderless window |
| `AntiAliasing` | 0 | Multisample anti-aliasing |
| `HQShadows` | 0 | High-quality shadows |
| `DistanceClipping` | 0 | Stops drawing beyond a distance (`ClippingDistance`) |
| `ClippingDistance` | 100 | That distance, 1-100 |
| `Fog` | 0 | Distance fog |
| `DisableBlur` | 1 | Keeps the motion blur off |
| `OcclusionCulling` | 1 | The original's occlusion culling; 0 draws everything in view (no distant scenery popping in) |

**`[Miscellaneous]`** - from PartyMod

| Key | Default | Effect |
|---|---|---|
| `UsePS2Controls` | 1 | PS2 controls (spine transfer, acid drop, break vert as on PS2); 0 = the Xbox layout |
| `UIControls` | 1 | PartyMod's menu-controls option |
| `PreferGlyphs` | 0 | Shows controller button glyphs even when playing with the keyboard |
| `GlyphStyle` | 1 | The button glyphs shown: 1 = PlayStation, other values = Xbox |
| `OnlineDomain` | `openspy.net` | The online service's domain (see Online play) |
| `OnlineGameName` / `OnlineGameKey` | (empty) | The online identity: empty = THUG2's own; `thugpro` = THUGPRO's lobbies (see Online play) |
| `Debug` | 0 | Opens a console window with the game's debug output |

**`[Keybinds]`**, **`[Gamepad]`**, **`[Logging]`**

| Key | Default | Effect |
|---|---|---|
| `[Keybinds]` (PartyMod's keys) | PartyMod's | Keyboard bindings; plus `Screenshot=` (an SDL key name or scan code; default Print Screen) |
| `[Gamepad] MovementStick` / `CameraStick` | left / right | Which stick moves and which turns the camera |
| `[Logging] Level` | 2 | How much goes into the log |

**`[editor]`** - remembered by the level editor; set from its toolbar, menus and Preferences window (Ctrl+,) rather
than by hand

| Key | Default | Effect |
|---|---|---|
| `families` | all | The Outliner's family chips (which node kinds show) |
| `pick` | 7 (all) | The toolbar's Pick filter: 1 nodes, 2 objects, 4 scenery |
| `overlays` | all | The Overlays menu's choices |
| `collision` | 0 | The collision view (C) |
| `triggers` | 1 | Trigger volumes shown (T) |
| `carry` | 1 | Objects and nodes standing on scenery move with it |
| `shared_collision` | 1 | Moved pieces take their faces from shared collision objects with them |
| `palette_grid` | 1 | The Add palette as a thumbnail grid (0 = a list) |
| `trail_record` | 1 | Records play-test trails |
| `fly_speed` | 800 | Camera fly speed |
| `fly_accel_ms` | 0 | Time to reach full fly speed |
| `look_pct` | 100 | Mouse-look sensitivity (%) |
| `invert_y` | 0 | Inverts mouse-look up / down |
| `gizmo_pct` | 100 | Gizmo size (%) |
| `ui_scale_pct` | 0 | UI scale (%); 0 = automatic for the window size (1080p 100 %, 1440p 125 %, 4K 200 %) |
| `autosave_s` | 60 | Seconds between autosaves |
| `snap_move` | 25 | Grid size for snapped moves |
| `snap_turn` | 15 | Angle step for snapped turns |
| `accent` | 0 | The UI's accent colour: 0 orange, 1 blue, 2 green, 3 purple, 4 red, 5 teal |
| `wasd_flies` | 0 | 1 = WASD always flies (the old way); 0 = fly while the right mouse button is held |
| `speed_by_selection` | 0 | Fly speed scaled to the selection's size |
| `pad` | 0 | Controller mode |
| `tips_seen` | 0 | The first-run tips have been shown |

**`[editor_keys]`**: rebound editor keys, written by Preferences > Keys (only the ones that differ from the defaults).

**`[memory]`** - the script pools' sizes (ours, as THUGPRO's: bigger than THUG2's so big converted levels fit; the
script heap and main memory grow to match - `src/game/core/memory_sizes.h`)

| Key | Default | THUG2's |
|---|---|---|
| `component_pool` | 184000 | 86000 |
| `struct_pool` | 49600 | 19800 |
| `vector_pool` | 19000 | 9500 |
| `pair_pool` | 1600 | 800 |
| `array_pool` | 16000 | 7000 |
| `symbol_pool` | 45000 | 12500 |
| `script_pool` | 1200 | 300 |
| `script_cache_pool` | 1200 | 300 |
| `string_heap` / `string_count` | 1 MB / 28000 | 190000 bytes / 7000 |
| `hash_item_pool` | 40000 | 10000 |
| `original_sizes` | 0 | 1 puts every pool back to THUG2's size |

**`[replacements]`** and **`[selftests]`**

| Key | Default | Effect |
|---|---|---|
| `[replacements] disable=` | (empty) | Leaves these as the original game's code: parts of source file names (`matrix.cpp`), function names (`Mth::FastSin`) or exact addresses (`0x00402640`), comma-separated; `*` disables everything |
| `[selftests] enabled` | 1 | 0 skips every self-test and shadow check |
| `[selftests] early` | 1 | 0 skips the early self-tests in the game (`thug2_check.exe` runs them offline) |
| `[selftests] shadow_calls` | 100 | For each REPLACE_SHADOW function, how many of the game's own calls also run the original to compare |
| `[selftests] filter=` | (empty) | Runs only the self-tests whose names contain one of these words |
| `[selftests] trace` | 0 | Logs each self-test as it starts (to find one that hangs) |

## Online play

Online goes through OpenSpy (`OnlineDomain=openspy.net`), PartyMod's replacement for the closed GameSpy service. By
default the game uses THUG2's own identity and lobby. `OnlineGameName=thugpro` (key left empty) joins THUGPRO's lobbies
instead; joining THUGPRO players' sessions may not work, as their scripts and DLL differ. The ported levels keep online
parity with THUGPRO's ports where both players must agree (spawns, crowns, flags, gaps, geometry they collide with):
that's what the compatibility register in `tools/compat/thugpro` records, edit by edit. The level editor's edits and
edited levels are never used online. **Online play is only ever tested by hand**: no tool, script or automation joins,
hosts or enters a lobby.

## Glossary

| Term | Meaning |
|---|---|
| QB / QB script | Neversoft's compiled script format (`.qb`); NeverScript (`tools/neverscript`) compiles and decompiles it |
| CFunction | A script command implemented in C++ (`data/derived/cfuncs.csv`: 1254 of them) |
| Node array | A level's list of nodes (`<level>_NodeArray` in `<level>.qb`): restarts, rails, triggers, objects, paths... |
| Sector / scene | A piece of a level's drawn geometry; the scene (`.scn.xbx`) holds them all |
| Collision (`.col.xbx`) | A level's collision objects (faces with flags: skatable, vert, wallride, trigger...), often separate from what's drawn |
| `.tex.xbx` | A level's textures |
| `.pre` / `.prx` | Neversoft's packed archives (LZSS) |
| Cluster / trick object | Pieces grouped for graffiti and trick-object scoring (a node's `TrickObject Cluster=`) |
| Data overlay | `Game\thug2-decomp-data`: our menus and ported levels, read by the game before its own `Data` folder |
| Edited copy | A level with a level editor layout on top, offered separately as "<LEVEL> (EDITED)"; offline only |
| REPLACE / REPLACE_SOURCE_ONLY | Our function installed over the original / written and checked but not installed |
| Self-test stages | Early (offline-capable), late, engine-started (need the running game) |
| TP-n / TPN-&lt;game&gt;-n / PC-&lt;game&gt;-n | The THUGPRO register's entries: THUGPRO's hand edits, THUGPRO's own content, our own deviations |
| Workspace | `C:\Games\THUG2-1..4`, copies of the main install for test runs |

## Contributing

- **Work in batches.** One coherent batch (10-40 functions, or one feature) per commit, following the
  `decomp-batch` skill for decompilation. Commit messages describe what changed and end with a `Co-Authored-By` line
  when an AI assistant wrote it.
- **Before a merge, all of these pass:** the build; `thug2_check.exe` against `THUG2PM.exe` and against a stock
  no-CD `THUG2.exe`; `node tools/check_code_writes.ts` ("No overlaps"); `npm run typecheck`, `npm run lint`,
  `npm test`. New C++ gets self-tests comparing it with the original, proven with `node tools/mutate.ts`.
- **Record it.** Batch records and feature notes go in `docs/pending/<name>.md` while work is in progress
  (with a numbered checklist of what to try in the game); `node tools/fold_pending.ts` folds batch records into
  `docs/decomp-progress.md`; `node tools/progress_report.ts` regenerates `docs/PROGRESS.md`.
- **Parallel work** uses git worktrees, one per task, each with its own build folder and (for game runs) its own
  workspace under `tools/game/game_lock.ps1`. Rules for that: `docs/agent-rules.md`.
- **Tools are TypeScript** (Node 24, no build step); conventions in `docs/typescript.md`. Python only for the Ghidra
  scripts.

## Project status

About 37% of the game's code (by bytes) is rewritten and running in place of the original (`docs/PROGRESS.md` has the
figure by subsystem). The game is fully playable throughout, since everything not yet rewritten still runs the
original code. The ported THPS3, THPS4, THUG1 and THAW levels, the level editor and the front-end changes are
playable now. The long-term plan: the whole game built from this source (no original exe, no DLL injection), the
libraries (D3DX9, GameSpy, CRT) linked instead of decompiled, and the QB script interpreter replaced by JavaScript
(QuickJS) with the scripts decompiled to readable JS.

## Legal and credits

You need your own copies of the games; no game files, exes or data extracted from them are in this repository, and
the level converters generate everything from your installs at conversion time. Third-party code and assets are
credited in `THIRD_PARTY_NOTICES.md`: PartyMod (MIT, PARTYMANX), Morten1337's thps-formats (MIT) and the libraries
the build downloads. Code from Morten1337's THUGPRO repositories is used with his permission for insight only - never
copied as is - and each borrowed idea is credited where it's used. Leaked Neversoft / Activision source code is never
read or used.

## Layout

| Path | Contents |
|---|---|
| `src/proxy/` | The dinput8.dll proxy and startup |
| `src/hook/` | Replacement mechanism, logging, self-tests |
| `src/game/` | Rewritten game code, by subsystem (see `docs/subsystems.md`) |
| `tools/` | TypeScript tools (level converters, menus, game runs, layouts, checks) and the Ghidra / PyGhidra scripts |
| `tools/<game>_levels/`, `tools/level_port/` | The THPS3 / THPS4 / THUG1 / THAW level converters and their shared code |
| `tools/compat/thugpro/` | The THUGPRO compatibility register |
| `tools/game/` | Running the game for tests, workspaces, the level editor's bake tool |
| `docs/` | Maps of the exe and project notes |
| `data/derived/` | Data extracted from the exe (not committed; regenerate with `tools/`) |

## Adding a function

1. Decompile it in Ghidra and check its calling convention in the disassembly.
2. Write it in the matching `src/game/<subsystem>/` file with a `// 0xADDRESS` comment.
3. Add `REPLACE(0xADDRESS, Namespace::Function);` - or `REPLACE_SHADOW(...)` if the function is pure (its
   result depends only on its arguments and it changes nothing), so the game's own calls get compared with
   the original (`src/hook/shadow.h`).
4. Where practical, add a self-test comparing it with `Original<Fn>(0xADDRESS)` and register it with
   `SELFTEST(Early|Late|EngineStarted, TestName)` for the earliest stage where what it needs exists
   (`src/hook/selftest.h`).
5. Build, play, check the log. Replacements whose argument size (from the C++ type) doesn't match the
   original's `RET n` (or whose jump can't be written) stop the game at startup with a message naming them -
   running on with the original code in their place would hide that our code isn't running. Fix them, or
   list them under `disable=` to skip them deliberately. Naked assembly stubs use `REPLACE_UNCHECKED`.
   `thug2_check.exe` (built alongside the DLL) runs that check and the early self-tests against the game's
   original code without starting the game: it loads THUG2PM.exe's image into a helper process at the game's
   own address. Exit code 1 on a mismatch or a failed test. (The late and engine-started self-tests need the
   real game.)
