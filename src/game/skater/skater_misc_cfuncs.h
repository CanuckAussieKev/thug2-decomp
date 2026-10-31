#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the skater in 0x005B45F0-0x005B4F80: a custom logo on a board part, the skater's
// velocity (scaled, optionally turned), and a random one of its voice's bail streams. All are cdecl (params,
// script) and return whether the script's `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_ApplyCustomLogoToPart(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetSkaterVelocity(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_PlaySkaterStream(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace ScriptCommands
