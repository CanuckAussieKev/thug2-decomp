#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B2420-0x005B2FC0: the custom skater's face texture (from the profile, from a
// video capture), changing a texture (offset, rotate, push to a point, tint) and converting colours between HSV and
// RGB (the game's ranges: hue 0-360, saturation 0-90, value 0-60, RGB 0-128). All are cdecl (params, script) and
// return whether the script's `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_LoadFaceTextureFromProfile(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetFaceTextureFromVideoCapture(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_OffsetTexture(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_RotateTexture(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_PushTextureToPoint(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_ApplyRGBAToTexture(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_HSVtoRGB(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_RGBtoHSV(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace ScriptCommands
