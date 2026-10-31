// The memory dump's text buffer (0x0059ED60): the pool-usage dump (0x00448330) writes its lines through it.

#include "engine/core/dump_text.h"

#include <cstring>

#include "engine/file/file_load.h"

namespace memory {

namespace {

// The buffer (0x007CE7B8, up to 0x007CEBA8), the write position (0x007CE7B0) and the file it's flushed to
// (0x007CEBB8 - nothing in this build ever opens it, so it stays null).
char g_dump_buffer[0x3F0];
char* g_text_dump_cursor;
file::Hdl g_dump_file;

char* dump_buffer() { return g_dump_buffer; }
char* dump_buffer_end() { return g_dump_buffer + sizeof(g_dump_buffer); }
char*& dump_write_cursor() { return g_text_dump_cursor; }
file::Hdl dump_file() { return g_dump_file; }

} // namespace

// 0x0059ED60
void __cdecl dump_text(const char* text)
{
    int length = static_cast<int>(std::strlen(text));
    char* position = dump_write_cursor();
    // Flushed when the text won't fit (checked before newlines become CR LF: they can run past the end).
    if (reinterpret_cast<int>(position + length) > reinterpret_cast<int>(dump_buffer_end()) && position != dump_buffer()) {
        file::emit_out(dump_buffer(), static_cast<int>(position - dump_buffer()), 1, dump_file());
        position = dump_buffer();
        dump_write_cursor() = position;
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
    dump_write_cursor() = position;
}

} // namespace Mem
