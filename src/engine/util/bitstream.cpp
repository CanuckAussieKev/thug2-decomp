#include "engine/util/bitstream.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

#include "engine/core/checked_index.h"

namespace util {

namespace {

constexpr int kBitsInByte = 8;
constexpr int kShiftMask = PackedBitBuffer::kWordBits - 1; // an x86 shift count counts modulo the word's width

} // namespace

// 0x00401050
// (the original leaves outset_ uninitialised; zeroing it here is harmless)
PackedBitBuffer::PackedBitBuffer() = default;

// 0x00401070
void PackedBitBuffer::change_intake_contents(void* data, int size)
{
    words_ = std::span<uint32_t>(static_cast<uint32_t*>(data), (static_cast<size_t>(size) + 3) / 4);
    next_word_ = 0;
    extent_ = size;
    free_bits_in_word_ = 0;
    bits_used_ = 0;
}

// 0x00401090
void PackedBitBuffer::bind_output_buffer(void* data, int size)
{
    words_ = std::span<uint32_t>(static_cast<uint32_t*>(data), (static_cast<size_t>(size) + 3) / 4);
    next_word_ = 0;
    extent_ = size;
    free_bits_in_word_ = kWordBits;
    bits_used_ = 0;
}

// 0x004010C0
int PackedBitBuffer::get_byte_count() const { return (bits_used_ + kBitsInByte - 1) >> 3; }

// 0x00401180
void PackedBitBuffer::drain()
{
    if (free_bits_in_word_ != kWordBits) {
        core::at(words_, next_word_++) = octets_;
        octets_ = 0;
        free_bits_in_word_ = kWordBits;
    }
}

// 0x004010D0
void PackedBitBuffer::write_bits(BitField bit_field)
{
    auto value = bit_field.value;
    auto count = bit_field.count;
    bits_used_ += count;
    if (count > free_bits_in_word_) {
        // Fill the rest of the current word, write it out, and continue with the remaining bits.
        const int used = kWordBits - free_bits_in_word_;
        octets_ = (octets_ & ((1U << used) - 1)) | (value << used);
        core::at(words_, next_word_++) = octets_;
        // Arithmetic shift (SAR in the original): a value with its top bit set carries ones into the next word.
        value = static_cast<uint32_t>(static_cast<int32_t>(value) >> free_bits_in_word_);
        count -= free_bits_in_word_;
        octets_ = 0;
        free_bits_in_word_ = kWordBits;
    }
    const int used = kWordBits - free_bits_in_word_;
    free_bits_in_word_ -= count;
    octets_ = (octets_ & ((1U << used) - 1)) | (value << used);
    if (free_bits_in_word_ == 0) {
        core::at(words_, next_word_++) = octets_;
        octets_ = 0;
        free_bits_in_word_ = kWordBits;
    }
}

// 0x004011B0
uint32_t PackedBitBuffer::read_bits(int count)
{
    bits_used_ += count;
    if (free_bits_in_word_ == 0) {
        octets_ = core::at(words_, next_word_++);
        free_bits_in_word_ = kWordBits;
    }

    const int available = free_bits_in_word_;
    if (count <= available) {
        free_bits_in_word_ = available - count;
        const uint32_t bits = octets_;
        if (count == kWordBits) {
            return bits;
        }
        octets_ = bits >> count;
        return bits & ((1U << count) - 1);
    }

    // The value straddles two words: low bits from this one, the rest from the next.
    const uint32_t low = octets_;
    const uint32_t next = core::at(words_, next_word_++);
    const int rest = count - available;
    octets_ = next >> rest;
    free_bits_in_word_ = kWordBits - rest;
    return (low & ((1U << available) - 1)) | ((next & ((1U << rest) - 1)) << available);
}

// 0x00401260
int32_t PackedBitBuffer::read_signed_bits(int count)
{
    uint32_t bits = read_bits(count);
    // Shift counts are taken mod 32, as the original's x86 shifts do.
    if ((bits & (1U << ((count - 1) & kShiftMask))) != 0U) {
        bits |= ~((1U << (count & kShiftMask)) - 1);
    }
    return static_cast<int32_t>(bits);
}

} // namespace util
