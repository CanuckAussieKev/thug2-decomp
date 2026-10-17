// The network library's message compression. See net_compress.h.

#include "game/net/net_compress.h"

#include <cstring>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/util/lzss.h"
#include "hook/hook.h"

namespace Net {

namespace {

constexpr uint8_t kMsgCompressed = 0x12;
constexpr int kMaxRun = 0x81;       // a run's bytes
constexpr int kMaxLiteral = 0x80;   // a literal's bytes
constexpr int kRunCode = 0x80;      // a run of n bytes: 0x80 + n - 2
constexpr int kMethodRle = 1;
constexpr int kMethodLzss = 2;

} // namespace

// 0x00454A60
int RleCompress(const uint8_t* in, int size, uint8_t* out, int /*outSize*/)
{
    uint8_t* start = out;
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
int RleDecompress(const uint8_t* in, int size, uint8_t* out, int* outSize)
{
    int written = 0;
    while (size != 0) {
        uint8_t code = *in++;
        --size;
        if (code < kRunCode) {
            // code + 1 bytes as they are. (A code longer than what's left reads on past the end, as the
            // original does: `size` goes below 0 and the loop doesn't stop there.)
            uint8_t copied = 0;
            do {
                if (written >= *outSize)
                    return -1;
                *out++ = *in++;
                ++written;
                --size;
                ++copied;
            } while (copied <= code);
        } else {
            int count = (code & 0x7F) + 2;
            uint8_t copied = 0;
            do {
                if (written >= *outSize)
                    return -1;
                *out++ = *in;
                ++written;
                ++copied;
            } while (copied < count);
            ++in;
            --size;
        }
    }
    *outSize = written;
    return 0;
}

// 0x00454E30
uint8_t* CompressMessage(uint8_t* msgId, uint16_t* length, const void* data, char method)
{
    // The whole message - id, length, data - is compressed.
    int size = static_cast<uint16_t>(*length + 3); // (16 bits: wraps for a length of 0xFFFD up)
    int compressedSize = 0;
    auto* message = static_cast<uint8_t*>(Mem::New(size));
    message[0] = *msgId;
    std::memcpy(message + 1, length, 2);
    std::memcpy(message + 3, data, *length);
    Dbg::Printf("*** Sending compressed message %d. Old size : %d ", *msgId, *length);
    int maxSize = size * 4 / 3 + size;
    auto* compressed = static_cast<uint8_t*>(Mem::New(maxSize + 3));
    compressed[0] = static_cast<uint8_t>(method);
    uint16_t expandedSize = static_cast<uint16_t>(size);
    std::memcpy(compressed + 1, &expandedSize, 2);
    if (method == kMethodRle) {
        compressedSize = RleCompress(message, size, compressed + 3, maxSize);
        Dbg::Printf("RLE : New payload len : %d, New size : %d\n", size, compressedSize);
    } else if (method == kMethodLzss) {
        compressedSize = Lzss::Encode(message, compressed + 3, size, false);
        Dbg::Printf("LZSS : New payload len : %d, New size : %d\n", size, compressedSize);
    }
    // (Another method compresses to nothing: the 3-byte header "wins".)
    Mem::Free(message);
    if (static_cast<uint32_t>(compressedSize) < *length) {
        *length = static_cast<uint16_t>(compressedSize + 3);
        *msgId = kMsgCompressed;
        return compressed;
    }
    Dbg::Printf("*** Compressed message was larger. Defaulting back to uncompressed version\n");
    Mem::Free(compressed);
    return nullptr;
}

} // namespace Net

REPLACE(0x00454A60, Net::RleCompress);
REPLACE(0x00454C20, Net::RleDecompress);
REPLACE(0x00454E30, Net::CompressMessage);
