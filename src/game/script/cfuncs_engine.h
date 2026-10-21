#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the engine, 0x00445B50-0x004489B0: the lens flare and the sun, gamma, fog,
// dynamic lighting, UV wibble and texture animation of world sectors, render modes, the animation system, the light
// manager, the screen, texture splats, clipping distances, scene lights, spawned polygons, vertex colours, metrics
// and the memory manager's commands (cfuncs_lighting.cpp, cfuncs_render.cpp, cfuncs_polys.cpp,
// cfuncs_memory.cpp). All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

// --- cfuncs_lighting.cpp -------------------------------------------------------------------------------------

// 0x00445DD0: the gamma (red, green, blue) written to the registry (Settings\Gamma R, G, B - floats stored with
// type REG_DWORD; nothing if the key can't be created).
void __cdecl WriteGamma(float red, float green, float blue);

bool __cdecl CFunc_HideLensFlare(Script::CStruct* params, Script::CScript* script);           // 0x00445B50
bool __cdecl CFunc_UnhideLensFlare(Script::CStruct* params, Script::CScript* script);         // 0x00445B70
bool __cdecl CFunc_GetTerrainTypeParam(Script::CStruct* params, Script::CScript* script);     // 0x00445B90
bool __cdecl CFunc_SetSunProps(Script::CStruct* params, Script::CScript* script);             // 0x00445C50
bool __cdecl CFunc_SetProgressive(Script::CStruct* params, Script::CScript* script);          // 0x00445D50
bool __cdecl CFunc_SetColorBufferClear(Script::CStruct* params, Script::CScript* script);     // 0x00445D90
bool __cdecl CFunc_ApplyChangeGamma(Script::CStruct* params, Script::CScript* script);        // 0x00445E60
bool __cdecl CFunc_SetDynamicLightModulationFactor(Script::CStruct* params, Script::CScript* script); // 0x00446000
bool __cdecl CFunc_EnableFog(Script::CStruct* params, Script::CScript* script);               // 0x004460B0
bool __cdecl CFunc_DisableFog(Script::CStruct* params, Script::CScript* script);              // 0x004460D0
bool __cdecl CFunc_SetFogExponent(Script::CStruct* params, Script::CScript* script);          // 0x004460F0
bool __cdecl CFunc_VCWibblePlayFromKeyframe(Script::CStruct* params, Script::CScript* script); // 0x00446130
bool __cdecl CFunc_SetUVWibbleParams(Script::CStruct* params, Script::CScript* script);       // 0x004461D0
bool __cdecl CFunc_EnableExplicitUVWibble(Script::CStruct* params, Script::CScript* script);  // 0x004462D0
bool __cdecl CFunc_DisableExplicitUVWibble(Script::CStruct* params, Script::CScript* script); // 0x00446300
bool __cdecl CFunc_SetUVWibbleOffsets(Script::CStruct* params, Script::CScript* script);      // 0x00446330
bool __cdecl CFunc_ResetTextureAnim(Script::CStruct* params, Script::CScript* script);        // 0x004463A0
bool __cdecl CFunc_StartTextureAnim(Script::CStruct* params, Script::CScript* script);        // 0x004463D0
bool __cdecl CFunc_StopTextureAnim(Script::CStruct* params, Script::CScript* script);         // 0x00446420
bool __cdecl CFunc_ToggleRenderMode(Script::CStruct* params, Script::CScript* script);        // 0x00446450

} // namespace CFuncs
