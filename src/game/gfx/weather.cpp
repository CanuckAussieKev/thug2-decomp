// The engine's weather: see weather.h.

#include "game/gfx/weather.h"

#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kWeatherVtable = 0x00649E30;

float FloatParameter(Script::CStruct* params, float fallback)
{
    float value = fallback;
    params->GetFloat(0u, &value, 0);
    return value;
}
int IntegerParameter(Script::CStruct* params, int fallback)
{
    int value = fallback;
    params->GetInteger(0u, &value, 0);
    return value;
}
int Frames(int frames)
{
    if (frames < 0)
        frames = 0;
    if (frames > 0xFE)
        frames = 0xFE;
    return frames;
}
void SetColour(uint8_t* colour, int value) { std::memcpy(colour, &value, 4); }

// A blend mode's name (the unnamed checksum) and fixed alpha (0x20) handed to virtual [slot].
bool PassBlendMode(CWeather* weather, Script::CStruct* params, int slot)
{
    uint32_t name = 0;
    if (!params->GetChecksum(0u, &name, 0))
        return false;
    int fixedAlpha = 0x20;
    params->GetInteger(0u, &fixedAlpha, 0);
    reinterpret_cast<void(__thiscall*)(CWeather*, uint32_t, int)>(weather->m_vtable[slot])(weather, name, fixedAlpha);
    return true;
}

} // namespace

// 0x004A14C0
CWeather* CWeather::Construct()
{
    std::memset(m_rainBottom, 0xFF, 4);
    m_vtable = reinterpret_cast<void**>(kWeatherVtable);
    m_rainHeight = 2000.0f;
    m_rainFrames = 40;
    m_rainLength = 100.0f;
    m_rainRate = 0.0f;
    std::memset(m_rainTop, 0, 4);
    m_splashRate = 0.0f;
    m_splashLife = 8;
    m_splashSize = 16.0f;
    std::memset(m_splashColour, 0x80, 4);
    m_snowHeight = 500.0f;
    m_snowFrames = 0xFE;
    m_snowSize = 4.0f;
    m_snowRate = 0.0f;
    std::memset(m_snowColour, 0x80, 4);
    m_raining = true;
    m_active = false;
    return this;
}

// 0x004A1080
void CWeather::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kWeatherVtable);
}

// 0x004A1550
CWeather* CWeather::Destroy(unsigned int freeFlags)
{
    m_vtable = reinterpret_cast<void**>(kWeatherVtable);
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004A1020
void CWeather::UpdateGrid()
{
    Dbg::Printf("STUB: plat_update_grid\n");
}

// 0x004A1030
void CWeather::Update(int)
{
    Dbg::Printf("STUB: plat_process\n");
}

// 0x004A1040
void CWeather::Render()
{
    Dbg::Printf("STUB: plat_render\n");
}

// 0x004A1050
void CWeather::SetRainBlendMode(uint32_t, int)
{
    Dbg::Printf("STUB: plat_set_rain_blend_mode\n");
}

// 0x004A1060
void CWeather::SetSplashBlendMode(uint32_t, int)
{
    Dbg::Printf("STUB: plat_set_splash_blend_mode\n");
}

// 0x004A1070
void CWeather::SetSnowBlendMode(uint32_t, int)
{
    Dbg::Printf("STUB: plat_set_snow_blend_mode\n");
}

// 0x004A10B0
bool CWeather::ScriptSetRainHeight(Script::CStruct* params, Script::CScript*)
{
    m_rainHeight = 2000.0f;
    params->GetFloat(0u, &m_rainHeight, 0);
    return true;
}

// 0x004A10D0
bool CWeather::ScriptSetRainFrames(Script::CStruct* params, Script::CScript*)
{
    m_rainFrames = 40;
    params->GetInteger(0u, &m_rainFrames, 0);
    m_rainFrames = Frames(m_rainFrames);
    return true;
}

// 0x004A1110
bool CWeather::ScriptSetRainLength(Script::CStruct* params, Script::CScript*)
{
    m_rainLength = 100.0f;
    params->GetFloat(0u, &m_rainLength, 0);
    return true;
}

// 0x004A1130
bool CWeather::ScriptSetRainBlendMode(Script::CStruct* params, Script::CScript*)
{
    return PassBlendMode(this, params, 4);
}

// 0x004A1190
bool CWeather::ScriptSetRainRate(Script::CStruct* params, Script::CScript*)
{
    m_rainRate = 0.0f;
    params->GetFloat(0u, &m_rainRate, 0);
    return true;
}

// 0x004A11B0
bool CWeather::ScriptSetRainColor(Script::CStruct* params, Script::CScript*)
{
    int colour = 0;
    params->GetInteger("top", &colour, 0);
    SetColour(m_rainTop, colour);
    colour = -1;
    params->GetInteger("bottom", &colour, 0);
    SetColour(m_rainBottom, colour);
    return true;
}

// 0x004A1240
bool CWeather::ScriptSetSplashRate(Script::CStruct* params, Script::CScript*)
{
    m_splashRate = 0.0f;
    params->GetFloat(0u, &m_splashRate, 0);
    return true;
}

// 0x004A1260
bool CWeather::ScriptSetSplashLife(Script::CStruct* params, Script::CScript*)
{
    m_splashLife = 8; // (not kept within 0-254)
    params->GetInteger(0u, &m_splashLife, 0);
    return true;
}

// 0x004A1280
bool CWeather::ScriptSetSplashSize(Script::CStruct* params, Script::CScript*)
{
    m_splashSize = 16.0f;
    params->GetFloat(0u, &m_splashSize, 0);
    return true;
}

// 0x004A12A0
bool CWeather::ScriptSetSplashColor(Script::CStruct* params, Script::CScript*)
{
    SetColour(m_splashColour, IntegerParameter(params, static_cast<int>(0x80808080)));
    return true;
}

// 0x004A12F0
bool CWeather::ScriptSetSplashBlendMode(Script::CStruct* params, Script::CScript*)
{
    return PassBlendMode(this, params, 5);
}

// 0x004A1350
bool CWeather::ScriptSetSnowHeight(Script::CStruct* params, Script::CScript*)
{
    m_snowHeight = 500.0f;
    params->GetFloat(0u, &m_snowHeight, 0);
    return true;
}

// 0x004A1370
bool CWeather::ScriptSetSnowFrames(Script::CStruct* params, Script::CScript*)
{
    m_snowFrames = 0xFE;
    params->GetInteger(0u, &m_snowFrames, 0);
    m_snowFrames = Frames(m_snowFrames);
    return true;
}

// 0x004A13B0
bool CWeather::ScriptSetSnowSize(Script::CStruct* params, Script::CScript*)
{
    m_snowSize = 4.0f;
    params->GetFloat(0u, &m_snowSize, 0);
    return true;
}

// 0x004A13D0
bool CWeather::ScriptSetSnowBlendMode(Script::CStruct* params, Script::CScript*)
{
    return PassBlendMode(this, params, 6);
}

// 0x004A1430
bool CWeather::ScriptSetSnowRate(Script::CStruct* params, Script::CScript*)
{
    m_snowRate = FloatParameter(params, 0.0f);
    return true;
}

// 0x004A1450
bool CWeather::ScriptSetSnowColor(Script::CStruct* params, Script::CScript*)
{
    SetColour(m_snowColour, IntegerParameter(params, static_cast<int>(0x80808080)));
    return true;
}

// 0x004A14A0
bool CWeather::ScriptSetSnowActive(Script::CStruct*, Script::CScript*)
{
    m_raining = false;
    return true;
}

// 0x004A14B0
bool CWeather::ScriptSetRainActive(Script::CStruct*, Script::CScript*)
{
    m_raining = true;
    return true;
}

// 0x004A1090
__declspec(naked) void VirtualSlot1Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 4]
    }
}

