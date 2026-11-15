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

} // namespace platform
