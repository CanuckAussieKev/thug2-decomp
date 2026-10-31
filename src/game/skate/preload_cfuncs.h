#pragma once

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) preloading what the level's nodes and the pedestrians use (0x005AAFA0-0x005ABBEF):
// the vehicles' and game objects' models (with their collision and the vehicles' scripts), the pedestrians' models
// and CAS parts, the random pedestrians' part choices, the cars' textures replaced. All are cdecl (params, script)
// and return whether the script's `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_PreloadModels(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005AAFA0
bool __cdecl CFunc_PreloadPedestrians(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005AB300
bool __cdecl CFunc_PreselectRandomPedestrians(Scripts::CStruct* params, Scripts::CScript* script); // 0x005AB690
bool __cdecl CFunc_ReplaceCarTextures(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005ABB20

} // namespace ScriptCommands
