#include <cstddef>
#include <cstdint>
#include <cstring>

#include "formats/lzss.h"

namespace lzss {

// NOT ORIGINAL - the bounded form of decode (0x004014F0) and of the archives' copy of it (0x005B7DF0): the same
// window, flags and references, but the input is read only within its size and the output written only within its
// capacity. A valid stream decodes to exactly the same bytes.
bool unpack(const uint8_t* in, size_t in_size, uint8_t* out, size_t out_capacity, size_t* written)
{
    constexpr int kWindow = 4096;
    constexpr int kLongest = 18;
    uint8_t window[kWindow];
    std::memset(window, ' ', kWindow - kLongest);
    int r = kWindow - kLongest;
    unsigned int flags = 0;
    size_t read = 0;
    size_t put = 0;
    bool ok = true;
    for (;;) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            if (read == in_size)
                break;
            flags = in[read++] | 0xFF00u; // the high byte counts the 8 flags off
        }
        if (flags & 1) {
            if (read == in_size)
                break;
            if (put == out_capacity) {
                ok = false;
                break;
            }
            const uint8_t c = in[read++];
            out[put++] = c;
            window[r] = c;
            r = (r + 1) & (kWindow - 1);
        } else {
            if (read == in_size)
                break;
            if (in_size - read < 2) { // a reference cut short (the original reads past the end, then on for ever)
                ok = false;
                break;
            }
            const int position = in[read] | ((in[read + 1] & 0xF0) << 4);
            const int last = (in[read + 1] & 0x0F) + 2;
            read += 2;
            if (out_capacity - put < static_cast<size_t>(last) + 1) {
                ok = false;
                break;
            }
            for (int k = 0; k <= last; ++k) {
                const uint8_t c = window[(position + k) & (kWindow - 1)];
                out[put++] = c;
                window[r] = c;
                r = (r + 1) & (kWindow - 1);
            }
        }
    }
    if (written)
        *written = put;
    return ok;
}

} // namespace lzss
