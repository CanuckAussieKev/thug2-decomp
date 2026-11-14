#pragma once

// NOT ORIGINAL - whether this game folder is a test workspace: a file `thug2-decomp-always-muted` beside the exe
// (tools/game's Set-WorkspaceMuted / ensureWorkspaceMuted write it in C:\Games\THUG2-n; never in the user's own install).
// A workspace's game always runs muted (mute_audio.h) and opens its window behind the user's (sdl_window.cpp: shown
// without taking the focus - the tests drive it by posted keys and window captures, which need no focus).
namespace platform {

[[nodiscard]] bool test_workspace();

} // namespace platform
