#pragma once

#include <string>

// The process's flags that WinMain and its window set and the rest of the game reads (the original's globals next to
// WinMain): whether the game's window is active, the network command line's host / join choice, and - NOT ORIGINAL -
// the command line as the program started. They are kept here, below the engine, so that readers don't depend on the
// program's composition (app/winmain.cpp, which sets them).
namespace sys {

// 0x007CCBE4: the game's window has the focus (set by the window's focus events, sys::create_sdl_window's handler,
// and the original's window procedure).
bool app_active();
void set_app_active(bool value);

// The command line's -HOST (hosting an internet game) and -CONNECT (joining one): sys::read_network_params.
bool internet_game_host();
void set_internet_game_host(bool value);
bool joining_internet_game();
void set_joining_internet_game(bool value);

// NOT ORIGINAL - the process's command line as it was when the program started (GetCommandLineA's text, copied at the
// top of win_main: the game's own parsing cuts that buffer into words later - a 0 after the first word).
const std::string& startup_command_line();
void set_startup_command_line(const char* text);

} // namespace sys
