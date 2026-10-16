#pragma once

#include <windows.h>

// The Win32 platform layer: input set-up, rumble, the window's message pump. The PC port replaces the input side
// with SDL (game/sio/sdl_input.h), so most of the original's DirectInput code survives here only in comments.
namespace Platform {

// 0x005F3A50 (cdecl): set a port's two motors, levels 0-65535 (Sio::SetSdlActuators).
void SetActuators(int port, int left, int right);

// 0x005F56E0: handle the window's pending messages (PC port: the SDL event pump, Platform::PumpSdlEvents).
void ProcessWindowEvents();

// 0x005F44F0 (cdecl): set up input once. False without a window; true if already set up.
bool InitInput(HINSTANCE instance, HWND window, int config);

} // namespace Platform
