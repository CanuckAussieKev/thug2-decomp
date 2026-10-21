// Script commands of the engine, 0x00445B50-0x00446450: the lens flare, terrain sounds, the sun, gamma, fog,
// dynamic lighting, UV wibble and texture animation of world sectors (see cfuncs_engine.h).

#include "game/script/cfuncs_engine.h"

#include <windows.h>

#include <cstdint>

#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

using Script::CScript;
using Script::CStruct;

namespace {

constexpr uint32_t kParam = 0x5B058376;       // "param"
constexpr uint32_t kTerrain = 0x3789AC4E;     // "terrain"
constexpr uint32_t kTerrainIndex = 0x75D274BE; // (name unknown) the terrain as a number
constexpr uint32_t kTheta = 0xF4E775F6;       // "theta"
constexpr uint32_t kPhi = 0xCD0576EE;         // "phi"
constexpr uint32_t kRadius = 0xC48391A5;      // "radius"
constexpr uint32_t kSize = 0x083FDB95;        // "size"
constexpr uint32_t kTexture = 0x7D99F28D;     // "texture"
constexpr uint32_t kCoronaTexture = 0xEB1CCC76; // "corona_texture"
constexpr uint32_t kEnable = 0x0C5F37CD;      // "enable"
constexpr uint32_t kDisable = 0xB0BAB2AE;     // "disable"
constexpr uint32_t kOn = 0xF649D637;          // "on"
constexpr uint32_t kClear = 0x1A4E0EF9;       // "clear"
constexpr uint32_t kRed = 0x059EA070;         // "red"
constexpr uint32_t kGreen = 0x2F6511DE;       // "green"
constexpr uint32_t kBlue = 0x61C9354B;        // "blue"
constexpr uint32_t kValue = 0xE288A7CB;       // "value"
constexpr uint32_t kValue2 = 0xC483C21F;      // "value2"
constexpr uint32_t kAmbient = 0xBCEE1D37;     // "ambient"
constexpr uint32_t kDirectional = 0xB8F8BBE6; // "directional"
constexpr uint32_t kSector = 0xB45C2617;      // "sector"
constexpr uint32_t kSeq = 0x69812EAC;         // "seq"
constexpr uint32_t kKey = 0x756F5456;         // "key"
constexpr uint32_t kTime = 0x906B67BA;        // "time"
constexpr uint32_t kReset = 0xAF6240B2;       // "reset"

// Fog and clipping distances are left alone while distance clipping (the setting) is on.
bool DistanceClipping() { return GameVariable<bool>(0x007D643A); }

// The world sector named `name` (null if the scene has none of that name).
void* GetSector(uint32_t name) { return GameFunction<void*(__cdecl*)(uint32_t)>(0x0048C510)(name); }

// The renderer's gamma (0x004B24D0 / 0x004B24C0 through the jumps 0x0048C140 / 0x0048C130).
void GetGamma(float* red, float* green, float* blue)
{
    GameFunction<void(__cdecl*)(float*, float*, float*)>(0x0048C140)(red, green, blue);
}
void SetGamma(float red, float green, float blue)
{
    GameFunction<void(__cdecl*)(float, float, float)>(0x0048C130)(red, green, blue);
}

// Whether the frame buffer is cleared each frame - compiled out of this build (0x0048C1B0, a jump to the empty
// function 0x005480A0).
inline void SetColorBufferClear(bool /*clear*/) {}

// The sun's settings as SetSunProps passes them (0x004426D0 copies the fields its mask names).
struct SunProps {
    float theta;               // +0x00 (mask 0x01)
    float phi;                 // +0x04 (0x02)
    float radius;              // +0x08 (0x04)
    float size;                // +0x0C (0x08)
    uint32_t texture;          // +0x10 (0x20)
    uint32_t coronaTexture;    // +0x14 (0x40)
    float colour[4];           // +0x18 (0x10: never set by the command)
    uint8_t unknown28[0x18];
    bool enabled;              // +0x40 (0x80)
    uint8_t pad41[3];
};
static_assert(sizeof(SunProps) == 0x44, "layout must match the original");

} // namespace

// 0x00445B50: HideLensFlare (0x00442DF0 with the same arguments). Always true.
bool __cdecl CFunc_HideLensFlare(CStruct* params, CScript* script)
{
    GameFunction<void(__cdecl*)(CStruct*, CScript*)>(0x00442DF0)(params, script);
    return true;
}

// 0x00445B70: UnhideLensFlare (0x00443030). Always true.
bool __cdecl CFunc_UnhideLensFlare(CStruct* params, CScript* script)
{
    GameFunction<void(__cdecl*)(CStruct*, CScript*)>(0x00443030)(params, script);
    return true;
}

