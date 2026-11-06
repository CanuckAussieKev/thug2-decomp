// NOT ORIGINAL - FFmpeg's media::Movie (ffmpeg_movie.h): an open movie is a Playback (ffmpeg_playback.h), whose
// first part is the header the game reads; each call is passed to it.

#include "media_ffmpeg/ffmpeg_movie.h"

#include <atomic>
#include <cstdint>
#include <memory>

#include "base/core/down_cast.h"
#include "base/core/no_destroy.h"
#include "engine/audio/audio_handles.h"
#include "engine/media/movie.h"
#include "media_ffmpeg/ffmpeg_api.h"
#include "media_ffmpeg/ffmpeg_playback.h"
#include "platform/run_log.h"

namespace media::ffmpeg {

namespace {

Playback* playback_of(MovieHeader* movie) { return core::down_cast<Playback>(movie); }

class FfmpegMovie final : public Movie {
public:
    // (the engine holds the movie by its header until it closes it)
    MovieHeader* open(const void* source, uint32_t flags) override
    {
        const Api* functions = api();
        return functions ? Playback::open(*functions, source, flags).release() : nullptr;
    }
    void close(MovieHeader* movie) override { const std::unique_ptr<Playback> closed(playback_of(movie)); }
    int wait(MovieHeader* movie) override { return movie ? playback_of(movie)->wait() : 1; }
    void do_frame(MovieHeader* movie) override
    {
        if (movie) {
            playback_of(movie)->do_frame();
        }
    }
    void next_frame(MovieHeader* movie) override
    {
        if (movie) {
            playback_of(movie)->next_frame();
        }
    }
    void go_to(MovieHeader* movie, FrameJump jump) override
    {
        if (movie) {
            playback_of(movie)->go_to(jump.frame, jump.flags);
        }
    }
    void pause(MovieHeader* movie, int pause) override
    {
        if (movie) {
            playback_of(movie)->pause(pause != 0);
        }
    }
    // (the sound isn't played yet: the tracks' pan and volume have nothing to change)
    void set_pan(MovieHeader* /*movie*/, TrackPan /*track_pan*/) override {}
    void set_volume(MovieHeader* /*movie*/, TrackVolume /*track_volume*/) override {}
    // (FFmpeg reads through its own buffer)
    void set_io_size(uint32_t /*size*/) override {}
    void set_video_on_off(MovieHeader* movie, int on) override
    {
        if (movie) {
            playback_of(movie)->set_video(on != 0);
        }
    }
    void set_sound_on_off(MovieHeader* movie, int on) override
    {
        if (movie) {
            playback_of(movie)->set_sound(on != 0);
        }
    }
    void copy_to_buffer(MovieHeader* movie, void* bits, FrameCopy frame_copy) override
    {
        if (movie) {
            playback_of(movie)->copy_to(bits, frame_copy);
        }
    }
    bool use_sound_output(audio::SoundOutput* /*output*/) override
    {
        static std::atomic_flag logged;
        if (!logged.test_and_set()) {
            get_log("ffmpeg: the movies' and streams' sound isn't played by this codec yet");
        }
        return true;
    }
};

} // namespace

bool usable() { return api() != nullptr; }

Movie& movie()
{
    static core::NoDestroy<FfmpegMovie> made;
    return made.get();
}

} // namespace media::ffmpeg
