// The script commands of the music and streams, 0x0044D100-0x0044D7C7 (see music_cfuncs.h).

#include "game/pcm/music_cfuncs.h"
#include "hook/game_code.h"

#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/pcm/music.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream_commands.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/sys/crt.h"

namespace CFuncs {

namespace {

using Script::CScript;
using Script::CStruct;

constexpr uint32_t kAdd = 0x02E58C18;       // "add"
constexpr uint32_t kRemove = 0x977FE2CF;    // "remove"
constexpr uint32_t kVol = 0xF6A36814;       // "vol"
constexpr uint32_t kLoop = 0x5EA0E211;      // "loop"
constexpr uint32_t kTrackTitle = 0x14BADF8A; // "TrackTitle"
constexpr uint32_t kPlaylist0 = 0x389129E4; // (name unknown) AddMusicTrack / ClearMusicTrackList: playlist 0, not 1
constexpr uint32_t kVolL = 0x5E285A66;      // "volL"
constexpr uint32_t kVolR = 0xA4276705;      // "volR"
constexpr uint32_t kPitch = 0xD8604126;     // "pitch"
constexpr uint32_t kPriority = 0x9D5923D8;  // "priority"
constexpr uint32_t kStreamOption = 0xE46111E0; // (name unknown) PlayStream: passed on to the platform
constexpr uint32_t kId = 0x40C698AF;        // "id"
constexpr uint32_t kLipsync = 0xF5C3922B;   // "lipsync"
constexpr uint32_t kCurrentTrack = 0x8D02705D; // "current_track"
constexpr uint32_t kStreamId = 0x8A68AB90;  // "streamid"
constexpr uint32_t kVolume = 0x46653221;    // "volume"

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return Crt::Sprintf(out, format, args...);
}

// A 64-bit mask with bit `bit` set, as the C runtime's _allshl (0x00623CF0) makes it from the count's low byte:
// 0 from 64 on (a negative track's bit too).
uint64_t TrackBit(int bit)
{
    uint8_t count = static_cast<uint8_t>(bit);
    return count >= 64 ? 0 : uint64_t{1} << count;
}

} // namespace

// 0x0044D100: StreamExists <name>: whether the stream index has it. False without a name.
bool __cdecl CFunc_StreamExists(CStruct* params, CScript*)
{
    uint32_t name = 0;
    return params->GetChecksum(0u, &name, 0) && Pcm::StreamExists(name);
}

// 0x0044D140: AddStream <stream> add=<name>: the playing stream (by id or name) also known by the name. False
// without both or if it isn't playing.
bool __cdecl CFunc_AddStream(CStruct* params, CScript*)
{
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    uint32_t name = 0;
    if (!params->GetChecksum(kAdd, &name, 0))
        return false;
    return Pcm::AddStream(stream, name, nullptr);
}

// 0x0044D1B0: RemoveStream <stream> remove=<entry>: the stream's queue drops that entry (if first). False without
// both or if the stream isn't playing.
bool __cdecl CFunc_RemoveStream(CStruct* params, CScript*)
{
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    uint32_t entry = 0;
    if (!params->GetChecksum(kRemove, &entry, 0))
        return false;
    return Pcm::RemoveStream(stream, entry, nullptr);
}

// 0x0044D220: StopStream [<id or name>]: that stream stopped, or every stream. Always true.
bool __cdecl CFunc_StopStream(CStruct* params, CScript*)
{
    uint32_t stream = 0; // (uninitialised in the original: only read when found)
    if (params->GetChecksum(0u, &stream, 0))
        Pcm::StopStream(stream);
    else
        Pcm::StopStreams(-1);
    return true;
}

// 0x0044D260: IsStreamPlaying <id or name>. False without one.
bool __cdecl CFunc_IsStreamPlaying(CStruct* params, CScript*)
{
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    return Pcm::IsStreamPlaying(stream);
}

