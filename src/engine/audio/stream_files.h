#pragma once

// The stream files read into memory (12, at 0x0069CAD8; bink_stream_files.cpp): the file's key (-1: none), the data, the use
// count when it was last asked for, and how many streams use it (only unused ones are reused). bink_stream.cpp's pcm.wad
// entries open as Bink streams share the table (from the data on).

#include <cstdint>
#include <array>

namespace audio {
constexpr size_t kCachedStreamFileCount = 12; // the streams' files kept open for the next play
constexpr size_t kCachedStreamFileBytes = 16;

struct CachedStreamFile {
    uint32_t key;
    void* data;
    uint32_t last_use_;
    int users;
};
static_assert(sizeof(CachedStreamFile) == kCachedStreamFileBytes, "layout must match the original");

} // namespace audio
