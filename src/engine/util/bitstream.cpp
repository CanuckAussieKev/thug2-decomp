#include "engine/util/bitstream.h"

#include <cstdint>
#include <new>

namespace Util {

// 0x00401050
PackedBitBuffer::PackedBitBuffer() : m_cursor(nullptr), m_outset(nullptr), m_octets(0), m_freeBitsInWord(32), m_extent(0), m_bitsUsed(0)
{
    // The original leaves m_outset uninitialised; zeroing it here is harmless.
}

// 0x00401070
void PackedBitBuffer::ChangeIntakeContents(void* data, int size)
{
    m_cursor = static_cast<uint32_t*>(data);
    m_outset = static_cast<uint32_t*>(data);
    m_extent = size;
    m_freeBitsInWord = 0;
    m_bitsUsed = 0;
}

// 0x00401090
void PackedBitBuffer::BindOutputBuffer(void* data, int size)
{
    m_cursor = static_cast<uint32_t*>(data);
    m_outset = static_cast<uint32_t*>(data);
    m_extent = size;
    m_freeBitsInWord = 32;
    m_bitsUsed = 0;
}

// 0x004010C0
int PackedBitBuffer::GetByteCount() const
{
    return (m_bitsUsed + 7) >> 3;
}

// 0x00401180
void PackedBitBuffer::Drain()
{
    if (m_freeBitsInWord != 32) {
        *m_cursor++ = m_octets;
        m_octets = 0;
        m_freeBitsInWord = 32;
    }
}

// 0x004010D0
void PackedBitBuffer::WriteBits(uint32_t value, int count)
{
    m_bitsUsed += count;
    if (count > m_freeBitsInWord) {
        // Fill the rest of the current word, write it out, and continue with the remaining bits.
        int used = 32 - m_freeBitsInWord;
        m_octets = (m_octets & ((1u << used) - 1)) | (value << used);
        *m_cursor++ = m_octets;
        // Arithmetic shift (SAR in the original): a value with its top bit set carries ones into the next word.
        value = static_cast<uint32_t>(static_cast<int32_t>(value) >> m_freeBitsInWord);
        count -= m_freeBitsInWord;
        m_octets = 0;
        m_freeBitsInWord = 32;
    }
    int used = 32 - m_freeBitsInWord;
    m_freeBitsInWord -= count;
    m_octets = (m_octets & ((1u << used) - 1)) | (value << used);
    if (m_freeBitsInWord == 0) {
        *m_cursor++ = m_octets;
        m_octets = 0;
        m_freeBitsInWord = 32;
    }
}

// 0x004011B0
uint32_t PackedBitBuffer::ReadBits(int count)
{
    m_bitsUsed += count;
    if (m_freeBitsInWord == 0) {
        m_octets = *m_cursor++;
        m_freeBitsInWord = 32;
    }

    int available = m_freeBitsInWord;
    if (count <= available) {
        m_freeBitsInWord = available - count;
        uint32_t bits = m_octets;
        if (count == 32)
            return bits;
        m_octets = bits >> count;
        return bits & ((1u << count) - 1);
    }

    // The value straddles two words: low bits from this one, the rest from the next.
    uint32_t low = m_octets;
    uint32_t next = *m_cursor++;
    int rest = count - available;
    m_octets = next >> rest;
    m_freeBitsInWord = 32 - rest;
    return (low & ((1u << available) - 1)) | ((next & ((1u << rest) - 1)) << available);
}

// 0x00401260
int32_t PackedBitBuffer::ReadSignedBits(int count)
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
    Util::PackedBitBuffer* MakeNew()
    {
        return new (this) Util::PackedBitBuffer();
    }
};

} // namespace
