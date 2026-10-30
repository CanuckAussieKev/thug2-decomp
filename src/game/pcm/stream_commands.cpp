// Streams: starting, preloading and pausing the streams on the three channels, their frame amplitudes.

#include "game/pcm/stream_commands.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/stubs.h"
#include "game/mth/vector.h"
#include "game/obj/stream_component.h"
#include "game/pcm/music.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/game_config.h"
#include "game/sys/timer.h"
#include "game/gfx/viewport.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Pcm {

namespace {

bool SoundOn() { return g_soundDevice && GameConfig::g_platform != 2; }
bool& StreamHeaderLoaded() { return g_streamsOn; }
bool StreamsOn() { return SoundOn() && StreamHeaderLoaded(); }

GAME_GLOBAL(int, g_lastStream, 0x0069BA94);
GAME_GLOBAL(bool, g_streamsPaused, 0x0069BAA9);
GAME_GLOBAL(bool, g_streamsBlocked, 0x0069BAAA);
GAME_GLOBAL(bool, g_streamsPausedBefore, 0x0069CAD1);

bool& StreamsPaused() { return g_streamsPaused; }
bool& StreamsPausedBefore() { return g_streamsPausedBefore; }
// Set: no new streams play (nothing in the game sets it).
bool StreamsBlocked() { return g_streamsBlocked; }

Stream* Streams() { return reinterpret_cast<Stream*>(0x0069B938); }
constexpr int kStreams = 3;
// The stream components that started each stream (null: none); they note the stream's id at +0x18 + 4 * stream.
void** Owners() { return reinterpret_cast<void**>(0x00698060); }
uint32_t& OwnerId(void* owner, int stream) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(owner) + 0x18 + 4 * stream); }
// The stream PlayStream started last.
int& LastStream() { return g_lastStream; }

FrameAmp* FrameAmps() { return reinterpret_cast<FrameAmp*>(0x0069BAC8); }
constexpr int kFrameAmps = 1;

// The platform's streams (stream.h): whether a channel is free (StreamStatus: 1), pause (streams: 0), stop a
// channel, preload (the platform's PlayStream: loaded, not started) or play (LoadStream) a stream on one.
int ChannelStatus(int channel) { return StreamStatus(channel); }
void PlatformPause(bool pause, int music) { PauseStreams(pause, music); }
void StopChannel(int channel) { StopStream(channel, 1); } // (the second argument is unused)
bool PreloadOnChannel(uint32_t checksum, int channel, int stopped) { return PlayStream(checksum, channel, stopped); }
bool PlayOnChannel(uint32_t checksum, int channel, const void* volume, float pitch, int stopped, int unknown, int option)
{
    // (the pitch and `stopped` reach two unused arguments; `unknown` is the preload flag, `option` the plays)
    return LoadStream(checksum, channel, static_cast<const StreamVolume*>(volume), std::bit_cast<int>(pitch), stopped,
                      unknown != 0, option);
}
// FrameAmp's load (FamData::Load: data\fam\<checksum>.fam into the slot).
bool LoadFrameAmp(FrameAmp* slot, uint32_t checksum) { return reinterpret_cast<FamData*>(slot)->Load(checksum); }
// A script integer that's read and ignored: what used it is compiled out (debug output).
void ReadDebugFlag() { Script::GetInteger(0xD7BB618Du); }

// The sound effects manager (0x006F85A8) and its volumes (Sfx::sVolume, 0x2C bytes; the first two channels are
// set below). sVolume's destructor is empty.
Sfx::CSfxManager* SoundManager() { return Sfx::g_sfxManager; }
using Volume = Sfx::sVolume;
void ConstructVolume(Volume* volume) { volume->Init(); }
void SetSilent(Volume* volume) { volume->SetSilent(); }
float LoudestChannel(Volume* volume) { return volume->GetLoudestChannel(); }
inline void DestroyVolume(Volume*) {} // (0x005480A0)
// Whether sound is off (the manager's IsPaused).
bool NoSoundPlease(Sfx::CSfxManager* manager) { return manager->IsPaused(); }
bool SetVolumeFromPos(Sfx::CSfxManager* manager, Volume* volume, const Mth::Vector* position, float dropoff,
                      int dropoffFunction, void* emitter, Mth::Vector* emitterDirection)
{
    return manager->GetVolume(volume, position, dropoff, dropoffFunction, emitter, emitterDirection);
}
// The emitter nearest to a position (0x004A09E0, not rewritten yet; its distance through the second argument),
// and the stream component's position for it and its emitter's extent.
void* NearestEmitter(const float* position)
{
    return Nx::GetClosestCamera(reinterpret_cast<const Mth::Vector*>(position), nullptr);
}
Mth::Vector* SoundPosition(void* component, Mth::Vector* out, void* emitter)
{
    return static_cast<Obj::CStreamComponent*>(component)->GetStreamPosition(out, emitter);
}
bool EmitterDirection(void* component, void* emitter, Mth::Vector* out)
{
    return static_cast<Obj::CStreamComponent*>(component)->GetStreamExtent(emitter, out);
}

