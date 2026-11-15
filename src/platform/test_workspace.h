#pragma once

// NOT ORIGINAL - whether this game folder is a test workspace: a file `thug2-decomp-always-muted` beside the exe
// (tools/game's Set-WorkspaceMuted / ensureWorkspaceMuted write it in C:\Games\THUG2-n; never in the user's own install).
// A workspace's game always runs muted (mute_audio.h) and opens its window behind the user's (sdl_window.cpp: shown
// without taking the focus - the tests drive it by posted keys and window captures, which need no focus).
namespace platform {

[[nodiscard]] bool test_workspace();
// NOT ORIGINAL - a batch run: the game started to make data and close (`-relit-bake <level>` - render_pc/relit_bake.h),
// in the user's own install too. Muted, windowed and behind the user's windows, as a workspace's game.
[[nodiscard]] bool batch_run();
// NOT ORIGINAL - a batch run's end: the game asked to close (as the window's close button asks it) with the process's
// exit code `code` (kBatch...; the run's log says more). The first end asked for is kept. sys::fast_exit ends the
// process with it.
inline constexpr int kBatchMade = 0;       // the run's data made
inline constexpr int kBatchNoLevel = 2;    // the level asked for never came up (not a level's load script, its scene not
                                           // installed, or never loaded - the log's "start level:" line says which)
inline constexpr int kBatchOtherScene = 3; // the level came up with a world of another scene than its level names
void end_batch_run(int code);
[[nodiscard]] int batch_exit_code();

} // namespace platform
