#pragma once

#include <string>

#include "base/core/api.h"

// NOT ORIGINAL - the editor mode (ours, at the user's request): thug2-decomp.exe as the level editor's application -
// "the game inside the editor" - rather than the game with the editor inside it.
//
//   thug2-decomp.exe -editor [-edit <level>]       (thug2-editor.exe, the launcher beside the exe, starts it so)
//   thug2-decomp.ini [features] editor_mode=1
//
// Only with the level editor installed (plugins\thug2-level-editor.dll: app/ turns the mode on once the plugins are
// loaded; without it the request is logged and the game starts as usual). In the editor mode the window is an
// application's (platform/sdl_window.h: resizable, its place kept, its own taskbar group and icon, never full
// screen), the intro movies are skipped and the game's front end is never shown: the editor's Start page and its
// level list stand over it while it runs hidden underneath, and playing is free skate in the level being edited
// (the level editor's own parts: editor/level_editor_shell.h). Without it everything is as it always was.
namespace sys::editor_mode {

// -editor on the command line (a word of its own) or [features] editor_mode=1 (read once).
bool requested();
// The mode is on: asked for and the level editor's plugin loaded (set once by app/ at start-up, before the window).
THUG2_API bool on();
void set_on(bool on);

// The command line's word `-<flag>` / `--<flag>` (pure): standing alone - at the start or after a space, a tab or a
// quote, and followed by the end, a space or a quote.
bool has_flag(const std::string& line, const char* flag);

} // namespace sys::editor_mode
