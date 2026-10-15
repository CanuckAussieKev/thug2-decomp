#pragma once

namespace Dbg {

// printf-style message to the debugger output (OutputDebugString). Messages are limited to 1023 characters.
// Always returns -1.
int Printf(const char* format, ...);

} // namespace Dbg
