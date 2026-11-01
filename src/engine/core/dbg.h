#pragma once

#include <cstdio>

namespace debug {

// A message to the debugger output (OutputDebugString), as it is.
void output_text(const char* text);

// printf-style message to the debugger output (OutputDebugString). Messages are limited to 1023 characters.
// Always returns -1.
template <typename... Args>
int printf(const char* format, Args... args)
{
    // The original uses unbounded vsprintf into a 1 KB buffer; this version truncates instead of overflowing.
    char message[1024];
    std::snprintf(message, sizeof(message), format, args...);
    output_text(message);
    return -1;
}

} // namespace debug