// A new stream's record: volume 100, dropoff 1020, one name (the stream's own).
void SetUpStream(Stream& stream, uint32_t checksum, int option)
{
    stream.dropoff = 1020.0f;
    stream.volume = 100.0f;
    stream.unknown1C = 0;
    stream.nameCount = 1;
    stream.option = option;
    stream.names[0].checksum = checksum;
}

} // namespace

// 0x00451010
void __cdecl PauseStream(int pause)
{
    if (!StreamsOn())
        return;
    if (pause == -1) {
        if (StreamsPausedBefore())
            return;
        pause = 0;
    }
    if (pause == 0) {
        ReadDebugFlag();
        StreamsPaused() = false;
        PlatformPause(false, 0);
    } else if (pause == 1) {
        ReadDebugFlag();
        StreamsPausedBefore() = StreamsPaused();
        StreamsPaused() = true;
        PlatformPause(true, 0);
    }
}

// 0x00451620
bool __cdecl LoadStreamHeader(const char* name)
{
    if (!StreamsOn())
        return false;
    StreamHeaderLoaded() = Stubs::ReturnTrue(); // (0x005B9760 with the name: the PS2's file check)
    (void)name;
    return StreamHeaderLoaded();
}

// 0x00451660
bool __cdecl IsStreamPlaying(uint32_t id) { return StreamsOn() && FindPlayingStream(id) >= 0; }

// 0x004516A0
FrameAmp* __cdecl LoadStreamFrameAmp(uint32_t checksum)
{
    for (int i = 0; i < kFrameAmps; ++i)
        if (FrameAmps()[i].checksum == 0)
            return LoadFrameAmp(&FrameAmps()[i], checksum) ? &FrameAmps()[i] : nullptr;
    return nullptr;
}

// 0x004516E0
FrameAmp* __cdecl GetStreamFrameAmp(uint32_t checksum)
{
    for (int i = 0; i < kFrameAmps; ++i)
        if (FrameAmps()[i].checksum == checksum)
            return &FrameAmps()[i];
    return nullptr;
}

// 0x00451720
bool __cdecl FreeStreamFrameAmp(uint32_t checksum)
{
    FrameAmp* slot = GetStreamFrameAmp(checksum);
    if (!slot || slot->checksum == 0)
        return false;
    slot->checksum = 0;
    slot->size = 0;
    return true;
}

// 0x00451770
int __cdecl StartStream(uint32_t checksum, const void* volume, float pitch, int option, int priority, uint32_t name,
                        bool preload)
{
    if (!StreamsOn())
        return -1;
    uint32_t oldest = Streams()[0].startFrame;
    int lowest = Streams()[0].priority;
    int channel = 0;
    uint32_t id = FreeStreamId(name);
    int free = -1;
    for (int i = 0; i < kStreams; ++i) {
        if (ChannelStatus(i) == 1) {
            free = i;
            break;
        }
        const Stream& stream = Streams()[i];
        if (stream.priority < lowest || (stream.priority == lowest && stream.startFrame < oldest)) {
            lowest = stream.priority;
            oldest = stream.startFrame;
            channel = i;
        }
    }
    bool started;
    if (free >= 0) {
        channel = free;
        started = preload ? PreloadOnChannel(checksum, channel, 0)
                          : PlayOnChannel(checksum, channel, volume, pitch, 0, 0, option);
    } else {
        if (priority < lowest)
            return -1;
        StopChannel(channel);
        started = preload ? PreloadOnChannel(checksum, channel, 1)
                          : PlayOnChannel(checksum, channel, volume, pitch, 1, 0, option);
    }
    if (!started)
        return -1;
    ReadDebugFlag();
    Stream& stream = Streams()[channel];
    stream.name = name;
    stream.id = id;
    stream.channel = channel;
    stream.priority = priority;
    stream.startFrame = static_cast<uint32_t>(Tmr::GetRenderFrame()); // (the low half)
    stream.listener = reinterpret_cast<uint32_t*>(GetStreamFrameAmp(checksum));
    return channel;
}

// 0x00451960
uint32_t __cdecl PlayStream(uint32_t checksum, const void* volume, float pitch, int option, int priority, uint32_t id)
{
    if (!StreamsOn() || StreamsBlocked() || StreamsPaused())
        return 0;
    if (!id)
        id = checksum;
    StreamIdFinished(id); // (its answer unused)
    int channel = StartStream(checksum, volume, pitch, option, priority, id, false);
    if (channel == -1)
        return 0;
    Stream& stream = Streams()[channel];
    SetUpStream(stream, checksum, option);
    stream.names[0].frameAmp = GetStreamFrameAmp(checksum);
    LastStream() = channel;
    return stream.id;
}

