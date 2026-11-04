#pragma once

// NOT ORIGINAL - audio::Device: what the engine asks of the sound output. The output is made and released by
// WinMain; the sound effects' voices (engine/sound/sfx_voices.cpp, the original's PC driver) make, fill, play, stop
// and set their buffers through it, and place them and the listener in 3D; the graphics device's start-up lists the
// output devices through it; and the movies' and streams' sound (Bink's own output, played into this output:
// media::Movie, engine/media/movie.h) gets its volume from it. app/ makes it and installs it at start-up
// (install_device), a run-time choice: DirectSound's (audio_dsound/dsound_device.h), or - with thug2-decomp.ini
// [features] mute_audio=1 - the muted output over it (muted_device.h: the null output, which plays everything
// silently). An SDL audio or OpenAL one could stand in: buffers are made from a sample format and filled whole,
// played from the start, and set per voice (volume, pan, frequency, a 3D place).
//
// The grain is a voice (a buffer made, filled, played, stopped, set) or the listener once a voice update; never per
// sample - the samples are the engine's, copied into a buffer whole. The units are the ones the game computes in:
// volumes and pans in hundredths of a decibel (DirectSound's: -10000 silent ... 0 full), frequencies in samples a
// second, Bink's track volume 0 ... 32768.
//
// The methods keep the order and arguments of the calls they stand for: each DirectSound method is the DirectSound
// calls the original made at that place, in the same order.
//
// Not installed, a call dereferences null: the device is installed before any of the original's code runs.

#include <array>
#include <cstdint>
#include <span>

#include "engine/audio/audio_handles.h"

namespace audio {

constexpr int kSilentAttenuation = -10000; // a buffer's volume that is silence (hundredths of a decibel)

// What the output can do (DirectSound's DSCAPS): the free hardware buffers the voices are counted from.
struct OutputCaps {
    uint32_t free_3d_buffers{};     // free hardware 3D buffers (none: no 3D)
    uint32_t free_mixing_buffers{}; // free hardware mixing buffers
};

// A voice's samples' format (the wave's WAVEFORMATEX fields, engine/sound/sfx.h PcWave).
struct SampleFormat {
    uint16_t format_tag{};
    uint16_t channels{};
    uint32_t samples_per_second_{};
    uint32_t bytes_per_second_{};
    uint16_t block_align_{};
    uint16_t bits_per_sample_{};
    uint16_t extra_size_{};
};

// A voice's buffer as the wave describes it (PcWave's buffer description: its size, DirectSound's DSBCAPS flags -
// 2D 0x400E0, 3D 0x400B0 -, the bytes, the reserved word and the 3D algorithm's GUID, as the game keeps them).
struct BufferDesc {
    SampleFormat format;
    uint32_t desc_size_{};
    uint32_t flags{};
    uint32_t bytes{};
    uint32_t reserved{};
    std::array<uint32_t, 4> algorithm{};
};

// A buffer asked for: what the output left as its handle (on failure too) and whether it was made.
struct MadeBuffer {
    SoundBuffer* buffer{};
    bool made{};
};

// A point or direction in the output's 3D space (the listener's own: the camera-relative position, the facing).
struct Point3 {
    float x{};
    float y{};
    float z{};
};

// A 3D buffer's range: full volume within `min_distance`, fading out to `max_distance`.
struct BufferRange {
    float max_distance{};
    float min_distance{};
};

// Told each output device's description, in the order the system lists them.
using OutputListed = void (*)(const char* description);

class Device {
public:
    Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    // --- the output (WinMain) ---
    // 0x005F59D0: the output for the game's `window` (a HWND; DirectSound at the priority level), or null when it
    // can't be made (then nothing plays: sound::no_sound).
    virtual SoundOutput* open(void* window) = 0;
    // 0x005F5A00: the output released (null: nothing to release).
    virtual void close(SoundOutput* output) = 0;
    // The output devices, each description told to `listed` (the graphics device's start-up notes the last one).
    virtual void list_outputs(OutputListed listed) = 0;

    // --- the voices (engine/sound/sfx_voices.cpp) ---
    // The output's capabilities into `caps`; false when it won't say.
    virtual bool get_caps(SoundOutput* output, OutputCaps& caps) = 0;
    // The 3D listener (the primary buffer's, placed at the origin; the primary buffer let go of again).
    virtual SoundListener* create_listener(SoundOutput* output) = 0;
    // The listener facing `front` with -y up (deferred: applied by commit_listener).
    virtual void orient_listener(SoundListener* listener, Point3 front) = 0;
    // The deferred 3D settings (the listener's and the buffers') applied.
    virtual void commit_listener(SoundListener* listener) = 0;
    // A buffer for a voice.
    virtual MadeBuffer create_buffer(SoundOutput* output, const BufferDesc& desc) = 0;
    // A buffer's 3D controls (null if it has none).
    virtual SoundBuffer3d* buffer_3d(SoundBuffer* buffer) = 0;
    // The whole buffer filled with `samples` (nothing when it can't be locked).
    virtual void fill_buffer(SoundBuffer* buffer, std::span<const uint8_t> samples) = 0;
    virtual void release_buffer(SoundBuffer* buffer) = 0;
    virtual void release_buffer_3d(SoundBuffer3d* buffer) = 0;
    // The buffer played from its start (looping or once); false if it won't play.
    virtual bool play_buffer(SoundBuffer* buffer, bool looping) = 0;
    virtual void stop_buffer(SoundBuffer* buffer) = 0;
    // Whether the buffer's status was read and it isn't playing any more.
    virtual bool buffer_finished(SoundBuffer* buffer) = 0;
    // The buffer's volume (kSilentAttenuation ... 0), pan (hundredths of a decibel, left negative) and frequency.
    virtual void set_buffer_volume(SoundBuffer* buffer, int attenuation) = 0;
    virtual void set_buffer_pan(SoundBuffer* buffer, int pan) = 0;
    virtual void set_buffer_frequency(SoundBuffer* buffer, uint32_t frequency) = 0;
    // A 3D buffer's range and its place relative to the listener (deferred).
    virtual void place_buffer_3d(SoundBuffer3d* buffer, BufferRange range, Point3 position) = 0;

    // --- the movies' and streams' sound (Bink's output, into this one) ---
    // The level a movie's or stream's sound track plays at for the level `volume` the game set (Bink's 0 ... 32768).
    virtual int track_volume(int volume) = 0;

protected:
    ~Device() = default;
};

// The installed device (app/ installs it before the original's code runs).
Device& device();
void install_device(Device& device);

} // namespace audio
