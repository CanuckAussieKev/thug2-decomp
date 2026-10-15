#include "game/core/dbg.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

#include "hook/hook.h"

namespace Dbg {

// 0x00401C30
int Printf(const char* format, ...)
{
    // The original uses unbounded vsprintf into a 1 KB buffer; this version truncates instead of overflowing.
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    OutputDebugStringA(message);
    return -1;
}

} // namespace Dbg

REPLACE(0x00401C30, Dbg::Printf);