// 0x0044D2A0: PlayTrack <file> | <track number> [loop=n]: the music stopped and the track played (looping for a
// non-zero loop); a number names playlist 0's track, as MUSIC\VAG\SONGS\<its name>. The current track becomes the
// number (999 for a file name). Always true. (Original quirk: a song name of 22 characters or more overruns the
// 40-byte buffer.)
bool __cdecl CFunc_PlayTrack(CStruct* params, CScript*)
{
    const char* name = nullptr;
    int track = 999;
    int loop = 0;
    char path[0x28];
    if (!params->GetString(0u, &name, 0)) {
        params->GetInteger(0u, &track, 0);
        name = Pcm::GetTrackName(track, 0);
        Sprintf(path, GAME_TEXT(0x00647244, "MUSIC\\VAG\\SONGS\\%s"), name); // "MUSIC\VAG\SONGS\%s"
        name = path;
    }
    params->GetInteger(kLoop, &loop, 0);
    if (name) {
        Dbg::Printf(GAME_TEXT(0x00647214, "PlayTrack songName ===================== %s\n"), name); // "PlayTrack songName ==... %s"
        Pcm::StopMusic();
        Pcm::PlayTrack(name, loop != 0);
        Pcm::SetCurrentTrack(track);
    }
    return true;
}

// 0x0044D380: PlayMusicStream <name> [vol=n (-1: the default)]. False without a name.
bool __cdecl CFunc_PlayMusicStream(CStruct* params, CScript*)
{
    uint32_t name = 0;
    if (!params->GetChecksum(0u, &name, 0))
        return false;
    float volume = -1.0f;
    params->GetFloat(kVol, &volume, 0);
    Pcm::PlayMusicStream(name, volume);
    return true;
}

// 0x0044D3E0: LoadStreamFrameAmp <stream>: whether its frame amplitudes loaded. False without a name.
bool __cdecl CFunc_LoadStreamFrameAmp(CStruct* params, CScript*)
{
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    return Pcm::LoadStreamFrameAmp(stream) != nullptr;
}

// 0x0044D420: FreeStreamFrameAmp <stream>: whether they were loaded (and are now freed). False without a name.
bool __cdecl CFunc_FreeStreamFrameAmp(CStruct* params, CScript*)
{
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    return Pcm::FreeStreamFrameAmp(stream);
}

// 0x0044D460: SetMusicVolume [n (100)]. Always true.
bool __cdecl CFunc_SetMusicVolume(CStruct* params, CScript*)
{
    float volume = 100.0f;
    params->GetFloat(0u, &volume, 0);
    Pcm::SetMusicVolume(volume);
    return true;
}

// 0x0044D490: SetMusicStreamVolume [n (100)]. Always true.
bool __cdecl CFunc_SetMusicStreamVolume(CStruct* params, CScript*)
{
    float volume = 100.0f;
    params->GetFloat(0u, &volume, 0);
    Pcm::SetMusicStreamVolume(volume);
    return true;
}

// 0x0044D4C0: StopMusic. Always true.
bool __cdecl CFunc_StopMusic(CStruct*, CScript*)
{
    Pcm::StopMusic();
    return true;
}

// 0x0044D4D0: PauseMusic [n (1)]: 1 pauses, 0 unpauses, -1 unpauses unless paused before. Always true.
bool __cdecl CFunc_PauseMusic(CStruct* params, CScript*)
{
    int pause = 1;
    params->GetInteger(0u, &pause, 0);
    Pcm::PauseMusic(pause);
    return true;
}

// 0x0044D500: PauseStream [n (1)]: as PauseMusic, for the streams. Always true.
bool __cdecl CFunc_PauseStream(CStruct* params, CScript*)
{
    int pause = 1;
    params->GetInteger(0u, &pause, 0);
    Pcm::PauseStream(pause);
    return true;
}

