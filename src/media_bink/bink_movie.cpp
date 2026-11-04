// NOT ORIGINAL - Bink's media::Movie (bink_movie.h): each method calls the binkw32 function it stands for (bink.h,
// looked up in the player's binkw32.dll on first use), with the game's arguments.

#include "media_bink/bink_movie.h"

#include <cstdint>

#include "base/core/no_destroy.h"
#include "engine/audio/audio_handles.h"
#include "engine/media/movie.h"
#include "media_bink/bink.h"

namespace media::bink {

namespace {

class BinkMovie final : public Movie {
public:
    MovieHeader* open(const void* source, uint32_t flags) override { return ::bink::open(source, flags); }
    void close(MovieHeader* movie) override { ::bink::close(movie); }
    int wait(MovieHeader* movie) override { return ::bink::wait(movie); }
    void do_frame(MovieHeader* movie) override { ::bink::do_frame(movie); }
    void next_frame(MovieHeader* movie) override { ::bink::next_frame(movie); }
    void go_to(MovieHeader* movie, FrameJump jump) override { ::bink::go_to(movie, jump); }
    void pause(MovieHeader* movie, int pause) override { ::bink::pause(movie, pause); }
    void set_pan(MovieHeader* movie, TrackPan track_pan) override { ::bink::set_pan(movie, track_pan); }
    void set_volume(MovieHeader* movie, TrackVolume track_volume) override { ::bink::set_volume(movie, track_volume); }
    void set_io_size(uint32_t size) override { ::bink::set_io_size(size); }
    void set_video_on_off(MovieHeader* movie, int on) override { ::bink::set_video_on_off(movie, on); }
    void set_sound_on_off(MovieHeader* movie, int on) override { ::bink::set_sound_on_off(movie, on); }
    void copy_to_buffer(MovieHeader* movie, void* bits, FrameCopy frame_copy) override
    {
        ::bink::copy_to_buffer(movie, bits, frame_copy);
    }
    // Bink's DirectSound output on the game's DirectSound object: the sound output's handle is that object
    // (audio::Device's DirectSound backend - the only output Bink can play into).
    bool use_sound_output(audio::SoundOutput* output) override { return ::bink::use_direct_sound(output); }
};

} // namespace

Movie& movie()
{
    static core::NoDestroy<BinkMovie> made;
    return made.get();
}

} // namespace media::bink
