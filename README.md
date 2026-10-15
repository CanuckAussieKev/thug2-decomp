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

## Build

Requires Visual Studio 2022 or later with "Desktop development with C++" (x86 tools + CMake).

1. In Visual Studio: **File > Open > Folder...** and pick this folder.
2. Choose the **x86 Debug** configuration in the toolbar.
3. **Build > Build All**. The DLL is copied to `C:\Games\THUG2` automatically
   (change `THUG2_GAME_DIR` in `CMakeLists.txt` or the CMake cache if your game is elsewhere).
4. Run the game (`THUG2PM.exe`) and check `thug2-decomp.log` in the game folder.

Prefer a solution file? Generate one (it lands in the git-ignored `out\` folder and regenerates itself when
`CMakeLists.txt` changes; add new source files to `CMakeLists.txt`, not the solution):

```
cmake -S . -B out/vs -G "Visual Studio 18 2026" -A Win32
```

then open `out\vs\thug2_decomp.slnx` and build **Debug | Win32**.

To uninstall, delete `dinput8.dll` from the game folder.

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
3. Add `REPLACE(0xADDRESS, Namespace::Function);` and, where practical, a self-test in `src/hook/selftest.cpp`.
4. Build, play, check the log.
