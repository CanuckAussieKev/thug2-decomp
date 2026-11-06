#pragma once

// NOT ORIGINAL - one movie's sound track played through FFmpeg's Bink audio decoder into the game's sound output
// (ffmpeg_playback.h's Playback has one when the movie has a sound track and the codec was given an output).
//
// binkw32 played its sound into the game's DirectSound object itself (a buffer of its own per track, kept filled by
// its own thread). Here the track plays through audio::Device (engine/audio/audio_device.h): one looping 16-bit PCM
// buffer in the track's format, made on the first samples, kept filled ahead of its play position each time the
// movie is serviced (wait and do_frame: the movie player asks in a loop, the stream thread every 5 ms) - the samples
// decoded so far, then silence, so a buffer the decoding falls behind plays silence and never old samples again. Its
// volume and pan are Bink's track volume and pan (set_volume / set_pan) converted to the output's hundredths of a
// decibel as binkw32 converts them. With [features] mute_audio the output is the muted one, which keeps every buffer
// at silence.
//
// The buffer's play position is the movie's clock (Playback::wait): how long the track has played says which frame
// is due, as with binkw32 (silence included when the samples ran dry: the frames keep their pace).
//
// A track is used from one thread at a time, but its level and pan may be set from another (the game's thread, while
// the stream thread services the stream): its own lock keeps the two apart.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "engine/audio/audio_handles.h"
#include "media_ffmpeg/ffmpeg_api.h"

namespace media::ffmpeg {

class Sound {
public:
    // The track `stream` played into `output`; nothing plays if its decoder won't open (open says).
    Sound(const Api& api, audio::SoundOutput* output);
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;
    Sound(Sound&&) = delete;
    Sound& operator=(Sound&&) = delete;
    ~Sound();

    // The decoder for `stream` (a Bink audio track); false if there is none.
    bool open(const AVStream* stream);
    // A packet of the track decoded, its samples queued (those before the time restart set dropped).
    void decode(const AVPacket* packet);
    // The queued samples dropped, the decoder emptied and the buffer stopped and silent: the track starts again at
    // `seconds` into it (a go-to: binkw32 plays the sound from the frame gone to, not the sound stored with that frame,
    // which is ahead of it) - from the start of the decoded block before the one that holds that time, as binkw32.
    void restart(double seconds);
    // The track read again from its start, played on (a loop): the first packet's samples follow the last's, the
    // decoder going on from the blocks it decoded last, as binkw32's does.
    void start_again();
    // The samples decoded ahead dropped, and of those written ahead only half a buffer's kept: what follows comes
    // after them (a go-to to the first frame, as binkw32 - which went on to the first frame's sound 0.28 s on).
    void cut_ahead();
    // The buffer filled ahead of its play position (started once there is something to play).
    void feed();
    void pause(bool paused);
    // Bink's track volume (0 ... 32768, the level the game set as the output gives it) and pan (0 left, 32768 centre,
    // 65536 right).
    void set_volume(int volume);
    void set_pan(int pan);
    // The seconds the track's buffer has played since it started (or restarted), the silence while the samples ran
    // dry included - binkw32's clock; none while it isn't playing.
    std::optional<double> heard_seconds();

private:
    using Clock = std::chrono::steady_clock;

    // The decoded frame's first `count` sample frames queued.
    void queue_frame(size_t count);
    [[nodiscard]] size_t queued_samples() const;
    void feed_locked();
    bool make_buffer();
    void write(uint64_t from, uint64_t to, bool silence);
    void stop_buffer();

    const Api* api_;
    audio::SoundOutput* output_;
    std::mutex mutex_;
    AVCodecContext* decoder_ = nullptr;
    AVFrame* frame_ = nullptr;
    uint32_t rate_ = 0;
    uint32_t channels_ = 0;
    uint32_t block_ = 0; // bytes a sample frame
    AVRational time_base_{}; // the packets' times
    uint64_t skip_before_ = 0; // samples before this one aren't played (restart)
    uint64_t position_ = 0;    // the sample the next decoded block starts at
    std::vector<int16_t> queued_; // decoded, not yet written from queued_from_ on (interleaved)
    size_t queued_from_ = 0;
    std::vector<uint8_t> silence_; // (zeros, as many as written at once)
    std::vector<uint8_t> staging_; // (the samples written, as bytes)
    audio::SoundBuffer* buffer_ = nullptr;
    uint32_t buffer_bytes_ = 0;
    bool playing_ = false;
    bool cut_pending_ = false; // (cut_ahead: done at the next feed, with the play position read)
    bool paused_ = false;
    int attenuation_ = 0; // the buffer's volume and pan (hundredths of a decibel)
    int pan_ = 0;
    // Counted in bytes from the buffer's start, never wrapping: what was played, what was written of the track's
    // samples, and what of the buffer is silence on from there.
    uint32_t last_position_ = 0;
    uint64_t played_ = 0;
    uint64_t data_end_ = 0;
    uint64_t clean_end_ = 0;
    Clock::time_point last_feed_;
};

} // namespace media::ffmpeg
