# thug2-decomp

A readable C++ reimplementation of Tony Hawk's Underground 2 (PC), built up one function at a time while the
game keeps running. No game files or binaries are included; you need your own copy of the game.

## Getting started

This section gets a new contributor from an empty PC to a built DLL, a running game and converted levels. The
paths below are the project's defaults; every tool takes a flag (`--thug2`, `--thps3`, `--game` ...) if yours differ.

### 1. What you need

**Games** (your own copies; nothing from them is ever committed):

| Game | Needed for | Default folder | What the tools read |
|---|---|---|---|
| Tony Hawk's Underground 2 (PC) | Everything | `C:\Games\THUG2` (the game in its `Game` subfolder) | The exe, `Game\Data\...` |
| THPS3 (PC) | Porting THPS3's levels | `C:\Games\THPS3` | `Data\Levels\<Lvl>\` (`.bsp`, `.tdx`, `.qb`), `Data\Scripts\` |
| THPS4 (PC) | Porting THPS4's levels, the THPS4 skate-shop menu backdrop | `C:\Games\THPS4` | `Game\data\levels\` |
| THUG (PC) | Porting THUG1's levels | `C:\Games\THUG1` | `Game\Data\pre\` (`<LV>scn.pre`, `<LV>col.pre`, `<LV>.pre`, `qb.pre`) |
| THAW (PC, version 1.01) | Porting THAW's levels | `C:\Games\THAW` | `Game\data\worlds\worldzones\<zone>\<zone>_net.pak.wpc`, `data\pak\qb.pak.wpc`, `data\pak\dbg.pak.wpc`, `data\skies\` |
| THUGPRO (optional) | THUGPRO's own additions to the ported levels, and online parity with its players | `%LOCALAPPDATA%\THUG Pro` | Read only, by reference (`tools/compat/thugpro`) |

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
folders and your temp build-script folder.

### 2. Folder layout on disk

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
   and `node tools\extract_cfuncs.ts` for the script command table.
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
