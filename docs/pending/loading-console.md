# Loading console (NOT ORIGINAL feature) + the loading screen's frame 0x004B6FF0

- [x] Loading console + 0x004B6FF0 (the user's request: "a console that shows what is currently loading, so we can
  differentiate between a hang and something still loading").

## What THUG2 PC does on its loading screen (found while doing this)
- `DisplayLoadingScreen` (0x0059F4E0) -> 0x00490890: 0x004B6F70 (frees the old picture's surface), 0x004B78B0 (loads
  `images\<name>`, draws it once - or clears to black with `blank`, or nothing with `freeze` -, presents, sets
  0x00786D04 = 1: from then the main loop draws nothing - Nx::StartRender and the renderer check it), 0x006FC49C = 1.
- With a number (`loading_time` from the level's struct) -> 0x00490770 -> 0x004B7400: tick 0.029 s, total = seconds *
  0.6, the bar's colours (0x004B6B80), and **`timeSetEvent(29 ms, periodic)` with the callback 0x004B6FF0** (a label,
  not a Ghidra function). The callback runs on winmm's timer thread, independent of the loader: it takes the loading
  screen's mutex 0x007052AC, BeginScene, the picture (0x004B6CD0), the bar (frame / gradient / unfilled part, three
  XYZRHW|DIFFUSE strips), EndScene, Present, releases. **The bar fills with time, not progress** (elapsed / total,
  clamped at 1) - so a hung load and a slow one look the same: the bar sits full.
- `HideLoadingScreen` -> 0x004908C0: 0x004B6F10 (under the mutex: fog state back, `timeKillEvent`, 0x00786D04 = 0),
  0x004B6F70, 0x006FC49C = 0.
- Nothing else takes the mutex (references: only 0x004B6CD0-0x004B7AC3 and the CRT's mutex create / close), so the
  loader on the main thread never blocks the timer: the screen keeps being redrawn during a hang. It only freezes if
  the timer thread blocks (the Direct3D device - created D3DCREATE_MULTITHREADED - held by a long main-thread call, a
  deadlock, a crash).
- A loading screen shown without a time gets no timer: drawn once, never again.

## What was done
- **0x004B6FF0 rewritten** (src/game/gfx/loading_bar.cpp, `Nx::DrawLoadingScreenFrame`, REPLACE): the original
  exactly (x87 double-precision intermediates, _ftol2's 64-bit truncation - NaN / infinite fill = 0), plus one call
  before EndScene: `Gfx::LoadingConsole::DrawPanel()` (nothing unless the console is active).
- **The loading console** (src/game/gfx/loading_console.h/.cpp, NOT ORIGINAL): `[features] loading_console=` (default
  1; 0 = the original screen: the same frame code, no panel, no extra timer). A panel in the bottom-left corner (left
  of the centred bar; JetBrains Mono rasterised once by ImGui's font atlas - no ImGui context, no shared state with the
  level editor - into a managed texture; its own state block around one DrawPrimitiveUP):
  `LOADING 12.3 s ... last activity 0.4 s ago` and the last 12 lines `[  time] what (xN)`. After 10 s with no new
  line the box turns dark orange and the header reads `NO ACTIVITY - last N s ago`; the newest line is what it is on.
- Line sources: File::Open (`open` = from an archive, `read` = loose / data overlay, `missing`; the size from the
  archive entry or GetFileSizeEx - not File::GetFileSize, which changes the archive-handled flag), Pip::Load /
  LoadFileInto (`unpack` from an archive, `read (cache)`, `read (async)`), PreMgr::LoadPre's async archive reads, the
  script interpreter (cscript_exec.cpp: every QScript called while loading as `script <name>`, and C functions named
  Load* / Create* / Parse* / Init* / Preload* / Spawn* / AddScene / AddCollision / AddSoundBanks / AddMusicTrack /
  AddStream with their first string / scene= / file= / name= parameter), and the game log (hook/log.cpp's new
  SetLogListener; not "data overlay:", "replaced 0x", "shadow checks:", self-test or indented lines). Identical
  consecutive lines count up as (xN).
- A loading screen without a time: the console's own timer (100 ms) draws it - the picture (0x004B6CD0) or black -
  with the panel, under the same mutex, only while the bar's timer isn't running, the screen is up, it wasn't shown
  `freeze`, and no Bink movie plays. Frozen screens (cutscene loads) get no timer.
- Start / stop: ScreenShown from CFunc_DisplayLoadingScreen and the cutscene loader (the only callers of 0x00490890);
  ScreenHidden from CFunc_HideLoadingScreen and WinMain's shutdown. Suspended while self-tests run
  (selftest_runner.cpp, as menu_backdrop's setting): they call the script commands (selftests_lfxc) and the main loop
  with the screen code stood in.
- Cost: a line = a vsnprintf into a 96-byte slot of a 64-line ring under an SRW lock, only while a loading screen is
  up (one bool test otherwise - also in the script interpreter's command dispatch); the panel laid out at most 10 times
  a second, drawn as one cached triangle list with each loading screen frame.
- Docs: CLAUDE.md's ini list, README.md's [features] table.

## Tests
- `TestLoadingScreenFrame` (src/hook/selftests_loading.cpp, Early): original 0x004B6FF0 vs ours, the device (BeginScene,
  EndScene, Present, SetRenderState, DrawPrimitiveUP), the picture 0x004B6CD0 and the renderer's SetTexture /
  SetVertexFormat (game's and ours) stood in by recorders; screens of every height 1-2400 plus 34 sizes with three bar
  widths (one wider than the screen: an odd negative half), times filling / full / past full / no total / negative /
  NaN, colours with bits above a byte, the early returns (no timer id, screen down); every call, its vertices, the
  elapsed time left, and whether the mutex is held at each call and released after.
- `TestLoadingConsoleParts`: file names, sizes, the command / log filters, the ring (repeats, wrap, cut at 95 chars).
- Mutations (out/scripts/mut_loading.ts, 32): 28 caught. 4 equivalent: `(double)(float)left` vs `(double)left` (exact
  below 2^24), and three float-rounding variants of the bar's y scale / inset (no screen height from 1 to 2400 gives a
  different pixel coordinate - the tests run every one).
- thug2_check on THUG2PM.exe and the stock THUG2.exe: 0 MISMATCHED, all early self-tests pass; check_code_writes: no
  overlaps.

## Game runs (C:\Games\THUG2-3)
- (see below)

## To switch it off
- `[features] loading_console=0`. The frame rewrite alone: `[replacements] disable=loading_bar.cpp` (the original
  callback; then no panel on bar screens).

## Shared files touched (small, additive)
- CMakeLists.txt (2 files), hook/log.h/.cpp (SetLogListener), file.cpp, pip.cpp, pre.cpp, cscript_exec.cpp,
  skate_cfuncs.cpp, cutscene_package.cpp, winmain.cpp, mlp.cpp (one line each: notes / show / hide / GameRunning),
  CLAUDE.md, README.md.
