#pragma once

#include <cstddef>
#include <cstdint>

// LZSS in the classic public-domain form (Haruhiko Okumura, 1989): a 4 KB sliding window, matches of 3-18 bytes.
// Each group of 8 items starts with a flag byte; a set bit is a literal byte, a clear bit a 2-byte (position, length)
// reference. The bounded decoder of the file formats (archives, stored scripts); the compressor and the original
// unbounded decoder are engine/util/lzss.h.
namespace lzss {

// NOT ORIGINAL (formats/lzss_unpack.cpp) - decode with both ends bounded: `in_size` bytes of compressed data
// into at most `out_capacity` bytes; `*written` (if given) gets the bytes written. False (with what was written
// so far) when the output would run past its capacity or the input ends inside a reference. A valid stream
// gives exactly decode's bytes.
bool unpack(const uint8_t* in, size_t in_size, uint8_t* out, size_t out_capacity, size_t* written);

} // namespace lzss