// 0x00445B90: GetTerrainTypeParam param=<name> terrain=<checksum> (or the terrain's number): the terrain's
// structure for `param` (0x00444480) added to the script's params as `param`. False if there is none.
bool __cdecl CFunc_GetTerrainTypeParam(CStruct* params, CScript* script)
{
    uint32_t param = 0;
    uint32_t terrainName = 0;
    int terrainIndex = 0;
    params->GetChecksum(kParam, &param, 1);
    uint8_t terrain;
    if (params->GetChecksum(kTerrain, &terrainName, 0)) {
        terrain = GameFunction<uint8_t(__cdecl*)(uint32_t)>(0x004442C0)(terrainName);
    } else {
        params->GetInteger(kTerrainIndex, &terrainIndex, 1);
        terrain = static_cast<uint8_t>(terrainIndex);
    }
    CStruct* value = nullptr; // (uninitialised in the original: the callee sets it)
    if (!GameFunction<bool(__cdecl*)(CStruct**, uint8_t, uint32_t)>(0x00444480)(&value, terrain, param))
        return false;
    script->m_params->AddStructure(param, value);
    return true;
}

// 0x00445C50: SetSunProps [theta] [phi] [radius] [size] [texture] [corona_texture] [enable | disable]: the
// settings given are passed on (0x004426D0 with the mask of those given). Always true.
bool __cdecl CFunc_SetSunProps(CStruct* params, CScript*)
{
    SunProps props{}; // (the fields not given are uninitialised in the original; the callee doesn't read them)
    uint32_t mask = 0;
    if (params->GetFloat(kTheta, &props.theta, 0))
        mask = 0x01;
    if (params->GetFloat(kPhi, &props.phi, 0))
        mask |= 0x02;
    if (params->GetFloat(kRadius, &props.radius, 0))
        mask |= 0x04;
    if (params->GetFloat(kSize, &props.size, 0))
        mask |= 0x08;
    if (params->GetChecksum(kTexture, &props.texture, 0))
        mask |= 0x20;
    if (params->GetChecksum(kCoronaTexture, &props.coronaTexture, 0))
        mask |= 0x40;
    if (params->ContainsFlag(kEnable)) {
        props.enabled = true;
        mask |= 0x80;
    } else if (params->ContainsFlag(kDisable)) {
        props.enabled = false;
        mask |= 0x80;
    }
    GameFunction<void(__cdecl*)(SunProps*, uint32_t)>(0x004426D0)(&props, mask);
    return true;
}

// 0x00445D50: SetProgressive on=<n>: progressive scan (0x005B6090) on for n > 0. Always true.
bool __cdecl CFunc_SetProgressive(CStruct* params, CScript*)
{
    int on = 0;
    params->GetInteger(kOn, &on, 0);
    GameFunction<void(__cdecl*)(bool)>(0x005B6090)(on > 0);
    return true;
}

// 0x00445D90: SetColorBufferClear clear=<n> (does nothing in this build). Always true.
bool __cdecl CFunc_SetColorBufferClear(CStruct* params, CScript*)
{
    int clear = 0;
    params->GetInteger(kClear, &clear, 0);
    SetColorBufferClear(clear > 0);
    return true;
}

// 0x00445DD0
void __cdecl WriteGamma(float red, float green, float blue)
{
    HKEY key = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Activision\\Tony Hawk's Underground 2\\Settings\\Gamma", 0,
                        nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExA(key, "R", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&red), 4);
    RegSetValueExA(key, "G", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&green), 4);
    RegSetValueExA(key, "B", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&blue), 4);
    RegCloseKey(key);
}

// 0x00445E60: ApplyChangeGamma color=red|green|blue change=<amount>: the channel's gamma changed by `amount`
// while it stays in range (raised only below 1, lowered only above 0), applied and saved - twice, as the original
// does. Always true.
bool __cdecl CFunc_ApplyChangeGamma(CStruct* params, CScript*)
{
    float red, green, blue;
    GetGamma(&red, &green, &blue);
    uint32_t color = 0;  // (uninitialised in the original: both are asserted)
    float change = 0.0f;
    params->GetChecksum("color", &color, 1);
    params->GetFloat("change", &change, 1);
    float* channel = nullptr;
    if (color == kRed)
        channel = &red;
    else if (color == kGreen)
        channel = &green;
    else if (color == kBlue)
        channel = &blue;
    if (channel && ((change > 0.0f && *channel < 1.0f) || (change < 0.0f && *channel > 0.0f)))
        *channel = change + *channel;
    SetGamma(red, green, blue);
    WriteGamma(red, green, blue);
    WriteGamma(red, green, blue);
    return true;
}

