#pragma once

// NOT ORIGINAL - the game exe's extensions (engine/ext/extension.h), registered by app/ in the order every extension
// point calls them (extensions.cpp), and the script commands they add.
namespace app {

// WinMain, first (before any of the original's code): the extensions registered.
void register_extensions();
// The start-up, after the game's own script commands: the commands the extensions and our NOT ORIGINAL game features
// add, in the order they have always been registered.
void register_added_script_commands();

} // namespace app
