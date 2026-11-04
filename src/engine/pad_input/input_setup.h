#pragma once

#include <windows.h>

// Input set-up and rumble (the original's Win32 platform layer; the window's message pump: platform/sdl_events.h).
// PartyMod replaces the input side with SDL (engine/pad_input/sdl_input.h), so most of the original's DirectInput code survives here only in comments.
namespace os_layer {

// 0x005F3A50 (cdecl): set a port's two motors, levels 0-65535 (input::rumble).
void set_actuators(int port, int left, int right);

// 0x005F44F0 (cdecl): set up input once. False without a window; true if already set up.
bool init_input(HINSTANCE instance, HWND window, const char* config);

} // namespace os_layer
