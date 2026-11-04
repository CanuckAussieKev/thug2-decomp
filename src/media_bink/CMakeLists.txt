# The Bink movie codec (src/media_bink): the player's own binkw32.dll, its functions looked up by name on first use
# (bink.cpp), behind media::Movie (engine/media/movie.h): the movies and the music / sound streams are Bink files.
# app/ installs it (app/main.cpp). It includes the engine and the layers below it (tools/layers.ts checks it); nothing
# below app/ includes or links it, so the engine builds without Bink.
add_library(thug2_media_bink STATIC
    bink.cpp
    bink_movie.cpp
)
target_link_libraries(thug2_media_bink PUBLIC thug2_engine)
