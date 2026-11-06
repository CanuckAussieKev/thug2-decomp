// NOT ORIGINAL - the stand-in sound outputs of thug2_movie_codec_check (movie_codec_check_sound.h).

#include "tests/movie_codec_check_sound.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mmsystem.h>
#include <dsound.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <ratio>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/audio/audio_device.h"
#include "engine/audio/audio_handles.h"

namespace tests::sound {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kListenEvery = std::chrono::milliseconds(2);
constexpr uint32_t kWriteLeadMilliseconds = 15; // the write position's lead over the play position (a sound card's)
constexpr uint32_t kMillisecondsASecond = 1000;
constexpr int kSampleBias = 128;    // 8-bit samples are unsigned
constexpr unsigned kByteShift = 8;  // 8-bit to 16-bit
constexpr size_t kSampleBytes = 2;
constexpr uint16_t kEightBits = 8;

// One buffer played by the clock.
class SimulatedBuffer {
public:
    SimulatedBuffer(const DSBUFFERDESC& desc, Clock::time_point epoch) : epoch_(epoch)
    {
        const WAVEFORMATEX* format = desc.lpwfxFormat;
        const uint32_t bytes = desc.dwBufferBytes;
        record_.flags = desc.dwFlags;
        record_.primary = (desc.dwFlags & DSBCAPS_PRIMARYBUFFER) != 0U;
        if (format != nullptr) {
            record_.channels = format->nChannels;
            record_.rate = format->nSamplesPerSec;
            record_.bits = format->wBitsPerSample;
            block_ = format->nBlockAlign;
        }
        record_.bytes = record_.primary ? 0 : bytes;
        bytes_.assign(record_.bytes, 0);
        frequency_ = record_.rate;
    }

    // The play position now (bytes, block-aligned).
    uint32_t position()
    {
        const std::scoped_lock lock(mutex_);
        return position_at(Clock::now());
    }
    [[nodiscard]] uint32_t write_lead() const { return block_ * (frequency_ * kWriteLeadMilliseconds / kMillisecondsASecond); }
    [[nodiscard]] uint32_t size() const { return record_.bytes; }
    std::span<uint8_t> memory() { return bytes_; }

    void write(uint32_t at, std::span<const uint8_t> samples)
    {
        const std::scoped_lock lock(mutex_);
        listen_at(Clock::now()); // (what was played before the write heard as it was)
        if (!bytes_.empty()) {
            // (round the end to the start)
            const std::span<uint8_t> memory(bytes_);
            size_t done = 0;
            size_t to = at % memory.size();
            while (done < samples.size()) {
                const size_t count = std::min(samples.size() - done, memory.size() - to);
                std::ranges::copy(samples.subspan(done, count), memory.subspan(to, count).begin());
                done += count;
                to = 0;
            }
        }
        record_.written_bytes += samples.size();
    }
    void wrote(uint32_t count)
    {
        const std::scoped_lock lock(mutex_);
        record_.written_bytes += count;
    }
    // A write's end, how far past the play position (the writer's lead).
    void note_lead(uint32_t end)
    {
        const std::scoped_lock lock(mutex_);
        if (record_.bytes == 0 || block_ == 0 || frequency_ == 0) {
            return;
        }
        const uint32_t ahead = (end + record_.bytes - position_at(Clock::now())) % record_.bytes;
        const double ms = static_cast<double>(ahead) / block_ * kMillisecondsASecond / frequency_;
        record_.lead_max_ms = std::max(record_.lead_max_ms, ms);
        record_.lead_sum_ms += ms;
        ++record_.leads;
    }

