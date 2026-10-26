#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the skater in 0x005B45F0-0x005B4F80: a custom logo on a board part, the skater's
// velocity (scaled, optionally turned), and a random one of its voice's bail streams. All are cdecl (params,
// script) and return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_ApplyCustomLogoToPart(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetSkaterVelocity(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_PlaySkaterStream(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