// 0x0044D530: AddMusicTrack <file> [TrackTitle=<text>] [0x389129E4]: the track added to playlist 1 (or 0 with the
// flag). Nothing without a file. Always true.
bool __cdecl CFunc_AddMusicTrack(CStruct* params, CScript*)
{
    const char* name = nullptr;
    const char* title = nullptr;
    params->GetString(0u, &name, 0);
    params->GetString(kTrackTitle, &title, 0);
    int list = params->ContainsFlag(kPlaylist0) ? 0 : 1;
    if (name)
        Pcm::AddTrackToPlaylist(name, list, title);
    return true;
}

// 0x0044D5B0: ChangeTrackState <track> [Off]: playlist 0's track switched off (with Off) or on. Always true.
bool __cdecl CFunc_ChangeTrackState(CStruct* params, CScript*)
{
    int track = 0;
    params->GetInteger(0u, &track, 0);
    bool off = params->ContainsFlag(GAME_TEXT(0x00647258, "Off")); // "Off"
    Pcm::ChangeTrackState(track, off, 0);
    return true;
}

// 0x0044D5F0: TrackEnabled <track>: whether playlist 0's track isn't switched off. False with no tracks.
bool __cdecl CFunc_TrackEnabled(CStruct* params, CScript*)
{
    int track = 0;
    params->GetInteger(0u, &track, 0);
    if (!Pcm::GetNumTracks(0))
        return false;
    uint64_t low;
    uint64_t high;
    Pcm::GetTracksOff(&low, &high);
    if (track < 64)
        return (TrackBit(track) & low) == 0;
    return (TrackBit(track - 64) & high) == 0;
}

// 0x0044D690: ClearMusicTrackList [0x389129E4]: playlist 1 (or 0 with the flag) emptied. Always true.
bool __cdecl CFunc_ClearMusicTrackList(CStruct* params, CScript*)
{
    Pcm::ClearPlaylist(params->ContainsFlag(kPlaylist0) ? 0 : 1);
    return true;
}

// 0x0044D6C0: SkipMusicTrack. Always true.
bool __cdecl CFunc_SkipMusicTrack(CStruct*, CScript*)
{
    Pcm::SkipMusicTrack();
    return true;
}

// 0x0044D6D0: SetMusicMode [n (1)]: play from playlist 0 for a non-zero n, else from playlist 1. Always true.
bool __cdecl CFunc_SetMusicMode(CStruct* params, CScript*)
{
    int mode = 1;
    params->GetInteger(0u, &mode, 0);
    Pcm::SetMusicMode(mode == 0 ? 1 : 0);
    return true;
}

// 0x0044D710: SetRandomMode [n (1)]. Always true.
bool __cdecl CFunc_SetRandomMode(CStruct* params, CScript*)
{
    int mode = 1;
    params->GetInteger(0u, &mode, 0);
    Pcm::SetRandomMode(mode);
    return true;
}

// 0x0044D740: SetMusicLooping [n (1)]. Always true.
bool __cdecl CFunc_SetMusicLooping(CStruct* params, CScript*)
{
    int loop = 1;
    params->GetInteger(0u, &loop, 0);
    Pcm::SetMusicLooping(loop != 0);
    return true;
}

// 0x0044D770: PreLoadStreamDone <id or name>: whether its preload is done. (Original quirk: no name leaves the id
// uninitialised; here 0.)
bool __cdecl CFunc_PreLoadStreamDone(CStruct* params, CScript*)
{
    uint32_t stream = 0;
    params->GetChecksum(0u, &stream, 1);
    return Pcm::PreLoadStreamDone(stream);
}

// 0x0044D790: UseUserSoundtrack <n>: play the user's soundtrack n. Always true. (Original quirk: no n is
// uninitialised; here 0.)
bool __cdecl CFunc_UseUserSoundtrack(CStruct* params, CScript*)
{
    int soundtrack = 0;
    params->GetInteger(0u, &soundtrack, 1);
    Pcm::UseUserSoundtrack(soundtrack);
    return true;
}

