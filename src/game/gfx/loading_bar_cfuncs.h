#pragma once

#include <cstdint>

namespace Scripts {
class CScript;
class CStruct;
}

// The loading bar's script commands and the engine's loading screen calls (Nx, cdecl).
namespace Engine3D {

// 0x00490770: with a time above 0 (or NaN), the loading bar run for it (kept at 0x006FC498; its colours worked out
// first).
void __cdecl SetLoadingBarTime(float seconds);
// 0x004907A0 / 0x004907F0 / 0x00490840: SetLoadingBarPos x y / SetLoadingBarSize width height /
// SetLoadingBarBorder width height (0x0067DDB4 / 0x0067DDBC / 0x0067DDC4; the colours worked out again). True.
bool __cdecl CFunc_SetLoadingBarPos(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetLoadingBarSize(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetLoadingBarBorder(Scripts::CStruct* params, Scripts::CScript* script);
// 0x004908E0 / 0x00490960 / 0x004909E0: SetLoadingBarStartColor / EndColor / BorderColor r g b (alpha 0x80;
// 0x0067DDCC / 0x0067DDD0 / 0x0067DDD4). True.
bool __cdecl CFunc_SetLoadingBarStartColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetLoadingBarEndColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetLoadingBarBorderColor(Scripts::CStruct* params, Scripts::CScript* script);
// 0x00490890: a loading screen shown (the last one's sprite freed first; 0x006FC49C set).
void __cdecl DisplayLoadingScreen(const char* name, bool keep, bool black);
// 0x004908C0: the loading screen hidden and its sprite freed (0x006FC49C cleared).
void __cdecl RemoveLoadingScreen();

extern uint8_t g_loadingScreenShown;
} // namespace Nx
