#pragma once

// NOT ORIGINAL - the retail-limits DLL's entry points (dllmain.cpp).

#include <windows.h>

// The DLL's start-up (called by the C runtime's DLL start-up when Windows loads it): the limits raised.
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved);

// DirectInput8Create (dinput8.def): the game's DirectInput call, passed on to the real dinput8.dll (loaded at the
// first call, not inside DllMain).
extern "C" HRESULT WINAPI retail_limits_direct_input8_create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPVOID outer);
