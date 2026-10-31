// The memory dump's text buffer (0x0059ED60): the pool-usage dump (0x00448330) writes its lines through it.

#include "engine/core/dump_text.h"

#include <cstring>

#include "engine/file/file_load.h"

namespace MemSys {

namespace {

// The buffer (0x007CE7B8, up to 0x007CEBA8), the write position (0x007CE7B0) and the file it's flushed to
// (0x007CEBB8 - nothing in this build ever opens it, so it stays null).
char g_dumpBuffer[0x3F0];
char* g_textDumpCursor;
FileLoad::Hdl g_dumpFile;

char* DumpBuffer() { return g_dumpBuffer; }
char* DumpBufferEnd() { return g_dumpBuffer + sizeof(g_dumpBuffer); }
char*& DumpWriteCursor() { return g_textDumpCursor; }
FileLoad::Hdl DumpFile() { return g_dumpFile; }

} // namespace

// 0x0059ED60
void __cdecl DumpText(const char* text)
{
    int length = static_cast<int>(std::strlen(text));
    char* position = DumpWriteCursor();
    // Flushed when the text won't fit (checked before newlines become CR LF: they can run past the end).
    if (reinterpret_cast<int>(position + length) > reinterpret_cast<int>(DumpBufferEnd()) && position != DumpBuffer()) {
        FileLoad::EmitOut(DumpBuffer(), static_cast<int>(position - DumpBuffer()), 1, DumpFile());
        position = DumpBuffer();
        DumpWriteCursor() = position;
    }
    if (length <= 0)
        return;
    for (; length > 0; --length, ++text) {
        if (*text == '\n') {
            *position++ = '\r';
            *position++ = '\n';
        } else {
            *position++ = *text;
        }
    }
    DumpWriteCursor() = position;
}

} // namespace Mem
