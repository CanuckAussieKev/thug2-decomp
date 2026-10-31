#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the players' appearances (0x005AAEF0-0x005B0A40, the appearance ones): a player's
// appearance and its face (the face points, the face texture and its overlay), the skater's model rebuilt
// from its profile (colours, scale, visibility, UV, the whole model), the appearance's parts and their colour, scale and
// UV settings. All are cdecl (params, script) and
// return whether the script's `if` should be taken (true where nothing is asked).
namespace ScriptCommands {

// A face's points (0x38 bytes; the face texture's 0x00487550 gives a copy, 0x00487570 sets them).
struct FacePoints {
    uint32_t m_words[14];
};

bool __cdecl CFunc_SetPlayerAppearance(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005AE550
bool __cdecl CFunc_GetPlayerFacePoints(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005AE5B0
bool __cdecl CFunc_SetPlayerFacePoints(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005AE710
bool __cdecl CFunc_SetPlayerFaceTexture(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005AE7A0
bool __cdecl CFunc_SetPlayerFaceOverlayTexture(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AE820
bool __cdecl CFunc_ClearPlayerFaceTexture(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005AE880
bool __cdecl CFunc_PlayerFaceIsValid(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005AE8C0
bool __cdecl CFunc_RefreshSkaterColors(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005AF750
bool __cdecl CFunc_RefreshSkaterScale(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AF7D0
bool __cdecl CFunc_RefreshSkaterVisibility(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005AF850
bool __cdecl CFunc_RefreshSkaterUV(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AF8D0
bool __cdecl CFunc_RefreshSkaterModel(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AFD80
bool __cdecl CFunc_EditPlayerAppearance(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005AFF30
bool __cdecl CFunc_GetActualCASOptionStruct(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005B0420
bool __cdecl CFunc_GetActualPlayerAppearancePart(Scripts::CStruct* params, Scripts::CScript* script); // 0x005B0480
bool __cdecl CFunc_GetPlayerAppearancePart(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005B0500
bool __cdecl CFunc_GetPlayerAppearance(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005B0580
bool __cdecl CFunc_SetPlayerAppearanceColor(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005B0670
bool __cdecl CFunc_SetPlayerAppearanceScale(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005B0780
bool __cdecl CFunc_SetPlayerAppearanceUV(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005B0920

} // namespace ScriptCommands
