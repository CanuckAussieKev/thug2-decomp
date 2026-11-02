#include "engine/core/dbg.h"

#include <windows.h>

namespace debug {

// 0x00401C30 (printf, dbg.h: formatting, then this)
void output_text(const char* text) { OutputDebugStringA(text); }

} // namespace debug
