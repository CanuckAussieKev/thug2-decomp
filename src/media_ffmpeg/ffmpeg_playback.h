#pragma once

// NOT ORIGINAL - one open Bink file played through FFmpeg (the FFmpeg movie codec's movie: ffmpeg_movie.cpp). It is
// the header the game reads (media::MovieHeader: the size, the frame count and rate, the frame decoded last), and
// behind it FFmpeg's Bink demuxer over the file - read from a path, or from memory - and its Bink video decoder.
//
// The frames go as binkw32's do: after the open the first frame is due at once; do_frame decodes frame_num_, next_frame
// moves on (from the last frame back to the first: a stream loops), wait says whether the next frame isn't due yet by
// the movie's frame rate (the first frame due at the first wait; after a go_to or a pause, a frame later). A frame that isn't the one after the
// last decoded is reached by decoding from the start (Bink's video frames are built on the frames before them; FFmpeg's
// Bink demuxer seeks only to the start), so a go_to costs the frames before it - the movies never go to a frame, the
// sound streams do (with the video off: nothing is decoded on the way).
//
// The sound: the first sound track, decoded as its packets are read and played into the sound output the codec was
// given (ffmpeg_sound.h), its samples heard the clock the frames are due by while it plays. A go-to starts the track
// again at the frame gone to; the start again after the last frame plays on without a break.

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "engine/audio/audio_handles.h"
#include "engine/media/movie.h"
#include "media_ffmpeg/ffmpeg_api.h"

namespace media::ffmpeg {

class Input;
class Sound;

class Playback : public MovieHeader {
public:
    // The movie at `source` (a path, or with Bink's from-memory flag a Bink file in memory), its sound played into
    // `output` (null: silent); null (logged) if it isn't a Bink file FFmpeg can play.
    static std::unique_ptr<Playback> open(const Api& api, const void* source, uint32_t flags,
                                          audio::SoundOutput* output);

    Playback(const Api& api, std::unique_ptr<Input> input);
    Playback(const Playback&) = delete;
    Playback& operator=(const Playback&) = delete;
    Playback(Playback&&) = delete;
    Playback& operator=(Playback&&) = delete;
    ~Playback();

    // Non-zero while the next frame isn't due.
    int wait();
    // Frame frame_num_ decoded (with the video on).
    void do_frame();
    void next_frame();
    // Frame `frame` (from 1) next: decoded by the next do_frame (with BinkGoto's flags).
    void go_to(uint32_t frame, int flags);
    void pause(bool paused);
    void set_video(bool on) { video_on_ = on; }
    void set_sound(bool on);
    // The sound track's level and pan (Bink's values; one track is played, whichever the game names).
    void set_volume(int volume);
    void set_pan(int pan);
    // The frame decoded last into a locked surface (BINKSURFACE32 only; others are logged once and left), through
    // swscale's YUV to RGB.
    void copy_to(void* bits, FrameCopy copy);

private:
    using Clock = std::chrono::steady_clock;

    bool start(const void* source, uint32_t flags, audio::SoundOutput* output);
    bool open_decoder();
    void open_sound(audio::SoundOutput* output);
    // The demuxer back at the first frame (the decoder emptied).
    void rewind();
    // The packets read through video packet `index` (from 0), each video packet decoded on the way if the video is on.
    void read_through(uint32_t index);
    void decode(AVPacket* packet);
    // The clock started again now, the next frame due `frames_off` frames from now.
    void restart_clock(uint32_t frames_off);
    // The sound track started again from frame `frame`'s time (the frames counted for it from 0).
    void restart_sound(uint32_t frame);
    // Whether the next frame is due by the sound heard (none: the sound isn't the clock now).
    [[nodiscard]] std::optional<bool> due_by_sound() const;

    const Api* api_;
    std::unique_ptr<Input> input_;
    AVIOContext* io_ = nullptr;
    AVFormatContext* format_ = nullptr;
    AVCodecContext* video_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwsContext* converter_ = nullptr; // the colour converter for this size and format
    int converter_columns_ = 0;
    int converter_rows_ = 0;
    int converter_format_ = -1;
    int video_stream_ = -1;
    int audio_stream_ = -1;
    std::unique_ptr<Sound> sound_;
    uint32_t next_packet_ = 0; // the video packet read next (from 0)
    bool have_frame_ = false;  // frame_ holds a decoded frame
    uint32_t decoded_ = 0;     // which (from 0)
    bool video_on_ = true;
    bool sound_on_ = true;
    bool paused_ = false;
    bool clock_running_ = false;
    Clock::time_point clock_start_;
    uint32_t frames_since_start_ = 0; // next_frame calls since the clock started
    uint32_t frames_since_sound_ = 0; // next_frame calls since the sound track (re)started
};

} // namespace media::ffmpeg
