#include "game/util/bitstream.h"

#include <new>

#include "hook/hook.h"

namespace Util {

// 0x00401050
BitStream::BitStream() : m_cursor(nullptr), m_start(nullptr), m_buffer(0), m_bitsLeft(32), m_size(0), m_bitsUsed(0)
{
    // The original leaves m_start uninitialised; zeroing it here is harmless.
}

// 0x00401070
void BitStream::SetInputData(void* data, int size)
{
    m_cursor = static_cast<uint32_t*>(data);
    m_start = static_cast<uint32_t*>(data);
    m_size = size;
    m_bitsLeft = 0;
    m_bitsUsed = 0;
}

// 0x00401090
void BitStream::SetOutputData(void* data, int size)
{
    m_cursor = static_cast<uint32_t*>(data);
    m_start = static_cast<uint32_t*>(data);
    m_size = size;
    m_bitsLeft = 32;
    m_bitsUsed = 0;
}

// 0x004010C0
int BitStream::GetByteCount() const
{
    return (m_bitsUsed + 7) >> 3;
}

// 0x00401180
void BitStream::Flush()
{
    if (m_bitsLeft != 32) {
        *m_cursor++ = m_buffer;
        m_buffer = 0;
        m_bitsLeft = 32;
    }
}

// 0x004010D0
void BitStream::WriteBits(uint32_t value, int count)
{
    m_bitsUsed += count;
    if (count > m_bitsLeft) {
        // Fill the rest of the current word, write it out, and continue with the remaining bits.
        int used = 32 - m_bitsLeft;
        m_buffer = (m_buffer & ((1u << used) - 1)) | (value << used);
        *m_cursor++ = m_buffer;
        value >>= m_bitsLeft;
        count -= m_bitsLeft;
        m_buffer = 0;
        m_bitsLeft = 32;
    }
    int used = 32 - m_bitsLeft;
    m_bitsLeft -= count;
    m_buffer = (m_buffer & ((1u << used) - 1)) | (value << used);
    if (m_bitsLeft == 0) {
        *m_cursor++ = m_buffer;
        m_buffer = 0;
        m_bitsLeft = 32;
    }
}

// 0x004011B0
uint32_t BitStream::ReadBits(int count)
{
    m_bitsUsed += count;
    if (m_bitsLeft == 0) {
        m_buffer = *m_cursor++;
        m_bitsLeft = 32;
    }

    int available = m_bitsLeft;
    if (count <= available) {
        m_bitsLeft = available - count;
        uint32_t bits = m_buffer;
        if (count == 32)
            return bits;
        m_buffer = bits >> count;
        return bits & ((1u << count) - 1);
    }

    // The value straddles two words: low bits from this one, the rest from the next.
    uint32_t low = m_buffer;
    uint32_t next = *m_cursor++;
    int rest = count - available;
    m_buffer = next >> rest;
    m_bitsLeft = 32 - rest;
    return (low & ((1u << available) - 1)) | ((next & ((1u << rest) - 1)) << available);
}

// 0x00401260
int32_t BitStream::ReadSignedBits(int count)
{
    uint32_t bits = ReadBits(count);
    // Shift counts are taken mod 32, as the original's x86 shifts do.
    if (bits & (1u << ((count - 1) & 31)))
        bits |= ~((1u << (count & 31)) - 1);
    return static_cast<int32_t>(bits);
}

} // namespace Util

namespace {

struct BitStreamAdapter {
    Util::BitStream* Construct()
    {
        return new (this) Util::BitStream();
    }
};

} // namespace

REPLACE(0x00401050, &BitStreamAdapter::Construct);
REPLACE(0x00401070, &Util::BitStream::SetInputData);
REPLACE(0x00401090, &Util::BitStream::SetOutputData);
REPLACE(0x004010C0, &Util::BitStream::GetByteCount);
REPLACE(0x004010D0, &Util::BitStream::WriteBits);
REPLACE(0x004011B0, &Util::BitStream::ReadBits);
REPLACE(0x00401260, &Util::BitStream::ReadSignedBits);
REPLACE(0x00401180, &Util::BitStream::Flush);
