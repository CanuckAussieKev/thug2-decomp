#pragma once

#include <cstdint>
#include "hook/game_global.h"

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) and helpers 0x005A9D80-0x005AA92F: SceAltPt, two searches through a structure for a
// value's name and an angle wrap (helpers of the cutscene / CAT code next to them), CompositeObjectExists, the
// textures (the texture dictionary 0x00700F1C: load, create, unload, the 32-bit image, flip, resize, scale, regions,
// pull to an edge, brightness, HSV, copy, combine; the particle dictionary 0x00700F20: particle and SFP textures) and
// SetParkName. The CFunctions are cdecl (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

// 0x005A9D80: SceAltPt: the flag 0x007CE441 set. True.
bool __cdecl CFunc_SceAltPt(Script::CStruct* params, Script::CScript* script);

// 0x005A9D90: the name of the first name component (in `structure`, its structures, and the global structures its
// components' values name - 10 levels at most) whose value is `value`; 0 if none.
uint32_t __cdecl FindNameOfValue(Script::CStruct* structure, uint32_t value, int depth);
// 0x005A9E30 (the structure in EBX; cdecl the value): the name of the last integer component of `structure` that
// is `value` (each found one printed: "s_find_cat_in_mapping found %i"); 0 if none.
uint32_t FindNameOfInteger(Script::CStruct* structure, uint32_t value);
void FindNameOfIntegerStub();
// 0x005A9E80: an angle in degrees brought into 0..360 (as the x87 computes it: in double precision).
float __cdecl WrapAngle(float degrees);

// 0x005A9ED0: CompositeObjectExists name=<id>: whether there's an object with that id.
bool __cdecl CFunc_CompositeObjectExists(Script::CStruct* params, Script::CScript* script);

// The texture dictionary's commands (each finds its texture by the unnamed checksum; true unless noted).
bool __cdecl CFunc_LoadTexture(Script::CStruct* params, Script::CScript* script);      // 0x005A9F00 (loaded?)
// (not original) Whether the texture dictionary has the texture of a file name as LoadTexture / UnloadTexture take it
// ("folder/name": the dictionary's own name lookup, as UnloadTexture's).
bool TextureLoaded(const char* file);
bool __cdecl CFunc_CreateTexture(Script::CStruct* params, Script::CScript* script);    // 0x005A9F60 (made?)
bool __cdecl CFunc_UnloadTexture(Script::CStruct* params, Script::CScript* script);    // 0x005AA040
bool __cdecl CFunc_Generate32BitImage(Script::CStruct* params, Script::CScript* script); // 0x005AA0C0
bool __cdecl CFunc_Put32BitImageIntoTexture(Script::CStruct* params, Script::CScript* script); // 0x005AA160
bool __cdecl CFunc_FlipTexture(Script::CStruct* params, Script::CScript* script);      // 0x005AA1E0
bool __cdecl CFunc_ResizeTextureInto(Script::CStruct* params, Script::CScript* script); // 0x005AA270
bool __cdecl CFunc_ScaleTexture(Script::CStruct* params, Script::CScript* script);     // 0x005AA2E0
bool __cdecl CFunc_AdjustTextureRegion(Script::CStruct* params, Script::CScript* script); // 0x005AA340
bool __cdecl CFunc_PullTextureToEdge(Script::CStruct* params, Script::CScript* script); // 0x005AA460
bool __cdecl CFunc_AdjustTextureBrightness(Script::CStruct* params, Script::CScript* script); // 0x005AA500
bool __cdecl CFunc_AdjustTextureHSV(Script::CStruct* params, Script::CScript* script);  // 0x005AA590
bool __cdecl CFunc_CopyTexture(Script::CStruct* params, Script::CScript* script);      // 0x005AA670
bool __cdecl CFunc_CombineTextures(Script::CStruct* params, Script::CScript* script);  // 0x005AA6E0
// The particle texture dictionary's (by the unnamed file name string).
bool __cdecl CFunc_LoadParticleTexture(Script::CStruct* params, Script::CScript* script);   // 0x005AA7B0 (loaded?)
bool __cdecl CFunc_UnloadParticleTexture(Script::CStruct* params, Script::CScript* script); // 0x005AA830
bool __cdecl CFunc_LoadSFPTexture(Script::CStruct* params, Script::CScript* script);        // 0x005AA880 (loaded?)
bool __cdecl CFunc_UnloadSFPTexture(Script::CStruct* params, Script::CScript* script);      // 0x005AA8B0

// 0x005AA8F0: SetParkName <string>: copied to the park name (0x007CED58, no length limit). False.
bool __cdecl CFunc_SetParkName(Script::CStruct* params, Script::CScript* script);

GAME_GLOBAL_EXTERN(bool, g_textureFlagCe441);
GAME_GLOBAL_EXTERN(char[128], g_parkName);
} // namespace CFuncs
