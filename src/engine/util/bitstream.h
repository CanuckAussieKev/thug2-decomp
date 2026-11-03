#pragma once

#include <cstdint>
#include <span>

namespace util {

// Reads or writes values packed at bit level, a 32-bit word at a time, least significant bits first.
class PackedBitBuffer {
public:
    static constexpr int kWordBits = 32; // the width of the words the bits are packed into
    PackedBitBuffer();

    // Prepare to read `size` bytes from `data`. The word buffer starts empty, so the first read loads it.
    void change_intake_contents(void* data, int size);
    // Prepare to write into `data` (capacity `size` bytes).
    void bind_output_buffer(void* data, int size);
    // Bytes touched so far, rounding a partial byte up.
    [[nodiscard]] int get_byte_count() const;
    // Write out a partially filled word (writing only).
    void drain();

    // A value and how many of its bits are written.
    struct BitField {
        uint32_t value{};
        int count{};
    };

    // Append the low `count` bits of `value` (count 0..32). Bits of `value` above `count` are not masked
    // off, matching the original; callers pass clean values.
    void write_bits(BitField bit_field);
    // Read `count` bits (0..32) as an unsigned value.
    uint32_t read_bits(int count);
    // Read `count` bits and sign-extend from the top bit read. (Quirk kept from the original: for count 32,
    // any negative value comes back as -1.)
    int32_t read_signed_bits(int count);

private:
    std::span<uint32_t> words_;  // the data as words (the last one may be a partial one's room)
    size_t next_word_{0};        // the next word to read/write
    uint32_t octets_{0};        // bits waiting to be written / not yet read
    int free_bits_in_word_{kWordBits}; // writing: free bits in octets_ (32 = empty); reading: unread bits (0 = empty)
    int extent_{0};             // size of the data in bytes
    int bits_used_{0};          // bits read or written so far
};

} // namespace util
