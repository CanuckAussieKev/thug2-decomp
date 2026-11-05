#pragma once

// NOT ORIGINAL - an exe's extensions (engine/ext/extension.h), registered by app/ in the order every extension point
// calls them, and the script commands and input sources they add. Each exe defines these in its own file - the game's
// (thug2-decomp.exe) in extensions.cpp, the level editor's (thug2-editor.exe) in editor_extensions.cpp; the rest of app/
// (the start-up, the main loop, the tables: main.cpp, startup.cpp, winmain.cpp...) is the same for both.
namespace app {

// The exe's name (the run log's first line).
const char* exe_name();

// WinMain, first (before any of the original's code): the extensions registered.
void register_extensions();
// The start-up, after the game's own script commands: the commands the extensions and our NOT ORIGINAL game features
// add, in the order they have always been registered.
void register_added_script_commands();
// WinMain, after the game's own input sources (DirectInput's keyboard, SDL's): the ones the extensions add.
void add_input_sources();

} // namespace app
