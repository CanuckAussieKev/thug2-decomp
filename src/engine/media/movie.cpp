// NOT ORIGINAL - the installed movie codec (movie.h).

#include "engine/media/movie.h"

namespace media {

namespace {

// The codec app/ installed (a constant-initialised pointer: no guard, nothing to destroy).
Movie*& installed()
{
    static Movie* codec = nullptr;
    return codec;
}

} // namespace

Movie& movie() { return *installed(); }

void install_movie(Movie& movie) { installed() = &movie; }

} // namespace media
