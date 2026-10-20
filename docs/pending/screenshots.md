# Screenshot key (NOT ORIGINAL - the user's request)

"Add a function to screenshot and save to Game/Screenshots. This will make it easier for me to reference to you things
that I have noticed." Branch `worktree-agent-a4afd18eb25c08ef8`. Built and checked offline; **NOT run in the game yet.**

## What
- **Print Screen** (anywhere: menus, a level, paused, the level editor, a loading screen) saves the frame as presented
  - the 2D, PartyMod's bars, the level editor's ImGui panels - as `<game folder>\Screenshots\thug2_YYYY-MM-DD_HH-MM-SS.png`
  (`_2`, `_3` ... when the name, or its .txt, is taken), plus a sidecar `.txt` of the same name.
- **Feedback:** "Screenshot saved: thug2_....png" for 2 s - the editor's message line in the editor, a panel message
  (`create_panel_message`, top centre) elsewhere; held while a loading screen is up (up to 10 s). The log gets
  `screenshot: saved <full path> (and <txt>) in N ms`. Failures: "Screenshot failed ..." + the reason in the log.
- **The sidecar** (every field "(not available)" when it can't be read - menus, loading, no skater):
  - `[screenshot]` file, folder, taken (local time), image size + back buffer format, window size;
  - `[build]` exe name (THUG2.exe / THUG2PM.exe) + its date and size, the DLL's name, date and size, the DLL's commit
    (`git rev-parse` at build time, "(with uncommitted changes)" when the tree had any);
  - `[game]` where (starting up / front end / level), the level's load script, the main scene's name, the game mode,
    paused, loading screen, level editor open, frames run;
  - `[skater]` position and heading (in a level, not loading, a local skater);
  - `[camera]` viewport 0's camera position and view direction (as last drawn);
  - `[level editor]` (when open) mode and UI kind, level, its camera and yaw / pitch, the selection (kind, name,
    position), the inspected node (name, class, family, position, added / hidden), what's under the cursor, changes,
    the message;
  - `[log]` the log's last 20 lines.

## Settings (thug2-decomp.ini)
- `[features] screenshots=0` turns it off (default 1).
- `[features] screenshot_dir=` another folder (relative: to the game folder; made if missing).
- `[Keybinds] Screenshot=` the key: an SDL scan code like the other `[Keybinds]` (70 Print Screen, 69 F12) or a key
  name (`F12`, `PrintScreen`, `F11` - SDL's names). Unset: Print Screen - or F12 when Windows' Snipping Tool takes
  Print Screen (HKCU\Control Panel\Keyboard PrintScreenKeyForSnippingEnabled = 1, Windows 11's default; with it on the
  game never gets the key). The start-up log says which key and why. On this machine the setting is 0, so Print
  Screen it is.

## How
- `src/game/sys/screenshot.cpp`: an SDL event handler (registered after the input's, in `Sio::InitSdlInput`) sets a
  request on the key's SDL_KEYDOWN. SDL2 on Windows synthesises Print Screen's down from its key-up (Windows sends no
  WM_KEYDOWN for it), so an event handler - not `SDL_GetKeyboardState` polling - is what sees it; and since the game
  only polls the state (where Print Screen is never down), no game action fires. Not bound to anything in the game.
- `Mlp::Manager::MainLoop` calls `Sys::Screenshot::BeforePresent(!SkipRender())` after `AfterRender`, just before
  the end-scene + Present (0x0048BF20 -> 0x004B2750). A request is taken on the next drawn frame: the backend copies the
  back buffer, then the main thread gathers the sidecar and starts a worker thread (one at a time; a press while one is
  being written waits for it) that converts to RGB, encodes the PNG and writes both files (`CREATE_NEW`). The main
  thread logs and shows the result when the worker is done (nothing logged from the worker: Log isn't thread-safe).
- `src/game/gfx/screenshot_capture.cpp` - the only Direct3D file of the feature (renderer independence, as the
  editor's UI backend): ends the scene, `GetBackBuffer` -> (multisampled, `[Graphics] AntiAliasing`: `StretchRect` into
  a plain render target) -> `GetRenderTargetData` into a system-memory surface -> lock, copy rows -> begins the scene
  again. Skipped (logged) while the device is lost. The copy waits for the GPU: one hitch of about a frame.
- `src/game/sys/screenshot_files.cpp` - pure: names / suffixes, the key setting, X8R8G8B8 / A8R8G8B8 / R5G6B5 /
  X1R5G5B5 -> RGB (opaque; 5/6-bit channels widened), the PNG (stb_image_write), the sidecar's text.
- `src/hook/log.cpp`: `LogTail(n)` - the last 32 log lines kept in fixed slots (the log file is opened exclusively, so
  it can't be read back).
- `cmake/build_info.cmake` + the `build_info` target: `build_info.h` with the commit, rewritten only when it changes.
- Also: `render.cpp` notes viewport 0's camera for the sidecar (`Sys::Screenshot::NoteView`);
  `Front::LevelEditor::DescribeForScreenshot()` (level_editor.h) gives the editor's state.

## Dependency
stb (Sean Barrett), commit 1ee679ca2ef753a528db5ba6801e1067b40481b8 (2021-07-12; stb_image_write v1.16 - its last
change - and stb_image v2.27), FetchContent of the commit's archive with its SHA-256; headers only. stb_image is only
used by the self-test (PNG-only build). Public domain or MIT: credited in THIRD_PARTY_NOTICES.md.

## Shared files touched (small, additive)
CMakeLists.txt (stb, build_info, 4 sources), src/hook/log.cpp/.h (LogTail), src/game/sys/mlp.cpp (one call),
src/game/gfx/render.cpp (one call), src/game/sio/sdl_input.cpp (Init), src/game/front/level_editor.cpp/.h
(DescribeForScreenshot), CLAUDE.md (one clause), THIRD_PARTY_NOTICES.md.

## Checked
- Build (out/build/verify), `thug2_check` on THUG2PM.exe and the stock THUG2.exe: 360 early self-tests pass on both
  (one THUG2PM run first stopped with exit 3 in the pcm tests - "trace sizes: original 2089 ours 2216" - and passed on
  the next two runs: the known flaky early tests, not this feature). `node tools/check_code_writes.ts`: no overlaps.
- Self-tests (`src/hook/selftests_screenshot.cpp`, Early): TestScreenshotKey, TestScreenshotNames,
  TestScreenshotPixels, TestScreenshotPng (signature, IHDR, every chunk's CRC, IEND, read back with stb_image),
  TestScreenshotSidecar (missing fields, log lines with their own line ends). `node tools/mutate.ts`: 10 / 10
  mutations caught (BGR order, 6-bit widening, X1R5G5B5 shift, suffix numbering, the sidecar-name check, missing-value
  text, key range, PNG stride, short buffer, log line ends).
- Not testable offline: the capture, the key, the worker, the message, the sidecar's game fields.

## In-game checklist (for the user)
- [ ] Main menu: Print Screen -> "Screenshot saved: thug2_....png" shows for ~2 s; `Game\Screenshots\` has the PNG
      and the .txt; the PNG looks like the screen (colours right, not upside down, whole width incl. the bars); the
      .txt says front end.
- [ ] In a level, skating: same; the .txt has the level, scene, game mode, the skater's position / heading, the camera.
- [ ] Paused (pause menu up): the menu is in the picture; the .txt says paused yes.
- [ ] Level editor with the panels open and something selected: the panels are in the picture; the message shows on
      the editor's message line; the .txt has a [level editor] section (mode, camera, selection, inspected node).
- [ ] While a level loads (loading screen): a picture is taken (or comes right after); no crash; the message shows
      once the loading screen is gone.
- [ ] Two presses within the same second: `..._2.png` / `..._2.txt`.
- [ ] Hitch: a short stall at the press at most; none afterwards.
- [ ] `[Graphics] AntiAliasing=1` (yours) and `=0`: both give a picture.
- [ ] `[features] screenshots=0`: the key does nothing, the log says "screenshot key: off".
- [ ] `[Keybinds] Screenshot=F12` (or `69`): F12 takes it, Print Screen doesn't; the log names the key.
- [ ] `[features] screenshot_dir=D:\Shots` (or a relative name): the folder is made and used.
- [ ] Print Screen doesn't do anything else in the game (menus, skating, editor).
- [ ] Windowed and full-screen both work.

## Concerns / untested
- The capture ends and re-begins the frame's scene around the copy (legal Direct3D 9; done so StretchRect /
  GetRenderTargetData run outside a scene). Untested on the real device; if it misbehaves, the copy could move to
  after Present only by rewriting 0x004B2750.
- The panel message outside the editor reuses `create_panel_message` (as the restart tester does in levels); in the
  front end it's untested. It's skipped until a level (or the front end's load script) is loaded and the script exists.
- Encoding runs on a detached worker thread in the Debug build (stb unoptimised): a 2560x1440 picture may take a
  second or two to appear; the game keeps running meanwhile. Quitting during that second loses the picture.
- Intro movies / start-up before the main loop: the press is kept and taken on the main loop's next drawn frame.
