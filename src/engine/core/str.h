#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <limits>
#include <span>

#include <gsl/assert>

#include "engine/core/cursor.h"

namespace text {

// UTF-8: the bytes of a code point (the front end's text, the editor's text fields).
constexpr uint32_t kUtf8OneByteLimit = 0x80;  // code points below this take one byte,
constexpr uint32_t kUtf8TwoByteLimit = 0x800; // below this two (above, three)
constexpr int kUtf8LeadTwo = 0xC0;            // the lead byte of two,
constexpr int kUtf8LeadThree = 0xE0;          // and of three
constexpr int kUtf8Continuation = 0x80;       // a continuation byte is 10xxxxxx:
constexpr int kUtf8ContinuationMask = 0xC0;   // the bits that tell one
constexpr int kUtf8PayloadBits = 6;           // the payload bits a continuation byte carries
constexpr uint32_t kUtf8PayloadMask = 0x3F;

// Convert ASCII letters in place; other bytes are left alone.
void to_lower_text(char* string);
void to_capitals(char* string);

// A text searched and the text looked for (any case).
struct CaselessSearch {
    const char* haystack{};
    const char* needle{};
};

// First occurrence of `needle` in `haystack`, ignoring ASCII case. Returns null if not found, and also for
// an empty needle (unlike strstr).
const char* find_caseless(CaselessSearch search);

// Compare ignoring ASCII case: -1, 0 or 1 (characters compare as signed chars).
int compare_caseless(const char* a, const char* b);

// `value` as text with thousands separators chosen by `language` (core::kLanguage*: a space for French, a dot for
// Spanish, German and Italian, else a comma), e.g. "1,234,567". The callers pass the game's language setting
// (game_config::get_language(), which the original read here). Returns a static buffer in the exe, overwritten by
// the next call.
const char* format_with_separators(int value, int language);

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

// NOT ORIGINAL - the original's strcpy into a buffer whose size we know: `source` and its terminator copied into
// `dest`. A text that doesn't fit ends the game with a report (the original wrote on past the buffer).
inline void copy_into(std::span<char> dest, const char* source)
{
    const std::size_t length = std::strlen(source);
    Expects(length < dest.size());
    std::memmove(dest.data(), source, length + 1);
}
// NOT ORIGINAL - the original's strcat into a buffer whose size we know: `source` copied onto the end of `dest`'s
// text (checked as copy_into; `dest` must hold a terminated text).
inline void append_into(std::span<char> dest, const char* source)
{
    const auto end = std::ranges::find(dest, '\0');
    Expects(end != dest.end());
    copy_into(dest.subspan(static_cast<std::size_t>(end - dest.begin())), source);
}
// NOT ORIGINAL - a text from outside the game (another player, the online service) copied into a buffer of the
// game's: cut to fit and always terminated (the original's strcpy wrote on past the buffer when it was too long).
inline void copy_cut(std::span<char> dest, const char* source)
{
    Expects(!dest.empty());
    const std::size_t length = std::min(std::strlen(source), dest.size() - 1);
    std::memmove(dest.data(), source, length);
    dest.subspan(length).front() = '\0';
}

} // namespace text
