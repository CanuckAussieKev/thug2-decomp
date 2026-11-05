#pragma once

#include "engine/media/movie.h"

// NOT ORIGINAL - Bink's media::Movie (bink_movie.cpp): each method is the binkw32 call it stands for (bink.h). app/
// installs it at start-up.
namespace media::bink {

// The Bink codec (made on first use, never destroyed).
Movie& movie();

} // namespace media::bink
