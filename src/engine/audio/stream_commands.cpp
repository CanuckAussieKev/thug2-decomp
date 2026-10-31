// Streams: starting, preloading and pausing the streams on the three channels, their frame amplitudes.

#include "engine/audio/stream_commands.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/stubs.h"
#include "engine/maths/vec4.h"
#include "engine/objects/stream_component.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/audio/stream.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/sys/game_config.h"
#include "platform/stopwatch.h"
#include "engine/gfx/viewport.h"

namespace AudioStream {

namespace {

bool SoundOn() { return g_soundDevice && GameConfig::g_platform != 2; }
bool& StreamHeaderLoaded() { return g_streamsOn; }
bool StreamsOn() { return SoundOn() && StreamHeaderLoaded(); }

int g_lastStream;
bool g_streamsHeld;
bool g_streamsBlocked;
bool g_streamsPausedBefore;

bool& StreamingHalted() { return g_streamsHeld; }
bool& StreamsPausedBefore() { return g_streamsPausedBefore; }
// Set: no new streams play (nothing in the game sets it).
bool StreamsBlocked() { return g_streamsBlocked; }

Stream* Streams() { return g_streams; }
constexpr int kStreams = 3;
// The stream components that started each stream (null: none); they note the stream's id at +0x18 + 4 * stream.
void** Owners() { return g_streamOwners; }
uint32_t& OwnerId(void* owner, int stream) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(owner) + 0x18 + 4 * stream); }
// The stream PlayStream started last.
int& LastStream() { return g_lastStream; }

StreamAmpCurve g_frameAmps[1]; // (one slot; a checksum of 0 marks it free)
StreamAmpCurve* FrameAmps() { return g_frameAmps; }
constexpr int kFrameAmps = 1;

// The platform's streams (stream.h): whether a channel is free (StreamStatus: 1), pause (streams: 0), stop a
// channel, preload (the platform's PlayStream: loaded, not started) or play (LoadStream) a stream on one.
int ChannelStatus(int channel) { return StreamStatus(channel); }
void PlatformPause(bool pause, int music) { PauseStreams(pause, music); }
void StopChannel(int channel) { HaltStream(channel, 1); } // (the second argument is unused)
bool PreloadOnChannel(uint32_t checksum, int channel, int stopped) { return PlayStream(checksum, channel, stopped); }
bool PlayOnChannel(uint32_t checksum, int channel, const void* volume, float pitch, int stopped, int unknown, int option)
{
    // (the pitch and `stopped` reach two unused arguments; `unknown` is the preload flag, `option` the plays)
    return LoadStream(checksum, channel, static_cast<const StreamGain*>(volume), std::bit_cast<int>(pitch), stopped,
                      unknown != 0, option);
}
// StreamAmpCurve's load (FamData::ReadIn: data\fam\<checksum>.fam into the slot).
bool ReadAmpCurve(StreamAmpCurve* slot, uint32_t checksum) { return reinterpret_cast<FamData*>(slot)->ReadIn(checksum); }
// A script integer that's read and ignored: what used it is compiled out (debug output).
void ReadDebugFlag() { Scripts::FetchIntValue(0xD7BB618Du); }

