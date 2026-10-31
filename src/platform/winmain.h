#pragma once

#include <windows.h>

namespace sys {

// 0x004E2680: the program's entry point (called by the C runtime's start-up). Unless another copy of the game
// is running (then that one's window is brought to the front): the network command line, the window, the
// command line split into arguments, the CD, DirectSound, the default key and pad settings, input and the
// keys' names - then the game's Main. PartyMod's changes are built in: its set-up (initPatch) before the
// command line is read, its SDL window in place of the game's own, the window not shown or minimised by the
// game, DirectInput left alone at the end.
int WINAPI win_main(HINSTANCE instance, HINSTANCE previous, char* command_line, int show);
// thug2-decomp.ini [features] allow_multiple_instances (default 0): WinMain skips its already-running check. Read from
// the ini once; self-tests set it for their run.
bool& allow_multiple_instances();

// 0x004E16B0: the game's window procedure (the window WinMain made before PartyMod; its SDL window has its own).
// Closing or destroying the window quits (PostQuitMessage, the quit flag); activation maximises the game's
// window and sets the active flag, deactivation minimises it, clears the flag, marks the device lost and
// invalidates every window; WM_PAINT validates; key presses and typed characters go to sio::special_key_input /
// sio::key_input; F10, Alt's menu (SC_KEYMENU) and power broadcasts are swallowed; the rest goes to
// DefWindowProcA.
LRESULT CALLBACK get_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

// 0x004E1220 (cdecl): the network options on the command line (-HOST, -HOSTNAME, -EMAIL, -PPASS, -CONNECT,
// -MAXPLAYERS, -GROUP, -NTEAMS, -SKILLLEVEL, -NAME, -PASSWORD; found in any case, values keep theirs). The
// read_network_params.
void __cdecl parse_network_params(const char* command_line);
// parse_network_params without the test stage (what the tests call).
void read_network_params(const char* command_line);

extern bool g_internet_game_host;
extern bool g_joining_internet_game;
extern bool g_app_active;

} // namespace sys
