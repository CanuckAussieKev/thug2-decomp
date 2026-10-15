#pragma once

namespace Str {

// Convert ASCII letters in place; other bytes are left alone.
void LowerCase(char* string);
void UpperCase(char* string);

// First occurrence of `needle` in `haystack`, ignoring ASCII case. Returns null if not found, and also for
// an empty needle (unlike strstr).
const char* FindCaseless(const char* haystack, const char* needle);

} // namespace Str
