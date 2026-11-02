// The memory dump's text buffer (0x0059ED60): the pool-usage dump (0x00448330) writes its lines through it.

#include "engine/core/dump_text.h"

#include <cstring>
#include <array>

#include "engine/file/file_load.h"
#include <string_view>
#include "engine/core/checked_index.h"

namespace memory {

namespace {

// The buffer (0x007CE7B8, up to 0x007CEBA8), the write position (0x007CE7B0) and the file it's flushed to
// (0x007CEBB8 - nothing in this build ever opens it, so it stays null).
std::array<char, 0x3F0> g_dump_buffer;
size_t g_text_dump_used; // (the write position as a count of bytes in the buffer)
file::Hdl g_dump_file;

std::array<char, 0x3F0>& dump_buffer() { return g_dump_buffer; }
size_t& dump_write_cursor() { return g_text_dump_used; }
file::Hdl dump_file() { return g_dump_file; }

} // namespace

// 0x0059ED60
void dump_text(const char* text)
{
    const int length = static_cast<int>(std::strlen(text));
    size_t position = dump_write_cursor();
    // Flushed when the text won't fit (checked before newlines become CR LF: they can run past the end).
    if (position + static_cast<size_t>(length) > dump_buffer().size() && position != 0) {
        file::emit_out(dump_buffer().data(), static_cast<int>(position), 1, dump_file());
        position = 0;
        dump_write_cursor() = position;
    }
    if (length <= 0)
        return;
    // (the newlines made into CR LF could run on past the end of the buffer, over the globals after it, in the
    // original: the bytes that do not fit are dropped here)
    auto put = [&](char c) {
        if (position < dump_buffer().size())
            core::at(dump_buffer(), position++) = c;
    };
    for (const char c : std::string_view(text, static_cast<size_t>(length))) {
        if (c == '\n') {
            put('\r');
            put('\n');
        } else {
            put(c);
        }
    }
    dump_write_cursor() = position;
}

} // namespace memory
