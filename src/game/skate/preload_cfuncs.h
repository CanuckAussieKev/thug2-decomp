#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) preloading what the level's nodes and the pedestrians use (0x005AAFA0-0x005ABBEF):
// the vehicles' and game objects' models (with their collision and the vehicles' scripts), the pedestrians' models
// and CAS parts, the random pedestrians' part choices, the cars' textures replaced. All are cdecl (params, script)
// and return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_PreloadModels(Script::CStruct* params, Script::CScript* script);              // 0x005AAFA0
bool __cdecl CFunc_PreloadPedestrians(Script::CStruct* params, Script::CScript* script);         // 0x005AB300
bool __cdecl CFunc_PreselectRandomPedestrians(Script::CStruct* params, Script::CScript* script); // 0x005AB690
bool __cdecl CFunc_ReplaceCarTextures(Script::CStruct* params, Script::CScript* script);         // 0x005ABB20

} // namespace CFuncs
