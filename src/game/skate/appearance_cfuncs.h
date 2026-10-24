#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the players' appearances (0x005AAEF0-0x005B0A40, the appearance ones): a player's
// appearance and its face (the face points, the face texture and its overlay). All are cdecl (params, script) and
// return whether the script's `if` should be taken (true where nothing is asked).
namespace CFuncs {

// A face's points (0x38 bytes; the face texture's 0x00487550 gives a copy, 0x00487570 sets them).
struct FacePoints {
    uint32_t m_words[14];
};

bool __cdecl CFunc_SetPlayerAppearance(Script::CStruct* params, Script::CScript* script);         // 0x005AE550
bool __cdecl CFunc_GetPlayerFacePoints(Script::CStruct* params, Script::CScript* script);         // 0x005AE5B0
bool __cdecl CFunc_SetPlayerFacePoints(Script::CStruct* params, Script::CScript* script);         // 0x005AE710
bool __cdecl CFunc_SetPlayerFaceTexture(Script::CStruct* params, Script::CScript* script);        // 0x005AE7A0
bool __cdecl CFunc_SetPlayerFaceOverlayTexture(Script::CStruct* params, Script::CScript* script); // 0x005AE820
bool __cdecl CFunc_ClearPlayerFaceTexture(Script::CStruct* params, Script::CScript* script);      // 0x005AE880
bool __cdecl CFunc_PlayerFaceIsValid(Script::CStruct* params, Script::CScript* script);           // 0x005AE8C0

} // namespace CFuncs
