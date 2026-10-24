#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the game in 0x005B1B60-0x005B34A0: interpolating parameters, a texture's name from
// a path, clearing power-ups, the nearest rail point, the park's name, the special bar's colours, the default
// initials, the fireball level, a skater's last name, the directional light's debug lines, the slap game,
// a preference set from a menu. All are
// cdecl (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_InterpolateParameters(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetTextureFromPath(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ClearPowerups(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_FindNearestRailPoint(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetParkName(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_SetSpecialBarColors(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetInitialsString(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_SetInitialsString(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetFireballLevel(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetSkaterLastNameString(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_DrawDirectionalLightLines(Script::CStruct* params, Script::CScript* script);
// (also called directly by the game: the arguments are ignored)
bool __cdecl CFunc_InSlapGame(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_SetPreferencesFromUI(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
