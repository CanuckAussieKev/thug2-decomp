#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B3A90-0x005B4260: create-a-graphic (the custom tag texture composed from layers
// in a workspace texture, and the workspace synced back from a skater's tag), a 2D position from a vector's length
// and angle, and the custom skater's head bone groups. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_GenerateCAGTexture(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SyncCAGWorkspace(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Get2DPosFrom2DVec(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_ResolveHeadBoneScale(Scripts::CStruct* params, Scripts::CScript* script);

extern float g_degreesToRadians; // pi / 180

} // namespace ScriptCommands