// 0x004A10A0
__declspec(naked) void VirtualSlot3Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0xC]
    }
}

} // namespace Nx

REPLACE(0x004A14C0, &Nx::CWeather::Construct);
REPLACE(0x004A1080, &Nx::CWeather::DestroyBody);
REPLACE(0x004A1550, &Nx::CWeather::Destroy);
REPLACE(0x004A1020, &Nx::CWeather::UpdateGrid);
REPLACE(0x004A1030, &Nx::CWeather::Update);
REPLACE(0x004A1040, &Nx::CWeather::Render); // (shared by other vtables: the same message)
REPLACE(0x004A1050, &Nx::CWeather::SetRainBlendMode);
REPLACE(0x004A1060, &Nx::CWeather::SetSplashBlendMode);
REPLACE(0x004A1070, &Nx::CWeather::SetSnowBlendMode);
REPLACE(0x004A10B0, &Nx::CWeather::ScriptSetRainHeight);
REPLACE(0x004A10D0, &Nx::CWeather::ScriptSetRainFrames);
REPLACE(0x004A1110, &Nx::CWeather::ScriptSetRainLength);
REPLACE(0x004A1130, &Nx::CWeather::ScriptSetRainBlendMode);
REPLACE(0x004A1190, &Nx::CWeather::ScriptSetRainRate);
REPLACE(0x004A11B0, &Nx::CWeather::ScriptSetRainColor);
REPLACE(0x004A1240, &Nx::CWeather::ScriptSetSplashRate);
REPLACE(0x004A1260, &Nx::CWeather::ScriptSetSplashLife);
REPLACE(0x004A1280, &Nx::CWeather::ScriptSetSplashSize);
REPLACE(0x004A12A0, &Nx::CWeather::ScriptSetSplashColor);
REPLACE(0x004A12F0, &Nx::CWeather::ScriptSetSplashBlendMode);
REPLACE(0x004A1350, &Nx::CWeather::ScriptSetSnowHeight);
REPLACE(0x004A1370, &Nx::CWeather::ScriptSetSnowFrames);
REPLACE(0x004A13B0, &Nx::CWeather::ScriptSetSnowSize);
REPLACE(0x004A13D0, &Nx::CWeather::ScriptSetSnowBlendMode);
REPLACE(0x004A1430, &Nx::CWeather::ScriptSetSnowRate);
REPLACE(0x004A1450, &Nx::CWeather::ScriptSetSnowColor);
REPLACE(0x004A14A0, &Nx::CWeather::ScriptSetSnowActive);
REPLACE(0x004A14B0, &Nx::CWeather::ScriptSetRainActive);
REPLACE_UNCHECKED(0x004A1090, Nx::VirtualSlot1Jump); // (a tail jump: the arguments are the slot's)
REPLACE_UNCHECKED(0x004A10A0, Nx::VirtualSlot3Jump);
