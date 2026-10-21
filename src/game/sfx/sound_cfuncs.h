#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The script commands (CFunctions) of the sound effects, 0x0044D7D0-0x0044E2BE: loading, playing, stopping and
// changing sounds through the sound effects manager (Sfx::CSfxManager at 0x006F85A8, sfx.h), its volume, pitch,
// dropoff and banks. All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_LoadSound(Script::CStruct* params, Script::CScript* script);           // 0x0044D7D0
bool __cdecl CFunc_PlaySound(Script::CStruct* params, Script::CScript* script);           // 0x0044DC10
bool __cdecl CFunc_StopSound(Script::CStruct* params, Script::CScript* script);           // 0x0044DD50
bool __cdecl CFunc_StopAllSounds(Script::CStruct* params, Script::CScript* script);       // 0x0044DDA0
bool __cdecl CFunc_SetSoundParams(Script::CStruct* params, Script::CScript* script);      // 0x0044DDB0
bool __cdecl CFunc_IsSoundPlaying(Script::CStruct* params, Script::CScript* script);      // 0x0044DED0
bool __cdecl CFunc_AddSoundBanks(Script::CStruct* params, Script::CScript* script);       // 0x0044DF10
bool __cdecl CFunc_ClearSoundBank(Script::CStruct* params, Script::CScript* script);      // 0x0044DF70
bool __cdecl CFunc_RemoveAllSoundBanks(Script::CStruct* params, Script::CScript* script); // 0x0044DFA0
bool __cdecl CFunc_SetSfxReverb(Script::CStruct* params, Script::CScript* script);        // 0x0044DFB0
bool __cdecl CFunc_SetSfxDropoff(Script::CStruct* params, Script::CScript* script);       // 0x0044E040
bool __cdecl CFunc_SetSfxVolume(Script::CStruct* params, Script::CScript* script);        // 0x0044E090
bool __cdecl CFunc_SetGlobalPitchModulation(Script::CStruct* params, Script::CScript* script); // 0x0044E0C0
bool __cdecl CFunc_GetValueFromVolume(Script::CStruct* params, Script::CScript* script);  // 0x0044E200
bool __cdecl CFunc_SetPositionalSoundSilenceOverrideMode(Script::CStruct* params, Script::CScript* script); // 0x0044E290

} // namespace CFuncs
