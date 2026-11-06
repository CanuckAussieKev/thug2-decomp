// NOT ORIGINAL: [features] mute_audio as a sound output - see muted_device.h.

#include "engine/audio/muted_device.h"

#include <cstdint>
#include <span>

#include <gsl/pointers>

#include "base/core/no_destroy.h"
#include "engine/audio/audio_device.h"
#include "engine/audio/audio_handles.h"
#include "platform/mute_audio.h"

namespace audio {

namespace {

class MutedDevice final : public Device {
public:
    explicit MutedDevice(Device& output) : output_(&output) {}

    // --- the output: the session muted while it is open ---
    SoundOutput* open(void* window) override
    {
        SoundOutput* const opened = output_->open(window);
        sound::set_audio_session_muted(true); // before anything plays
        return opened;
    }
    void close(SoundOutput* output) override
    {
        output_->close(output);
        sound::set_audio_session_muted(false); // or Windows would keep the session muted for the exe's next run
    }
    void list_outputs(OutputListed listed) override { output_->list_outputs(listed); }

    // --- the voices: all as the output does them, but silent ---
    bool get_caps(SoundOutput* output, OutputCaps& caps) override { return output_->get_caps(output, caps); }
    SoundListener* create_listener(SoundOutput* output) override { return output_->create_listener(output); }
    void orient_listener(SoundListener* listener, Point3 front) override
    {
        output_->orient_listener(listener, front);
    }
    void commit_listener(SoundListener* listener) override { output_->commit_listener(listener); }
    MadeBuffer create_buffer(SoundOutput* output, const BufferDesc& desc) override
    {
        return output_->create_buffer(output, desc);
    }
    SoundBuffer3d* buffer_3d(SoundBuffer* buffer) override { return output_->buffer_3d(buffer); }
    void fill_buffer(SoundBuffer* buffer, std::span<const uint8_t> samples) override
    {
        output_->fill_buffer(buffer, samples);
    }
    void release_buffer(SoundBuffer* buffer) override { output_->release_buffer(buffer); }
    void release_buffer_3d(SoundBuffer3d* buffer) override { output_->release_buffer_3d(buffer); }
    bool play_buffer(SoundBuffer* buffer, bool looping) override { return output_->play_buffer(buffer, looping); }
    void stop_buffer(SoundBuffer* buffer) override { output_->stop_buffer(buffer); }
    bool buffer_finished(SoundBuffer* buffer) override { return output_->buffer_finished(buffer); }
    void set_buffer_volume(SoundBuffer* buffer, int /*attenuation*/) override
    {
        output_->set_buffer_volume(buffer, kSilentAttenuation);
    }
    void set_buffer_pan(SoundBuffer* buffer, int pan) override { output_->set_buffer_pan(buffer, pan); }
    void set_buffer_frequency(SoundBuffer* buffer, uint32_t frequency) override
    {
        output_->set_buffer_frequency(buffer, frequency);
    }
    void place_buffer_3d(SoundBuffer3d* buffer, BufferRange range, Point3 position) override
    {
        output_->place_buffer_3d(buffer, range, position);
    }

    // --- a streaming buffer: as the output does it (its volume set through set_buffer_volume: silent) ---
    bool buffer_position(SoundBuffer* buffer, uint32_t& position) override
    {
        return output_->buffer_position(buffer, position);
    }
    void write_buffer(SoundBuffer* buffer, uint32_t at, std::span<const uint8_t> samples) override
    {
        output_->write_buffer(buffer, at, samples);
    }
    bool resume_buffer(SoundBuffer* buffer) override { return output_->resume_buffer(buffer); }

    // --- the movies' and streams' sound: silent ---
    int track_volume(int /*volume*/) override { return 0; }

private:
    gsl::not_null<Device*> output_;
};

} // namespace

Device& muted_device(Device& output)
{
    static core::NoDestroy<MutedDevice> made(output);
    return made.get();
}

} // namespace audio
