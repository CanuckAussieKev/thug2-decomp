// The loading bar's script commands and the engine's loading screen calls (Nx, cdecl): see loading_bar_cfuncs.h.

#include "game/gfx/loading_bar_cfuncs.h"

#include <cstdint>

#include "game/gfx/loading_bar.h"
#include "game/gfx/loading_screen.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// The bar's settings (0x0067DDB4 on): position, size, border, and the three colours (r, g, b, 0x80).
int* BarSetting(uint32_t address) { return &GameVariable<int>(address); }

// Two integers into two settings (each left unset when missing - the original's stack garbage; 0 here).
bool SetPair(Script::CStruct* params, uint32_t first, uint32_t second, uint32_t at)
{
    int a = 0, b = 0;
    params->GetInteger(first, &a, 0);
    params->GetInteger(second, &b, 0);
    BarSetting(at)[0] = a;
    BarSetting(at)[1] = b;
    UpdateLoadingBarColors();
    return true;
}

// A colour from r, g, b (alpha 0x80).
bool SetColour(Script::CStruct* params, uint32_t at)
{
    int r = 0, g = 0, b = 0; // (unset when missing in the original)
    params->GetInteger(0x93F60062u, &r, 0);
    params->GetInteger(0xFE2BE489u, &g, 0);
    params->GetInteger(0x8E411006u, &b, 0);
    uint8_t colour[4] = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 0x80};
    GameVariable<uint32_t>(at) = *reinterpret_cast<uint32_t*>(colour);
    UpdateLoadingBarColors();
    return true;
}

bool& LoadingScreenShown() { return GameVariable<bool>(0x006FC49C); }

} // namespace

// 0x00490770
void __cdecl SetLoadingBarTime(float seconds)
{
    if (seconds <= 0.0f) // (NaN: started)
        return;
    GameVariable<float>(0x006FC498) = seconds;
    UpdateLoadingBarColors();
    StartLoadingBar(seconds);
}

// 0x004907A0
bool __cdecl CFunc_SetLoadingBarPos(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetPair(params, 0x7323E97Cu, 0x0424D9EAu, 0x0067DDB4);
}

// 0x004907F0
bool __cdecl CFunc_SetLoadingBarSize(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetPair(params, 0x73E5BAD0u, 0x0AB21AF0u, 0x0067DDBC);
}

// 0x00490840
bool __cdecl CFunc_SetLoadingBarBorder(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetPair(params, 0x73E5BAD0u, 0x0AB21AF0u, 0x0067DDC4);
}

// 0x004908E0
bool __cdecl CFunc_SetLoadingBarStartColor(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetColour(params, 0x0067DDCC);
}

// 0x00490960
bool __cdecl CFunc_SetLoadingBarEndColor(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetColour(params, 0x0067DDD0);
}

// 0x004909E0
bool __cdecl CFunc_SetLoadingBarBorderColor(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetColour(params, 0x0067DDD4);
}

// 0x00490890
void __cdecl DisplayLoadingScreen(const char* name, bool keep, bool black)
{
    FreeLoadingScreenSprite();
    ShowLoadingScreen(name, keep, black);
    LoadingScreenShown() = true;
}

// 0x004908C0
void __cdecl RemoveLoadingScreen()
{
    HideLoadingScreen();
    FreeLoadingScreenSprite();
    LoadingScreenShown() = false;
}

} // namespace Nx

REPLACE(0x00490770, Nx::SetLoadingBarTime);
REPLACE(0x004907A0, Nx::CFunc_SetLoadingBarPos);
REPLACE(0x004907F0, Nx::CFunc_SetLoadingBarSize);
REPLACE(0x00490840, Nx::CFunc_SetLoadingBarBorder);
REPLACE(0x004908E0, Nx::CFunc_SetLoadingBarStartColor);
REPLACE(0x00490960, Nx::CFunc_SetLoadingBarEndColor);
REPLACE(0x004909E0, Nx::CFunc_SetLoadingBarBorderColor);
REPLACE(0x00490890, Nx::DisplayLoadingScreen);
REPLACE(0x004908C0, Nx::RemoveLoadingScreen);
