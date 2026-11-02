#pragma once

#include <cstddef>
#include <cstring>
#include <limits>
#include <span>

#include "engine/core/cursor.h"

namespace text {

// Convert ASCII letters in place; other bytes are left alone.
void to_lower_text(char* string);
void to_capitals(char* string);

// First occurrence of `needle` in `haystack`, ignoring ASCII case. Returns null if not found, and also for
// an empty needle (unlike strstr).
const char* find_caseless(const char* haystack, const char* needle);

// Compare ignoring ASCII case: -1, 0 or 1 (characters compare as signed chars).
int compare_caseless(const char* a, const char* b);

// `value` as text with thousands separators chosen by the game language (space, dot or comma), e.g.
// "1,234,567". Returns a static buffer in the exe, overwritten by the next call.
const char* format_with_separators(int value);

// Value of one base-32 digit: '0'-'9' -> 0-9, 'A'-'V' / 'a'-'v' -> 10-31, anything else -> 0.
int hex_digit_value(char digit);

// Copy the next word from `cursor` into `word` (the original's buffer had no limit; one that is too small ends the
// game with a report) and advance past it. The word ends at (and keeps) the next space.
// Escapes: "\_" becomes a space, "\n" ends the line, any other "\x" is copied as-is.
// Returns 0 for a word, 1 at an end-of-line escape, 2 at the end of the text.
int next_word(std::span<char> word, core::CStringCursor& cursor);

// NOT ORIGINAL - the original's inline strcpy: `source` copied into `dest` up to and including its terminator, with
// no length limit (the destination is the original's buffer, sized as it was). Returns `dest`.
inline char* copy_unbounded(char* dest, const char* source)
{
    // (memmove: a forward copy a byte at a time, as the original, whichever way the two overlap)
    std::memmove(dest, source, std::strlen(source) + 1);
    return dest;
}
// The original's inline strcat: `source` copied onto the end of `dest`'s text, with no length limit.
inline char* append_unbounded(char* dest, const char* source)
{
    // (the original's buffer has no extent we know: the span reaches as far as indexing does)
    const std::span<char> from_end = std::span<char>(dest, std::numeric_limits<std::ptrdiff_t>::max()).subspan(std::strlen(dest));
    copy_unbounded(from_end.data(), source);
    return dest;
}

} // namespace text
