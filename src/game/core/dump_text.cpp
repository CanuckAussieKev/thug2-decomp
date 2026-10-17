// The memory dump's text buffer (0x0059ED60): the pool-usage dump (0x00448330) writes its lines through it.

#include "game/core/dump_text.h"

#include <cstring>

#include "game/file/file.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mem {

namespace {

// The buffer (0x007CE7B8, up to 0x007CEBA8), the write position (0x007CE7B0) and the file it's flushed to
// (0x007CEBB8 - nothing in this build ever opens it, so it stays null).
char* DumpBuffer() { return &GameVariable<char>(0x007CE7B8); }
constexpr uint32_t kDumpBufferEnd = 0x007CEBA8;
char*& DumpPosition() { return GameVariable<char*>(0x007CE7B0); }
File::Handle DumpFile() { return GameVariable<File::Handle>(0x007CEBB8); }

} // namespace

// 0x0059ED60
void __cdecl DumpText(const char* text)
{
    int length = static_cast<int>(std::strlen(text));
    char* position = DumpPosition();
    // Flushed when the text won't fit (checked before newlines become CR LF: they can run past the end).
    if (reinterpret_cast<int>(position + length) > static_cast<int>(kDumpBufferEnd) && position != DumpBuffer()) {
        File::Write(DumpBuffer(), static_cast<int>(position - DumpBuffer()), 1, DumpFile());
        position = DumpBuffer();
        DumpPosition() = position;
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
    DumpPosition() = position;
}

} // namespace Mem

REPLACE(0x0059ED60, Mem::DumpText);
