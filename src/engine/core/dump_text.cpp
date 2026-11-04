// The memory dump's text buffer (0x0059ED60): the pool-usage dump (0x00448330) writes its lines through it.

#include "engine/core/dump_text.h"

#include <cstring>
#include <array>

#include "engine/file/file_load.h"
#include <string_view>
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace memory {

namespace {
constexpr size_t kDumpBufferSize = 0x3F0;

// NOT ORIGINAL grouping - the text dump's buffer, how much of it is used and its file. Made on first use, never
// destroyed (dump_state()).
struct DumpState {
    // The buffer (0x007CE7B8, up to 0x007CEBA8), the write position (0x007CE7B0) and the file it's flushed to
    // (0x007CEBB8 - nothing in this build ever opens it, so it stays null).
    std::array<char, kDumpBufferSize> dump_buffer_{};
    size_t text_dump_used_{}; // (the write position as a count of bytes in the buffer)
    file::Hdl dump_file_{};
};

DumpState& dump_state()
{
    static core::NoDestroy<DumpState> state;
    return state.get();
}

std::array<char, kDumpBufferSize>& dump_buffer() { return dump_state().dump_buffer_; }
size_t& dump_write_cursor() { return dump_state().text_dump_used_; }
file::Hdl dump_file() { return dump_state().dump_file_; }

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
    if (length <= 0) {
        return;
    }
    // (the newlines made into CR LF could run on past the end of the buffer, over the globals after it, in the
    // original: the bytes that do not fit are dropped here)
    auto put = [&](char character) {
        if (position < dump_buffer().size()) {
            core::at(dump_buffer(), position++) = character;
        }
    };
    for (const char character : std::string_view(text, static_cast<size_t>(length))) {
        if (character == '\n') {
            put('\r');
            put('\n');
        } else {
            put(character);
        }
    }
    dump_write_cursor() = position;
}

} // namespace memory
