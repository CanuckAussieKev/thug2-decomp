#include "engine/util/bitstream.h"

#include <cstdint>
#include <new>

namespace util {

// 0x00401050
PackedBitBuffer::PackedBitBuffer()
    : cursor_(nullptr), outset_(nullptr), octets_(0), free_bits_in_word_(32), extent_(0), bits_used_(0)
{
    // The original leaves outset_ uninitialised; zeroing it here is harmless.
}

// 0x00401070
void PackedBitBuffer::change_intake_contents(void* data, int size)
{
    cursor_ = static_cast<uint32_t*>(data);
    outset_ = static_cast<uint32_t*>(data);
    extent_ = size;
    free_bits_in_word_ = 0;
    bits_used_ = 0;
}

// 0x00401090
void PackedBitBuffer::bind_output_buffer(void* data, int size)
{
    cursor_ = static_cast<uint32_t*>(data);
    outset_ = static_cast<uint32_t*>(data);
    extent_ = size;
    free_bits_in_word_ = 32;
    bits_used_ = 0;
}

// 0x004010C0
int PackedBitBuffer::get_byte_count() const { return (bits_used_ + 7) >> 3; }

// 0x00401180
void PackedBitBuffer::drain()
{
    if (free_bits_in_word_ != 32) {
        *cursor_++ = octets_;
        octets_ = 0;
        free_bits_in_word_ = 32;
    }
}

// 0x004010D0
void PackedBitBuffer::write_bits(uint32_t value, int count)
{
    bits_used_ += count;
    if (count > free_bits_in_word_) {
        // Fill the rest of the current word, write it out, and continue with the remaining bits.
        int used = 32 - free_bits_in_word_;
        octets_ = (octets_ & ((1u << used) - 1)) | (value << used);
        *cursor_++ = octets_;
        // Arithmetic shift (SAR in the original): a value with its top bit set carries ones into the next word.
        value = static_cast<uint32_t>(static_cast<int32_t>(value) >> free_bits_in_word_);
        count -= free_bits_in_word_;
        octets_ = 0;
        free_bits_in_word_ = 32;
    }
    int used = 32 - free_bits_in_word_;
    free_bits_in_word_ -= count;
    octets_ = (octets_ & ((1u << used) - 1)) | (value << used);
    if (free_bits_in_word_ == 0) {
        *cursor_++ = octets_;
        octets_ = 0;
        free_bits_in_word_ = 32;
    }
}

// 0x004011B0
uint32_t PackedBitBuffer::read_bits(int count)
{
    bits_used_ += count;
    if (free_bits_in_word_ == 0) {
        octets_ = *cursor_++;
        free_bits_in_word_ = 32;
    }

    int available = free_bits_in_word_;
    if (count <= available) {
        free_bits_in_word_ = available - count;
        uint32_t bits = octets_;
        if (count == 32)
            return bits;
        octets_ = bits >> count;
        return bits & ((1u << count) - 1);
    }

    // The value straddles two words: low bits from this one, the rest from the next.
    uint32_t low = octets_;
    uint32_t next = *cursor_++;
    int rest = count - available;
    octets_ = next >> rest;
    free_bits_in_word_ = 32 - rest;
    return (low & ((1u << available) - 1)) | ((next & ((1u << rest) - 1)) << available);
}

// 0x00401260
int32_t PackedBitBuffer::read_signed_bits(int count)
{
    uint32_t bits = read_bits(count);
    // Shift counts are taken mod 32, as the original's x86 shifts do.
    if (bits & (1u << ((count - 1) & 31)))
        bits |= ~((1u << (count & 31)) - 1);
    return static_cast<int32_t>(bits);
}

} // namespace util

namespace {

struct BitStreamAdapter {
    util::PackedBitBuffer* make_new() { return new (this) util::PackedBitBuffer(); }
};

} // namespace