// 0x00446000: SetDynamicLightModulationFactor value=<f> [value2=<f>] ambient | directional=<index>: the ambient
// modulation factors (0x0048E970) or a directional light's (0x0048E990). Nothing without `value`. Always true.
bool __cdecl CFunc_SetDynamicLightModulationFactor(CStruct* params, CScript*)
{
    float value = 0.0f;
    float value2 = 0.0f;
    int directional = 0;
    if (!params->GetFloat(kValue, &value, 0))
        return true;
    params->GetFloat(kValue2, &value2, 0);
    if (params->ContainsFlag(kAmbient)) {
        GameFunction<void(__cdecl*)(float, float)>(0x0048E970)(value, value2);
        return true;
    }
    params->GetInteger(kDirectional, &directional, 0);
    GameFunction<void(__cdecl*)(int, float, float)>(0x0048E990)(directional, value, value2);
    return true;
}

// 0x004460B0: EnableFog (0x00490DE0), unless distance clipping is on. Always true.
bool __cdecl CFunc_EnableFog(CStruct*, CScript*)
{
    if (!DistanceClipping())
        GameFunction<void(__cdecl*)(bool)>(0x00490DE0)(true);
    return true;
}

// 0x004460D0: DisableFog, unless distance clipping is on. Always true.
bool __cdecl CFunc_DisableFog(CStruct*, CScript*)
{
    if (!DistanceClipping())
        GameFunction<void(__cdecl*)(bool)>(0x00490DE0)(false);
    return true;
}

// 0x004460F0: SetFogExponent exponent=<f> (0x00490E10). False (and nothing) while distance clipping is on.
bool __cdecl CFunc_SetFogExponent(CStruct* params, CScript*)
{
    if (DistanceClipping())
        return false;
    float exponent = 0.0f; // (uninitialised in the original)
    params->GetFloat("exponent", &exponent, 0);
    GameFunction<void(__cdecl*)(float)>(0x00490E10)(exponent);
    return true;
}

// 0x00446130: VCWibblePlayFromKeyframe sector=<name> [seq] [key] [time=seconds]: the sector's vertex colour
// wibble played from a keyframe (0x0049E820, time in ms). False without a sector name; true otherwise (also if
// the scene has no such sector).
bool __cdecl CFunc_VCWibblePlayFromKeyframe(CStruct* params, CScript*)
{
    uint32_t name;
    if (!params->GetChecksum(kSector, &name, 0))
        return false;
    void* sector = GetSector(name);
    if (!sector)
        return true;
    int seq = 0; // (seq and key are uninitialised in the original)
    int key = 0;
    int time = 0;
    params->GetInteger(kSeq, &seq, 0);
    params->GetInteger(kKey, &key, 0);
    params->GetInteger(kTime, &time, 0);
    GameFunction<void(__thiscall*)(void*, int, int, int)>(0x0049E820)(sector, seq, key, time * 1000);
    return true;
}

// 0x004461D0: SetUVWibbleParams sector=<name> u_vel u_amp u_freq u_phase v_vel v_amp v_freq v_phase: the
// sector's UV wibble (0x0049E830 - on a missing sector too, as the original). Always true.
bool __cdecl CFunc_SetUVWibbleParams(CStruct* params, CScript*)
{
    uint32_t name = 0; // (these are uninitialised in the original)
    params->GetChecksum("sector", &name, 0);
    void* sector = GetSector(name);
    float uVel = 0.0f, uAmp = 0.0f, uFreq = 0.0f, uPhase = 0.0f;
    float vVel = 0.0f, vAmp = 0.0f, vFreq = 0.0f, vPhase = 0.0f;
    params->GetFloat("u_vel", &uVel, 0);
    params->GetFloat("u_amp", &uAmp, 0);
    params->GetFloat("u_freq", &uFreq, 0);
    params->GetFloat("u_phase", &uPhase, 0);
    params->GetFloat("v_vel", &vVel, 0);
    params->GetFloat("v_amp", &vAmp, 0);
    params->GetFloat("v_freq", &vFreq, 0);
    params->GetFloat("v_phase", &vPhase, 0);
    GameFunction<void(__thiscall*)(void*, float, float, float, float, float, float, float, float)>(0x0049E830)(
        sector, uVel, uAmp, uFreq, uPhase, vVel, vAmp, vFreq, vPhase);
    return true;
}

