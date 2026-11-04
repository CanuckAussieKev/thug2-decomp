#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "formats/lzss.h"
#include "engine/core/checked_index.h"

namespace lzss {

constexpr int kFlagsMarker = 0x100;         // the flag byte is used up when this bit is shifted down to the low byte
constexpr unsigned kFlagsFresh = 0xFF00U;   // the high byte counts the eight flags off

// NOT ORIGINAL - the bounded form of decode (0x004014F0) and of the archives' copy of it (0x005B7DF0): the same
// window, flags and references, but the input is read only within its size and the output written only within its
// capacity. A valid stream decodes to exactly the same bytes.
bool unpack(const uint8_t* in_bytes, size_t in_size, uint8_t* out_bytes, size_t out_capacity, size_t* written)
{
    const std::span<const uint8_t> in(in_bytes, in_size);
    const std::span<uint8_t> out(out_bytes, out_capacity);
    constexpr int kWindow = 4096;
    constexpr int kLongest = 18;
    std::array<uint8_t, kWindow> window{};
    std::memset(window.data(), ' ', kWindow - kLongest);
    int r = kWindow - kLongest;
    unsigned int flags = 0;
    size_t read = 0;
    size_t put = 0;
    bool ok = true;
    for (;;) {
        flags >>= 1;
        if ((flags & kFlagsMarker) == 0U) {
            if (read == in_size) {
                break;
            }
            flags = core::at(in, read++) | kFlagsFresh; // the high byte counts the 8 flags off
        }
        if ((flags & 1) != 0U) {
            if (read == in_size) {
                break;
            }
            if (put == out_capacity) {
                ok = false;
                break;
            }
            const uint8_t c = core::at(in, read++);
            core::at(out, put++) = c;
            core::at(window, r) = c;
            r = (r + 1) & (kWindow - 1);
        } else {
            if (read == in_size) {
                break;
            }
            if (in_size - read < 2) { // a reference cut short (the original reads past the end, then on for ever)
                ok = false;
                break;
            }
            const int position = core::at(in, read) | ((core::at(in, read + 1) & 0xF0) << 4);
            const int last = (core::at(in, read + 1) & 0x0F) + 2;
            read += 2;
            if (out_capacity - put < static_cast<size_t>(last) + 1) {
                ok = false;
                break;
            }
            for (int k = 0; k <= last; ++k) {
                const uint8_t c = core::at(window, (position + k) & (kWindow - 1));
                core::at(out, put++) = c;
                core::at(window, r) = c;
                r = (r + 1) & (kWindow - 1);
            }
        }
    }
    if (written) {
        *written = put;
    }
    return ok;
}

} // namespace lzss
