#pragma once

// NOT ORIGINAL: which movie codec the movies and the music / sound streams play with - [features] movie_codec=bink|ffmpeg
// in thug2-decomp.ini (default bink: the player's binkw32.dll, media_bink/bink_movie.h). ffmpeg is FFmpeg's Bink
// decoders (media_ffmpeg/ffmpeg_movie.h), used when its DLLs are next to the exe; otherwise the choice logs why and
// falls back to Bink. Video only so far: with ffmpeg the movies and streams play silent. Read once, at start-up.

namespace media {
class Movie;
} // namespace media

namespace app {

// The chosen codec (the key, after the fallback).
media::Movie& chosen_movie_codec();

} // namespace app