// 0x0044D7C0: UseStandardSoundtrack: back to the game's own music. Always true.
bool __cdecl CFunc_UseStandardSoundtrack(CStruct*, CScript*)
{
    Pcm::UseStandardSoundtrack();
    return true;
}

// 0x0044D8F0: PlayStream <stream> [volL= volR= | vol= (100)] [pitch=100] [priority=50] [0xE46111E0=n] [id=<id>]
// [lipsync=1 (its frame amplitudes loaded first)]: the stream played (stereo). False without a stream, or when
// lipsync's amplitudes can't be loaded; else true (whether it plays or not).
bool __cdecl CFunc_PlayStream(CStruct* params, CScript*)
{
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    float vol = 100.0f;
    float left = 100.0f;
    float right = 100.0f;
    float pitch = 100.0f;
    int priority = 50;
    int option = 0;
    uint32_t id = 0;
    int lipsync = 0;
    params->GetFloat(kVolL, &left, 0);
    params->GetFloat(kVolR, &right, 0);
    if (params->GetFloat(kVol, &vol, 0)) {
        left = vol;
        right = vol;
    }
    params->GetFloat(kPitch, &pitch, 0);
    params->GetInteger(kPriority, &priority, 0);
    params->GetInteger(kStreamOption, &option, 0);
    params->GetChecksum(kId, &id, 0);
    params->GetInteger(kLipsync, &lipsync, 0);
    if (lipsync == 1 && !Pcm::LoadStreamFrameAmp(stream))
        return false;
    Sfx::sVolume volume;
    volume.SetType(Sfx::sVolume::TYPE_STEREO); // (the channels as they were, the first two set below)
    volume.m_channels[0] = left;
    volume.m_channels[1] = right;
    Pcm::PlayStream(stream, &volume, pitch, option, priority, id); // (the original pushes a seventh argument, 1)
    return true;
}

// 0x0044DAA0: SetStreamParams <id or name> [volL= volR= | vol=] [pitch=]: the playing stream's volume (if any is
// given: the manager's default type, the channels set) and pitch (if given). False without a stream or when
// either change fails.
bool __cdecl CFunc_SetStreamParams(CStruct* params, CScript*)
{
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    float left = 0.0f;  // (uninitialised in the original: a channel not given passes on whatever was there)
    float right = 0.0f;
    float vol = 0.0f;
    bool gotLeft = params->GetFloat(kVolL, &left, 0);
    bool gotVolume = params->GetFloat(kVolR, &right, 0) || gotLeft;
    if (params->GetFloat(kVol, &vol, 0)) {
        left = vol;
        right = vol;
        gotVolume = true;
    }
    bool ok = true;
    if (gotVolume) {
        Sfx::sVolume volume;
        volume.Init();
        volume.SetSilent();
        volume.m_channels[0] = left;
        volume.m_channels[1] = right;
        ok = Pcm::SetStreamVolume(stream, &volume);
    }
    float pitch = 0.0f;
    if (params->GetFloat(kPitch, &pitch, 0))
        return Pcm::SetStreamPitch(stream, pitch) && ok;
    return ok;
}

// 0x0044E120 / 0x0044E180: LoadMusicHeader / LoadStreamHeader <file>: loaded from the bottom-up heap; whether it
// was. False without a file.
bool __cdecl CFunc_LoadMusicHeader(CStruct* params, CScript*)
{
    const char* file = nullptr;
    params->GetString(0u, &file, 0);
    if (!file)
        return false;
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_bottomUpHeap);
    bool loaded = Pcm::LoadMusicHeader(file);
    Mem::Manager::Get()->PopContext();
    return loaded;
}
bool __cdecl CFunc_LoadStreamHeader(CStruct* params, CScript*)
{
    const char* file = nullptr;
    params->GetString(0u, &file, 0);
    if (!file)
        return false;
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_bottomUpHeap);
    bool loaded = Pcm::LoadStreamHeader(file);
    Mem::Manager::Get()->PopContext();
    return loaded;
}

