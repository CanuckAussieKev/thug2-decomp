#pragma once

#include "hook/game_global.h"

#include <windows.h>

namespace Sys {

// 0x004E2680: the program's entry point (called by the C runtime's start-up). Unless another copy of the game
// is running (then that one's window is brought to the front): the network command line, the window, the
// command line split into arguments, the CD, DirectSound, the default key and pad settings, input and the
// keys' names - then the game's Main. PartyMod's changes are built in: its set-up (initPatch) before the
// command line is read, its SDL window in place of the game's own, the window not shown or minimised by the
// game, DirectInput left alone at the end.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char* commandLine, int show);
// thug2-decomp.ini [features] allow_multiple_instances (default 0): WinMain skips its already-running check. Read from
// the ini once; self-tests set it for their run.
bool& AllowMultipleInstances();

// 0x004E16B0: the game's window procedure (the window WinMain made before PartyMod; its SDL window has its own).
// Closing or destroying the window quits (PostQuitMessage, the quit flag); activation maximises the game's
// window and sets the active flag, deactivation minimises it, clears the flag, marks the device lost and
// invalidates every window; WM_PAINT validates; key presses and typed characters go to Sio::SpecialKeyInput /
// Sio::KeyInput; F10, Alt's menu (SC_KEYMENU) and power broadcasts are swallowed; the rest goes to
// DefWindowProcA.
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

// 0x004E1220 (cdecl): the network options on the command line (-HOST, -HOSTNAME, -EMAIL, -PPASS, -CONNECT,
// -MAXPLAYERS, -GROUP, -NTEAMS, -SKILLLEVEL, -NAME, -PASSWORD; found in any case, values keep theirs). The
// test runner's Late stage runs first (hook/selftest.h), then ReadNetworkParams.
void __cdecl ParseNetworkParams(const char* commandLine);
// ParseNetworkParams without the test stage (what the tests call).
void ReadNetworkParams(const char* commandLine);

GAME_GLOBAL_EXTERN(bool, g_internetGameHost);
GAME_GLOBAL_EXTERN(bool, g_joiningInternetGame);
GAME_GLOBAL_EXTERN(bool, g_appActive);

} // namespace Sys
