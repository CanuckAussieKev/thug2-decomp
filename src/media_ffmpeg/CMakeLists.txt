# The FFmpeg movie codec (src/media_ffmpeg): the game's Bink files decoded by FFmpeg's Bink demuxer and decoders (the
# LGPL DLLs built by tools/ffmpeg/build_ffmpeg.ts, loaded by name at run time: ffmpeg_api.cpp), behind media::Movie
# (engine/media/movie.h) - beside Bink's (src/media_bink), for a 64-bit build. app/ installs it with
# [features] movie_codec=ffmpeg (app/movie_choice.cpp). It includes the engine and the layers below it (tools/layers.ts
# checks it); nothing below app/ includes or links it.
add_library(thug2_media_ffmpeg STATIC
    ffmpeg_api.cpp
    ffmpeg_movie.cpp
    ffmpeg_playback.cpp
    ffmpeg_sound.cpp
)
target_link_libraries(thug2_media_ffmpeg PUBLIC thug2_engine PRIVATE ffmpeg_headers)
