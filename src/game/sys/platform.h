#pragma once

#include <windows.h>

// The Win32 platform layer: input set-up, rumble, the window's message pump. PartyMod replaces the input side
// with its own (SDL), so most of the original's DirectInput code survives here only in comments.
namespace Platform {

// 0x005F3A50 (cdecl): set a port's two motors, levels 0-65535 (PartyMod's set_actuators).
void SetActuators(int port, int left, int right);

// 0x005F56E0: handle the window's pending messages (PartyMod: its SDL event pump, handleEvents).
void ProcessWindowEvents();

// 0x005F44F0 (cdecl): set up input once. False without a window; true if already set up.
bool InitInput(HINSTANCE instance, HWND window, int config);

} // namespace Platform
