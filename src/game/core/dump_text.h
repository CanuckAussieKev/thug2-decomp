#pragma once

namespace Mem {

// 0x0059ED60: append `text` to the memory dump's buffer, newlines as CR LF; when it won't fit, the buffer is
// first written to the dump file (never opened in this build).
void __cdecl DumpText(const char* text);

} // namespace Mem
