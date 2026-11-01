#pragma once

#include <cstdio>
#include <array>

namespace debug {

// A message to the debugger output (OutputDebugString), as it is.
void output_text(const char* text);

// printf-style message to the debugger output (OutputDebugString). Messages are limited to 1023 characters.
// Always returns -1.
template <typename... Args>
int printf(const char* format, Args... args)
{
    // The original uses unbounded vsprintf into a 1 KB buffer; this version truncates instead of overflowing.
    std::array<char, 1024> message{};
    std::snprintf(message.data(), sizeof(message), format, args...);
    output_text(message.data());
    return -1;
}

} // namespace debug
