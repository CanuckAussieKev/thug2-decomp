#pragma once

#include <cstdint>
#include <span>

#include "base/core/cursor.h"

// NOT ORIGINAL - compiled script code (qb.h) read through a checked cursor instead of a bare pointer.
//
// The runtime keeps plain pointers into code, as the original did (a script's next token, its loops' starts and ends,
// its callers' return points, a structure's script body: Script and its frames keep their sizes, the script pools are
// unchanged). Every block that holds code is noted here with its size where it is made and forgotten where it is freed:
// the script cache's decompressed scripts, a script's private copy, a structure's script bodies, pre-compiled images.
// tokens_at turns a kept pointer back into a cursor over its whole block - once where the interpreter takes up a
// script (a start, a call, a return), not per token - and from there every token is read inside the block.
//
// The table lives outside the game's heaps (the C++ runtime's), so no heap's figures change.
namespace script {

using Tokens = core::Cursor<const uint8_t>;
using MutableTokens = core::Cursor<uint8_t>;

// A block of code made (`block` its bytes) / freed (its first byte; nothing for null). A block noted over others
// replaces them (they were freed without being forgotten).
void note_code_block(std::span<const uint8_t> block);
void forget_code_block(const void* block);
// NOT ORIGINAL: forget_code_block for a script's own code (cached, or a script value) - the stored randoms of its
// Randoms go with it. The original leaves them, keyed by the freed address, for whatever code is allocated there next
// to take over (which depends on the allocator); here a script's RandomNoRepeat / RandomPermute state lasts as long
// as its code, as a TypeScript site's lasts as long as its module. (A running script's private copy keeps the
// original's behaviour: forget_code_block.)
void forget_script_code(const void* block);
// The cursor at `place` over the block that holds it (null: an empty cursor). A pointer into no block noted ends the
// game with a report.
Tokens tokens_at(const uint8_t* place);
// The same for a pointer that is most likely in `near`'s block (a loop's start, a jump's target): no look-up then.
Tokens tokens_at(Tokens near, const uint8_t* place);
// The code from `from` up to `to` (in the same block).
std::span<const uint8_t> code_between(Tokens from, Tokens to);
// Counted up whenever a block is noted or forgotten: a cursor kept since may be over a block that is gone.
uint32_t code_blocks_generation();

} // namespace script
