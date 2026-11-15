# Building and checking

The README's "Build" section is the quick start (Visual Studio, `tools\verify_build.cmd`). This page is the longer
version: the command-line builds, the checks a change must pass, and the usual build problems.

## A fresh clone or worktree

```
git submodule update --init      (tools/pipeline and tools/anteara are submodules)
npm install                      (the TypeScript tools' dependencies, the level converters among them)
tools\build_neverscript.ps1      (once: out\neverscript\ns.exe)
node tools/ffmpeg/build_ffmpeg.ts   (FFmpeg's DLLs: needed for the 64-bit build)
```

## Builds

- **x86 Debug** (the reference build): `tools\verify_build.cmd` -> `out\build\verify\thug2-decomp.exe`; nothing is
  copied anywhere.
- **x64** (what the ray-traced renderer and the upscalers need), from a Developer prompt (`vcvars64.bat`):

  ```
  cmake -S . -B out\build\x64-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTHUG2_GAME_DIR=NONE
  ninja -C out\build\x64-release
  ```

  `tools\verify_build.cmd x64` makes a Debug one in `out\build\verify-x64`.
- **`THUG2_GAME_DIR`** is the game folder a build copies itself into (exe, PDBs, the editor plugin, the DLLs). Its
  default is `C:\Games\THUG2\Game`; pass `NONE` for a build that must not touch a game folder (a second working copy,
  an experiment).
- To put a build into a game folder by hand: `powershell -File tools\game\deploy_build.ps1 -Arch x64 -BuildDir
  <build folder> -Game <game folder>`.
- Memory: a full build is heavy - on a machine shared with other work, limit Ninja (`-j3`).

## Checks before a commit

1. The build.
2. `npm run typecheck`, `npm run lint`, `npm test`. `npm test` includes the repository's rule tests:
   - `tools/vulkan_headers.test.ts`: Vulkan and shader-compiler headers only in `src/gpu_vulkan` and the check
     programs it lists - a new `src/tests/vulkan_*_check.cpp` is added to its `CHECKS` set;
   - `tools/no_nolint.test.ts`: no `NOLINT` under `src/`;
   - `tools/layers.test.ts`: the layers' include rules (`docs/architecture.md`).
3. The check programs for what you changed, in the build folder: `thug2_options_check`, `thug2_daylight_check`,
   `thug2_rt_scene_check`, the Vulkan ones (`thug2_vulkan_*_check`, `thug2_ff_shader_check` - x64 with a ray-tracing GPU;
   the `thug2_ff_shader_check --surface/--water/--detail` sweeps take a lot of memory).
4. clang-tidy (the project's `.clang-tidy`) on the files you changed.

## Build problems

- **C1033 / C2471 / C1051 / LNK1201 / LNK1318** (the PDB): delete the named `vc140.pdb` and rebuild, or give the build
  its own PDB server with a unique `_MSPDBSRV_ENDPOINT_` environment variable when two builds run at once.
- **An old CMake cache** after a big change to the build: delete the build folder's `CMakeCache.txt`.
- **Antivirus**: keep build scripts and outputs inside the repository (an exception for the repo folder is enough);
  some antivirus programs stop freshly built programs run from `%TEMP%`.
