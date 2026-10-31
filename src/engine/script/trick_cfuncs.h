#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B0A40-0x005B1B60: dead objects, the configurable tricks (key combos bound in the
// profile's trick_mapping or its special trick slots, trick types, display text, locking), records, soundtracks,
// controller binding and vibration, the profile lock. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_FlushDeadObjects(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_BindTrickToKeyCombo(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetKeyComboBoundToTrick(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_UpdateTrickMappings(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetConfigurableTricksFromType(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_TrickIsLocked(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetTrickDisplayText(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetSpecialTrickInfo(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetTrickType(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetIndexOfItemContaining(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetLevelRecords(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_ResetComboRecords(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetNumberOfTrickOccurrences(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetNumSoundtracks(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetSoundtrackName(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_BindControllerToSkater(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_BindFrontEndToController(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_ControllerBoundToDifferentSkater(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_ControllerBoundToSkater(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetKeyComboArrayFromTrickArray(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_FirstInputReceived(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_VibrateController(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_LockCurrentSkaterProfileIndex(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetSpecialTrickInfo(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace ScriptCommands
