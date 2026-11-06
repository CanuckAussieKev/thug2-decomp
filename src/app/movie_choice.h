#pragma once

// NOT ORIGINAL: which movie codec the movies and the music / sound streams play with - [features] movie_codec=bink|ffmpeg
// in thug2-decomp.ini (default bink: the player's binkw32.dll, media_bink/bink_movie.h). ffmpeg is FFmpeg's Bink
// decoders (media_ffmpeg/ffmpeg_movie.h), used when its DLLs are next to the exe; otherwise the choice logs why and
// falls back to Bink. A 64-bit build has no Bink codec (binkw32.dll is 32-bit): it always uses FFmpeg's, and
// logs why when the key asks for Bink. Read once, at start-up.

namespace media {
class Movie;
} // namespace media

namespace app {

// The chosen codec (the key, after the fallback).
media::Movie& chosen_movie_codec();

} // namespace app
