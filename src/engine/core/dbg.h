#pragma once

namespace debug {

// printf-style message to the debugger output (OutputDebugString). Messages are limited to 1023 characters.
// Always returns -1.
int printf(const char* format, ...);

} // namespace debug