// 0x00451A50
uint32_t __cdecl PreLoadStream(uint32_t checksum, int priority)
{
    if (!StreamsOn())
        return 0;
    Volume volume;
    ConstructVolume(&volume);
    SetSilent(&volume);
    uint32_t id = 0;
    int channel = StartStream(checksum, &volume, 0.0f, 0, priority, checksum, true);
    if (channel != -1) {
        Stream& stream = Streams()[channel];
        SetUpStream(stream, checksum, 0);
        stream.names[0].frameAmp = GetStreamFrameAmp(checksum);
        id = stream.id;
    }
    DestroyVolume(&volume);
    return id;
}

// 0x00451B80
bool __cdecl AddStream(uint32_t stream, uint32_t name, void* owner)
{
    if (!StreamsOn())
        return false;
    int found = -1;
    if (owner) {
        for (int i = 0; i < kStreams; ++i) {
            if (Owners()[i] == owner && Streams()[i].name == stream && ChannelStatus(Streams()[i].channel) != 1) {
                found = i;
                break;
            }
        }
        if (found < 0)
            return false;
    } else {
        found = FindPlayingStream(stream);
        if (found < 0)
            return false;
    }
    Stream& playing = Streams()[found];
    int n = playing.nameCount++;
    playing.names[n].checksum = name;
    playing.names[n].frameAmp = GetStreamFrameAmp(name);
    return true;
}

// 0x00451E20
uint32_t __cdecl PlayStreamFromObject(void* component, uint32_t checksum, float dropoff, float volume, float pitch,
                                      int option, int priority, int usePosInfo, int dropoffFunction, uint32_t id)
{
    Sfx::CSfxManager* manager = SoundManager();
    if (NoSoundPlease(manager) || !StreamsOn())
        return 0;
    Volume streamVolume;
    ConstructVolume(&streamVolume);
    SetSilent(&streamVolume);
    if (usePosInfo) {
        void* object = *reinterpret_cast<void**>(static_cast<uint8_t*>(component) + 0xC);
        void* emitter = NearestEmitter(reinterpret_cast<const float*>(static_cast<uint8_t*>(object) + 0x4C));
        bool audible = true;
        if (emitter) {
            Mth::Vector direction;
            Mth::Vector* towards = EmitterDirection(component, emitter, &direction) ? &direction : nullptr;
            Mth::Vector position;
            audible = SetVolumeFromPos(manager, &streamVolume, SoundPosition(component, &position, emitter), dropoff,
                                       dropoffFunction, emitter, towards);
        } else {
            SetSilent(&streamVolume);
        }
        if (std::fabs(LoudestChannel(&streamVolume)) < 2.0f) { // (NaN: on)
            ReadDebugFlag();
            if (!audible) {
                DestroyVolume(&streamVolume);
                return 0;
            }
        }
    } else {
        std::memcpy(&streamVolume.m_channels[0], &volume, 4);
        std::memcpy(&streamVolume.m_channels[1], &volume, 4);
    }
    uint32_t playing = PlayStream(checksum, &streamVolume, pitch, option, priority, id);
    if (playing) {
        int last = LastStream();
        if (Owners()[last]) {
            OwnerId(Owners()[last], last) = 0;
            Owners()[last] = nullptr;
        }
        Owners()[last] = component;
        OwnerId(component, last) = playing;
        if (volume != 100.0f) {
            for (int i = 0; i < kStreams; ++i) {
                if (Streams()[i].id == playing) {
                    Streams()[i].dropoff = dropoff;
                    Streams()[i].volume = static_cast<float>(static_cast<double>(Streams()[i].volume) * volume * 0.01f);
                }
            }
        }
        for (int i = 0; i < kStreams; ++i) {
            if (Streams()[i].id != playing)
                continue;
            if (usePosInfo) {
                Streams()[i].usePosInfo = 1;
                Streams()[i].dropoffFunction = dropoffFunction;
            } else {
                Streams()[i].usePosInfo = 0;
            }
        }
    }
    DestroyVolume(&streamVolume);
    return playing;
}

} // namespace Pcm

REPLACE(0x00451010, Pcm::PauseStream);
REPLACE(0x00451620, Pcm::LoadStreamHeader);
REPLACE(0x00451660, Pcm::IsStreamPlaying);
REPLACE(0x004516A0, Pcm::LoadStreamFrameAmp);
REPLACE(0x004516E0, Pcm::GetStreamFrameAmp);
REPLACE(0x00451720, Pcm::FreeStreamFrameAmp);
REPLACE(0x00451770, Pcm::StartStream);
REPLACE(0x00451960, static_cast<uint32_t(__cdecl*)(uint32_t, const void*, float, int, int, uint32_t)>(Pcm::PlayStream));
REPLACE(0x00451A50, Pcm::PreLoadStream);
REPLACE(0x00451B80, Pcm::AddStream);
REPLACE(0x00451E20, Pcm::PlayStreamFromObject);
REPLACE_SOURCE_ONLY(0x00451E7D, Pcm::PlayStreamFromObject); // (not a function: the rest of 0x00451E20's body)
