// Music: picking the next track of a playlist.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/pcm/music.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/stubs.h"
#include "game/gfx/movie.h"
#include "game/mth/random.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/game_config.h"

namespace Pcm {

bool g_musicHeaderLoaded{true};
bool g_streamsOn{true};
float g_musicVolume{50.0f};
float g_musicStreamVolume{100.0f};
int g_musicStreamType;
bool g_shuffleUser;
void* g_soundDevice;
Playlist g_playlists[2];       // (the music's two playlists; 0 is the game's, 1 the script's)
char g_trackTitles[80][100];
Stream g_streams[3];
void* g_streamOwners[3];
int g_userOrder[600];
int g_trackOrder[600];          // the shuffled order (the word before it, 0x00696D9C, is read once)
char g_specialTrack[44];
char g_soundtrackName[100];

namespace {

Playlist* Playlists() { return g_playlists; }
int& CurrentPlaylist() { return g_currentPlaylist; }
// The track playing (40 bytes, just before the playlists) and whether it loops (SetMusicLooping).
void* SpecialTrack() { return g_specialTrack; }

uint8_t g_randomOrder{1};
bool g_reshuffle{true};
int g_orderCount{1};
bool g_looping;
int g_clearedByClearPlaylist;
bool g_musicPaused;
int g_orderPosition;
bool g_musicPausedBefore;

bool& Looping() { return g_looping; }
int& CurrentTrack() { return g_currentTrackIndex; }

// The shuffled order: the indices of the tracks that are on, how many, and the position of the last one
// played. The track played before the shuffle is read at order[count - 1] - with an empty order that's the
// word before it (0x00696D9C, the movie frame dump counter).
int* Order() { return g_trackOrder; }
int& OrderCount() { return g_orderCount; }
int& OrderPosition() { return g_orderPosition; }
bool& Reshuffle() { return g_reshuffle; }

bool MusicOn()
{
    return g_soundDevice && GameConfig::g_platform != 2 && g_musicHeaderLoaded;
}
// The same for streamed sound effects (0x0067D7E1).
bool StreamsOn()
{
    return g_soundDevice && GameConfig::g_platform != 2 && g_streamsOn;
}

Stream* Streams() { return g_streams; }
constexpr int kStreams = 3;
// Whether the platform's stream `channel` is playing (StreamStatus: 1 free, 2 playing).
bool Playing(const Stream& stream) { return StreamStatus(stream.channel) != 1; }
// The objects that started each stream (null: none); they note the stream's id at +0x18 + 4 * stream.
uint32_t** StreamOwners() { return reinterpret_cast<uint32_t**>(g_streamOwners); }
int& FrameCount() { return g_frameCount; }

// The user soundtrack: which, whether it's playing (in place of the game's music), its shuffled order and the
// track in it, whether it's shuffled.
int& UserSoundtrack() { return g_userSoundtrack; }
bool& UsingUserSoundtrack() { return g_usingUserSoundtrack; }
int* UserOrder() { return g_userOrder; }
constexpr int kUserOrderSize = 600;
int& UserTrack() { return reinterpret_cast<int&>(g_userTrack); }
int& UserPosition() { return reinterpret_cast<int&>(g_userPosition); }
bool ShuffleUser() { return g_shuffleUser; }

// 1 << n as the C runtime's 64-bit shift (__allshl) does it: by n's low byte, 0 from 64 up.
uint64_t Bit(int n)
{
    uint8_t shift = static_cast<uint8_t>(n);
    return shift >= 64 ? 0 : uint64_t{1} << shift;
}
bool IsOff(const Playlist& list, int track)
{
    return track < 64 ? (Bit(track) & list.off[0]) != 0 : (Bit(track - 64) & list.off[1]) != 0;
}

// The track's start: the stream played, and the script told which track it is.
void StartTrack(Playlist& list, int track)
{
    PlayTrack(reinterpret_cast<const char*>(list.tracks[track]), false);
    CurrentTrack() = track;
    Script::CStruct* params = Script::NewStruct();
    params = params ? params->Init() : nullptr;
    params->AddInteger(0x8D02705Du, CurrentTrack());
    Script::RunScript(0x300CAD34u, params, nullptr, false, nullptr);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

// The volumes (0-100): the music's, and that of streams played on the music's channel.
float& MusicVolume() { return g_musicVolume; }
float& MusicStreamVolume() { return g_musicStreamVolume; }
// What the music's channel plays: 0 a track, 1 a music stream (0x00450EF0), 2 another kind of stream (0x004509E0).
int& MusicStreamType() { return g_musicStreamType; }
// Set by the music's initialisation (0x00450D60).
bool Initialised() { return g_initialised; }
bool& MusicPaused() { return g_musicPaused; }
bool& MusicPausedBefore() { return g_musicPausedBefore; }
bool& MusicHeaderLoaded() { return g_musicHeaderLoaded; }
uint8_t& RandomOrder() { return g_randomOrder; }
bool& ShuffleUserFlag() { return g_shuffleUser; }
// Playlist 0's track titles, by the track's index (100 characters each).
char* Title(int track) { return g_trackTitles[track]; }
// A global ClearPlaylist clears (just past the three streams; nothing else uses it).
int& ClearedByClearPlaylist() { return g_clearedByClearPlaylist; }

// The platform's music: its volume and pause (music 1 / streams 0).
void SetPlatformVolume(float volume) { PlatformSetMusicVolume(volume); }
void PlatformPause(bool pause, int music) { PauseStreams(pause, music); }
// The sound effects manager's volume (the original calls it as a method of 0x006F85A8; it reads a global).
float SfxVolume() { return Sfx::GetMainVolume(); }
// 0x005B9760 (the shared `return true` stub): whether the file is there (the PS2's check, nothing on the PC).
bool FileThere(const char*, int) { return Stubs::ReturnTrue(); }
// A script integer that's read and ignored: what used it is compiled out (debug output).
void ReadDebugFlag() { Script::GetInteger(0xD7BB618Du); }
// Set: playlists other than 0 play at the music volume, not the sound effects one.
bool OtherListsAtMusicVolume() { return Script::GetInteger(0x450BAE2Du) != 0; }

bool InRange(float volume) { return !(volume > 100.0f) && !(volume < 0.0f); } // (NaN passes)

} // namespace

// 0x00450F70
void __cdecl PauseMusic(int pause)
{
    if (!MusicOn())
        return;
    if (pause == -1) {
        if (MusicPausedBefore())
            return;
        pause = 0;
    }
    if (pause == 0) {
        ReadDebugFlag();
        MusicPaused() = false;
        PlatformPause(false, 1);
    } else if (pause == 1) {
        ReadDebugFlag();
        MusicPausedBefore() = MusicPaused();
        MusicPaused() = true;
        PlatformPause(true, 1);
    }
}

// 0x004510B0
void __cdecl SetMusicVolume(float volume)
{
    if (!MusicOn() || !InRange(volume))
        return;
    MusicVolume() = volume;
    if (!Initialised() || MusicStreamType() != 0)
        return;
    if (CurrentPlaylist() != 0 && !OtherListsAtMusicVolume())
        return;
    SetPlatformVolume(volume);
}

// 0x00451130
void __cdecl SetMusicStreamVolume(float volume)
{
    if (!MusicOn() || !InRange(volume))
        return;
    MusicStreamVolume() = volume;
    if (Initialised() && MusicStreamType() == 1)
        SetPlatformVolume(volume);
}

// 0x004511A0
void __cdecl AddTrackToPlaylist(const char* name, int list, const char* title)
{
    if (!MusicOn())
        return;
    Playlist& playlist = Playlists()[list];
    if (playlist.count >= 80)
        return;
    char* track = reinterpret_cast<char*>(playlist.tracks[playlist.count]);
#pragma warning(suppress : 4996) // (the slot is 40 bytes; names are shorter)
    std::strcpy(track, name);
    for (size_t i = 0; i < std::strlen(track); ++i) {
        signed char c = static_cast<signed char>(track[i]);
        if (c >= 'a' && c <= 'z')
            track[i] = static_cast<char>(c - 0x20);
    }
    if (list == 0) {
        char* slot = Title(playlist.count);
        if (title) {
#pragma warning(suppress : 4996) // (terminated by hand, as in the original)
            std::strncpy(slot, title, 100);
            slot[99] = 0;
        } else {
            std::memcpy(slot, "No title provided", 18); // "No title provided"
        }
    }
    if (FileThere(track, 1)) {
        playlist.unknownCA8 = 0;
        ++playlist.count;
    }
}

// 0x004512D0
void __cdecl ClearPlaylist(int list)
{
    if (!MusicOn())
        return;
    Playlist& playlist = Playlists()[list];
    playlist.count = 0;
    playlist.unknownCA8 = 1;
    playlist.off[0] = 0;
    playlist.off[1] = 0;
    playlist.played[0] = 0;
    playlist.played[1] = 0;
    ClearedByClearPlaylist() = 0;
}

// 0x00451350
void __cdecl SkipMusicTrack()
{
    if (MusicOn() && CurrentPlaylist() == 0)
        StopMusic();
}

// 0x00451380
void __cdecl SetRandomMode(int mode)
{
    if (!MusicOn())
        return;
    RandomOrder() = mode != 0;
    ShuffleUserFlag() = mode > 0;
    if (mode != 0) {
        ShuffleUserSoundtrack();
        UserPosition() = 0;
        UserTrack() = UserOrder()[0];
    }
    Playlist& playlist = Playlists()[CurrentPlaylist()];
    playlist.played[0] = 0;
    playlist.played[1] = 0;
}

// 0x00451400
void __cdecl ApplyTrackVolume()
{
    bool atMusicVolume = OtherListsAtMusicVolume();
    if (UsingUserSoundtrack())
        SetPlatformVolume(MusicVolume());
    else if (CurrentPlaylist() == 1 && !atMusicVolume)
        SetPlatformVolume(SfxVolume());
}

// 0x00451450
bool __cdecl IsTrackPlaying(int list, int track)
{
    if (!MusicOn() || MusicStatus() == 1) // 1: no stream playing
        return false;
    if (track >= Playlists()[list].count)
        return false;
    uint32_t name = Crc::GenerateCRCFromString(reinterpret_cast<const char*>(Playlists()[list].tracks[track]));
    return Crc::GenerateCRCFromString(static_cast<const char*>(SpecialTrack())) == name;
}

// 0x004514C0
int __cdecl GetNumTracks(int list) { return MusicOn() ? Playlists()[list].count : 0; }

// 0x00451500
const char* __cdecl GetTrackName(int track, int list)
{
    if (!MusicOn()) {
        char* first = reinterpret_cast<char*>(Playlists()[0].tracks[0]);
        std::memcpy(first, "Empty.", 7); // "Empty."
        return first;
    }
    const char* name = reinterpret_cast<const char*>(Playlists()[list].tracks[track]);
    const char* file = name;
    int length = static_cast<int>(std::strlen(name));
    for (int i = 0; i < length; ++i)
        if (name[i] == '\\' || name[i] == '/')
            file = name + i + 1;
    return file;
}

// 0x004515A0
void __cdecl SetMusicMode(int list)
{
    if (!MusicOn())
        return;
    if (CurrentPlaylist() != list)
        StopMusic();
    CurrentPlaylist() = list;
}

// 0x004515E0
bool __cdecl LoadMusicHeader(const char* name)
{
    if (!MusicOn())
        return false;
    MusicHeaderLoaded() = FileThere(name, 0) != 0;
    return MusicHeaderLoaded();
}

// 0x00451C70
void __cdecl ChangeTrackState(int track, bool off, int list)
{
    if (!MusicOn())
        return;
    Playlist& playlist = Playlists()[list];
    Reshuffle() = true;
    if (!off) {
        if (track < 64)
            playlist.off[0] &= ~Bit(track);
        else
            playlist.off[1] &= ~Bit(track - 64);
        playlist.unknownCA8 = 0;
        return;
    }
    if (track < 64)
        playlist.off[0] |= Bit(track);
    else
        playlist.off[1] |= Bit(track - 64);
    if (IsTrackPlaying(list, track))
        StopMusic();
    // Original quirk: every track is tested against both masks (1 << (i - 64) is 0 for i < 64), so any track at
    // all counts as one that's on - only an empty playlist is marked.
    uint64_t low = playlist.off[0];
    for (int i = 0; i < playlist.count; ++i) {
        if ((Bit(i) & low) == 0 || (Bit(i - 64) & playlist.off[1]) == 0) {
            playlist.unknownCA8 = 0;
            return;
        }
    }
    playlist.unknownCA8 = 1;
}

// 0x004520D0
void __cdecl PlayTrack(const char* name, bool loop)
{
    if (!MusicOn() || MusicStreamType() != 0)
        return;
    char* playing = static_cast<char*>(SpecialTrack());
#pragma warning(suppress : 4996) // (40 bytes, as in the original)
    std::strcpy(playing, name);
    int list = CurrentPlaylist();
    bool atMusicVolume = OtherListsAtMusicVolume();
    float volume = list != 0 && !atMusicVolume ? SfxVolume() : MusicVolume();
    Looping() = loop;
    if (PlayTrackByName(playing, volume) || GameConfig::g_fromCD) // (the debug checks)
        return;
    // It doesn't play: switch it off.
    Playlist& playlist = Playlists()[list];
    for (int i = 0; i < playlist.count; ++i) {
        uint32_t crc = Crc::GenerateCRCFromString(reinterpret_cast<const char*>(playlist.tracks[i]));
        if (Crc::GenerateCRCFromString(name) == crc) {
            ChangeTrackState(i, true, list);
            return;
        }
    }
}

// PartyMod's our_random, at the shuffle's two draws (0x004523A7 / 0x004523B4). It keeps drawing the
// game's number so the game's random stream advances as before. (rand() % 0 would divide by zero; the game's
// Rnd(0) is 0, so that's kept for an empty order.)
int ShuffleRandom(int n)
{
    int game = Mth::Rnd(n);
    if (n == 0)
        return game;
    return std::rand() % n;
}

// 0x00452200
void __cdecl PlayNextTrack()
{
    Playlist& list = Playlists()[CurrentPlaylist()];
    if (!MusicOn() || list.unknownCA8)
        return;
    if (MusicStatus() != 1) // 1: no stream playing
        return;
    g_musicStreamType = 0;
    ApplyTrackVolume(); // the music volume
    if (CurrentPlaylist() == 1) {
        int track = Script::GetInteger(0xF18AB1F9u);
        if (track < Playlists()[1].count)
            PlayTrack(reinterpret_cast<const char*>(Playlists()[1].tracks[track]), false);
        return;
    }
    if (Looping()) {
        PlayTrack(static_cast<const char*>(SpecialTrack()), true);
        return;
    }
    if (list.count == 0)
        return;

    int position;
    if (Reshuffle()) {
        // (an empty order: the original reads the word before it, which is the movie frame dump counter - by name
        // here, as the standalone build's globals aren't laid out as the original's)
        int last = OrderCount() > 0 ? Order()[OrderCount() - 1] : Nx::g_movieDumpCount;
        OrderCount() = 0;
        int count = 0;
        for (int track = 0; track < list.count; ++track)
            if (!IsOff(list, track))
                Order()[count++] = track;
        OrderCount() = count;
        Dbg::Printf("shuffling random song order num_songs=%i\n", count); // "shuffling random song order num_songs=%i\n"
        // 2000 swaps of two random places. PartyMod draws them with ShuffleRandom; the original with
        // Mth::Rnd(OrderCount()).
        for (int i = 0; i < 2000; ++i) {
            int a = ShuffleRandom(OrderCount());
            int b = ShuffleRandom(OrderCount());
            if (a != b) {
                int swap = Order()[a];
                Order()[a] = Order()[b];
                Order()[b] = swap;
            }
        }
        // Not the last one again first: swap it with another place (this draw is the game's, under PartyMod too).
        if (Order()[0] == last) {
            int other = OrderCount() > 1 ? Mth::Rnd(OrderCount() - 1) + 1 : 0;
            int swap = Order()[0];
            Order()[0] = Order()[other];
            Order()[other] = swap;
        }
        OrderPosition() = 0;
        Reshuffle() = false;
        list.played[0] = 0;
        list.played[1] = 0;
        position = 0;
    } else {
        position = ++OrderPosition();
        if (position >= OrderCount() - 1)
            Reshuffle() = true;
    }
    if (list.count <= 0)
        return;
    // The track at that position, unless it's off or already played (then nothing). (The original repeats
    // this same test list.count times.)
    int track = Order()[position];
    if ((Bit(track) & list.off[0]) == 0 && track < 64 && (Bit(track) & list.played[0]) == 0) {
        list.played[0] |= Bit(track);
        StartTrack(list, track);
    } else if ((Bit(track - 64) & list.off[1]) == 0 && track >= 64 && (Bit(track - 64) & list.played[1]) == 0) {
        list.played[1] |= Bit(track - 64);
        StartTrack(list, track);
    }
}

// 0x00452680
void __cdecl PlayNextTrackInOrder()
{
    if (!MusicOn())
        return;
    Playlist& list = Playlists()[CurrentPlaylist()];
    if (list.unknownCA8)
        return;
    if (MusicStatus() != 1) // 1: no stream playing
        return;
    g_musicStreamType = 0;
    ApplyTrackVolume(); // the music volume
    if (Looping()) {
        PlayTrack(static_cast<const char*>(SpecialTrack()), true);
        return;
    }
    for (int track = 0; track < list.count; ++track) {
        if ((Bit(track) & list.off[0]) == 0 && track < 64 && (Bit(track) & list.played[0]) == 0) {
            list.played[0] |= Bit(track);
            StartTrack(list, track);
            return;
        }
        if ((Bit(track - 64) & list.off[1]) == 0 && track >= 64 && (Bit(track - 64) & list.played[1]) == 0) {
            list.played[1] |= Bit(track - 64);
            StartTrack(list, track);
            return;
        }
    }
    list.played[0] = 0;
    list.played[1] = 0;
}

// 0x00452910
void __cdecl Update()
{
    if (!g_soundDevice || GameConfig::g_platform == 2)
        return;
    if (!g_musicHeaderLoaded && !g_streamsOn)
        return;
    int streams = UpdateStreams(); // the stream system's update
    if (streams != 0) {
        if (StreamsOn())
            StopStreams();
        if (streams < 0)
            StopMusic(); // pause the music
    }
    ++FrameCount();
    for (int i = 0; i < kStreams; ++i) {
        uint32_t*& owner = StreamOwners()[i];
        if (owner && !UpdateStreamVolume(i)) { // (false: the stream is over)
            owner[6 + i] = 0;
            owner = nullptr;
        }
    }
    for (int i = 0; i < kStreams; ++i) {
        Stream& stream = Streams()[i];
        if (stream.listener && !Playing(stream)) {
            if (stream.listener[0]) {
                stream.listener[0] = 0;
                stream.listener[1] = 0;
            }
            stream.listener = nullptr;
        }
    }
    if (FrameCount() & 0x3F)
        return;
    int playlist = CurrentPlaylist();
    if (g_musicVolume == 0.0f && playlist != 1) // (the music volume)
        return;
    if (MusicIsPaused())
        return;
    if (!RandomTrackOrder() && playlist != 1)
        PlayNextTrackInOrder();
    else
        PlayNextTrack();
}

// 0x00450220
bool __cdecl StreamIdFinished(uint32_t id)
{
    for (int i = 0; i < kStreams; ++i)
        if (Streams()[i].id == id && Playing(Streams()[i]))
            return false;
    return true;
}

// 0x00450260
int __cdecl FindPlayingStream(uint32_t idOrName)
{
    for (int i = 0; i < kStreams; ++i)
        if (Streams()[i].id == idOrName && Playing(Streams()[i]))
            return i;
    for (int i = 0; i < kStreams; ++i)
        if (Streams()[i].name == idOrName && Playing(Streams()[i]))
            return i;
    return -1;
}

// 0x004502D0
uint32_t __cdecl FreeStreamId(uint32_t id)
{
    for (;; ++id) {
        bool used = false;
        for (int i = 0; i < kStreams && !used; ++i)
            used = Streams()[i].id == id && Playing(Streams()[i]);
        if (!used)
            return id;
    }
}

// 0x00450310 (with the game's random numbers: PartyMod's shuffle change is only in PlayNextTrack's)
void __cdecl ShuffleUserSoundtrack()
{
    int count = UserSoundtrackTracks(UserSoundtrack()); // the soundtrack's tracks
    if (count == 0)
        return;
    if (count > kUserOrderSize)
        count = kUserOrderSize;
    int last = UserOrder()[count - 1]; // (the last of the order before - not the track played last)
    for (int i = 0; i < count; ++i)
        UserOrder()[i] = i;
    for (int i = 0; i < 2000; ++i) {
        int a = Mth::Rnd(count);
        int b = Mth::Rnd(count);
        int swap = UserOrder()[a];
        UserOrder()[a] = UserOrder()[b];
        UserOrder()[b] = swap;
    }
    if (UserOrder()[0] == last) {
        int other = count > 1 ? Mth::Rnd(count - 1) + 1 : 0;
        int swap = UserOrder()[0];
        UserOrder()[0] = UserOrder()[other];
        UserOrder()[other] = swap;
    }
}

// 0x004503D0
void __cdecl UseUserSoundtrack(int soundtrack)
{
    UserSoundtrack() = soundtrack;
    UsingUserSoundtrack() = true;
    if (ShuffleUser()) {
        ShuffleUserSoundtrack();
        UserPosition() = 0;
        UserTrack() = UserOrder()[0];
    } else {
        UserTrack() = 0;
    }
}

// 0x00450410
void __cdecl UseStandardSoundtrack() { UsingUserSoundtrack() = false; }

// 0x00450420
void __cdecl StopUserSoundtrack() { StopStreamThread(); }

// 0x004B2350
int __cdecl GetNumSoundtracks() { return UserSoundtrackCount(); }

// 0x004B2360
const char* __cdecl GetSoundtrackName(int index)
{
    char* name = g_soundtrackName;
    const wchar_t* wide = UserSoundtrackName(index); // (null past the last)
    if (wide) {
        char* out = name;
        int length = 0;
        for (; *wide; ++wide) {
            wchar_t one[2] = {*wide, 0};
            char converted[12];
            wsprintfA(converted, "%ls", one);
            if (length < 99) {
                *out++ = converted[0] ? converted[0] : '~';
                ++length;
            }
        }
        *out = 0;
    } else {
        std::memcpy(name, "~", 2);
    }
    int length = static_cast<int>(std::strlen(name));
    for (int i = 0; i < length; ++i) {
        signed char c = static_cast<signed char>(name[i]);
        if (c >= static_cast<signed char>(0xA1) && c <= static_cast<signed char>(0xAC))
            name[i] = '~';
    }
    return name;
}

// 0x00450430
bool __cdecl MusicIsPaused() { return g_musicPaused; }

// 0x00450440
float __cdecl GetMusicVolume() { return g_musicVolume; }

// 0x00450450
float __cdecl GetSfxVolume() { return g_musicStreamVolume; }

// 0x00450460
int __cdecl RandomTrackOrder() { return g_randomOrder; }

// 0x00450470
void __cdecl SetMusicLooping(bool loop) { Looping() = loop; }

// 0x00450480
void __cdecl GetTracksOff(uint64_t* low, uint64_t* high)
{
    *low = Playlists()[0].off[0];
    *high = Playlists()[0].off[1];
}

// 0x004504B0
void __cdecl SetTracksOff(uint64_t low, uint64_t high)
{
    Playlists()[0].off[0] = low;
    Playlists()[0].off[1] = high;
}

// 0x004504E0
void __cdecl SetUserSoundtrackVolume(float volume)
{
    if (CurrentPlaylist() == 1)
        PlatformSetMusicVolume(volume);
}

} // namespace Pcm
