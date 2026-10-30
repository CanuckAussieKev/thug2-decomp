// The music and streams front end: the game's calls for music tracks, music streams and streamed sounds.

#include "game/pcm/music_api.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/stubs.h"
#include "game/file/async.h"
#include "game/mth/vector.h"
#include "game/obj/stream_component.h"
#include "game/pcm/music.h"
#include "game/pcm/stream.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/gfx/viewport.h"
#include "game/sys/game_config.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/obj/anim_movies.h"

namespace Pcm {

GAME_GLOBAL(int, g_frameCount, 0x0067D7F0, 1);
GAME_GLOBAL(int, g_currentTrackIndex, 0x0069BA98);
GAME_GLOBAL(int, g_currentPlaylist, 0x0069BA9C);
GAME_GLOBAL(bool, g_initialised, 0x0069BAA1);
GAME_GLOBAL(bool, g_usingUserSoundtrack, 0x0069BAB0);
GAME_GLOBAL(int, g_userSoundtrack, 0x0069BAB4);
GAME_GLOBAL(uint32_t, g_userTrack, 0x0069BAB8);
GAME_GLOBAL(uint32_t, g_userPosition, 0x0069BAC0);

namespace {

bool SoundAvailable() { return g_soundDevice && GameConfig::g_platform != 2; }
bool MusicOn() { return SoundAvailable() && g_musicHeaderLoaded; }
bool StreamsOn() { return SoundAvailable() && g_streamsOn; }

bool& Initialised() { return g_initialised; }
// The music stream: 0 none, 1 playing, 2 preloaded.
int& MusicStreamType() { return g_musicStreamType; }
enum : int { kMusicStreamNone = 0, kMusicStreamNormal = 1, kMusicStreamPreload = 2 };
// Frames until PlayNextTrack is next tried (Update counts them).
int& FrameCount() { return g_frameCount; }
float& DefaultStreamVolume() { return g_musicVolume; }
float& DefaultMusicVolume() { return g_musicStreamVolume; }
float Zero() { return Obj::g_floatZero; }
int& CurrentTrackIndex() { return g_currentTrackIndex; }

// The stream fields used here (Stream in music.h; 0x74 bytes each, three at 0x0069B938).
struct StreamFields {
    uint32_t name;      // +0x00
    uint32_t id;        // +0x04
    uint32_t volume;    // +0x08 (a float: percent)
    uint32_t dropoff;   // +0x0C (a float)
    int channel;        // +0x10
    uint8_t pad14[4];
    int priority;       // +0x18
    int queuePosition;  // +0x1C
    int queueCount;     // +0x20
    uint8_t pad24[4];
    bool positional;    // +0x28: the volume follows the owner's object
    uint8_t pad29[3];
    int dropoffFunction; // +0x2C
    uint8_t pad30[4];
    struct Entry {
        uint32_t first;
        uint32_t second;
    } queue[8]; // +0x34
};
static_assert(sizeof(StreamFields) == 0x74, "layout must match the original");
StreamFields* StreamAt(int index) { return reinterpret_cast<StreamFields*>(g_streams) + index; }
constexpr int kStreams = 3;
// The objects that started each stream (null: none); each notes the ids of the streams it started at
// +0x18 + 4 * slot.
uint8_t** Owners() { return reinterpret_cast<uint8_t**>(g_streamOwners); }
uint32_t* OwnerIds(uint8_t* owner) { return reinterpret_cast<uint32_t*>(owner + 0x18); }

// The user soundtrack (see music.cpp).
int& UserSoundtrack() { return g_userSoundtrack; }
bool UsingUserSoundtrack() { return g_usingUserSoundtrack; }
int* UserOrder() { return g_userOrder; }
uint32_t& UserTrack() { return g_userTrack; }
uint32_t& UserPosition() { return g_userPosition; }
bool ShuffleUser() { return g_shuffleUser; }
int CurrentPlaylist() { return g_currentPlaylist; }

// The platform layer (stream.h). The original passes the music's stop 1 (StopMusic) or 0 (the music streams),
// which it doesn't use; the stream's stop is given 1 (unused too).
void PlatformStopStreams() { Pcm::StopStreams(); }
void PlatformStopStream(int index) { Pcm::StopStream(index, 1); }
void PlatformSetStreamVolume(const void* volume, int index)
{
    // (an sVolume starts with the platform's StreamVolume: five channels and the type)
    Pcm::SetStreamVolume(static_cast<const StreamVolume*>(volume), index);
}
int ChannelStatus(int channel) { return StreamStatus(channel); }
uint32_t NumUserTracks(int soundtrack) { return static_cast<uint32_t>(UserSoundtrackTracks(soundtrack)); }

constexpr uint32_t kHundredPercent = 0x42C80000; // 100.0f
Sfx::CSfxManager* SoundManager() { return Sfx::g_sfxManager; }

} // namespace

// 0x004505F0
bool __cdecl NoMusicPlease() { return !MusicOn(); }

// 0x00450620
bool __cdecl StreamsDisabled() { return !StreamsOn(); }

// 0x00450500
bool __cdecl StreamExists(uint32_t name) { return StreamExists(name, 0) != 0; }

// 0x00450520
int __cdecl GetCurrentTrack() { return CurrentTrackIndex(); }

// 0x00450530
void __cdecl SetCurrentTrack(int track) { CurrentTrackIndex() = track; }

// 0x00450650
void __cdecl StopMusic()
{
    if (!MusicOn())
        return;
    FrameCount() = 1;
    MusicStreamType() = kMusicStreamNone;
    PlatformStopMusic();
}

// 0x00450820
bool __cdecl PlayTrackByName(const char* name, float volume)
{
    if (!MusicOn())
        return false;
    if (!UsingUserSoundtrack() || CurrentPlaylist() == 1) {
        if (!LoadMusicTrackByName(name))
            return false;
        PlatformSetMusicVolume(volume);
        return true;
    }
    if (!NumUserTracks(UserSoundtrack()))
        return false;
    if (!PlayUserTrack(UserSoundtrack(), static_cast<int>(UserTrack())))
        return false;
    if (ShuffleUser()) {
        if (++UserPosition() >= NumUserTracks(UserSoundtrack())) {
            ShuffleUserSoundtrack();
            UserPosition() = 0;
        }
        UserTrack() = UserOrder()[UserPosition()];
    } else if (++UserTrack() >= NumUserTracks(UserSoundtrack())) {
        UserTrack() = 0;
    }
    PlatformSetMusicVolume(volume);
    return true;
}

// 0x00450EF0
void __cdecl PlayMusicStream(uint32_t /*name*/, float volume)
{
    if (!MusicOn())
        return;
    PlatformStopMusic();
    if (volume < Zero())
        volume = DefaultMusicVolume();
    if (MusicOn())
        PlatformSetMusicVolume(volume);
    MusicStreamType() = kMusicStreamNormal;
}

// 0x004509E0
bool __cdecl PreLoadMusicStream(uint32_t name)
{
    if (!MusicOn())
        return false;
    PlatformStopMusic();
    if (!MusicOn())
        return false;
    bool loading = PlayMusicTrack(name);
    if (loading)
        MusicStreamType() = kMusicStreamPreload;
    return loading;
}

// 0x00450A50
bool __cdecl PreLoadMusicStreamDone()
{
    if (!MusicOn())
        return false;
    return MusicPreloadDone();
}

// 0x00450A80
bool __cdecl StartPreLoadedMusicStream(float volume)
{
    if (!MusicOn())
        return false;
    if (volume < Zero())
        volume = DefaultMusicVolume();
    if (!StartPreloadedMusic())
        return false;
    PlatformSetMusicVolume(volume);
    return true;
}

// 0x00450690
void __cdecl StopStreams(int which)
{
    if (!StreamsOn())
        return;
    if (which == -1)
        PlatformStopStreams();
    else
        PlatformStopStream(which);
}

// 0x004506D0
void __cdecl StopStream(uint32_t idOrName)
{
    if (!StreamsOn())
        return;
    uint32_t id = idOrName;
    int index = FindPlayingStream(idOrName);
    if (index >= 0) {
        PlatformStopStream(index);
        id = StreamAt(index)->id;
    }
    for (int slot = 0; slot < kStreams; ++slot) {
        uint8_t* owner = Owners()[slot];
        if (!owner)
            continue;
        for (int i = 0; i < kStreams; ++i) {
            if (OwnerIds(owner)[i] == id) {
                OwnerIds(owner)[i] = 0;
                Owners()[slot] = nullptr;
                return;
            }
        }
    }
}

// 0x00450780
bool __cdecl SetStreamVolume(uint32_t idOrName, void* volume)
{
    if (StreamsOn()) {
        int index = FindPlayingStream(idOrName);
        if (index < 0)
            return false;
        PlatformSetStreamVolume(volume, index);
    }
    return true;
}

// 0x004507D0
bool __cdecl SetStreamPitch(uint32_t idOrName, float /*pitch*/)
{
    if (StreamsOn()) {
        int index = FindPlayingStream(idOrName);
        if (index < 0)
            return false;
        Stubs::ReturnTrue(); // (the platform's set pitch, 0x005B9760: nothing on PC)
    }
    return true;
}

// 0x00450940
bool __cdecl PreLoadStreamDone(uint32_t idOrName)
{
    if (!StreamsOn())
        return false;
    int index = FindPlayingStream(idOrName);
    if (index < 0)
        return false;
    return StreamPreloadDone(index);
}

// 0x00450980
bool __cdecl StartPreLoadedStream(uint32_t idOrName, void* volume, float pitch, int priority)
{
    if (!StreamsOn())
        return false;
    int index = FindPlayingStream(idOrName);
    if (index < 0)
        return false;
    StreamAt(index)->priority = priority;
    (void)pitch; // (the original passes it on; the platform's start doesn't take it)
    return StartPreloadedStream(index, static_cast<const StreamVolume*>(volume));
}

// 0x00450AE0
bool __cdecl RemoveStream(uint32_t idOrName, uint32_t entry, void* owner)
{
    if (!StreamsOn())
        return false;
    int index = -1;
    if (owner) {
        for (int i = 0; i < kStreams; ++i) {
            if (Owners()[i] == owner && StreamAt(i)->name == idOrName && ChannelStatus(StreamAt(i)->channel) != 1) {
                index = i;
                break;
            }
        }
        if (index < 0)
            return false;
    } else {
        index = FindPlayingStream(idOrName);
        if (index < 0)
            return false;
    }
    StreamFields* stream = StreamAt(index);
    if (stream->queueCount <= 0)
        return false;
    if (stream->queue[0].first != entry)
        return true;
    for (int i = 1; i < stream->queueCount; ++i)
        stream->queue[i - 1] = stream->queue[i];
    int count = --stream->queueCount;
    if (stream->queuePosition >= 0 && --stream->queuePosition < 0) {
        stream->queuePosition = count - 1;
        if (stream->priority >= 0)
            ++stream->priority;
    }
    return true;
}

// 0x00450BE0
bool __cdecl UpdateStreamVolume(int slot)
{
    if (!StreamsOn())
        return false;
    uint8_t* owner = Owners()[slot];
    uint32_t id = OwnerIds(owner)[slot];
    int index = 0;
    while (index < kStreams && StreamAt(index)->id != id)
        ++index;
    if (index == kStreams)
        return false;
    // (the status is of stream `slot`'s channel, not the one found - as the original)
    if (ChannelStatus(StreamAt(slot)->channel) == 1)
        return false;
    StreamFields* stream = StreamAt(index);
    Sfx::sVolume volume;
    volume.Init();
    Sfx::CSfxManager* manager = SoundManager();
    if (stream->positional) {
        // (the emitter nearest the owner's object: 0x004A09E0, not rewritten yet)
        void* object = Nx::GetClosestCamera(
            reinterpret_cast<const Mth::Vector*>(*reinterpret_cast<uint8_t**>(owner + 0xC) + 0x4C), nullptr);
        if (object) {
            auto* component = reinterpret_cast<Obj::CStreamComponent*>(owner);
            Mth::Vector extent;
            Mth::Vector* extentOrNull = nullptr;
            if (component->GetStreamExtent(object, &extent))
                extentOrNull = &extent;
            Mth::Vector position;
            Mth::Vector* where = component->GetStreamPosition(&position, object);
            manager->GetVolume(&volume, where, std::bit_cast<float>(stream->dropoff), stream->dropoffFunction, object,
                               extentOrNull);
        } else {
            volume.SetSilent();
        }
    } else {
        std::memcpy(&volume.m_channels[0], &stream->volume, 4);
        std::memcpy(&volume.m_channels[1], &stream->volume, 4);
    }
    if (stream->volume != kHundredPercent)
        volume.PercentageAdjustment(std::bit_cast<float>(stream->volume));
    PlatformSetStreamVolume(&volume, stream->channel);
    return true;
}

// 0x00450D60
void __cdecl Init()
{
    Owners()[0] = Owners()[1] = Owners()[2] = nullptr;
    if (SoundAvailable() && (g_musicHeaderLoaded || StreamsOn())) {
        InitPlatform();
        Initialised() = true;
        DefaultStreamVolume() = static_cast<float>(Script::GetInteger(0xABD4A575u));
        if (DefaultStreamVolume() == Zero())
            DefaultStreamVolume() = 50.0f;
        DefaultMusicVolume() = static_cast<float>(Script::GetInteger(0x73F8E03Bu));
        if (DefaultMusicVolume() == Zero())
            DefaultMusicVolume() = 100.0f;
        auto* playlists = g_playlists;
        for (int i = 0; i < 2; ++i) { // (the first two of the three)
            Playlist& list = playlists[i];
            list.count = 0;
            list.off[0] = list.off[1] = 0;
            list.played[0] = list.played[1] = 0;
            list.unknownCA8 = 1;
        }
        for (int i = 0; i < kStreams; ++i) {
            StreamAt(i)->name = 0;
            StreamAt(i)->id = 0;
        }
    }
    Owners()[0] = Owners()[1] = Owners()[2] = nullptr;
}

// 0x00450EB0
bool __cdecl UsingCD()
{
    if (GameConfig::g_fromCD && Initialised()) {
        StopMusic();
        if (StreamsOn())
            PlatformStopStreams();
    }
    return false;
}

// 0x00450540
bool FamData::Load(uint32_t famName)
{
    char path[256];
    std::snprintf(path, sizeof(path), "fam\\%x.fam", famName);
    File::AsyncFileHandle* file = File::AsyncOpen(path, true, 100);
    if (!file)
        return false;
    size = file->GetFileSize();
    if (size >= 0x1000) {
        File::AsyncClose(file);
        return false;
    }
    file->Read(data, 1, size);
    File::AsyncClose(file);
    name = famName;
    return true;
}

} // namespace Pcm

