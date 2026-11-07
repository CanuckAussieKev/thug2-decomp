#pragma once

// NOT ORIGINAL - the exe's extensions (engine/ext/extension.h), registered by app/ in the order every extension point
// calls them, and the script commands and input sources they add (extensions.cpp). The level editor is a plugin
// (engine/ext/plugin.h): its extension is put at its place in that order when its DLL is there.
namespace app {

// The exe's name (the run log's first line).
const char* exe_name();

// WinMain, first (before any of the original's code): the plugins loaded, the extensions registered.
void register_extensions();
// The start-up, after the game's own script commands: the commands the extensions and our NOT ORIGINAL game features
// add, in the order they have always been registered.
void register_added_script_commands();
// WinMain, after the game's own input sources (DirectInput's keyboard, SDL's): the ones the extensions add.
void add_input_sources();

} // namespace app
