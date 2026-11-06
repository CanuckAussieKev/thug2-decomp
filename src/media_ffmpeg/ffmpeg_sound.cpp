// NOT ORIGINAL - a movie's sound track through FFmpeg's Bink audio decoder into audio::Device (ffmpeg_sound.h).

#include "media_ffmpeg/ffmpeg_sound.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/audio/audio_device.h"
#include "engine/audio/audio_handles.h"
#include "media_ffmpeg/ffmpeg_api.h"
#include "platform/run_log.h"

namespace media::ffmpeg {

namespace {

constexpr uint16_t kPcm = 1; // WAVE_FORMAT_PCM
constexpr uint16_t kSampleBits = 16;
constexpr uint32_t kSampleBytes = 2;
// DSBUFFERDESC's size, as the voices' descriptions give it (engine/sound/sfx_voices.cpp): 0x24 bytes, 0x28 on x64
constexpr uint32_t kBufferDescBytes = (4 * sizeof(uint32_t)) + sizeof(void*) + 16;
// The buffer as binkw32 made its own (seen through a stand-in DirectSound object: src/tests/movie_codec_check_sound.cpp):
// volume, pan and frequency controls, the accurate play position (DSBCAPS_GETCURRENTPOSITION2), sound while another
// window has the focus (DSBCAPS_GLOBALFOCUS); 0.6 seconds of samples, rounded up to 256 sample frames.
constexpr uint32_t kBufferFlags = 0x000180E0;
constexpr uint32_t kBufferTenths = 6; // (tenths of a second)
constexpr uint32_t kTenthsASecond = 10;
constexpr uint32_t kBufferFrameStep = 256;
constexpr uint32_t kMillisecondsASecond = 1000;
constexpr unsigned kBitsPerByte = 8;
constexpr double kHalf = 0.5;
constexpr uint32_t kSkipLeadTenths = 10; // a restart decodes from this long before its time (the blocks' overlap settled)
constexpr uint32_t kGuardMilliseconds = 10; // left unwritten just behind the play position (the cursor's granularity)
// Where the samples go on after running dry (a stream serviced late, the end of a loop before its first frame comes
// round): this far past the play position - binkw32's went on 61-76 ms past it.
constexpr uint32_t kLeadMilliseconds = 65;
constexpr auto kFeedEvery = std::chrono::milliseconds(10); // (binkw32 wrote every 50 ms or so)
constexpr double kFullScale = 32768.0;
constexpr int kSampleMax = 32767;
constexpr int kSampleMin = -32768;
// binkw32's track volume to the buffer's: 32767 (and above) full, then 10000/3 hundredths of a decibel down for each
// tenfold drop (-10 dB at 3277, -30 dB at 33), rounded, never below silence; its pan: 0 left ... 32768 centre ... 65536
// right to DirectSound's -10000 ... 10000, in a straight line, the fraction dropped toward the centre. (Both as binkw32
// sets them for every level and pan: src/tests/movie_codec_check_audio.cpp check_levels.)
constexpr double kAttenuationPerDecade = 10000.0 / 3.0;
constexpr double kFullVolume = 32767.0;
constexpr int kPanCentre = 32768;
constexpr int kPanRange = 10000;

int attenuation_of(int volume)
{
    if (volume <= 0) {
        return audio::kSilentAttenuation;
    }
    const auto attenuation = std::lrint(kAttenuationPerDecade * std::log10(volume / kFullVolume));
    return static_cast<int>(std::clamp<long>(attenuation, audio::kSilentAttenuation, 0));
}

int pan_of(int pan)
{
    const int64_t centred = static_cast<int64_t>(std::clamp(pan, 0, 2 * kPanCentre)) - kPanCentre;
    return static_cast<int>(centred * kPanRange / kPanCentre);
}

int16_t to_sample(float value)
{
    const auto scaled = std::lrint(static_cast<double>(value) * kFullScale);
    return static_cast<int16_t>(std::clamp<long>(scaled, kSampleMin, kSampleMax));
}

} // namespace

Sound::Sound(const Api& api, audio::SoundOutput* output) : api_(&api), output_(output), last_feed_(Clock::now()) {}

Sound::~Sound()
{
    stop_buffer();
    if (buffer_) {
        audio::device().release_buffer(buffer_);
    }
    api_->frame_free(&frame_);
    api_->codec_free_context(&decoder_);
}

bool Sound::open(const AVStream* stream)
{
    const AVCodecParameters* parameters = stream->codecpar;
    const AVCodec* codec = api_->find_decoder(parameters->codec_id);
    if (!codec) {
        return false;
    }
    decoder_ = api_->codec_alloc_context(codec);
    if (!decoder_ || api_->parameters_to_context(decoder_, parameters) < 0) {
        return false;
    }
    decoder_->thread_count = 1;
    if (api_->codec_open(decoder_, codec, nullptr) < 0) {
        return false;
    }
    frame_ = api_->frame_alloc();
    time_base_ = stream->time_base;
    rate_ = static_cast<uint32_t>(decoder_->sample_rate);
    channels_ = static_cast<uint32_t>(decoder_->ch_layout.nb_channels);
    block_ = channels_ * kSampleBytes;
    return frame_ != nullptr && rate_ != 0 && channels_ != 0;
}

void Sound::decode(const AVPacket* packet)
{
    const std::scoped_lock lock(mutex_);
    if (!decoder_) {
        return;
    }
    if (packet->pts != AV_NOPTS_VALUE && time_base_.den != 0) {
        const auto at = static_cast<uint64_t>(static_cast<double>(packet->pts) * time_base_.num * rate_ / time_base_.den);
        if (skip_before_ > at + (kSkipLeadTenths * rate_ / kTenthsASecond)) {
            return; // (long before the time restarted at: not decoded)
        }
        position_ = at;
    }
    // A Bink sound packet starts with the size of its samples decoded (16-bit): binkw32 plays exactly that many - the
    // decoder gives whole blocks, so the last packet's are cut to it (which keeps a loop's sound as long as its frames).
    size_t declared = 0;
    if (std::cmp_greater_equal(packet->size, sizeof(uint32_t))) {
        const std::span<const uint8_t> bytes(packet->data, sizeof(uint32_t));
        for (size_t at = sizeof(uint32_t); at != 0; --at) {
            declared = (declared << kBitsPerByte) | core::at(bytes, at - 1);
        }
        declared /= block_;
    }
    if (api_->send_packet(decoder_, packet) < 0) {
        return;
    }
    while (api_->receive_frame(decoder_, frame_) == 0) {
        const size_t count = std::min(static_cast<size_t>(frame_->nb_samples), declared);
        declared -= count;
        position_ += count;
        if (position_ <= skip_before_) {
            // Before the time restarted at: only the block just before the one that holds it is kept (binkw32's
            // sound after a go-to starts a block early).
            queued_.clear();
            queued_from_ = 0;
            queue_frame(count);
            continue;
        }
        skip_before_ = 0;
        queue_frame(count);
    }
}

void Sound::cut_ahead()
{
    const std::scoped_lock lock(mutex_);
    queued_.clear();
    queued_from_ = 0;
    cut_pending_ = playing_;
}

void Sound::start_again()
{
    const std::scoped_lock lock(mutex_);
    position_ = 0;
}

// The frame's samples (float or 16-bit, planar or interleaved) queued as 16-bit interleaved.
void Sound::queue_frame(size_t count)
{
    const auto format = static_cast<AVSampleFormat>(frame_->format);
    const auto decoded = static_cast<size_t>(frame_->nb_samples);
    const bool planar = format == AV_SAMPLE_FMT_FLTP || format == AV_SAMPLE_FMT_S16P;
    const bool floats = format == AV_SAMPLE_FMT_FLTP || format == AV_SAMPLE_FMT_FLT;
    if (!floats && format != AV_SAMPLE_FMT_S16 && format != AV_SAMPLE_FMT_S16P) {
        return; // (Bink audio decodes to floats)
    }
    const std::span<uint8_t* const> planes(frame_->extended_data, planar ? channels_ : 1U);
    const size_t first = queued_.size();
    queued_.resize(first + (count * channels_));
    const std::span<int16_t> into = std::span(queued_).subspan(first);
    for (size_t plane = 0; plane < planes.size(); ++plane) {
        // (a plane is one channel's samples - or, interleaved, all of them; the first `count` sample frames taken)
        const size_t values = planar ? count : count * channels_;
        const size_t held = planar ? decoded : decoded * channels_;
        const size_t stride = planar ? channels_ : 1U;
        const void* const data = core::at(planes, plane);
        if (floats) {
            const std::span<const float> from = std::span(static_cast<const float*>(data), held).first(values);
            if (stride == 1) {
                std::ranges::transform(from, into.begin(), to_sample);
            } else {
                for (size_t at = 0; at < values; ++at) {
                    core::at(into, (at * stride) + plane) = to_sample(core::at(from, at));
                }
            }
        } else {
            const std::span<const int16_t> from = std::span(static_cast<const int16_t*>(data), held).first(values);
            if (stride == 1) {
                std::ranges::copy(from, into.begin());
            } else {
                for (size_t at = 0; at < values; ++at) {
                    core::at(into, (at * stride) + plane) = core::at(from, at);
                }
            }
        }
    }
}

// The samples still to be written.
size_t Sound::queued_samples() const { return queued_.size() - queued_from_; }

void Sound::restart(double seconds)
{
    const std::scoped_lock lock(mutex_);
    skip_before_ = static_cast<uint64_t>(std::max(seconds, 0.0) * rate_);
    position_ = 0;
    queued_.clear();
    queued_from_ = 0;
    if (decoder_) {
        api_->flush_buffers(decoder_);
    }
    stop_buffer();
    playing_ = false;
}

bool Sound::make_buffer()
{
    if (buffer_) {
        return true;
    }
    if (!output_ || block_ == 0) {
        return false;
    }
    const uint32_t frames = ((rate_ * kBufferTenths / kTenthsASecond) + kBufferFrameStep - 1) / kBufferFrameStep *
                            kBufferFrameStep;
    buffer_bytes_ = frames * block_;
    audio::BufferDesc desc;
    desc.format = {.format_tag = kPcm,
                   .channels = static_cast<uint16_t>(channels_),
                   .samples_per_second_ = rate_,
                   .bytes_per_second_ = rate_ * block_,
                   .block_align_ = static_cast<uint16_t>(block_),
                   .bits_per_sample_ = kSampleBits,
                   .extra_size_ = 0};
    desc.desc_size_ = kBufferDescBytes;
    desc.flags = kBufferFlags;
    desc.bytes = buffer_bytes_;
    const audio::MadeBuffer made = audio::device().create_buffer(output_, desc);
    if (!made.made) {
        if (made.buffer) {
            audio::device().release_buffer(made.buffer);
        }
        output_ = nullptr; // (no sound for this track)
        return false;
    }
    buffer_ = made.buffer;
    static std::atomic_flag logged;
    if (!logged.test_and_set()) {
        get_log("ffmpeg: the movies' and streams' sound plays through the game's output (the first: %u Hz, %u channels)",
                rate_, channels_);
    }
    audio::device().set_buffer_volume(buffer_, attenuation_);
    audio::device().set_buffer_pan(buffer_, pan_);
    return true;
}

void Sound::stop_buffer()
{
    if (buffer_ && playing_) {
        audio::device().stop_buffer(buffer_);
    }
}

// Bytes from..to (counted from the buffer's start) written: the queued samples, or silence.
void Sound::write(uint64_t from, uint64_t to, bool silence)
{
    const auto bytes = static_cast<size_t>(to - from);
    const auto at = static_cast<uint32_t>(from % buffer_bytes_);
    if (silence) {
        silence_.resize(std::max(silence_.size(), bytes));
        audio::device().write_buffer(buffer_, at, std::span<const uint8_t>(silence_).first(bytes));
        return;
    }
    const std::span<const int16_t> samples = std::span<const int16_t>(queued_).subspan(queued_from_, bytes / kSampleBytes);
    // (the samples as the buffer holds them: 16-bit little-endian, as in memory here)
    staging_.resize(samples.size_bytes());
    std::memcpy(staging_.data(), samples.data(), samples.size_bytes());
    audio::device().write_buffer(buffer_, at, staging_);
    queued_from_ += samples.size();
    if (queued_from_ == queued_.size()) {
        queued_.clear();
        queued_from_ = 0;
    } else if (queued_from_ > queued_.size() / 2) {
        queued_.erase(queued_.begin(), queued_.begin() + static_cast<std::ptrdiff_t>(queued_from_));
        queued_from_ = 0;
    }
}

void Sound::feed()
{
    const std::scoped_lock lock(mutex_);
    feed_locked();
}

void Sound::feed_locked()
{
    if (paused_ || (!playing_ && queued_samples() == 0)) {
        return;
    }
    const Clock::time_point now = Clock::now();
    if (playing_ && now - last_feed_ < kFeedEvery) {
        return;
    }
    if (!make_buffer()) {
        queued_.clear();
        queued_from_ = 0;
        return;
    }
    if (!playing_) {
        // The start: the samples queued from the buffer's start, silence after them.
        played_ = 0;
        last_position_ = 0;
        data_end_ = 0;
        clean_end_ = 0;
    } else {
        uint32_t position = 0;
        if (!audio::device().buffer_position(buffer_, position)) {
            return;
        }
        const uint64_t moved = (position + buffer_bytes_ - last_position_) % buffer_bytes_;
        // (not serviced for a while, the position may have gone round unseen: the times round that the clock says)
        const double expected =
            std::chrono::duration<double>(now - last_feed_).count() * static_cast<double>(rate_) * block_;
        const double rounds = std::floor(((expected - static_cast<double>(moved)) / buffer_bytes_) + kHalf);
        played_ += moved + (static_cast<uint64_t>(std::max(rounds, 0.0)) * buffer_bytes_);
        last_position_ = position;
    }
    last_feed_ = now;
    if (cut_pending_) {
        cut_pending_ = false;
        const uint64_t keep = played_ + (static_cast<uint64_t>(buffer_bytes_ / block_ / 2) * block_);
        if (data_end_ > keep) {
            data_end_ = keep;
            clean_end_ = keep; // (the rest written again: the samples that follow, then silence)
        }
    }
    if (data_end_ < played_ && queued_samples() != 0) {
        // Run dry: the samples go on a little past the play position, silence before them.
        data_end_ = played_ + (static_cast<uint64_t>(rate_) * kLeadMilliseconds / kMillisecondsASecond * block_);
        clean_end_ = std::max(clean_end_, data_end_);
    }
    if (data_end_ < played_) {
        return; // (nothing to play: the buffer keeps its silence)
    }
    const uint64_t guard = static_cast<uint64_t>(rate_) * kGuardMilliseconds / kMillisecondsASecond * block_;
    const uint64_t limit = played_ + buffer_bytes_ - guard;
    const uint64_t queued_bytes = static_cast<uint64_t>(queued_samples()) / channels_ * block_;
    const uint64_t data_to = std::min(limit, data_end_ + queued_bytes);
    if (data_to > data_end_) {
        write(data_end_, data_to, false);
        data_end_ = data_to;
        clean_end_ = std::max(clean_end_, data_end_);
    }
    if (limit > clean_end_) {
        write(clean_end_, limit, true);
        clean_end_ = limit;
    }
    if (!playing_) {
        playing_ = audio::device().play_buffer(buffer_, true);
        last_feed_ = Clock::now();
    }
}

void Sound::pause(bool paused)
{
    const std::scoped_lock lock(mutex_);
    if (paused == paused_) {
        return;
    }
    paused_ = paused;
    if (!buffer_ || !playing_) {
        return;
    }
    if (paused) {
        audio::device().stop_buffer(buffer_);
    } else {
        audio::device().resume_buffer(buffer_);
        last_feed_ = Clock::now();
    }
}

void Sound::set_volume(int volume)
{
    const std::scoped_lock lock(mutex_);
    attenuation_ = attenuation_of(volume);
    if (buffer_) {
        audio::device().set_buffer_volume(buffer_, attenuation_);
    }
}

void Sound::set_pan(int pan)
{
    const std::scoped_lock lock(mutex_);
    pan_ = pan_of(pan);
    if (buffer_) {
        audio::device().set_buffer_pan(buffer_, pan_);
    }
}

std::optional<double> Sound::heard_seconds()
{
    const std::scoped_lock lock(mutex_);
    if (!playing_ || paused_ || block_ == 0) {
        return std::nullopt;
    }
    return static_cast<double>(played_) / block_ / rate_;
}

} // namespace media::ffmpeg
