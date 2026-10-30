#pragma once

#include <cstdint>
#include "hook/game_global.h"

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B3A90-0x005B4260: create-a-graphic (the custom tag texture composed from layers
// in a workspace texture, and the workspace synced back from a skater's tag), a 2D position from a vector's length
// and angle, and the custom skater's head bone groups. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_GenerateCAGTexture(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_SyncCAGWorkspace(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Get2DPosFrom2DVec(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ResolveHeadBoneScale(Script::CStruct* params, Script::CScript* script);

GAME_GLOBAL_EXTERN(float, g_degreesToRadians); // pi / 180

} // namespace CFuncs
