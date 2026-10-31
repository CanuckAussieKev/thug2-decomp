#pragma once

#include <windows.h>

// The Win32 platform layer: input set-up, rumble, the window's message pump. PartyMod replaces the input side
// with SDL (engine/pad_input/sdl_input.h), so most of the original's DirectInput code survives here only in comments.
namespace os_layer {

// 0x005F3A50 (cdecl): set a port's two motors, levels 0-65535 (sio::set_sdl_actuators).
void set_actuators(int port, int left, int right);

// 0x005F56E0: handle the window's pending messages (Originally from PartyMod Fixes: the SDL event pump, os_layer::pump_sdl_events).
void process_window_events();

// 0x005F44F0 (cdecl): set up input once. False without a window; true if already set up.
bool init_input(HINSTANCE instance, HWND window, int config);

} // namespace os_layer
