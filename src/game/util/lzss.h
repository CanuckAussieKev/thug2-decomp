#pragma once

#include <cstdint>

// LZSS compression in the classic public-domain form (Haruhiko Okumura, 1989): a 4 KB sliding window,
// matches of 3-18 bytes, and a binary search tree to find them. Each group of 8 items starts with a flag
// byte; a set bit is a literal byte, a clear bit a 2-byte (position, length) reference.
//
// The state (window, tree, statistics) lives in globals in the exe, shared with the original code.
namespace Lzss {

// Compress `size` bytes from `in` to `out` and return the compressed size. With `print_stats`, the sizes and
// ratio are sent to the debug output. `out` needs room for size * 9 / 8 + 2 bytes in the worst case.
int Encode(const uint8_t* in, uint8_t* out, int size, bool print_stats);

// Decompress `size` bytes of compressed data from `in` into `out` and return the end of the output (one past
// the last byte written). Callers use that pointer to get the decompressed size and to pad the end of the
// data. Like the original, an odd byte count that runs out mid-reference isn't detected.
uint8_t* Decode(const uint8_t* in, uint8_t* out, int size);

// Encoder internals (exposed for the replacement table and tests).
void InitTree();
void InsertNode(int r);
void DeleteNode(int p);

} // namespace Lzss
