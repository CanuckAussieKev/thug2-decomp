// The engine's weather: see weather.h.

#include "game/gfx/weather.h"

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"

namespace Engine3D {

namespace {

extern void** const kWeatherVtable;

float FloatParameter(Scripts::CStruct* params, float fallback)
{
    float value = fallback;
    params->FetchFloat(0u, &value, 0);
    return value;
}
int IntegerParameter(Scripts::CStruct* params, int fallback)
{
    int value = fallback;
    params->FetchIntValue(0u, &value, 0);
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
void ChangeColour(uint8_t* colour, int value) { std::memcpy(colour, &value, 4); }

// A blend mode's name (the unnamed checksum) and fixed alpha (0x20) handed to virtual [slot].
bool PassBlendMode(CWeatherSystem* weather, Scripts::CStruct* params, int slot)
{
    uint32_t name = 0;
    if (!params->ReadHashValue(0u, &name, 0))
        return false;
    int fixedAlpha = 0x20;
    params->FetchIntValue(0u, &fixedAlpha, 0);
    reinterpret_cast<void(__thiscall*)(CWeatherSystem*, uint32_t, int)>(weather->m_vtable[slot])(weather, name, fixedAlpha);
    return true;
}

} // namespace

// 0x004A14C0
CWeatherSystem* CWeatherSystem::MakeNew()
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
    m_rainOn = true;
    m_active = false;
    return this;
}

// 0x004A1080
void CWeatherSystem::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kWeatherVtable);
}

