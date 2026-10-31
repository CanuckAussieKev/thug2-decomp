#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// The script commands (CFunctions) of the sound effects, 0x0044D7D0-0x0044E2BE: loading, playing, stopping and
// changing sounds through the sound effects manager (SoundFx::CAudioMixer at 0x006F85A8, sfx.h), its volume, pitch,
// dropoff and banks. All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_LoadSound(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0044D7D0
bool __cdecl CFunc_PlaySound(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0044DC10
bool __cdecl CFunc_StopSound(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0044DD50
bool __cdecl CFunc_StopAllSounds(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044DDA0
bool __cdecl CFunc_SetSoundParams(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0044DDB0
bool __cdecl CFunc_IsSoundPlaying(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0044DED0
bool __cdecl CFunc_AddSoundBanks(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044DF10
bool __cdecl CFunc_ClearSoundBank(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0044DF70
bool __cdecl CFunc_RemoveAllSoundBanks(Scripts::CStruct* params, Scripts::CScript* script); // 0x0044DFA0
bool __cdecl CFunc_SetSfxReverb(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0044DFB0
bool __cdecl CFunc_SetSfxDropoff(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044E040
bool __cdecl CFunc_SetSfxVolume(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0044E090
bool __cdecl CFunc_SetGlobalPitchModulation(Scripts::CStruct* params, Scripts::CScript* script); // 0x0044E0C0
bool __cdecl CFunc_GetValueFromVolume(Scripts::CStruct* params, Scripts::CScript* script);  // 0x0044E200
bool __cdecl CFunc_SetPositionalSoundSilenceOverrideMode(Scripts::CStruct* params, Scripts::CScript* script); // 0x0044E290

} // namespace ScriptCommands
