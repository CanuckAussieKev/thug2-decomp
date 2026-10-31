#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// The script commands (CFunctions) of the music and streams, 0x0044D100-0x0044D7C7 and (between the sound effects'
// commands, sfx/sound_cfuncs.h) 0x0044D8F0-0x0044E399: thin wrappers reading their parameters and calling the Pcm
// functions (music.h, music_api.h, stream_commands.h). All are cdecl (params, script) and return whether the
// script's `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_StreamExists(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0044D100
bool __cdecl CFunc_AddStream(Scripts::CStruct* params, Scripts::CScript* script);             // 0x0044D140
bool __cdecl CFunc_RemoveStream(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0044D1B0
bool __cdecl CFunc_StopStream(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0044D220
bool __cdecl CFunc_IsStreamPlaying(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044D260
bool __cdecl CFunc_PlayTrack(Scripts::CStruct* params, Scripts::CScript* script);             // 0x0044D2A0
bool __cdecl CFunc_PlayMusicStream(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044D380
bool __cdecl CFunc_LoadStreamFrameAmp(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0044D3E0
bool __cdecl CFunc_FreeStreamFrameAmp(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0044D420
bool __cdecl CFunc_SetMusicVolume(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0044D460
bool __cdecl CFunc_SetMusicStreamVolume(Scripts::CStruct* params, Scripts::CScript* script);  // 0x0044D490
bool __cdecl CFunc_StopMusic(Scripts::CStruct* params, Scripts::CScript* script);             // 0x0044D4C0
bool __cdecl CFunc_PauseMusic(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0044D4D0
bool __cdecl CFunc_PauseStream(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0044D500
bool __cdecl CFunc_AddMusicTrack(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0044D530
bool __cdecl CFunc_ChangeTrackState(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0044D5B0
bool __cdecl CFunc_TrackEnabled(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0044D5F0
bool __cdecl CFunc_ClearMusicTrackList(Scripts::CStruct* params, Scripts::CScript* script);   // 0x0044D690
bool __cdecl CFunc_SkipMusicTrack(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0044D6C0
bool __cdecl CFunc_SetMusicMode(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0044D6D0
bool __cdecl CFunc_SetRandomMode(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0044D710
bool __cdecl CFunc_SetMusicLooping(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044D740
bool __cdecl CFunc_PreLoadStreamDone(Scripts::CStruct* params, Scripts::CScript* script);     // 0x0044D770
bool __cdecl CFunc_UseUserSoundtrack(Scripts::CStruct* params, Scripts::CScript* script);     // 0x0044D790
bool __cdecl CFunc_UseStandardSoundtrack(Scripts::CStruct* params, Scripts::CScript* script); // 0x0044D7C0
bool __cdecl CFunc_PlayStream(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0044D8F0
bool __cdecl CFunc_SetStreamParams(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044DAA0
bool __cdecl CFunc_LoadMusicHeader(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044E120
bool __cdecl CFunc_LoadStreamHeader(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0044E180
bool __cdecl CFunc_GetCurrentTrack(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044E1E0
bool __cdecl CFunc_StartPreLoadedStream(Scripts::CStruct* params, Scripts::CScript* script);  // 0x0044E2C0

} // namespace ScriptCommands
