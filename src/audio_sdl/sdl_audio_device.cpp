// NOT ORIGINAL - the macOS (arm64) port's sound output: audio::Device (engine/audio/audio_device.h) over SDL3's audio.
// It stands in for src/audio_dsound's DirectSound device, and is the same shape: the game's voices are buffers it fills
// (fill_buffer) or streams into (write_buffer), plays, stops and asks where playback has got to, while the 3D listener
// and each buffer's place in space are its own business.
//
// DirectSound mixed those buffers in the driver; here the mixing is ours, in SDL's audio callback: every playing
// buffer's samples are read at its own rate, scaled by its volume (hundredths of a decibel, as the game sets it) and
// panned, and summed into the output. The output is 48 kHz stereo S16 - what the game's own music and streams ask for.
//
// The 3D side is a pan from the listener's point of view: the game places voices by distance and direction, and the
// far ones fade with the range it gives (full volume within `min_distance`, silent past `max_distance`), in the
// listener's frame (the camera's, which is what DirectSound's listener was).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/audio/audio_device.h"
#include "platform/run_log.h"
#include "audio_sdl/sdl_audio_device.h"

namespace audio {

namespace {

constexpr int kOutputRate = 48000;
constexpr int kOutputChannels = 2;
// (the game's attenuation units are not one scale: see gain_of)
constexpr float kPanScale = 1000.0F; // (sfx_voices' kPanScale: pan = log10(left/right) * 1000)

struct SdlOutput;

struct SdlBuffer {
    SdlOutput* owner_ = nullptr;        // the output whose list holds it
    std::vector<std::uint8_t> samples_; // the buffer's own bytes (DirectSound's locked buffer)
    SampleFormat format_{};
    bool playing_ = false;
    bool looping_ = false;
    bool streaming_ = false;      // (written through write_buffer: a ring the game keeps filled, not a one-shot voice)
    int attenuation_ = 0;         // hundredths of a decibel, as the game sets it (kSilentAttenuation: silence)
    int pan_ = 0;                 // -10000 .. 10000
    std::uint32_t frequency_ = 0; // the playback rate the game asked for (0: the buffer's own)
    double position_ = 0.0;       // in source sample frames
    bool spatial_ = false;
    float place_[3] = {0.0F, 0.0F, 0.0F};
    BufferRange range_{};
};

// NOT ORIGINAL - [debug] audio_debug (thug2-decomp.ini; the settings model declares it, as it does the other debug
// switches): the mixer's numbers once a second, which is how its volume, pan and speed were checked.
bool audio_debug()
{
    static const bool on = config_int("debug", "audio_debug", 0) != 0;
    return on;
}

// The one lock over every buffer's state and the list of them: the audio thread holds it for a whole mix, and the game
// takes it for every write, play, stop and position it reads. (Two locks - one for the mix and one for the game's calls
// - let the game write into a buffer while the mixer read it: the torn reads were heard as a hiss, as cropped sounds
// and as a ring that seemed to jump.)
std::mutex& mix_lock()
{
    static std::mutex the_lock;
    return the_lock;
}

struct SdlOutput {
    SDL_AudioStream* stream_ = nullptr;
    SDL_AudioDeviceID device_ = 0;
    std::vector<SdlBuffer*> buffers_; // (added by create_buffer, removed by release_buffer)
    float listener_front_[3] = {0.0F, 1.0F, 0.0F};
};

struct SdlListener {
    SdlOutput* output_ = nullptr;
};

SdlOutput& output_of(SoundOutput* output) { return *reinterpret_cast<SdlOutput*>(output); }
SdlBuffer& buffer_of(SoundBuffer* buffer) { return *reinterpret_cast<SdlBuffer*>(buffer); }
SdlBuffer& buffer_of(SoundBuffer3d* buffer) { return *reinterpret_cast<SdlBuffer*>(buffer); }

// A sample as it leaves the mix: linear below the knee, bent towards full scale above it, so a loud moment softens
// instead of splintering into harmonics.
std::int16_t to_output(float value)
{
    constexpr float kKnee = 24000.0F;
    constexpr float kCeiling = 32760.0F;
    const float magnitude = std::fabs(value);
    float out = value;
    if (magnitude > kKnee) {
        const float over = (magnitude - kKnee) / (kCeiling - kKnee);
        const float bent = kKnee + ((kCeiling - kKnee) * (1.0F - std::exp(-over)));
        out = value < 0.0F ? -bent : bent;
    }
    return static_cast<std::int16_t>(std::clamp(out, -kCeiling, kCeiling));
}

// An attenuation as a gain. The game's two paths write it in different units: a voice's comes from sfx_voices'
// kLevelDecibelScale (2000 per decade - DirectSound's own centibels), and a movie's or a stream's from ffmpeg_sound's
// kAttenuationPerDecade (10000/3 per decade). Undoing both with 2000 made every music track and movie -10 dB where the
// code meant -6 dB; each is undone with the constant its own code used.
float gain_of(int attenuation, bool streamed)
{
    if (attenuation <= kSilentAttenuation) {
        return 0.0F;
    }
    constexpr double kVoiceDecade = 2000.0;         // (sfx_voices: kLevelDecibelScale)
    constexpr double kStreamDecade = 10000.0 / 3.0; // (ffmpeg_sound: kAttenuationPerDecade)
    const double scale = streamed ? kStreamDecade : kVoiceDecade;
    return static_cast<float>(std::pow(10.0, static_cast<double>(attenuation) / scale));
}

// A sample frame's size: the wave's own block alignment when it states one (that is how its data is laid out), else
// from the format. A wave whose alignment disagrees with its format is played by its alignment, not by the guess.
std::size_t frame_bytes_of(const SdlBuffer& buffer)
{
    if (buffer.format_.block_align_ != 0) {
        return buffer.format_.block_align_;
    }
    const int channels = buffer.format_.channels > 0 ? buffer.format_.channels : 1;
    const std::size_t bytes_per_sample =
        buffer.format_.bits_per_sample_ > 0 ? buffer.format_.bits_per_sample_ / 8U : 2U;
    return bytes_per_sample * static_cast<std::size_t>(channels);
}

float sample_at(const SdlBuffer& buffer, std::uint32_t frame, int channel)
{
    const int channels = buffer.format_.channels > 0 ? buffer.format_.channels : 1;
    const std::size_t bytes_per_sample =
        buffer.format_.bits_per_sample_ > 0 ? buffer.format_.bits_per_sample_ / 8U : 2U;
    const std::size_t frame_bytes = frame_bytes_of(buffer);
    const std::size_t offset = (static_cast<std::size_t>(frame) * frame_bytes) +
                               (static_cast<std::size_t>(channel % channels) * bytes_per_sample);
    if (offset + bytes_per_sample > buffer.samples_.size()) {
        return 0.0F;
    }
    if (bytes_per_sample == 2) {
        std::int16_t value = 0;
        std::memcpy(&value, buffer.samples_.data() + offset, sizeof(value));
        return static_cast<float>(value) / 32768.0F;
    }
    if (bytes_per_sample == 1) {
        return (static_cast<float>(buffer.samples_[offset]) - 128.0F) / 128.0F;
    }
    if (bytes_per_sample == 4) {
        // (32-bit: DirectSound's float format is what the movies and streams use; treat it as float)
        if (buffer.format_.format_tag == 3) { // IEEE float
            float value = 0.0F;
            std::memcpy(&value, buffer.samples_.data() + offset, sizeof(value));
            return value;
        }
        std::int32_t value = 0;
        std::memcpy(&value, buffer.samples_.data() + offset, sizeof(value));
        return static_cast<float>(value) / 2147483648.0F;
    }
    return 0.0F;
}

std::uint32_t frames_of(const SdlBuffer& buffer)
{
    const std::size_t frame_bytes = frame_bytes_of(buffer);
    return frame_bytes == 0 ? 0 : static_cast<std::uint32_t>(buffer.samples_.size() / frame_bytes);
}

// The peaks of the mix's two halves since the last report (stream L/R, effects L/R): the debug log's numbers for
// comparing the music with the effects. Only the audio thread touches it.
float* mix_peaks()
{
    static thread_local float peaks[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    return peaks;
}

// SDL's callback: the mix.
void mix_audio(void* userdata, SDL_AudioStream* /*stream*/, int additional, int /*total*/)
{
    auto* output = static_cast<SdlOutput*>(userdata);
    if (output == nullptr || additional <= 0) {
        return;
    }
    // (a master gain: the game's voices are each at full scale, and their sum would clip - which is heard as a hiss
    // over everything. The limiter keeps a full mix soft at the top instead of hard-clipping it.)
    constexpr float kMasterGain = 1.0F;
    const int frames = additional / static_cast<int>(sizeof(std::int16_t) * kOutputChannels);
    std::vector<std::int16_t> mix(static_cast<std::size_t>(frames) * kOutputChannels, 0);

    std::lock_guard<std::mutex> guard(mix_lock());
    for (SdlBuffer* buffer : output->buffers_) {
        if (buffer == nullptr || !buffer->playing_) {
            continue;
        }
        const std::uint32_t total_frames = frames_of(*buffer);
        if (total_frames == 0) {
            continue;
        }
        const std::uint32_t rate =
            buffer->frequency_ != 0
                ? buffer->frequency_
                : (buffer->format_.samples_per_second_ != 0 ? buffer->format_.samples_per_second_ : kOutputRate);
        const double step = static_cast<double>(rate) / static_cast<double>(kOutputRate);
        const float gain =
            gain_of(buffer->attenuation_, buffer->streaming_);
        // (the pan the game passes is DirectSound's own: -10000 hard left, 0 centre, +10000 hard right - that is the
        // range its calls are in. A hard pan takes the far side down by 12 dB, not to nothing: a 2004 mix panned
        // whole voices without removing them, and nothing here should be inaudible in one ear because of a pan.)
        float left = 1.0F;
        float right = 1.0F;
        if (!buffer->streaming_ && buffer->pan_ != 0) {
            constexpr float kPanFull = 10000.0F;
            constexpr float kPanDepthDb = 12.0F;
            const float tilt = std::clamp(static_cast<float>(buffer->pan_) / kPanFull, -1.0F, 1.0F);
            const float attenuated = std::pow(10.0F, -(kPanDepthDb / 20.0F) * std::fabs(tilt));
            if (tilt > 0.0F) {
                left = attenuated;
            } else {
                right = attenuated;
            }
        }
        if (buffer->spatial_) {
            // A voice the game placed in the world: the range it gave (full within min_distance, silent past
            // max_distance) and the side from where it lies in the listener's frame. The place is relative to the
            // listener and the listener's facing is the camera's (orient_listener), so the side is the place's part
            // along the listener's right vector - the world up (+Y) crossed with that facing.
            const float px = buffer->place_[0];
            const float py = buffer->place_[1];
            const float pz = buffer->place_[2];
            const float distance = std::sqrt((px * px) + (py * py) + (pz * pz));
            float fade = 1.0F;
            if (buffer->range_.max_distance > buffer->range_.min_distance) {
                fade = std::clamp((buffer->range_.max_distance - distance) /
                                      (buffer->range_.max_distance - buffer->range_.min_distance),
                                  0.0F, 1.0F);
            }
            const float fx = output->listener_front_[0];
            const float fy = output->listener_front_[1];
            const float fz = output->listener_front_[2];
            const float flen = std::sqrt((fx * fx) + (fy * fy) + (fz * fz));
            float side = 0.0F;
            if (flen > 0.0F && distance > 0.0F) {
                const float rx = fz / flen; // right = up x front, up = (0, 1, 0)
                const float rz = -fx / flen;
                side = std::clamp(((px * rx) + (pz * rz)) / distance, -1.0F, 1.0F); // -1 left .. +1 right
            }
            // (the far side is taken down by up to 12 dB, as a pan is: equal power, so passing the middle keeps level)
            const float depth = std::pow(10.0F, -(12.0F / 20.0F) * std::fabs(side));
            left *= fade * (side > 0.0F ? depth : 1.0F) * std::cos((side + 1.0F) * 0.785398163F) * 1.41421356F * 0.5F;
            right *= fade * (side < 0.0F ? depth : 1.0F) * std::sin((side + 1.0F) * 0.785398163F) * 1.41421356F * 0.5F;
        }
        for (int frame = 0; frame < frames; ++frame) {
            if (buffer->position_ >= static_cast<double>(total_frames)) {
                if (buffer->looping_ || buffer->streaming_) {
                    buffer->position_ = std::fmod(buffer->position_, static_cast<double>(total_frames));
                } else {
                    buffer->playing_ = false;
                    buffer->position_ = static_cast<double>(total_frames);
                    break;
                }
            }
            const auto source = static_cast<std::uint32_t>(buffer->position_);
            const float sample_left = sample_at(*buffer, source, 0);
            const float sample_right = buffer->format_.channels > 1 ? sample_at(*buffer, source, 1) : sample_left;
            const float added_left = sample_left * gain * left * kMasterGain;
            const float added_right = sample_right * gain * right * kMasterGain;
            mix[static_cast<std::size_t>(frame) * kOutputChannels] = to_output(
                static_cast<float>(mix[static_cast<std::size_t>(frame) * kOutputChannels]) + (added_left * 32767.0F));
            mix[(static_cast<std::size_t>(frame) * kOutputChannels) + 1] =
                to_output(static_cast<float>(mix[(static_cast<std::size_t>(frame) * kOutputChannels) + 1]) +
                          (added_right * 32767.0F));
            if (audio_debug()) {
                // (what each side of the mix is contributing, for comparing the music with the effects)
                float* peaks = mix_peaks();
                float& sl = peaks[buffer->streaming_ ? 0 : 2];
                float& sr = peaks[buffer->streaming_ ? 1 : 3];
                sl = std::max(sl, std::fabs(added_left));
                sr = std::max(sr, std::fabs(added_right));
            }
            buffer->position_ += step;
        }
    }
    if (audio_debug()) {
        static int counter = 0;
        if ((counter += frames) >= kOutputRate) {
            counter = 0;
            {
                float* peaks = mix_peaks();
                SDL_Log("peak: stream L=%.3f R=%.3f  effects L=%.3f R=%.3f", peaks[0], peaks[1], peaks[2], peaks[3]);
                for (int i = 0; i < 4; ++i) {
                    peaks[i] = 0.0F;
                }
            }
            for (SdlBuffer* buffer : output->buffers_) {
                if (buffer != nullptr && buffer->playing_) {
                    SDL_Log("mix: tag=%u ch=%d rate=%u bits=%u bytes=%zu freq=%u pos=%.1f frames=%u loop=%d stream=%d "
                            "att=%d pan=%d",
                            buffer->format_.format_tag, buffer->format_.channels, buffer->format_.samples_per_second_,
                            buffer->format_.bits_per_sample_, buffer->samples_.size(), buffer->frequency_,
                            buffer->position_, frames_of(*buffer), buffer->looping_ ? 1 : 0, buffer->streaming_ ? 1 : 0,
                            buffer->attenuation_, buffer->pan_);
                }
            }
        }
    }
    SDL_PutAudioStreamData(output->stream_, mix.data(),
                           frames * static_cast<int>(sizeof(std::int16_t) * kOutputChannels));
}

class SdlDevice final : public Device {
public:
    SoundOutput* open(void* /*window*/) override
    {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == false) {
            SDL_Log("audio: SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
            return nullptr;
        }
        auto* output = new SdlOutput();
        const SDL_AudioSpec spec{.format = SDL_AUDIO_S16, .channels = kOutputChannels, .freq = kOutputRate};
        output->stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mix_audio, output);
        if (output->stream_ == nullptr) {
            SDL_Log("audio: no output: %s", SDL_GetError());
            delete output;
            return nullptr;
        }
        output->device_ = SDL_GetAudioStreamDevice(output->stream_);
        SDL_ResumeAudioStreamDevice(output->stream_);
        return reinterpret_cast<SoundOutput*>(output);
    }

    void close(SoundOutput* output) override
    {
        if (output == nullptr) {
            return;
        }
        SdlOutput& state = output_of(output);
        if (state.stream_ != nullptr || state.device_ != 0) {
            if (state.stream_ != nullptr) {
                SDL_DestroyAudioStream(state.stream_);
            } else {
                SDL_CloseAudioDevice(state.device_);
            }
        }
        for (SdlBuffer* buffer : state.buffers_) {
            delete buffer;
        }
        delete &state;
    }

    void list_outputs(OutputListed listed) override
    {
        if (listed == nullptr) {
            return;
        }
        const SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(nullptr);
        int count = 0;
        if (devices != nullptr) {
            for (const SDL_AudioDeviceID* at = SDL_GetAudioPlaybackDevices(&count); at != nullptr && *at != 0; ++at) {
                listed(SDL_GetAudioDeviceName(*at));
            }
        }
        if (count == 0) {
            listed("the system's default output");
        }
    }

    bool get_caps(SoundOutput* /*output*/, OutputCaps& caps) override
    {
        // (nothing is hardware-mixed here: the voices are ours to mix, so plenty of room for them)
        caps.free_3d_buffers = 32;
        caps.free_mixing_buffers = 64;
        return true;
    }

    SoundListener* create_listener(SoundOutput* output) override
    {
        auto* listener = new SdlListener();
        listener->output_ = &output_of(output);
        return reinterpret_cast<SoundListener*>(listener);
    }

    void orient_listener(SoundListener* listener, Point3 front) override
    {
        if (listener == nullptr) {
            return;
        }
        auto* state = reinterpret_cast<SdlListener*>(listener);
        state->output_->listener_front_[0] = front.x;
        state->output_->listener_front_[1] = front.y;
        state->output_->listener_front_[2] = front.z;
    }

    void commit_listener(SoundListener* /*listener*/) override {}

    MadeBuffer create_buffer(SoundOutput* output, const BufferDesc& desc) override
    {
        auto* buffer = new SdlBuffer();
        buffer->owner_ = &output_of(output);
        buffer->format_ = desc.format;
        buffer->samples_.assign(desc.bytes, 0);
        SdlOutput& state = output_of(output);
        {
            std::lock_guard<std::mutex> guard(mix_lock());
            state.buffers_.push_back(buffer);
        }
        if (audio_debug()) {
            const unsigned expected = static_cast<unsigned>(desc.format.channels) * desc.format.bits_per_sample_ / 8U;
            SDL_Log("buffer made: tag=%u ch=%d rate=%u bits=%u bytes=%u block=%u flags=%u%s", desc.format.format_tag,
                    desc.format.channels, desc.format.samples_per_second_, desc.format.bits_per_sample_, desc.bytes,
                    desc.format.block_align_, desc.flags,
                    (expected != 0 && expected != desc.format.block_align_) ? "  <-- BLOCK/CHANNELS DISAGREE" : "");
        }
        return MadeBuffer{.buffer = reinterpret_cast<SoundBuffer*>(buffer), .made = true};
    }

    SoundBuffer3d* buffer_3d(SoundBuffer* buffer) override
    {
        // (the same object: a voice's place in space is part of its buffer here)
        return reinterpret_cast<SoundBuffer3d*>(buffer);
    }

    void fill_buffer(SoundBuffer* buffer, std::span<const uint8_t> samples) override
    {
        if (buffer == nullptr) {
            return;
        }
        SdlBuffer& state = buffer_of(buffer);
        std::lock_guard<std::mutex> guard(mix_lock());
        if (audio_debug() && state.playing_) {
            // (a voice's samples replaced while it is still sounding: the wave it was playing is gone mid-note)
            SDL_Log("fill under playing: %.0f of %u frames, %zu new bytes of %zu", state.position_, frames_of(state),
                    samples.size(), state.samples_.size());
        }
        const std::size_t count = std::min(samples.size(), state.samples_.size());
        std::memcpy(state.samples_.data(), samples.data(), count);
    }

    void release_buffer(SoundBuffer* buffer) override
    {
        if (buffer == nullptr) {
            return;
        }
        auto* state = reinterpret_cast<SdlBuffer*>(buffer);
        if (audio_debug() && state->playing_) {
            // (a voice freed while it was still sounding: the engine recycled its slot, and the sound stops dead)
            SDL_Log("free under playing: %.0f of %u frames", state->position_, frames_of(*state));
        }
        {
            // (taken out of the output's list first: the mixer walks that list, and a freed buffer left in it is a
            // dangling pointer the audio thread reads - the once-a-minute crash and the notes that swallowed)
            std::lock_guard<std::mutex> guard(mix_lock());
            if (state->owner_ != nullptr) {
                auto& buffers = state->owner_->buffers_;
                const auto found = std::find(buffers.begin(), buffers.end(), state);
                if (found != buffers.end()) {
                    buffers.erase(found);
                }
            }
        }
        delete state;
    }

    void release_buffer_3d(SoundBuffer3d* /*buffer*/) override
    {
        // (buffer_3d hands back the buffer itself: it goes with release_buffer, as DirectSound's 3D interface did)
    }

    bool play_buffer(SoundBuffer* buffer, bool looping) override
    {
        if (buffer == nullptr) {
            return false;
        }
        SdlBuffer& state = buffer_of(buffer);
        std::lock_guard<std::mutex> guard(mix_lock());
        if (audio_debug() && state.playing_ && state.position_ > 64.0) {
            // (a sound restarted while it was still sounding: its tail is lost - the "swallowed" voice)
            SDL_Log("cut: restart while playing at %.0f of %u frames (%.0f ms in)", state.position_, frames_of(state),
                    state.position_ * 1000.0 /
                        (state.format_.samples_per_second_ != 0 ? state.format_.samples_per_second_ : 1));
        }
        state.playing_ = true;
        state.looping_ = looping;
        state.position_ = 0.0;
        return true;
    }

    void stop_buffer(SoundBuffer* buffer) override
    {
        if (buffer == nullptr) {
            return;
        }
        SdlBuffer& state = buffer_of(buffer);
        std::lock_guard<std::mutex> guard(mix_lock());
        if (audio_debug() && state.playing_ && !state.looping_ && state.position_ + 512.0 < frames_of(state)) {
            SDL_Log("cut: stop at %.0f of %u frames", state.position_, frames_of(state));
        }
        state.playing_ = false;
        state.position_ = 0.0;
    }

    bool buffer_finished(SoundBuffer* buffer) override
    {
        if (buffer == nullptr) {
            return true;
        }
        SdlBuffer& state = buffer_of(buffer);
        std::lock_guard<std::mutex> guard(mix_lock());
        if (state.streaming_) {
            return !state.playing_; // (a stream is the game's to stop: it never runs out here)
        }
        return !state.playing_;
    }

    void set_buffer_volume(SoundBuffer* buffer, int attenuation) override
    {
        if (buffer != nullptr) {
            std::lock_guard<std::mutex> guard(mix_lock());
            buffer_of(buffer).attenuation_ = attenuation;
        }
    }

    void set_buffer_pan(SoundBuffer* buffer, int pan) override
    {
        if (buffer != nullptr) {
            std::lock_guard<std::mutex> guard(mix_lock());
            buffer_of(buffer).pan_ = pan;
        }
    }

    void set_buffer_frequency(SoundBuffer* buffer, uint32_t frequency) override
    {
        if (buffer != nullptr) {
            std::lock_guard<std::mutex> guard(mix_lock());
            buffer_of(buffer).frequency_ = frequency;
        }
    }

    void place_buffer_3d(SoundBuffer3d* buffer, BufferRange range, Point3 position) override
    {
        if (buffer == nullptr) {
            return;
        }
        SdlBuffer& state = buffer_of(buffer);
        std::lock_guard<std::mutex> guard(mix_lock());
        state.spatial_ = true;
        state.range_ = range;
        state.place_[0] = position.x;
        state.place_[1] = position.y;
        state.place_[2] = position.z;
    }

    bool buffer_position(SoundBuffer* buffer, uint32_t& position) override
    {
        if (buffer == nullptr) {
            return false;
        }
        SdlBuffer& state = buffer_of(buffer);
        std::lock_guard<std::mutex> guard(mix_lock());
        const std::uint32_t total_frames = frames_of(state);
        if (total_frames == 0) {
            position = 0;
            return true;
        }
        const std::uint32_t channels = state.format_.channels > 0 ? state.format_.channels : 1U;
        const std::uint32_t bytes_per_sample =
            state.format_.bits_per_sample_ > 0 ? state.format_.bits_per_sample_ / 8U : 2U;
        const std::uint32_t frame_bytes = bytes_per_sample * channels;
        const std::uint32_t frame = static_cast<std::uint32_t>(state.position_);
        position = (frame % total_frames) * frame_bytes;
        return true;
    }

    void write_buffer(SoundBuffer* buffer, uint32_t at, std::span<const uint8_t> samples) override
    {
        if (buffer == nullptr) {
            return;
        }
        SdlBuffer& state = buffer_of(buffer);
        std::lock_guard<std::mutex> guard(mix_lock());
        state.streaming_ = true;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const std::size_t index = (static_cast<std::size_t>(at) + i) % state.samples_.size();
            state.samples_[index] = samples[i];
        }
    }

    bool resume_buffer(SoundBuffer* buffer) override
    {
        if (buffer == nullptr) {
            return false;
        }
        {
            std::lock_guard<std::mutex> guard(mix_lock());
            buffer_of(buffer).playing_ = true;
        }
        return true;
    }

    int track_volume(int volume) override { return volume; }

private:
};

} // namespace

Device& sdl_device()
{
    static SdlDevice the_device;
    return the_device;
}

} // namespace audio
