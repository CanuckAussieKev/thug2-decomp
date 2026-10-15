#pragma once

namespace Str {

// Convert ASCII letters in place; other bytes are left alone.
void LowerCase(char* string);
void UpperCase(char* string);

// First occurrence of `needle` in `haystack`, ignoring ASCII case. Returns null if not found, and also for
// an empty needle (unlike strstr).
const char* FindCaseless(const char* haystack, const char* needle);

// Compare ignoring ASCII case: -1, 0 or 1 (characters compare as signed chars).
int CompareCaseless(const char* a, const char* b);

// Value of one base-32 digit: '0'-'9' -> 0-9, 'A'-'V' / 'a'-'v' -> 10-31, anything else -> 0.
int DehexifyDigit(const char* digit);

// Copy the next word from `cursor` into `out` and advance past it. The word ends at (and keeps) the next space.
// Escapes: "\_" becomes a space, "\n" ends the line, any other "\x" is copied as-is.
// Returns 0 for a word, 1 at an end-of-line escape, 2 at the end of the text.
int NextWord(char* out, const char*& cursor);

} // namespace Str
