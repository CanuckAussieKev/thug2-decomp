#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "formats/lzss.h"

// LZSS compression in the classic public-domain form (Haruhiko Okumura, 1989): a 4 KB sliding window,
// matches of 3-18 bytes, and a binary search tree to find them. Each group of 8 items starts with a flag
// byte; a set bit is a literal byte, a clear bit a 2-byte (position, length) reference.
//
// The state (window, tree, statistics) lives in globals in the exe, shared with the original code.
namespace lzss {

// Compress `input` into `output` and return the compressed size. With `print_stats`, the sizes and ratio are sent to
// the debug output. `output` needs room for size * 9 / 8 + 2 bytes in the worst case.
int pack_up(std::span<const uint8_t> input, std::span<uint8_t> output, bool print_stats);

// The decoder, unpack: formats/lzss.h (the original's unbounded one is gone: its callers hold the output's size).

// Encoder internals.
void reset_match_tree();
void add_match_node(int r);
void unlink_list_node(int p);

} // namespace lzss