namespace {
// The sector named by the params' "sector" (uninitialised in the original when missing).
void* NamedSector(CStruct* params)
{
    uint32_t name = 0;
    params->GetChecksum("sector", &name, 0);
    return GetSector(name);
}
} // namespace

// 0x004462D0: EnableExplicitUVWibble sector=<name> (0x0049E840). Always true.
bool __cdecl CFunc_EnableExplicitUVWibble(CStruct* params, CScript*)
{
    GameFunction<void(__thiscall*)(void*, bool)>(0x0049E840)(NamedSector(params), true);
    return true;
}

// 0x00446300: DisableExplicitUVWibble sector=<name>. Always true.
bool __cdecl CFunc_DisableExplicitUVWibble(CStruct* params, CScript*)
{
    GameFunction<void(__thiscall*)(void*, bool)>(0x0049E840)(NamedSector(params), false);
    return true;
}

// 0x00446330: SetUVWibbleOffsets sector=<name> u_off v_off (0x0049E850). Always true.
bool __cdecl CFunc_SetUVWibbleOffsets(CStruct* params, CScript*)
{
    void* sector = NamedSector(params);
    float uOff = 0.0f; // (uninitialised in the original)
    float vOff = 0.0f;
    params->GetFloat("u_off", &uOff, 0);
    params->GetFloat("v_off", &vOff, 0);
    GameFunction<void(__thiscall*)(void*, float, float)>(0x0049E850)(sector, uOff, vOff);
    return true;
}

// 0x004463A0: ResetTextureAnim sector=<name> (0x0049E860). Always true.
bool __cdecl CFunc_ResetTextureAnim(CStruct* params, CScript*)
{
    GameFunction<void(__thiscall*)(void*)>(0x0049E860)(NamedSector(params));
    return true;
}

// 0x004463D0: StartTextureAnim sector=<name> [reset] (0x0049E870). Always true.
bool __cdecl CFunc_StartTextureAnim(CStruct* params, CScript*)
{
    void* sector = NamedSector(params);
    bool reset = params->ContainsFlag(kReset);
    GameFunction<void(__thiscall*)(void*, bool)>(0x0049E870)(sector, reset);
    return true;
}

// 0x00446420: StopTextureAnim sector=<name> (0x0049E880). Always true.
bool __cdecl CFunc_StopTextureAnim(CStruct* params, CScript*)
{
    GameFunction<void(__thiscall*)(void*)>(0x0049E880)(NamedSector(params));
    return true;
}

// 0x00446450: ToggleRenderMode: the next render mode (0x0048C0D0: 0..3 in turn). Always true.
bool __cdecl CFunc_ToggleRenderMode(CStruct*, CScript*)
{
    GameFunction<void(__cdecl*)()>(0x0048C0D0)();
    return true;
}

} // namespace CFuncs

REPLACE(0x00445B50, CFuncs::CFunc_HideLensFlare);
REPLACE(0x00445B70, CFuncs::CFunc_UnhideLensFlare);
REPLACE(0x00445B90, CFuncs::CFunc_GetTerrainTypeParam);
REPLACE(0x00445C50, CFuncs::CFunc_SetSunProps);
REPLACE(0x00445D50, CFuncs::CFunc_SetProgressive);
REPLACE(0x00445D90, CFuncs::CFunc_SetColorBufferClear);
REPLACE(0x00445DD0, CFuncs::WriteGamma);
REPLACE(0x00445E60, CFuncs::CFunc_ApplyChangeGamma);
REPLACE(0x00446000, CFuncs::CFunc_SetDynamicLightModulationFactor);
REPLACE(0x004460B0, CFuncs::CFunc_EnableFog);
REPLACE(0x004460D0, CFuncs::CFunc_DisableFog);
REPLACE(0x004460F0, CFuncs::CFunc_SetFogExponent);
REPLACE(0x00446130, CFuncs::CFunc_VCWibblePlayFromKeyframe);
REPLACE(0x004461D0, CFuncs::CFunc_SetUVWibbleParams);
REPLACE(0x004462D0, CFuncs::CFunc_EnableExplicitUVWibble);
REPLACE(0x00446300, CFuncs::CFunc_DisableExplicitUVWibble);
REPLACE(0x00446330, CFuncs::CFunc_SetUVWibbleOffsets);
REPLACE(0x004463A0, CFuncs::CFunc_ResetTextureAnim);
REPLACE(0x004463D0, CFuncs::CFunc_StartTextureAnim);
REPLACE(0x00446420, CFuncs::CFunc_StopTextureAnim);
REPLACE(0x00446450, CFuncs::CFunc_ToggleRenderMode);