// The sound effects manager (0x006F85A8) and its volumes (SoundFx::volumeRef, 0x2C bytes; the first two channels are
// set below). volumeRef's destructor is empty.
SoundFx::CAudioMixer* SoundManager() { return SoundFx::g_audioMixer; }
using Loudness = SoundFx::volumeRef;
void ConstructVolume(Loudness* volume) { volume->PrepareAll(); }
void ChangeMuted(Loudness* volume) { volume->ChangeMuted(); }
float PeakChannelIndex(Loudness* volume) { return volume->FindPeakChannel(); }
inline void DestroyVolume(Loudness*) {} // (0x005480A0)
// Whether sound is off (the manager's IsFrozen).
bool SoundOffRequested(SoundFx::CAudioMixer* manager) { return manager->IsFrozen(); }
bool LoudnessFromPosition(SoundFx::CAudioMixer* manager, Loudness* volume, const Maths::Vec4* position, float dropoff,
                      int falloffCurve, void* emitter, Maths::Vec4* emitterDirection)
{
    return manager->FetchLoudness(volume, position, dropoff, falloffCurve, emitter, emitterDirection);
}
// The emitter nearest to a position (0x004A09E0, not rewritten yet; its distance through the second argument),
// and the stream component's position for it and its emitter's extent.
void* NearestEmitter(const float* position)
{
    return Engine3D::FetchNearestCamera(reinterpret_cast<const Maths::Vec4*>(position), nullptr);
}
Maths::Vec4* SoundPosition(void* component, Maths::Vec4* out, void* emitter)
{
    return static_cast<GameObjects::CAudioStreamModule*>(component)->GetStreamPosition(out, emitter);
}
bool EmitterDirection(void* component, void* emitter, Maths::Vec4* out)
{
    return static_cast<GameObjects::CAudioStreamModule*>(component)->GetStreamExtent(emitter, out);
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
void __cdecl FreezeStream(int pause)
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
        StreamingHalted() = false;
        PlatformPause(false, 0);
    } else if (pause == 1) {
        ReadDebugFlag();
        StreamsPausedBefore() = StreamingHalted();
        StreamingHalted() = true;
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
StreamAmpCurve* __cdecl LoadStreamFrameAmp(uint32_t checksum)
{
    for (int i = 0; i < kFrameAmps; ++i)
        if (FrameAmps()[i].checksum == 0)
            return ReadAmpCurve(&FrameAmps()[i], checksum) ? &FrameAmps()[i] : nullptr;
    return nullptr;
}

// 0x004516E0
StreamAmpCurve* __cdecl FetchStreamAmpCurve(uint32_t checksum)
{
    for (int i = 0; i < kFrameAmps; ++i)
        if (FrameAmps()[i].checksum == checksum)
            return &FrameAmps()[i];
    return nullptr;
}

// 0x00451720
bool __cdecl FreeStreamFrameAmp(uint32_t checksum)
{
    StreamAmpCurve* slot = FetchStreamAmpCurve(checksum);
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
    uint32_t oldest = Streams()[0].beginFrame;
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
        if (stream.priority < lowest || (stream.priority == lowest && stream.beginFrame < oldest)) {
            lowest = stream.priority;
            oldest = stream.beginFrame;
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
    stream.beginFrame = static_cast<uint32_t>(FrameClock::GetDrawFrame()); // (the low half)
    stream.listener = reinterpret_cast<uint32_t*>(FetchStreamAmpCurve(checksum));
    return channel;
}

// 0x00451960
uint32_t __cdecl PlayStream(uint32_t checksum, const void* volume, float pitch, int option, int priority, uint32_t id)
{
    if (!StreamsOn() || StreamsBlocked() || StreamingHalted())
        return 0;
    if (!id)
        id = checksum;
    StreamIdFinished(id); // (its answer unused)
    int channel = StartStream(checksum, volume, pitch, option, priority, id, false);
    if (channel == -1)
        return 0;
    Stream& stream = Streams()[channel];
    SetUpStream(stream, checksum, option);
    stream.names[0].frameAmp = FetchStreamAmpCurve(checksum);
    LastStream() = channel;
    return stream.id;
}

// 0x00451A50
uint32_t __cdecl PrimeStream(uint32_t checksum, int priority)
{
    if (!StreamsOn())
        return 0;
    Loudness volume;
    ConstructVolume(&volume);
    ChangeMuted(&volume);
    uint32_t id = 0;
    int channel = StartStream(checksum, &volume, 0.0f, 0, priority, checksum, true);
    if (channel != -1) {
        Stream& stream = Streams()[channel];
        SetUpStream(stream, checksum, 0);
        stream.names[0].frameAmp = FetchStreamAmpCurve(checksum);
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
    playing.names[n].frameAmp = FetchStreamAmpCurve(name);
    return true;
}

// 0x00451E20
uint32_t __cdecl PlayStreamAtObject(void* component, uint32_t checksum, float dropoff, float volume, float pitch,
                                      int option, int priority, int usePosInfo, int falloffCurve, uint32_t id)
{
    SoundFx::CAudioMixer* manager = SoundManager();
    if (SoundOffRequested(manager) || !StreamsOn())
        return 0;
    Loudness streamVolume;
    ConstructVolume(&streamVolume);
    ChangeMuted(&streamVolume);
    if (usePosInfo) {
        void* object = *reinterpret_cast<void**>(static_cast<uint8_t*>(component) + 0xC);
        void* emitter = NearestEmitter(reinterpret_cast<const float*>(static_cast<uint8_t*>(object) + 0x4C));
        bool audible = true;
        if (emitter) {
            Maths::Vec4 direction;
            Maths::Vec4* towards = EmitterDirection(component, emitter, &direction) ? &direction : nullptr;
            Maths::Vec4 position;
            audible = LoudnessFromPosition(manager, &streamVolume, SoundPosition(component, &position, emitter), dropoff,
                                       falloffCurve, emitter, towards);
        } else {
            ChangeMuted(&streamVolume);
        }
        if (std::fabs(PeakChannelIndex(&streamVolume)) < 2.0f) { // (NaN: on)
            ReadDebugFlag();
            if (!audible) {
                DestroyVolume(&streamVolume);
                return 0;
            }
        }
    } else {
        std::memcpy(&streamVolume.m_lanes[0], &volume, 4);
        std::memcpy(&streamVolume.m_lanes[1], &volume, 4);
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
                Streams()[i].falloffCurve = falloffCurve;
            } else {
                Streams()[i].usePosInfo = 0;
            }
        }
    }
    DestroyVolume(&streamVolume);
    return playing;
}

} // namespace Pcm
