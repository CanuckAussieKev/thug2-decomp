#pragma once

#include <cstdint>

namespace Util {

// Reads or writes values packed at bit level, a 32-bit word at a time.
// Only setup/teardown is decompiled so far; the read/write functions follow at 0x004010D0 onwards.
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

    uint32_t* m_cursor;   // next word to read/write
    uint32_t* m_start;    // start of the data
    uint32_t m_buffer;    // bits waiting to be written / not yet read
    int m_bitsFree;       // free bits left in m_buffer (32 = empty when writing)
    int m_size;           // size of the data in bytes
    int m_bitsUsed;       // bits read or written so far
};

} // namespace Util
