#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the game in 0x005B1B60-0x005B1EE0 and their neighbours: interpolating parameters,
// a texture's name from a path, clearing power-ups. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_InterpolateParameters(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetTextureFromPath(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ClearPowerups(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
