#pragma once

// NOT ORIGINAL - the script command table (the Function table of 1254 commands and the 250 member-function names) is
// composed here, in the original's order, and handed to the script engine.
namespace app {

// WinMain, before any of the original's code: the tables given to the script engine (script_tables.h).
void install_command_tables();

} // namespace app
