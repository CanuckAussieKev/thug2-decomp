#pragma once

// NOT ORIGINAL - thug2_movie_codec_check's sound comparison (movie_codec_check_audio.cpp): a Bink file played through
// each codec as the game plays it - a movie (sound on, the level the movie player sets, wait / decode / step until the
// end) or a sound stream (video off, opened from a path or from memory, serviced every 5 ms as the stream thread does)
// -, each codec's sound into its stand-in output (movie_codec_check_sound.h); then what the outputs were handed is
// compared: the buffers' formats, the volumes and pans set for the game's levels, when the sound started, the samples
// heard (aligned, the largest and mean difference, the signal-to-noise ratio, the levels) and the frames' pace.

#include <cstdint>
#include <vector>

namespace tests::audio_check {

struct Options {
    double seconds = 0.0; // how long each file plays (0: a movie or a pcm.wad sound to its end, a music stream 8 s)
    bool stream = false;  // the files are sound streams (the video off), not movies
    bool from_memory = false;
    bool events = false;  // streams: a go-to half way at 2 s, a pause from 4 to 4.5 s, a go-to to the start at 6 s
    uint32_t go_to = 0;   // (the first go-to's frame, 0: half way)
    uint32_t wad_every = 0; // with a .wad: every n-th Bink file in it (0: the first 12)
    const char* dump = nullptr; // the samples each codec's output heard written to <dump>.bink.raw / <dump>.ffmpeg.raw
};

// The codecs' sound outputs set up (once, before the first file).
void set_up();
// One file through both codecs; false if it doesn't open with both or the sound differs beyond the rounding.
bool check_file(const char* file, const Options& options);
// The Bink files inside a .wad (pcm.wad) opened from memory, as the sound streams open them.
bool check_wad(const char* file, const Options& options);
// The levels and pans the game sets mapped by each codec (on `file`'s movie), and a paused memory stream's reopen;
// with options.dump, the levels into <dump>.levels.txt (level, Bink's attenuation, FFmpeg's).
void check_levels(const char* file, const Options& options);

} // namespace tests::audio_check