// 0x0044E1E0: GetCurrentTrack: the track playing as the script's current_track. Always true.
bool __cdecl CFunc_GetCurrentTrack(CStruct*, CScript* script)
{
    script->m_params->AddInteger(kCurrentTrack, Pcm::GetCurrentTrack());
    return true;
}

// 0x0044E2C0: StartPreLoadedStream streamid=<id> [volume=100]: the preloaded stream started (pitch 100, priority
// 0) at that volume on both channels; whether it did. (Original quirk: no streamid passes an uninitialised one;
// here 0.)
bool __cdecl CFunc_StartPreLoadedStream(CStruct* params, CScript*)
{
    uint32_t stream = 0;
    params->GetChecksum(kStreamId, &stream, 1);
    float level = 100.0f;
    params->GetFloat(kVolume, &level, 0);
    auto* volume = static_cast<Sfx::sVolume*>(Mem::New(sizeof(Sfx::sVolume)));
    if (volume)
        volume = volume->Init();
    volume->m_channels[0] = level;
    volume->m_channels[1] = level;
    bool started = Pcm::StartPreLoadedStream(stream, volume, 100.0f, 0);
    Mem::Manager::Get()->Free(volume);
    return started;
}

} // namespace CFuncs

REPLACE(0x0044D100, CFuncs::CFunc_StreamExists);
REPLACE(0x0044D140, CFuncs::CFunc_AddStream);
REPLACE(0x0044D1B0, CFuncs::CFunc_RemoveStream);
REPLACE(0x0044D220, CFuncs::CFunc_StopStream);
REPLACE(0x0044D260, CFuncs::CFunc_IsStreamPlaying);
REPLACE(0x0044D2A0, CFuncs::CFunc_PlayTrack);
REPLACE(0x0044D380, CFuncs::CFunc_PlayMusicStream);
REPLACE(0x0044D3E0, CFuncs::CFunc_LoadStreamFrameAmp);
REPLACE(0x0044D420, CFuncs::CFunc_FreeStreamFrameAmp);
REPLACE(0x0044D460, CFuncs::CFunc_SetMusicVolume);
REPLACE(0x0044D490, CFuncs::CFunc_SetMusicStreamVolume);
REPLACE(0x0044D4C0, CFuncs::CFunc_StopMusic);
REPLACE(0x0044D4D0, CFuncs::CFunc_PauseMusic);
REPLACE(0x0044D500, CFuncs::CFunc_PauseStream);
REPLACE(0x0044D530, CFuncs::CFunc_AddMusicTrack);
REPLACE(0x0044D5B0, CFuncs::CFunc_ChangeTrackState);
REPLACE(0x0044D5F0, CFuncs::CFunc_TrackEnabled);
REPLACE(0x0044D690, CFuncs::CFunc_ClearMusicTrackList);
REPLACE(0x0044D6C0, CFuncs::CFunc_SkipMusicTrack);
REPLACE(0x0044D6D0, CFuncs::CFunc_SetMusicMode);
REPLACE(0x0044D710, CFuncs::CFunc_SetRandomMode);
REPLACE(0x0044D740, CFuncs::CFunc_SetMusicLooping);
REPLACE(0x0044D770, CFuncs::CFunc_PreLoadStreamDone);
REPLACE(0x0044D790, CFuncs::CFunc_UseUserSoundtrack);
REPLACE(0x0044D7C0, CFuncs::CFunc_UseStandardSoundtrack);
REPLACE(0x0044D8F0, CFuncs::CFunc_PlayStream);
REPLACE(0x0044DAA0, CFuncs::CFunc_SetStreamParams);
REPLACE(0x0044E120, CFuncs::CFunc_LoadMusicHeader);
REPLACE(0x0044E180, CFuncs::CFunc_LoadStreamHeader);
REPLACE(0x0044E1E0, CFuncs::CFunc_GetCurrentTrack);
REPLACE(0x0044E2C0, CFuncs::CFunc_StartPreLoadedStream);
