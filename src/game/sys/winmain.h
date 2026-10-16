#pragma once

#include <windows.h>

namespace Sys {

// 0x004E2680: the program's entry point (called by the C runtime's start-up). Unless another copy of the game
// is running (then that one's window is brought to the front): the network command line, the window, the
// command line split into arguments, the CD, DirectSound, the default key and pad settings, input and the
// keys' names - then the game's Main. PartyMod's changes are built in: its set-up (initPatch) before the
// command line is read, its SDL window in place of the game's own, the window not shown or minimised by the
// game, DirectInput left alone at the end.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char* commandLine, int show);

// 0x004E1220 (cdecl): the network options on the command line (-HOST, -HOSTNAME, -EMAIL, -PPASS, -CONNECT,
// -MAXPLAYERS, -GROUP, -NTEAMS, -SKILLLEVEL, -NAME, -PASSWORD; found in any case, values keep theirs). The
// test runner's Late stage runs first (hook/selftest.h), then ReadNetworkParams.
void __cdecl ParseNetworkParams(const char* commandLine);
// ParseNetworkParams without the test stage (what the tests call).
void ReadNetworkParams(const char* commandLine);

} // namespace Sys
