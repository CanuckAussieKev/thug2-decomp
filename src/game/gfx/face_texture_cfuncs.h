#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B2420-0x005B2FC0: the custom skater's face texture (from the profile, from a
// video capture), changing a texture (offset, rotate, push to a point, tint) and converting colours between HSV and
// RGB (the game's ranges: hue 0-360, saturation 0-90, value 0-60, RGB 0-128). All are cdecl (params, script) and
// return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_LoadFaceTextureFromProfile(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_SetFaceTextureFromVideoCapture(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_OffsetTexture(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_RotateTexture(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_PushTextureToPoint(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ApplyRGBAToTexture(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_HSVtoRGB(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_RGBtoHSV(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
