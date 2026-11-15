# Contributing to thug2-decomp

Thanks for wanting to help. This project rebuilds Tony Hawk's Underground 2 from readable C++ that runs on the
player's own copy of the game's data. A few rules keep the source clean and the engine coherent.

## Where the code comes from

- **Our own decompilation only.** Never read, reference or copy leaked Neversoft / Activision source code of any
  Tony Hawk game, wherever it is published - and don't use names you remember from it. Leaked code would taint the
  project. Every function comes from the original game's machine code; the `// 0x...` comment above it is the address
  it was decompiled from, handy with a disassembler open on your own copy of `THUG2.exe`.
- **Names are ours.** Name things for what the code does, consistent with the names already here. Names from other
  people's dumps or symbol lists may help you understand code, but they don't decide what we call it, and existing
  code isn't renamed to match them.
- **Other people's code:** credit any idea taken from another project where it's used. Code from Morten1337's THUGPRO
  repositories is used with his permission for insight only, rewritten, never copied as is. OpenSpy has no licence: its
  server code may be read to understand the GameSpy protocols, never copied, not even rewritten. The GameSpy SDK used
  is IGN's 2011 BSD-3 release. Third-party notices go in `THIRD_PARTY_NOTICES.md`.
- **Nothing of the game is committed.** Tools read the player's own files each time; no game data, no original
  binaries.

## How we change the engine

- **Change the engine, don't patch symptoms.** When something looks or behaves wrong, find the part of the engine
  that produces it and change that system so it is right everywhere: one state, one code path that every consumer
  reads. No special cases for one level, object or material; no "reapply it every N frames" passes; no extra
  multiplier to hide a mismatch between two systems. If the proper fix is bigger than you planned, open an issue
  with the proposal rather than landing a workaround.
- **The original's behaviour is the default.** A change that differs from it carries a `NOT ORIGINAL` comment
  saying what it does and why, and an ini switch where a player might want the original back.
- **A new or changed setting** comes with: its row in the README's settings table, its row in the Options screen
  (`src/frontend/new_frontend_options.cpp`, the text in `new_frontend_text.h`), the settings report, live reload in
  `src/engine/gfx/graphics_options.cpp` when it can change while playing, and `options_check`.
- **Portable C++.** The goal is a source tree that builds with MSVC and GCC: standard C++ and standard library, no
  MSVC-only extensions, no `NOLINT` (fix what clang-tidy reports).
- **Layers.** `src/` is built in layers (`docs/architecture.md`); `tools/layers.test.ts` checks who may include whom.
  Vulkan and shader-compiler headers are seen only in `src/gpu_vulkan` and its check programs
  (`tools/vulkan_headers.test.ts`).

## Tools

- Tools are TypeScript run by Node 24, no build step (conventions in `docs/typescript.md`); Python only for the
  crash tools and the Ghidra scripts (`python -I`, files opened as UTF-8).
- The level converters live in their own repositories, pulled in through `tools/pipeline` (README "Other games'
  levels").

## Before you open a pull request

1. The game builds - see `docs/building.md` (x86 Debug with `tools\verify_build.cmd`, or x64).
2. `npm run typecheck`, `npm run lint`, `npm test` pass.
3. The check programs for what you touched pass (`out\build\<build>\thug2_*_check.exe`; the Vulkan ones need an x64
   build and a ray-tracing GPU), with new cases for what you changed.
4. Changes to settings are documented as above; a graphics change has its section in `docs/design/vulkan.md`.
5. You've run the game on your own install and say what you tried.
