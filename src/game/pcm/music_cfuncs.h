#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The script commands (CFunctions) of the music and streams, 0x0044D100-0x0044D7C7: thin wrappers reading their
// parameters and calling the Pcm functions (music.h, music_api.h, stream_commands.h). All are cdecl (params,
// script) and return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_StreamExists(Script::CStruct* params, Script::CScript* script);          // 0x0044D100
bool __cdecl CFunc_AddStream(Script::CStruct* params, Script::CScript* script);             // 0x0044D140
bool __cdecl CFunc_RemoveStream(Script::CStruct* params, Script::CScript* script);          // 0x0044D1B0
bool __cdecl CFunc_StopStream(Script::CStruct* params, Script::CScript* script);            // 0x0044D220
bool __cdecl CFunc_IsStreamPlaying(Script::CStruct* params, Script::CScript* script);       // 0x0044D260
bool __cdecl CFunc_PlayTrack(Script::CStruct* params, Script::CScript* script);             // 0x0044D2A0
bool __cdecl CFunc_PlayMusicStream(Script::CStruct* params, Script::CScript* script);       // 0x0044D380
bool __cdecl CFunc_LoadStreamFrameAmp(Script::CStruct* params, Script::CScript* script);    // 0x0044D3E0
bool __cdecl CFunc_FreeStreamFrameAmp(Script::CStruct* params, Script::CScript* script);    // 0x0044D420
bool __cdecl CFunc_SetMusicVolume(Script::CStruct* params, Script::CScript* script);        // 0x0044D460
bool __cdecl CFunc_SetMusicStreamVolume(Script::CStruct* params, Script::CScript* script);  // 0x0044D490
bool __cdecl CFunc_StopMusic(Script::CStruct* params, Script::CScript* script);             // 0x0044D4C0
bool __cdecl CFunc_PauseMusic(Script::CStruct* params, Script::CScript* script);            // 0x0044D4D0
bool __cdecl CFunc_PauseStream(Script::CStruct* params, Script::CScript* script);           // 0x0044D500
bool __cdecl CFunc_AddMusicTrack(Script::CStruct* params, Script::CScript* script);         // 0x0044D530
bool __cdecl CFunc_ChangeTrackState(Script::CStruct* params, Script::CScript* script);      // 0x0044D5B0
bool __cdecl CFunc_TrackEnabled(Script::CStruct* params, Script::CScript* script);          // 0x0044D5F0
bool __cdecl CFunc_ClearMusicTrackList(Script::CStruct* params, Script::CScript* script);   // 0x0044D690
bool __cdecl CFunc_SkipMusicTrack(Script::CStruct* params, Script::CScript* script);        // 0x0044D6C0
bool __cdecl CFunc_SetMusicMode(Script::CStruct* params, Script::CScript* script);          // 0x0044D6D0
bool __cdecl CFunc_SetRandomMode(Script::CStruct* params, Script::CScript* script);         // 0x0044D710
bool __cdecl CFunc_SetMusicLooping(Script::CStruct* params, Script::CScript* script);       // 0x0044D740
bool __cdecl CFunc_PreLoadStreamDone(Script::CStruct* params, Script::CScript* script);     // 0x0044D770
bool __cdecl CFunc_UseUserSoundtrack(Script::CStruct* params, Script::CScript* script);     // 0x0044D790
bool __cdecl CFunc_UseStandardSoundtrack(Script::CStruct* params, Script::CScript* script); // 0x0044D7C0

} // namespace CFuncs
