#pragma once

namespace Sys {

// 0x004E1EF0: the game's main (called by WinMain): configuration and memory set-up, then - until the game is
// quit - the singletons and modules, the main loop and their teardown. The PC port's fast exit is built in: when the
// main loop returns, the process ends there (Sys::FastExit) instead of tearing everything down.
int __cdecl Main(int argc, char** argv);

} // namespace Sys
