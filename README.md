# thug2-decomp

A readable C++ reimplementation of Tony Hawk's Underground 2 (PC), built up one function at a time while the
game keeps running. No game files or binaries are included; you need your own copy of the game.

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

Requires Visual Studio 2022 or later with "Desktop development with C++" (x86 tools + CMake).

1. In Visual Studio: **File > Open > Folder...** and pick this folder.
2. Choose the **x86 Debug** configuration in the toolbar.
3. **Build > Build All**. The DLL (with SDL2.dll and gamecontrollerdb.txt) is copied to `C:\Games\THUG2\Game`
   automatically (change `THUG2_GAME_DIR` in `CMakeLists.txt` or the CMake cache if your game is elsewhere). The game
   folder needs an unwrapped exe: a no-CD `THUG2.exe` or PartyMod's `THUG2PM.exe` (the same addresses; the exe is never
   modified) - not the retail SafeDisc-encrypted one (`tools/game/install_manifest.py check --game <folder>` says which).
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
| `tools/` | Analysis scripts (Ghidra/PyGhidra, exe parsing) |
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