// 0x004A1550
CWeatherSystem* CWeatherSystem::Teardown(unsigned int freeFlags)
{
    m_vtable = reinterpret_cast<void**>(kWeatherVtable);
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004A1020
void CWeatherSystem::TickGrid()
{
    DebugOut::Printf("STUB: plat_update_grid\n");
}

// 0x004A1030
void CWeatherSystem::TickFrame(int)
{
    DebugOut::Printf("STUB: plat_process\n");
}

// 0x004A1040
void CWeatherSystem::Render()
{
    DebugOut::Printf("STUB: plat_render\n");
}

// 0x004A1050
void CWeatherSystem::SetRainBlendMode(uint32_t, int)
{
    DebugOut::Printf("STUB: plat_set_rain_blend_mode\n");
}

// 0x004A1060
void CWeatherSystem::SetSplashBlendMode(uint32_t, int)
{
    DebugOut::Printf("STUB: plat_set_splash_blend_mode\n");
}

// 0x004A1070
void CWeatherSystem::SetSnowBlendMode(uint32_t, int)
{
    DebugOut::Printf("STUB: plat_set_snow_blend_mode\n");
}

// 0x004A10B0
bool CWeatherSystem::ScriptSetRainHeight(Scripts::CStruct* params, Scripts::CScript*)
{
    m_rainHeight = 2000.0f;
    params->FetchFloat(0u, &m_rainHeight, 0);
    return true;
}

// 0x004A10D0
bool CWeatherSystem::ScriptSetRainFrames(Scripts::CStruct* params, Scripts::CScript*)
{
    m_rainFrames = 40;
    params->FetchIntValue(0u, &m_rainFrames, 0);
    m_rainFrames = Frames(m_rainFrames);
    return true;
}

// 0x004A1110
bool CWeatherSystem::ScriptSetRainLength(Scripts::CStruct* params, Scripts::CScript*)
{
    m_rainLength = 100.0f;
    params->FetchFloat(0u, &m_rainLength, 0);
    return true;
}

// 0x004A1130
bool CWeatherSystem::ScriptSetRainBlendMode(Scripts::CStruct* params, Scripts::CScript*)
{
    return PassBlendMode(this, params, 4);
}

// 0x004A1190
bool CWeatherSystem::ScriptSetRainRate(Scripts::CStruct* params, Scripts::CScript*)
{
    m_rainRate = 0.0f;
    params->FetchFloat(0u, &m_rainRate, 0);
    return true;
}

// 0x004A11B0
bool CWeatherSystem::ScriptSetRainColor(Scripts::CStruct* params, Scripts::CScript*)
{
    int colour = 0;
    params->FetchIntValue("top", &colour, 0);
    ChangeColour(m_rainTop, colour);
    colour = -1;
    params->FetchIntValue("bottom", &colour, 0);
    ChangeColour(m_rainBottom, colour);
    return true;
}

// 0x004A1240
bool CWeatherSystem::ScriptSetSplashRate(Scripts::CStruct* params, Scripts::CScript*)
{
    m_splashRate = 0.0f;
    params->FetchFloat(0u, &m_splashRate, 0);
    return true;
}

// 0x004A1260
bool CWeatherSystem::ScriptSetSplashLife(Scripts::CStruct* params, Scripts::CScript*)
{
    m_splashLife = 8; // (not kept within 0-254)
    params->FetchIntValue(0u, &m_splashLife, 0);
    return true;
}

// 0x004A1280
bool CWeatherSystem::ScriptSetSplashSize(Scripts::CStruct* params, Scripts::CScript*)
{
    m_splashSize = 16.0f;
    params->FetchFloat(0u, &m_splashSize, 0);
    return true;
}

// 0x004A12A0
bool CWeatherSystem::ScriptSetSplashColor(Scripts::CStruct* params, Scripts::CScript*)
{
    ChangeColour(m_splashColour, IntegerParameter(params, static_cast<int>(0x80808080)));
    return true;
}

// 0x004A12F0
bool CWeatherSystem::ScriptSetSplashBlendMode(Scripts::CStruct* params, Scripts::CScript*)
{
    return PassBlendMode(this, params, 5);
}

// 0x004A1350
bool CWeatherSystem::ScriptSetSnowHeight(Scripts::CStruct* params, Scripts::CScript*)
{
    m_snowHeight = 500.0f;
    params->FetchFloat(0u, &m_snowHeight, 0);
    return true;
}

// 0x004A1370
bool CWeatherSystem::ScriptSetSnowFrames(Scripts::CStruct* params, Scripts::CScript*)
{
    m_snowFrames = 0xFE;
    params->FetchIntValue(0u, &m_snowFrames, 0);
    m_snowFrames = Frames(m_snowFrames);
    return true;
}

// 0x004A13B0
bool CWeatherSystem::ScriptSetSnowSize(Scripts::CStruct* params, Scripts::CScript*)
{
    m_snowSize = 4.0f;
    params->FetchFloat(0u, &m_snowSize, 0);
    return true;
}

// 0x004A13D0
bool CWeatherSystem::ScriptSetSnowBlendMode(Scripts::CStruct* params, Scripts::CScript*)
{
    return PassBlendMode(this, params, 6);
}

// 0x004A1430
bool CWeatherSystem::ScriptSetSnowRate(Scripts::CStruct* params, Scripts::CScript*)
{
    m_snowRate = FloatParameter(params, 0.0f);
    return true;
}

// 0x004A1450
bool CWeatherSystem::ScriptSetSnowColor(Scripts::CStruct* params, Scripts::CScript*)
{
    ChangeColour(m_snowColour, IntegerParameter(params, static_cast<int>(0x80808080)));
    return true;
}

// 0x004A14A0
bool CWeatherSystem::ScriptSetSnowActive(Scripts::CStruct*, Scripts::CScript*)
{
    m_rainOn = false;
    return true;
}

// 0x004A14B0
bool CWeatherSystem::ScriptSetRainActive(Scripts::CStruct*, Scripts::CScript*)
{
    m_rainOn = true;
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

namespace {
static VtableSlot kWeatherVtable_slots[] = {&Engine3D::CWeatherSystem::Teardown, &Engine3D::CWeatherSystem::TickGrid, &Engine3D::CWeatherSystem::TickFrame,
                &Engine3D::CWeatherSystem::Render, &Engine3D::CWeatherSystem::SetRainBlendMode, &Engine3D::CWeatherSystem::SetSplashBlendMode,
                &Engine3D::CWeatherSystem::SetSnowBlendMode};
void** const kWeatherVtable = reinterpret_cast<void**>(kWeatherVtable_slots);
} // namespace

} // namespace Nx
