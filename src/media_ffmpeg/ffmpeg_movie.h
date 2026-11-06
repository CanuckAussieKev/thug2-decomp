#pragma once

#include "engine/media/movie.h"

// NOT ORIGINAL - FFmpeg's media::Movie (ffmpeg_movie.cpp): the game's Bink files decoded by FFmpeg's Bink demuxer and
// decoders (the LGPL DLLs avformat-61, avcodec-61, avutil-59 and swscale-8 next to the exe, loaded by name) instead
// of the player's binkw32.dll - the codec a 64-bit build needs, since binkw32.dll is 32-bit. app/ installs it with
// [features] movie_codec=ffmpeg (app/movie_choice.h). The sound plays into the game's own output through audio::Device
// (ffmpeg_sound.h).
namespace media::ffmpeg {

// Whether FFmpeg's DLLs are there and are the versions this codec is built for (loaded on the first call; what's
// missing is logged).
bool usable();

// The FFmpeg codec (made on first use, never destroyed).
Movie& movie();

} // namespace media::ffmpeg