    void play(bool looping)
    {
        const std::scoped_lock lock(mutex_);
        listen_at(Clock::now());
        if (!playing_) {
            playing_ = true;
            start_ = Clock::now();
            start_position_ = stopped_position_;
            heard_position_ = stopped_position_;
            if (record_.first_play_ms < 0.0) {
                record_.first_play_ms = std::chrono::duration<double, std::milli>(start_ - epoch_).count();
            }
        }
        looping_ = looping;
        ++record_.plays;
        note_time("play");
    }
    void stop()
    {
        const std::scoped_lock lock(mutex_);
        listen_at(Clock::now());
        if (playing_) {
            stopped_position_ = position_at(Clock::now());
            playing_ = false;
        }
        ++record_.stops;
        note_time("stop");
    }
    void set_position(uint32_t position)
    {
        const std::scoped_lock lock(mutex_);
        listen_at(Clock::now());
        stopped_position_ = record_.bytes != 0 ? position % record_.bytes : 0;
        if (playing_) {
            start_ = Clock::now();
            start_position_ = stopped_position_;
            heard_position_ = stopped_position_;
        }
    }
    bool playing()
    {
        const std::scoped_lock lock(mutex_);
        return playing_;
    }
    void set_volume(int volume)
    {
        const std::scoped_lock lock(mutex_);
        record_.volumes.push_back(volume);
    }
    void set_pan(int pan)
    {
        const std::scoped_lock lock(mutex_);
        record_.pans.push_back(pan);
    }
    void set_frequency(uint32_t frequency)
    {
        const std::scoped_lock lock(mutex_);
        listen_at(Clock::now());
        if (playing_) { // (the position so far kept, at the new rate from now)
            start_position_ = position_at(Clock::now());
            start_ = Clock::now();
        }
        frequency_ = frequency != 0 ? frequency : record_.rate;
        record_.frequencies.push_back(frequency);
    }
    Settings settings()
    {
        const std::scoped_lock lock(mutex_);
        return {.volume = record_.volumes.empty() ? 0 : record_.volumes.back(),
                .pan = record_.pans.empty() ? 0 : record_.pans.back()};
    }
    [[nodiscard]] bool primary() const { return record_.primary; }
    // (under the lock)
    void note_time(const char* what)
    {
        const auto at = std::chrono::duration<double, std::milli>(Clock::now() - epoch_).count();
        record_.timeline += std::string(record_.timeline.empty() ? "" : ", ") + what + " " +
                            std::to_string(static_cast<int>(at)) + " ms (heard " +
                            std::to_string(record_.heard.size() / std::max<size_t>(record_.channels, 1) *
                                           kMillisecondsASecond / std::max<uint32_t>(record_.rate, 1)) +
                            " ms)";
    }
    void note(const char* call)
    {
        const std::scoped_lock lock(mutex_);
        if (record_.other_calls.find(call) == std::string::npos) {
            record_.other_calls += record_.other_calls.empty() ? "" : ", ";
            record_.other_calls += call;
        }
    }
    void listen()
    {
        const std::scoped_lock lock(mutex_);
        listen_at(Clock::now());
    }
    BufferRecord record()
    {
        const std::scoped_lock lock(mutex_);
        listen_at(Clock::now());
        return record_;
    }

private:
    [[nodiscard]] uint32_t position_at(Clock::time_point now) const
    {
        if (!playing_ || record_.bytes == 0 || block_ == 0) {
            return stopped_position_;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - start_).count();
        const auto blocks = static_cast<uint64_t>(elapsed) * frequency_ / (static_cast<uint64_t>(kMillisecondsASecond) * kMillisecondsASecond);
        const uint64_t moved = blocks * block_;
        if (!looping_ && start_position_ + moved >= record_.bytes) {
            return 0; // (a buffer played once stops at its end)
        }
        return static_cast<uint32_t>((start_position_ + moved) % record_.bytes);
    }

    // The bytes from the last heard position to the play position kept as samples.
    void listen_at(Clock::time_point now)
    {
        if (!playing_ || bytes_.empty()) {
            return;
        }
        const uint32_t to = position_at(now);
        uint32_t at = heard_position_;
        while (at != to) {
            if (record_.bits == kEightBits) {
                record_.heard.push_back(
                    static_cast<int16_t>((static_cast<int>(core::at(bytes_, at)) - kSampleBias) << kByteShift));
                at = (at + 1) % record_.bytes;
            } else {
                int16_t sample = 0;
                std::memcpy(&sample, &core::at(bytes_, at), kSampleBytes);
                record_.heard.push_back(sample);
                at = (at + static_cast<uint32_t>(kSampleBytes)) % record_.bytes;
            }
        }
        heard_position_ = to;
    }