REPLACE(0x004505F0, Pcm::NoMusicPlease);
REPLACE(0x00450620, Pcm::StreamsDisabled);
REPLACE(0x00450500, static_cast<bool(__cdecl*)(uint32_t)>(Pcm::StreamExists));
REPLACE(0x00450520, Pcm::GetCurrentTrack);
REPLACE(0x00450530, Pcm::SetCurrentTrack);
REPLACE(0x00450650, Pcm::StopMusic);
REPLACE(0x00450820, Pcm::PlayTrackByName);
REPLACE(0x00450EF0, Pcm::PlayMusicStream);
REPLACE(0x004509E0, Pcm::PreLoadMusicStream);
REPLACE(0x00450A50, Pcm::PreLoadMusicStreamDone);
REPLACE(0x00450A80, Pcm::StartPreLoadedMusicStream);
REPLACE(0x00450690, static_cast<void(__cdecl*)(int)>(Pcm::StopStreams));
REPLACE(0x004506D0, static_cast<void(__cdecl*)(uint32_t)>(Pcm::StopStream));
REPLACE(0x00450780, static_cast<bool(__cdecl*)(uint32_t, void*)>(Pcm::SetStreamVolume));
REPLACE(0x004507D0, Pcm::SetStreamPitch);
REPLACE(0x00450940, Pcm::PreLoadStreamDone);
REPLACE(0x00450980, Pcm::StartPreLoadedStream);
REPLACE(0x00450AE0, Pcm::RemoveStream);
REPLACE(0x00450BE0, Pcm::UpdateStreamVolume);
REPLACE(0x00450D60, Pcm::Init);
REPLACE(0x00450EB0, Pcm::UsingCD);
REPLACE(0x00450540, &Pcm::FamData::Load);
