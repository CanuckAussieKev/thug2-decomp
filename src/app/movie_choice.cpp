#include "app/movie_choice.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>

#include "engine/media/movie.h"
#ifdef THUG2_BINK
#include "media_bink/bink_movie.h"
#endif
#include "media_ffmpeg/ffmpeg_movie.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"

namespace app {

namespace {

constexpr size_t kValueSize = 32;

} // namespace

media::Movie& chosen_movie_codec()
{
    // (main runs before the PC set-up that finds the settings file; finding it again there changes nothing)
    build_info::prepare_all();
    std::array<char, kValueSize> buffer{};
    build_info::fetch_text("features", "movie_codec", "bink", buffer.data(), buffer.size());
    std::string value(buffer.data());
    std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
#ifdef THUG2_BINK
    if (value == "ffmpeg") {
        if (media::ffmpeg::usable()) {
            get_log("movies: FFmpeg's Bink decoders (movie_codec=ffmpeg)");
            return media::ffmpeg::movie();
        }
        get_log("movies: FFmpeg's DLLs can't be used (above); Bink, the default");
    } else if (value != "bink" && !value.empty()) {
        get_log("movie_codec=%s is not a movie codec (bink or ffmpeg); Bink, the default", value.c_str());
    }
    return media::bink::movie();
#else
    // (a 64-bit build: binkw32.dll is 32-bit and can't be loaded, so FFmpeg's decoders are the only codec)
    if (value != "ffmpeg" && !value.empty()) {
        get_log("movies: FFmpeg's Bink decoders - movie_codec=%s isn't used: binkw32.dll is 32-bit, this 64-bit build can't "
                "load it",
                value.c_str());
    } else {
        get_log("movies: FFmpeg's Bink decoders (this 64-bit build's only codec: binkw32.dll is 32-bit)");
    }
    if (!media::ffmpeg::usable()) {
        get_log("movies: FFmpeg's DLLs can't be used (above): the movies and the music / sound streams won't play");
    }
    return media::ffmpeg::movie();
#endif
}

} // namespace app
