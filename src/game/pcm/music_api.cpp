// The music and streams front end: the game's calls for music tracks, music streams and streamed sounds.

#include "game/pcm/music_api.h"

#include <cstdint>
#include <cstdio>

#include "game/core/stubs.h"
#include "game/file/async.h"
#include "game/pcm/music.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Pcm {

namespace {

bool SoundAvailable() { return GameVariable<void*>(0x007D6A38) && GameVariable<int>(0x007CEFA8) != 2; }
bool MusicOn() { return SoundAvailable() && GameVariable<bool>(0x0067D7E0); }
bool StreamsOn() { return SoundAvailable() && GameVariable<bool>(0x0067D7E1); }

bool& Initialised() { return GameVariable<bool>(0x0069BAA1); }
// The music stream: 0 none, 1 playing, 2 preloaded.
int& MusicStreamType() { return GameVariable<int>(0x0069BAAC); }
enum : int { kMusicStreamNone = 0, kMusicStreamNormal = 1, kMusicStreamPreload = 2 };
// Frames until PlayNextTrack is next tried (Update counts them).
int& FrameCount() { return GameVariable<int>(0x0067D7F0); }
float& DefaultStreamVolume() { return GameVariable<float>(0x0067D7E4); }
float& DefaultMusicVolume() { return GameVariable<float>(0x0067D7E8); }
float Zero() { return GameVariable<float>(0x0066D520); }
int& CurrentTrackIndex() { return GameVariable<int>(0x0069BA98); }

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
StreamFields* StreamAt(int index) { return reinterpret_cast<StreamFields*>(0x0069B938) + index; }
constexpr int kStreams = 3;
// The objects that started each stream (null: none); each notes the ids of the streams it started at
// +0x18 + 4 * slot.
uint8_t** Owners() { return reinterpret_cast<uint8_t**>(0x00698060); }
uint32_t* OwnerIds(uint8_t* owner) { return reinterpret_cast<uint32_t*>(owner + 0x18); }

// The user soundtrack (see music.cpp).
int& UserSoundtrack() { return GameVariable<int>(0x0069BAB4); }
bool UsingUserSoundtrack() { return GameVariable<bool>(0x0069BAB0); }
int* UserOrder() { return reinterpret_cast<int*>(0x00697700); }
uint32_t& UserTrack() { return GameVariable<uint32_t>(0x0069BAB8); }
uint32_t& UserPosition() { return GameVariable<uint32_t>(0x0069BAC0); }
bool ShuffleUser() { return GameVariable<bool>(0x0069BABC); }
int CurrentPlaylist() { return GameVariable<int>(0x0069BA9C); }

// The platform layer (PCMAudio_*, not rewritten yet).
void PlatformStopMusic(bool wait) { GameFunction<void(__cdecl*)(int)>(0x00453AA0)(wait ? 1 : 0); }
void PlatformStopStreams() { GameFunction<void(__cdecl*)()>(0x00453BD0)(); }
void PlatformStopStream(int index) { GameFunction<void(__cdecl*)(int, int)>(0x00453B30)(index, 1); }
void PlatformSetMusicVolume(float volume) { GameFunction<void(__cdecl*)(float)>(0x00453400)(volume); }
void PlatformSetStreamVolume(void* volume, int index) { GameFunction<void(__cdecl*)(void*, int)>(0x00453DF0)(volume, index); }
int ChannelStatus(int channel) { return GameFunction<int(__cdecl*)(int)>(0x00453270)(channel); }
uint32_t NumUserTracks(int soundtrack) { return GameFunction<uint32_t(__cdecl*)(int)>(0x00454320)(soundtrack); }

// A sound's volume (Sfx::sVolume, 0x2C bytes): five channels and a type from the sound manager (the constructor
// sets these), then a direction and a float the positional volume (0x0047BE10) sets.
struct SfxVolume {
    uint32_t channels[5]; // (floats, copied as bits)
    int type;
    float direction[4]; // +0x18
    float unknown28;
};
static_assert(sizeof(SfxVolume) == 0x2C, "the original's frame keeps 0x2C bytes for it");
constexpr uint32_t kHundredPercent = 0x42C80000; // 100.0f
void* SoundManager() { return GameVariable<void*>(0x006F85A8); }

} // namespace

// 0x004505F0
bool __cdecl NoMusicPlease() { return !MusicOn(); }

// 0x00450620
bool __cdecl StreamsDisabled() { return !StreamsOn(); }

// 0x00450500
bool __cdecl StreamExists(uint32_t name) { return GameFunction<int(__cdecl*)(uint32_t, int)>(0x004533E0)(name, 0) != 0; }

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
    PlatformStopMusic(true);
}

