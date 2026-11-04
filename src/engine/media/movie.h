#pragma once

// NOT ORIGINAL - media::Movie: what the engine asks of the movie codec. The game's movies (render_d3d9/movie.cpp:
// the start-up logos, the intro, the cutscene movies) and its music and sound streams (engine/audio/bink_stream*.cpp:
// sound-only movie files, from a path, an open file handle or memory) are Bink files, played by binkw32.dll - the
// player's own copy, looked up by name on first use (media_bink/bink.h). The engine opens, decodes, steps, seeks and
// pauses them and sets their sound through this interface; app/ makes it and installs it at start-up
// (install_movie), as it does render::Device and audio::Device: Bink's (media_bink/bink_movie.h) is the only one.
// A movie's sound plays into the sound output (use_sound_output: audio::Device's output, engine/audio/audio_device.h),
// at the level the output gives it (audio::Device::track_volume).
//
// The grain is a movie (opened, closed, paused, a track's level or pan) or a frame (wait, decode, step, copy out);
// never per sample or pixel - the codec decodes a frame and copies it into the caller's locked surface whole. The
// stream thread services its streams through it every 5 ms, a frame at a time.
//
// The methods keep the names and arguments of the binkw32 functions they stand for (the flags and values are Bink's,
// as the game passes them), and Bink's methods are those calls, in the same order.
//
// Not installed, a call dereferences null: the codec is installed before any of the original's code runs.

#include <cstdint>

#include "engine/audio/audio_handles.h"

namespace media {

// An open movie's header (Bink's BINK: the fields the game reads). The codec's own object; the engine holds its
// pointer as the movie's handle.
struct MovieHeader {
    uint32_t width;
    uint32_t height;
    uint32_t frames;
    uint32_t frame_num_; // the frame decoded last, from 1
    uint32_t last_frame_num_;
    uint32_t frame_rate_; // frames per frame_rate_div_ seconds
    uint32_t frame_rate_div_;
};

// A frame to go to and the flags.
struct FrameJump {
    uint32_t frame{};
    int flags{};
};

// A sound track and its pan.
struct TrackPan {
    uint32_t track{};
    int pan{};
};

// A sound track and its volume.
struct TrackVolume {
    uint32_t track{};
    int volume{};
};

// Where a frame is copied: the buffer's pitch and height, the corner and the flags.
struct FrameCopy {
    int pitch{};
    uint32_t height{};
    uint32_t x{};
    uint32_t y{};
    uint32_t flags{};
};

class Movie {
public:
    Movie() = default;
    Movie(const Movie&) = delete;
    Movie& operator=(const Movie&) = delete;
    Movie(Movie&&) = delete;
    Movie& operator=(Movie&&) = delete;

    // A movie by path - or, with the flags, an open file handle's address / a file in memory (null if it won't open).
    virtual MovieHeader* open(const void* source, uint32_t flags) = 0;
    virtual void close(MovieHeader* movie) = 0;
    // Non-zero while the next frame isn't due.
    virtual int wait(MovieHeader* movie) = 0;
    // The frame decoded.
    virtual void do_frame(MovieHeader* movie) = 0;
    // On to the next frame.
    virtual void next_frame(MovieHeader* movie) = 0;
    virtual void go_to(MovieHeader* movie, FrameJump jump) = 0;
    virtual void pause(MovieHeader* movie, int pause) = 0;
    virtual void set_pan(MovieHeader* movie, TrackPan track_pan) = 0;
    virtual void set_volume(MovieHeader* movie, TrackVolume track_volume) = 0;
    // The read buffer's size for the movies opened next.
    virtual void set_io_size(uint32_t size) = 0;
    virtual void set_video_on_off(MovieHeader* movie, int on) = 0;
    virtual void set_sound_on_off(MovieHeader* movie, int on) = 0;
    // The decoded frame into a locked surface.
    virtual void copy_to_buffer(MovieHeader* movie, void* bits, FrameCopy frame_copy) = 0;
    // The movies' sound played into the sound output `output` (null: none made); false if the codec can't use it.
    virtual bool use_sound_output(audio::SoundOutput* output) = 0;

protected:
    ~Movie() = default;
};

// The installed codec (app/ installs it before the original's code runs).
Movie& movie();
void install_movie(Movie& movie);

} // namespace media