    std::mutex mutex_;
    Clock::time_point epoch_;
    BufferRecord record_;
    std::vector<uint8_t> bytes_;
    uint32_t block_ = 1;
    uint32_t frequency_ = 0;
    bool playing_ = false;
    bool looping_ = false;
    Clock::time_point start_;
    uint32_t start_position_ = 0;
    uint32_t stopped_position_ = 0;
    uint32_t heard_position_ = 0;
};

// The same address as a pointer to To (a handle and the object behind it).
template <typename To, typename From>
To* same_address(From* pointer)
{
    void* const address = static_cast<void*>(pointer);
    return static_cast<To*>(address);
}

// The buffers made, in order, and the listener's thread.
class Registry {
public:
    std::shared_ptr<SimulatedBuffer> make(const DSBUFFERDESC& desc)
    {
        const std::scoped_lock lock(mutex_);
        auto made = std::make_shared<SimulatedBuffer>(desc, epoch_);
        buffers_.push_back(made);
        return made;
    }
    void begin()
    {
        const std::scoped_lock lock(mutex_);
        buffers_.clear();
        epoch_ = Clock::now();
    }
    std::vector<BufferRecord> records()
    {
        const std::scoped_lock lock(mutex_);
        std::vector<BufferRecord> made;
        made.reserve(buffers_.size());
        for (const auto& buffer : buffers_) {
            made.push_back(buffer->record());
        }
        return made;
    }
    Settings last_settings()
    {
        const std::scoped_lock lock(mutex_);
        for (auto buffer = buffers_.rbegin(); buffer != buffers_.rend(); ++buffer) {
            if (!(*buffer)->primary()) {
                return (*buffer)->settings();
            }
        }
        return {};
    }
    void listen_all()
    {
        const std::scoped_lock lock(mutex_);
        for (const auto& buffer : buffers_) {
            buffer->listen();
        }
    }
    void start()
    {
        if (!listener_.joinable()) {
            listener_ = std::jthread([this](const std::stop_token& stop) {
                while (!stop.stop_requested()) {
                    listen_all();
                    std::this_thread::sleep_for(kListenEvery);
                }
            });
        }
    }

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<SimulatedBuffer>> buffers_;
    Clock::time_point epoch_ = Clock::now();
    std::jthread listener_;
};

Registry& registry()
{
    static Registry made;
    return made;
}

// --- binkw32's: a DirectSound object and its buffers --------------------------------------------------------

class FakeBuffer final : public IDirectSoundBuffer {
public:
    explicit FakeBuffer(std::shared_ptr<SimulatedBuffer> buffer) : buffer_(std::move(buffer)) {}
    FakeBuffer(const FakeBuffer&) = delete;
    FakeBuffer& operator=(const FakeBuffer&) = delete;
    FakeBuffer(FakeBuffer&&) = delete;
    FakeBuffer& operator=(FakeBuffer&&) = delete;
    ~FakeBuffer() = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(const IID& /*iid*/, void** object) override
    {
        buffer_->note("QueryInterface");
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = --references_;
        if (left == 0) {
            buffer_->stop();
            const std::unique_ptr<FakeBuffer> released(this);
        }
        return left;
    }
    HRESULT STDMETHODCALLTYPE GetCaps(DSBCAPS* caps) override
    {
        buffer_->note("GetCaps");
        caps->dwFlags = buffer_->record().flags;
        caps->dwBufferBytes = buffer_->size();
        return DS_OK;
    }
    // (DirectSound's play and write positions: where the region that mustn't be written begins and ends)
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(DWORD* begin, DWORD* end) override
    {
        const uint32_t position = buffer_->position();
        if (begin != nullptr) {
            *begin = position;
        }
        if (end != nullptr) {
            *end = buffer_->size() != 0 ? (position + buffer_->write_lead()) % buffer_->size() : 0;
        }
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFormat(WAVEFORMATEX* /*format*/, DWORD /*bytes*/, DWORD* written) override
    {
        buffer_->note("GetFormat");
        if (written) {
            *written = 0;
        }
        return DSERR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE GetVolume(LONG* volume) override
    {
        buffer_->note("GetVolume");
        *volume = 0;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPan(LONG* pan) override
    {
        buffer_->note("GetPan");
        *pan = 0;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFrequency(DWORD* frequency) override
    {
        buffer_->note("GetFrequency");
        *frequency = buffer_->record().rate;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStatus(DWORD* status) override
    {
        *status = buffer_->playing() ? DSBSTATUS_PLAYING | DSBSTATUS_LOOPING : 0U;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IDirectSound* /*sound*/, const DSBUFFERDESC* /*desc*/) override
    {
        buffer_->note("Initialize");
        return DSERR_ALREADYINITIALIZED;
    }
    HRESULT STDMETHODCALLTYPE Lock(DWORD offset, DWORD bytes, void** first, DWORD* first_bytes, void** second,
                                   DWORD* second_bytes, DWORD flags) override
    {
        const uint32_t size = buffer_->size();
        if (size == 0) {
            return DSERR_INVALIDCALL;
        }
        buffer_->listen(); // (what was played before the write heard as it was)
        if ((flags & DSBLOCK_FROMWRITECURSOR) != 0U) {
            offset = (buffer_->position() + buffer_->write_lead()) % size;
        }
        if ((flags & DSBLOCK_ENTIREBUFFER) != 0U) {
            bytes = size;
        }
        bytes = std::min<DWORD>(bytes, size);
        offset %= size;
        buffer_->note_lead(static_cast<uint32_t>((offset + bytes) % size));
        const std::span<uint8_t> memory = buffer_->memory();
        const DWORD to_end = std::min<DWORD>(bytes, size - offset);
        *first = memory.subspan(offset).data();
        *first_bytes = to_end;
        if (second) {
            *second = bytes > to_end ? memory.data() : nullptr;
        }
        if (second_bytes) {
            *second_bytes = bytes - to_end;
        }
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Play(DWORD /*reserved*/, DWORD /*priority*/, DWORD flags) override
    {
        buffer_->play((flags & DSBPLAY_LOOPING) != 0U);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD position) override
    {
        buffer_->set_position(position);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFormat(const WAVEFORMATEX* format) override
    {
        buffer_->note(format && format->nChannels == 2 ? "SetFormat(stereo)" : "SetFormat");
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVolume(LONG volume) override
    {
        buffer_->set_volume(volume);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPan(LONG pan) override
    {
        buffer_->set_pan(pan);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD frequency) override
    {
        buffer_->set_frequency(frequency);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Stop() override
    {
        buffer_->stop();
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock(void* /*first*/, DWORD first_bytes, void* /*second*/, DWORD second_bytes) override
    {
        buffer_->wrote(first_bytes + second_bytes);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Restore() override
    {
        buffer_->note("Restore");
        return DS_OK;
    }

private:
    std::shared_ptr<SimulatedBuffer> buffer_;
    ULONG references_ = 1;
};

class FakeDirectSound final : public IDirectSound8 {
public:
    FakeDirectSound() = default;
    FakeDirectSound(const FakeDirectSound&) = delete;
    FakeDirectSound& operator=(const FakeDirectSound&) = delete;
    FakeDirectSound(FakeDirectSound&&) = delete;
    FakeDirectSound& operator=(FakeDirectSound&&) = delete;
    ~FakeDirectSound() = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(const IID& /*iid*/, void** object) override
    {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; } // (never released: it lives as long as the check)
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateSoundBuffer(const DSBUFFERDESC* desc, IDirectSoundBuffer** made,
                                                IUnknown* /*outer*/) override
    {
        auto buffer = registry().make(*desc);
        *made = std::make_unique<FakeBuffer>(std::move(buffer)).release();
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCaps(DSCAPS* caps) override
    {
        const DWORD size = caps->dwSize;
        *caps = {};
        caps->dwSize = size;
        caps->dwFlags = DSCAPS_PRIMARY16BIT | DSCAPS_PRIMARYSTEREO | DSCAPS_SECONDARY16BIT | DSCAPS_SECONDARYSTEREO;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(IDirectSoundBuffer* /*original*/,
                                                   IDirectSoundBuffer** duplicate) override
    {
        *duplicate = nullptr;
        return DSERR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND /*window*/, DWORD /*level*/) override { return DS_OK; }
    HRESULT STDMETHODCALLTYPE Compact() override { return DS_OK; }
    HRESULT STDMETHODCALLTYPE GetSpeakerConfig(DWORD* config) override
    {
        *config = DSSPEAKER_STEREO;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD /*config*/) override { return DS_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(const GUID* /*device*/) override { return DSERR_ALREADYINITIALIZED; }
    HRESULT STDMETHODCALLTYPE VerifyCertification(DWORD* certified) override
    {
        *certified = DS_CERTIFIED;
        return DS_OK;
    }
};

// --- the FFmpeg codec's: an audio::Device -------------------------------------------------------------------

// A recording device's buffer handle: the simulated buffer it stands for.
struct DeviceBuffer {
    std::shared_ptr<SimulatedBuffer> buffer;
};

SimulatedBuffer& simulated(audio::SoundBuffer* buffer)
{
    return *same_address<DeviceBuffer>(buffer)->buffer;
}

class RecordingDevice final : public audio::Device {
public:
    audio::SoundOutput* open(void* /*window*/) override { return recording_output(); }
    void close(audio::SoundOutput* /*output*/) override {}
    void list_outputs(audio::OutputListed /*listed*/) override {}
    bool get_caps(audio::SoundOutput* /*output*/, audio::OutputCaps& caps) override
    {
        caps = {};
        return true;
    }
    audio::SoundListener* create_listener(audio::SoundOutput* /*output*/) override { return nullptr; }
    void orient_listener(audio::SoundListener* /*listener*/, audio::Point3 /*front*/) override {}
    void commit_listener(audio::SoundListener* /*listener*/) override {}
    audio::MadeBuffer create_buffer(audio::SoundOutput* /*output*/, const audio::BufferDesc& desc) override
    {
        WAVEFORMATEX format{};
        format.wFormatTag = desc.format.format_tag;
        format.nChannels = desc.format.channels;
        format.nSamplesPerSec = desc.format.samples_per_second_;
        format.nAvgBytesPerSec = desc.format.bytes_per_second_;
        format.nBlockAlign = desc.format.block_align_;
        format.wBitsPerSample = desc.format.bits_per_sample_;
        DSBUFFERDESC made_desc{};
        made_desc.dwSize = sizeof(made_desc);
        made_desc.dwFlags = desc.flags;
        made_desc.dwBufferBytes = desc.bytes;
        made_desc.lpwfxFormat = &format;
        auto made = std::make_unique<DeviceBuffer>();
        made->buffer = registry().make(made_desc);
        return {.buffer = same_address<audio::SoundBuffer>(made.release()), .made = true};
    }
    audio::SoundBuffer3d* buffer_3d(audio::SoundBuffer* /*buffer*/) override { return nullptr; }
    void fill_buffer(audio::SoundBuffer* buffer, std::span<const uint8_t> samples) override
    {
        simulated(buffer).write(0, samples);
    }
    void release_buffer(audio::SoundBuffer* buffer) override
    {
        simulated(buffer).stop();
        const std::unique_ptr<DeviceBuffer> released(same_address<DeviceBuffer>(buffer));
    }
    void release_buffer_3d(audio::SoundBuffer3d* /*buffer*/) override {}
    bool play_buffer(audio::SoundBuffer* buffer, bool looping) override
    {
        simulated(buffer).set_position(0);
        simulated(buffer).play(looping);
        return true;
    }
    void stop_buffer(audio::SoundBuffer* buffer) override { simulated(buffer).stop(); }
    bool buffer_finished(audio::SoundBuffer* buffer) override { return !simulated(buffer).playing(); }
    void set_buffer_volume(audio::SoundBuffer* buffer, int attenuation) override
    {
        simulated(buffer).set_volume(attenuation);
    }
    void set_buffer_pan(audio::SoundBuffer* buffer, int pan) override { simulated(buffer).set_pan(pan); }
    void set_buffer_frequency(audio::SoundBuffer* buffer, uint32_t frequency) override
    {
        simulated(buffer).set_frequency(frequency);
    }
    void place_buffer_3d(audio::SoundBuffer3d* /*buffer*/, audio::BufferRange /*range*/,
                         audio::Point3 /*position*/) override
    {}
    bool buffer_position(audio::SoundBuffer* buffer, uint32_t& position) override
    {
        position = simulated(buffer).position();
        return true;
    }
    void write_buffer(audio::SoundBuffer* buffer, uint32_t at, std::span<const uint8_t> samples) override
    {
        simulated(buffer).write(at, samples);
        if (simulated(buffer).size() != 0) {
            simulated(buffer).note_lead(static_cast<uint32_t>((at + samples.size()) % simulated(buffer).size()));
        }
    }
    bool resume_buffer(audio::SoundBuffer* buffer) override
    {
        simulated(buffer).play(true);
        return true;
    }
    int track_volume(int volume) override { return volume; }
};

} // namespace

audio::SoundOutput* bink_output()
{
    static FakeDirectSound made;
    return same_address<audio::SoundOutput>(static_cast<IDirectSound8*>(&made));
}

audio::Device& recording_device()
{
    static RecordingDevice made;
    return made;
}

audio::SoundOutput* recording_output()
{
    static int stands_for_an_output = 0;
    return same_address<audio::SoundOutput>(&stands_for_an_output);
}

void start_listening() { registry().start(); }

void begin_records() { registry().begin(); }

std::vector<BufferRecord> records() { return registry().records(); }

Settings last_settings() { return registry().last_settings(); }

} // namespace tests::sound
