#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the game in 0x005B1B60-0x005B34A0: interpolating parameters, a texture's name from
// a path, clearing power-ups, the nearest rail point, the park's name, the special bar's colours, the default
// initials, the fireball level, a skater's last name, the directional light's debug lines, the slap game,
// a preference set from a menu. All are
// cdecl (params, script) and return whether the script's `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_InterpolateParameters(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetTextureFromPath(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_ClearPowerups(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_FindNearestRailPoint(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetParkName(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetSpecialBarColors(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetInitialsString(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetInitialsString(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetFireballLevel(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetSkaterLastNameString(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_DrawDirectionalLightLines(Scripts::CStruct* params, Scripts::CScript* script);
// (also called directly by the game: the arguments are ignored)
bool __cdecl CFunc_InSlapGame(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetPreferencesFromUI(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace ScriptCommands
