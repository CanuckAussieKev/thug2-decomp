#pragma once

#include <cstdint>
#include <span>

#include "engine/core/cursor.h"

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
// The cursor at `place` over the block that holds it (null: an empty cursor). A pointer into no block noted ends the
// game with a report.
Tokens tokens_at(const uint8_t* place);

} // namespace script
