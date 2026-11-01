// The network library's message compression. See net_compress.h.

#include "engine/net/net_compress.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/net/net_lzham.h"
#include "engine/net/thugpro_peer.h"
#include "engine/util/lzss.h"

namespace net {

namespace {

constexpr uint8_t kMsgCompressed = 0x12;
constexpr int kMaxRun = 0x81;     // a run's bytes
constexpr int kMaxLiteral = 0x80; // a literal's bytes
constexpr int kRunCode = 0x80;    // a run of n bytes: 0x80 + n - 2
constexpr int kMethodRle = 1;
constexpr int kMethodLzss = 2;

} // namespace

// 0x00454A60
int rle_compress(const uint8_t* in, int size, uint8_t* out, int /*out_size*/)
{
    const uint8_t* start = out;
    if (size == 0)
        return 0;
    // The two bytes in hand: `current` and `next` (both taken from the input).
    uint8_t current = *in++;
    if (--size == 0)
        return 0;
    uint8_t next = *in++;
    --size;
    for (;;) {
        if (current == next) {
            // A run: the bytes equal to `current`.
            int count = 2;
            while (size != 0 && current == next && count < kMaxRun) {
                next = *in++;
                --size;
                ++count;
            }
            if (current == next) {
                *out++ = static_cast<uint8_t>(kRunCode + count - 2);
                *out++ = current;
                if (size == 0)
                    return static_cast<int>(out - start);
                current = *in++;
                --size;
            } else {
                // `next` ended it: it's the next run's or literal's first byte.
                *out++ = static_cast<uint8_t>(kRunCode + count - 3);
                *out++ = current;
                current = next;
            }
        } else {
            // A literal: bytes up to where two in a row are equal (they start a run) or the most.
            uint8_t bytes[kMaxLiteral + 4];
            bytes[0] = current;
            bytes[1] = next;
            int count = 2;
            while (size != 0 && bytes[count - 2] != bytes[count - 1] && count < kMaxLiteral) {
                bytes[count++] = *in++;
                --size;
            }
            if (bytes[count - 2] == bytes[count - 1]) {
                // The last two start a run.
                *out++ = static_cast<uint8_t>(count - 3);
                if (count - 2 > 0) {
                    std::memcpy(out, bytes, count - 2);
                    out += count - 2;
                }
                current = next = bytes[count - 2];
                continue;
            }
            *out++ = static_cast<uint8_t>(count - 1);
            std::memcpy(out, bytes, count);
            out += count;
            if (size == 0)
                return static_cast<int>(out - start);
            current = *in++;
            --size;
        }
        if (size == 0) {
            // One byte left: a literal of one.
            *out++ = 0;
            *out++ = current;
            return static_cast<int>(out - start);
        }
        next = *in++;
        --size;
    }
}

// 0x00454C20
int rle_decompress(const uint8_t* in, int size, uint8_t* out, int* out_size)
{
    int written = 0;
    while (size != 0) {
        const uint8_t code = *in++;
        --size;
        if (code < kRunCode) {
            // code + 1 bytes as they are. (A code longer than what's left reads on past the end, as the
            // original does: `size` goes below 0 and the loop doesn't stop there.)
            uint8_t copied = 0;
            do {
                if (written >= *out_size)
                    return -1;
                *out++ = *in++;
                ++written;
                --size;
                ++copied;
            } while (copied <= code);
        } else {
            const int count = (code & 0x7F) + 2;
            uint8_t copied = 0;
            do {
                if (written >= *out_size)
                    return -1;
                *out++ = *in;
                ++written;
                ++copied;
            } while (copied < count);
            ++in;
            --size;
        }
    }
    *out_size = written;
    return 0;
}

// 0x00454E30
uint8_t* compress_message(uint8_t* msg_id, uint16_t* length, const void* data, char method)
{
    // NOT ORIGINAL: in the THUGPRO peer mode LZSS goes as THUGPRO's LZHAM. THUGPRO's receivers don't expand LZSS
    // (its patch over handle_compressed_messages' method test), and its senders turn THUG2's two LZSS messages - the join
    // request (80) and player create (34) - into LZHAM (TP-R11).
    if (method == kMethodLzss && online::thugpro::peer_mode())
        method = static_cast<char>(lzham::kMethod);
    // The whole message - id, length, data - is compressed.
    const int size = static_cast<uint16_t>(*length + 3); // (16 bits: wraps for a length of 0xFFFD up)
    int compressed_size = 0;
    auto* message = static_cast<uint8_t*>(memory::alloc(size));
    message[0] = *msg_id;
    std::memcpy(message + 1, length, 2);
    std::memcpy(message + 3, data, *length);
    debug::printf("*** Sending compressed message %d. Old size : %d ", *msg_id, *length);
    const int max_size = size * 4 / 3 + size;
    auto* compressed = static_cast<uint8_t*>(memory::alloc(max_size + 3));
    compressed[0] = static_cast<uint8_t>(method);
    auto expanded_size = static_cast<uint16_t>(size);
    std::memcpy(compressed + 1, &expanded_size, 2);
    if (method == kMethodRle) {
        compressed_size = rle_compress(message, size, compressed + 3, max_size);
        debug::printf("RLE : New payload len : %d, New size : %d\n", size, compressed_size);
    } else if (method == kMethodLzss) {
        compressed_size = lzss::pack_up(message, compressed + 3, size, false);
        debug::printf("LZSS : New payload len : %d, New size : %d\n", size, compressed_size);
    } else if (method == lzham::kMethod) {
        // NOT ORIGINAL: THUGPRO's LZHAM (TP-R11). A stream that fails or doesn't fit counts as "no smaller".
        const size_t lzham =
            lzham::compress(message, static_cast<size_t>(size), compressed + 3, static_cast<size_t>(max_size));
        compressed_size = lzham != 0 ? static_cast<int>(lzham) : size;
        debug::printf("LZHAM : New payload len : %d, New size : %d\n", size, compressed_size);
    }
    // (Another method compresses to nothing: the 3-byte header "wins".)
    memory::dealloc(message);
    if (static_cast<uint32_t>(compressed_size) < *length) {
        *length = static_cast<uint16_t>(compressed_size + 3);
        *msg_id = kMsgCompressed;
        return compressed;
    }
    debug::printf("*** Compressed message was larger. Defaulting back to uncompressed version\n");
    memory::dealloc(compressed);
    return nullptr;
}

} // namespace net
