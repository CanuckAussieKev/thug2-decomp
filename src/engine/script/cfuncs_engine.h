#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the engine, 0x00445B50-0x004489B0: the lens flare and the sun, gamma, fog,
// dynamic lighting, UV wibble and texture animation of world sectors, render modes, the animation system, the light
// manager, the screen, texture splats, clipping distances, scene lights, spawned polygons, vertex colours, metrics
// and the memory manager's commands (cfuncs_lighting.cpp, cfuncs_render.cpp, cfuncs_polys.cpp,
// cfuncs_memory.cpp). All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace ScriptCommands {

// --- cfuncs_lighting.cpp -------------------------------------------------------------------------------------

// 0x00445DD0: the gamma (red, green, blue) written to the registry (Settings\Gamma R, G, B - floats stored with
// type REG_DWORD; nothing if the key can't be created).
void __cdecl WriteGamma(float red, float green, float blue);

bool __cdecl CFunc_HideLensFlare(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00445B50
bool __cdecl CFunc_UnhideLensFlare(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00445B70
bool __cdecl CFunc_GetTerrainTypeParam(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00445B90
bool __cdecl CFunc_SetSunProps(Scripts::CStruct* params, Scripts::CScript* script);             // 0x00445C50
bool __cdecl CFunc_SetProgressive(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00445D50
bool __cdecl CFunc_SetColorBufferClear(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00445D90
bool __cdecl CFunc_ApplyChangeGamma(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00445E60
bool __cdecl CFunc_SetDynamicLightModulationFactor(Scripts::CStruct* params, Scripts::CScript* script); // 0x00446000
bool __cdecl CFunc_EnableFog(Scripts::CStruct* params, Scripts::CScript* script);               // 0x004460B0
bool __cdecl CFunc_DisableFog(Scripts::CStruct* params, Scripts::CScript* script);              // 0x004460D0
bool __cdecl CFunc_SetFogExponent(Scripts::CStruct* params, Scripts::CScript* script);          // 0x004460F0
bool __cdecl CFunc_VCWibblePlayFromKeyframe(Scripts::CStruct* params, Scripts::CScript* script); // 0x00446130
bool __cdecl CFunc_SetUVWibbleParams(Scripts::CStruct* params, Scripts::CScript* script);       // 0x004461D0
bool __cdecl CFunc_EnableExplicitUVWibble(Scripts::CStruct* params, Scripts::CScript* script);  // 0x004462D0
bool __cdecl CFunc_DisableExplicitUVWibble(Scripts::CStruct* params, Scripts::CScript* script); // 0x00446300
bool __cdecl CFunc_SetUVWibbleOffsets(Scripts::CStruct* params, Scripts::CScript* script);      // 0x00446330
bool __cdecl CFunc_ResetTextureAnim(Scripts::CStruct* params, Scripts::CScript* script);        // 0x004463A0
bool __cdecl CFunc_StartTextureAnim(Scripts::CStruct* params, Scripts::CScript* script);        // 0x004463D0
bool __cdecl CFunc_StopTextureAnim(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00446420
bool __cdecl CFunc_ToggleRenderMode(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00446450

// --- cfuncs_render.cpp ---------------------------------------------------------------------------------------

bool __cdecl CFunc_DebugRenderIgnore(Scripts::CStruct* params, Scripts::CScript* script);       // 0x00446460
bool __cdecl CFunc_InitAnimCompressTable(Scripts::CStruct* params, Scripts::CScript* script);   // 0x004464C0
bool __cdecl CFunc_InitAnimSystem(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00446570
bool __cdecl CFunc_UpdateAnimCache(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00446650
bool __cdecl CFunc_FinishRendering(Scripts::CStruct* params, Scripts::CScript* script);         // 0x004466F0
bool __cdecl CFunc_InitLightManager(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00446700
bool __cdecl CFunc_UninitLightManager(Scripts::CStruct* params, Scripts::CScript* script);      // 0x00446760
bool __cdecl CFunc_PushWorldLights(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00446770
bool __cdecl CFunc_PopWorldLights(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00446780
bool __cdecl CFunc_SetScreen(Scripts::CStruct* params, Scripts::CScript* script);               // 0x00446790
bool __cdecl CFunc_AllowBoneLOD(Scripts::CStruct* params, Scripts::CScript* script);            // 0x00446810
bool __cdecl CFunc_DebugLine(Scripts::CStruct* params, Scripts::CScript* script);               // 0x00446850
bool __cdecl CFunc_GetPausePosByResolution(Scripts::CStruct* params, Scripts::CScript* script); // 0x004468E0
bool __cdecl CFunc_KillAllTextureSplats(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00446930
bool __cdecl CFunc_GetGammaValues(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00446C20
bool __cdecl CFunc_GetDynamicLightModulationFactor(Scripts::CStruct* params, Scripts::CScript* script); // 0x00446CD0
bool __cdecl CFunc_SetClippingDistances(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00446D70
bool __cdecl CFunc_SetFogDistance(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00446DF0
bool __cdecl CFunc_SetFogColor(Scripts::CStruct* params, Scripts::CScript* script);             // 0x00446E50
bool __cdecl CFunc_SetRenderMode(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00446F40
bool __cdecl CFunc_SetWireframeMode(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00446F70
bool __cdecl CFunc_SetLightAmbientColor(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00446FA0
bool __cdecl CFunc_GetLightDirection(Scripts::CStruct* params, Scripts::CScript* script);       // 0x00447010
bool __cdecl CFunc_SetLightDiffuseColor(Scripts::CStruct* params, Scripts::CScript* script);    // 0x004470A0
bool __cdecl CFunc_GetLightCurrentColor(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00447130
bool __cdecl CFunc_GetBonePosition(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00447220

// --- cfuncs_polys.cpp ----------------------------------------------------------------------------------------

// 0x004472D0: a spawned polygon's details given back to their pool pair (0x00700500).
void __cdecl FreeSpawnedPolyDetails(void* details);
// 0x00447710: a scene's sector vertex colours (of `lightGroup`, if not 0) moved towards `target` by `factor`.
void __cdecl CompressSceneVertexColours(uint8_t* scene, float target, float factor, uint32_t lightGroup);
// 0x00447A60: a scene's sector vertex colours scaled by a pseudo-random amount (by position) of up to `amount`.
void __cdecl NudgeSceneVertexColours(uint8_t* scene, float amount);

bool __cdecl CFunc_SpawnPoly(Scripts::CStruct* params, Scripts::CScript* script);               // 0x00447330
bool __cdecl CFunc_CompressVC(Scripts::CStruct* params, Scripts::CScript* script);              // 0x00447980
bool __cdecl CFunc_NudgeVC(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x00447D90
bool __cdecl CFunc_SetLightDirection(Scripts::CStruct* params, Scripts::CScript* script);       // 0x00447E00
bool __cdecl CFunc_GetMetrics(Scripts::CStruct* params, Scripts::CScript* script);              // 0x00447F30

// --- cfuncs_memory.cpp ---------------------------------------------------------------------------------------

// 0x00448330 (the first flag in BL: DumpPoolUsageStub): the pools' usage written to the dump file (MemSys::DumpText)
// and / or printed.
void __cdecl DumpPoolUsage(bool toDumpFile, bool print);
void DumpPoolUsageStub();

bool __cdecl CFunc_PushMemProfile(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00447FF0
bool __cdecl CFunc_PopMemProfile(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00448020
bool __cdecl CFunc_SetMemThreadSafe(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00448030
bool __cdecl CFunc_DumpFragments(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00448060
bool __cdecl CFunc_MemViewToggle(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00448090
bool __cdecl CFunc_GotExtraMemory(Scripts::CStruct* params, Scripts::CScript* script);          // 0x004480A0
bool __cdecl CFunc_MemPushContext(Scripts::CStruct* params, Scripts::CScript* script);          // 0x004480B0
bool __cdecl CFunc_MemPopContext(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00448130
bool __cdecl CFunc_MemInitHeap(Scripts::CStruct* params, Scripts::CScript* script);             // 0x00448140
bool __cdecl CFunc_MemDeleteHeap(Scripts::CStruct* params, Scripts::CScript* script);           // 0x004481A0
bool __cdecl CFunc_MemHeapExists(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00448200
bool __cdecl CFunc_AnalyzeHeap(Scripts::CStruct* params, Scripts::CScript* script);             // 0x00448270
bool __cdecl CFunc_PrintMemInfo(Scripts::CStruct* params, Scripts::CScript* script);            // 0x004482C0
bool __cdecl CFunc_DumpHeaps(Scripts::CStruct* params, Scripts::CScript* script);               // 0x00448840

} // namespace ScriptCommands
