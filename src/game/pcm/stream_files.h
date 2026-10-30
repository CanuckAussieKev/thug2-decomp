#pragma once

// The stream files read into memory (12, at 0x0069CAD8; bink_stream_files.cpp): the file's key (-1: none), the data, the use
// count when it was last asked for, and how many streams use it (only unused ones are reused). bink_stream.cpp's pcm.wad
// entries open as Bink streams share the table (from the data on).

#include <cstdint>

#include "hook/game_global.h"

namespace Pcm {

struct CachedStreamFile {
    uint32_t key;
    void* data;
    uint32_t lastUse;
    int users;
};
static_assert(sizeof(CachedStreamFile) == 16, "layout must match the original");

GAME_GLOBAL_EXTERN(CachedStreamFile[12], g_streamFiles);

} // namespace Pcm
