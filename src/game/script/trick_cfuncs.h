#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B0A40-0x005B1B60: dead objects, the configurable tricks (key combos bound in the
// profile's trick_mapping or its special trick slots, trick types, display text, locking), records, soundtracks,
// controller binding and vibration, the profile lock. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_FlushDeadObjects(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_BindTrickToKeyCombo(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetKeyComboBoundToTrick(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_UpdateTrickMappings(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetConfigurableTricksFromType(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TrickIsLocked(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetTrickDisplayText(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetSpecialTrickInfo(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetTrickType(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetIndexOfItemContaining(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetLevelRecords(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ResetComboRecords(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetNumberOfTrickOccurrences(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetNumSoundtracks(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetSoundtrackName(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_BindControllerToSkater(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_BindFrontEndToController(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ControllerBoundToDifferentSkater(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ControllerBoundToSkater(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetKeyComboArrayFromTrickArray(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_FirstInputReceived(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_VibrateController(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_LockCurrentSkaterProfileIndex(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_SetSpecialTrickInfo(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