// 0x00450820
bool __cdecl PlayTrackByName(const char* name, float volume)
{
    if (!MusicOn())
        return false;
    if (!UsingUserSoundtrack() || CurrentPlaylist() == 1) {
        if (!GameFunction<bool(__cdecl*)(const char*)>(0x00453D70)(name))
            return false;
        PlatformSetMusicVolume(volume);
        return true;
    }
    if (!NumUserTracks(UserSoundtrack()))
        return false;
    if (!GameFunction<bool(__cdecl*)(int, uint32_t)>(0x00453850)(UserSoundtrack(), UserTrack()))
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
    PlatformStopMusic(false);
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
    PlatformStopMusic(false);
    if (!MusicOn())
        return false;
    bool loading = GameFunction<bool(__cdecl*)(uint32_t)>(0x00453DB0)(name);
    if (loading)
        MusicStreamType() = kMusicStreamPreload;
    return loading;
}

// 0x00450A50
bool __cdecl PreLoadMusicStreamDone()
{
    if (!MusicOn())
        return false;
    return GameFunction<bool(__cdecl*)()>(0x00453220)();
}

// 0x00450A80
bool __cdecl StartPreLoadedMusicStream(float volume)
{
    if (!MusicOn())
        return false;
    if (volume < Zero())
        volume = DefaultMusicVolume();
    if (!GameFunction<bool(__cdecl*)()>(0x00453BF0)())
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
    return GameFunction<bool(__cdecl*)(int)>(0x004531D0)(index);
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
    return GameFunction<bool(__cdecl*)(int, void*, float)>(0x00454270)(index, volume, pitch);
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
    SfxVolume volume;
    GameFunction<void(__thiscall*)(SfxVolume*)>(0x0047B030)(&volume);
    void* manager = SoundManager();
    if (stream->positional) {
        void* object = GameFunction<void*(__cdecl*)(void*, int)>(0x004A09E0)(*reinterpret_cast<uint8_t**>(owner + 0xC) + 0x4C, 0);
        if (object) {
            uint8_t velocity[16];
            uint8_t* velocityOrNull = nullptr;
            if (GameFunction<bool(__thiscall*)(void*, void*, void*)>(0x00433D30)(owner, object, velocity))
                velocityOrNull = velocity;
            uint8_t position[16];
            void* where = GameFunction<void*(__thiscall*)(void*, void*, void*)>(0x00433CD0)(owner, position, object);
            GameFunction<void(__thiscall*)(void*, SfxVolume*, void*, uint32_t, int, void*, void*)>(0x0047BE10)(
                manager, &volume, where, stream->dropoff, stream->dropoffFunction, object, velocityOrNull);
        } else {
            GameFunction<void(__thiscall*)(SfxVolume*)>(0x0047AD60)(&volume); // silent
        }
    } else {
        volume.channels[0] = stream->volume;
        volume.channels[1] = stream->volume;
    }
    if (stream->volume != kHundredPercent)
        GameFunction<void(__thiscall*)(SfxVolume*, uint32_t)>(0x0047AC50)(&volume, stream->volume);
    PlatformSetStreamVolume(&volume, stream->channel);
    return true;
}

// 0x00450D60
void __cdecl Init()
{
    Owners()[0] = Owners()[1] = Owners()[2] = nullptr;
    if (SoundAvailable() && (GameVariable<bool>(0x0067D7E0) || StreamsOn())) {
        GameFunction<void(__cdecl*)()>(0x00453670)();
        Initialised() = true;
        DefaultStreamVolume() = static_cast<float>(Script::GetInteger(0xABD4A575u));
        if (DefaultStreamVolume() == Zero())
            DefaultStreamVolume() = 50.0f;
        DefaultMusicVolume() = static_cast<float>(Script::GetInteger(0x73F8E03Bu));
        if (DefaultMusicVolume() == Zero())
            DefaultMusicVolume() = 100.0f;
        auto* playlists = reinterpret_cast<Playlist*>(0x00698098);
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
    if (GameVariable<bool>(0x00684FA8) && Initialised()) {
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
REPLACE(0x00450500, Pcm::StreamExists);
REPLACE(0x00450520, Pcm::GetCurrentTrack);
REPLACE(0x00450530, Pcm::SetCurrentTrack);
REPLACE(0x00450650, Pcm::StopMusic);
REPLACE(0x00450820, Pcm::PlayTrackByName);
REPLACE(0x00450EF0, Pcm::PlayMusicStream);
REPLACE(0x004509E0, Pcm::PreLoadMusicStream);
REPLACE(0x00450A50, Pcm::PreLoadMusicStreamDone);
REPLACE(0x00450A80, Pcm::StartPreLoadedMusicStream);
REPLACE(0x00450690, Pcm::StopStreams);
REPLACE(0x004506D0, Pcm::StopStream);
REPLACE(0x00450780, Pcm::SetStreamVolume);
REPLACE(0x004507D0, Pcm::SetStreamPitch);
REPLACE(0x00450940, Pcm::PreLoadStreamDone);
REPLACE(0x00450980, Pcm::StartPreLoadedStream);
REPLACE(0x00450AE0, Pcm::RemoveStream);
REPLACE(0x00450BE0, Pcm::UpdateStreamVolume);
REPLACE(0x00450D60, Pcm::Init);
REPLACE(0x00450EB0, Pcm::UsingCD);
REPLACE(0x00450540, &Pcm::FamData::Load);
