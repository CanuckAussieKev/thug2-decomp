#pragma once

#include <cstdint>

namespace Util {

// Reads or writes values packed at bit level, a 32-bit word at a time, least significant bits first.
class BitStream {
public:
    BitStream();

    // Prepare to read `size` bytes from `data`. The word buffer starts empty, so the first read loads it.
    void SetInputData(void* data, int size);
    // Prepare to write into `data` (capacity `size` bytes).
    void SetOutputData(void* data, int size);
    // Bytes touched so far, rounding a partial byte up.
    int GetByteCount() const;
    // Write out a partially filled word (writing only).
    void Flush();

    // Append the low `count` bits of `value` (count 0..32). Bits of `value` above `count` are not masked
    // off, matching the original; callers pass clean values.
    void WriteBits(uint32_t value, int count);
    // Read `count` bits (0..32) as an unsigned value.
    uint32_t ReadBits(int count);
    // Read `count` bits and sign-extend from the top bit read. (Quirk kept from the original: for count 32,
    // any negative value comes back as -1.)
    int32_t ReadSignedBits(int count);

    uint32_t* m_cursor;   // next word to read/write
    uint32_t* m_start;    // start of the data
    uint32_t m_buffer;    // bits waiting to be written / not yet read
    int m_bitsLeft;       // writing: free bits in m_buffer (32 = empty); reading: unread bits (0 = empty)
    int m_size;           // size of the data in bytes
    int m_bitsUsed;       // bits read or written so far
};

} // namespace Util
