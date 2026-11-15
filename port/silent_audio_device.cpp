// NOT ORIGINAL - the macOS (arm64) port's stand-in for the sound output (audio::Device, engine/audio/audio_device.h).
// There is no DirectSound here, and no SDL3 output has been written yet (port/README.md), so the game runs with its
// voices made and played but never heard: every buffer is made, filled and played without a sound, and reports itself
// finished at once. The game's own logic (which voice plays when, the music and the streams' bookkeeping) runs as it
// does with a device.

#include <cstddef>
#include <span>

#include "engine/audio/audio_device.h"
#include "port/silent_audio_device.h"

namespace audio {

namespace {

struct SilentOutput {
    int placeholder = 0;
};
struct SilentBuffer {
    uint32_t bytes = 0;
};
struct SilentBuffer3d {
    int placeholder = 0;
};
struct SilentListener {
    int placeholder = 0;
};

class SilentDevice final : public Device {
public:
    SoundOutput* open(void* /*window*/) override
    {
        static SilentOutput output;
        return reinterpret_cast<SoundOutput*>(&output);
    }
    void close(SoundOutput* /*output*/) override {}
    void list_outputs(OutputListed listed) override
    {
        if (listed != nullptr) {
            listed("no sound output (the macOS port has none yet)");
        }
    }
    bool get_caps(SoundOutput* /*output*/, OutputCaps& caps) override
    {
        caps = OutputCaps{};
        return true;
    }
    SoundListener* create_listener(SoundOutput* /*output*/) override
    {
        static SilentListener listener;
        return reinterpret_cast<SoundListener*>(&listener);
    }
    void orient_listener(SoundListener* /*listener*/, Point3 /*front*/) override {}
    void commit_listener(SoundListener* /*listener*/) override {}
    MadeBuffer create_buffer(SoundOutput* /*output*/, const BufferDesc& desc) override
    {
        auto* buffer = new SilentBuffer();
        buffer->bytes = desc.bytes;
        return MadeBuffer{.buffer = reinterpret_cast<SoundBuffer*>(buffer), .made = true};
    }
    SoundBuffer3d* buffer_3d(SoundBuffer* buffer) override
    {
        // (no 3D on a silent output: a voice placed in space keeps its volume as it is)
        auto* spatial = new SilentBuffer3d();
        (void)buffer;
        return reinterpret_cast<SoundBuffer3d*>(spatial);
    }
    void fill_buffer(SoundBuffer* /*buffer*/, std::span<const uint8_t> /*samples*/) override {}
    void release_buffer(SoundBuffer* buffer) override { delete reinterpret_cast<SilentBuffer*>(buffer); }
    void release_buffer_3d(SoundBuffer3d* buffer) override { delete reinterpret_cast<SilentBuffer3d*>(buffer); }
    bool play_buffer(SoundBuffer* /*buffer*/, bool /*looping*/) override { return true; }
    void stop_buffer(SoundBuffer* /*buffer*/) override {}
    bool buffer_finished(SoundBuffer* /*buffer*/) override { return true; }
    void set_buffer_volume(SoundBuffer* /*buffer*/, int /*attenuation*/) override {}
    void set_buffer_pan(SoundBuffer* /*buffer*/, int /*pan*/) override {}
    void set_buffer_frequency(SoundBuffer* /*buffer*/, uint32_t /*frequency*/) override {}
    void place_buffer_3d(SoundBuffer3d* /*buffer*/, BufferRange /*range*/, Point3 /*position*/) override {}
    bool buffer_position(SoundBuffer* /*buffer*/, uint32_t& position) override
    {
        position = 0;
        return true;
    }
    void write_buffer(SoundBuffer* /*buffer*/, uint32_t /*at*/, std::span<const uint8_t> /*samples*/) override {}
    bool resume_buffer(SoundBuffer* /*buffer*/) override { return true; }
    int track_volume(int volume) override { return volume; }
};

} // namespace

Device& silent_device()
{
    static SilentDevice the_device;
    return the_device;
}

} // namespace audio
