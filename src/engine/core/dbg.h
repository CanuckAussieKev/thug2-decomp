#pragma once

#include <array>
#include <string_view>

#include "engine/core/format.h"

namespace debug {

// A message to the debugger output (OutputDebugString), as it is.
void output_text(const char* text);

// printf-style message to the debugger output (OutputDebugString). Messages are limited to 1023 characters.
// Always returns -1. The format is checked against the arguments when it's compiled (text::format).
template <typename... Args>
int printf(text::FormatFor<Args...> format, Args... args)
{
    // The original uses unbounded vsprintf into a 1 KB buffer; this version truncates instead of overflowing.
    std::array<char, 1024> message{};
    text::format_to(message, format, args...);
    output_text(message.data());
    return -1;
}

// A text printed as its own format, as the original's printf(text) did (a message passed in, a line made earlier):
// its conversions, if it has any, read nothing (0 / "") - the original read the stack. Always returns -1.
inline int printf_text(std::string_view text)
{
    std::array<char, 1024> message{};
    text::format_data_to(message, text);
    output_text(message.data());
    return -1;
}

} // namespace debug
