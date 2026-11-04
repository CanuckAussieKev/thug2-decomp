// BPS binary patches.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "base/util/bps.h"

#include <cstdint>
#include <cstring>
#include <array>
#include <span>
#include <vector>

#include "base/core/log_sink.h"
#include "base/core/checked_index.h"

namespace bps {

namespace {

constexpr size_t kByteValues = 256;                 // the values of a byte
constexpr int kBitsInByte = 8;
constexpr int kBitsInShort = 16;
constexpr int kBitsInThreeBytes = 24;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kCrcPolynomial = 0xEDB88320U;   // CRC-32's polynomial, bit-reversed
constexpr uint32_t kCrcInverted = 0xFFFFFFFF;      // the CRC starts as, and ends inverted
constexpr uint32_t kNumberDataMask = 0x7F;         // a variable-length number's byte: 7 data bits...
constexpr uint8_t kNumberEndFlag = 0x80;           // ...and the top bit set on the last byte
constexpr uint64_t kNumberDataBits = 7;
constexpr size_t kFooterSize = 12;                 // the three CRC-32s that end a patch (source, target, patch)
constexpr size_t kMinPatchSize = 16;               // the magic, three numbers at the least and the footer
constexpr size_t kMagicSize = 4;                   // "BPS1"

// The standard CRC-32 (zlib's).
uint32_t crc32(const uint8_t* bytes, size_t size)
{
    const std::span<const uint8_t> data(bytes, size);
    static std::array<uint32_t, kByteValues> table;
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < kByteValues; ++i) {
            uint32_t entry = i;
            for (int bit = 0; bit < kBitsInByte; ++bit) {
                entry = ((entry & 1) != 0U) ? kCrcPolynomial ^ (entry >> 1) : entry >> 1;
            }
            core::at(table, i) = entry;
        }
        made = true;
    }
    uint32_t crc = kCrcInverted;
    for (const uint8_t byte : data) {
        crc = core::at(table, (crc ^ byte) & kByteMask) ^ (crc >> kBitsInByte);
    }
    return crc ^ kCrcInverted;
}

uint8_t read_byte(std::span<const uint8_t> buffer, size_t* offset) { return core::at(buffer, (*offset)++); }

uint32_t read_word(std::span<const uint8_t> buffer, size_t* offset)
{
    uint32_t value = read_byte(buffer, offset);
    value |= read_byte(buffer, offset) << kBitsInByte;
    value |= read_byte(buffer, offset) << kBitsInShort;
    value |= static_cast<uint32_t>(read_byte(buffer, offset)) << kBitsInThreeBytes;
    return value;
}

// A variable-length number: 7 bits per byte, the last byte's top bit set (each continuation adds one more).
uint64_t read_number(std::span<const uint8_t> buffer, size_t* offset)
{
    uint64_t result = 0;
    uint64_t shift = 0;
    for (;;) {
        const uint8_t byte = read_byte(buffer, offset);
        result += static_cast<int>((byte & kNumberDataMask) << static_cast<int>(shift)); // (32-bit shifts, as upstream)
        if ((byte & kNumberEndFlag) != 0) {
            break;
        }
        shift += kNumberDataBits;
        const int next_bit = 1 << static_cast<int>(shift); // (a 32-bit shift, as upstream)
        result += next_bit;
    }
    return result;
}

// A signed relative offset: the low bit is the sign.
int32_t decode_varint(std::span<const uint8_t> buffer, size_t* offset)
{
    auto value = static_cast<int32_t>(read_number(buffer, offset));
    return ((value & 1) != 0) ? -(value >> 1) : (value >> 1);
}

} // namespace

uint32_t patch_checksum(const uint8_t* patch, size_t size)
{
    size_t offset = size - 4;
    return read_word(std::span<const uint8_t>(patch, size), &offset);
}

// NOT ORIGINAL - the source's size (the first number after the magic) and CRC-32 (12 bytes from the end) checked
// before patching.
bool made_for(const uint8_t* patch, size_t patch_size, const uint8_t* input, size_t input_size)
{
    if (!patch || patch_size < kMinPatchSize || !input || std::memcmp(patch, "BPS1", 4) != 0) {
        return false;
    }
    const std::span<const uint8_t> patch_bytes(patch, patch_size);
    size_t at = 4;
    if (read_number(patch_bytes, &at) != input_size) {
        return false;
    }
    size_t crc_at = patch_size - kFooterSize;
    return read_word(patch_bytes, &crc_at) == crc32(input, input_size);
}

bool get_apply(const uint8_t* patch, size_t patch_size, const uint8_t* input, size_t input_size,
               std::vector<uint8_t>* output)
{
    const std::span<const uint8_t> patch_bytes(patch, patch_size);
    const std::span<const uint8_t> input_bytes(input, input_size);
    size_t at = 0;
    std::array<char, kMagicSize + 1> magic{};
    for (int i = 0; i < 4; ++i) {
        core::at(magic, i) = static_cast<char>(read_byte(patch_bytes, &at));
    }
    core::at(magic, 4) = '\0';
    if (std::strcmp(magic.data(), "BPS1") != 0) {
        core::log_error("BPS magic number doesn't match expected: %s\n", magic.data());
        return false;
    }
    auto expected_input_size = static_cast<uint32_t>(read_number(patch_bytes, &at));
    if (expected_input_size != input_size) {
        core::log_error("Patch input of unexpected size!  expected: %d actual: %d\n", expected_input_size,
                        input_size);
        return false;
    }
    output->assign(static_cast<size_t>(read_number(patch_bytes, &at)), 0);
    const uint8_t* out = output->data();
    const std::span<uint8_t> out_bytes(output->data(), output->size());
    at += static_cast<uint32_t>(read_number(patch_bytes, &at)); // (the metadata, skipped)

    size_t output_at = 0;
    int32_t source_at = 0;
    int32_t target_at = 0;
    while (at < patch_size - kFooterSize) {
        auto head = static_cast<uint32_t>(read_number(patch_bytes, &at));
        const uint32_t length = (head >> 2) + 1;
        switch (head & 3) {
        case 0: // the input's bytes at the same place
            std::memcpy(out_bytes.subspan(output_at, length).data(), input_bytes.subspan(output_at, length).data(), length);
            output_at += length;
            break;
        case 1: // bytes from the patch
            std::memcpy(out_bytes.subspan(output_at, length).data(), patch_bytes.subspan(at, length).data(), length);
            at += length;
            output_at += length;
            break;
        case 2: // the input's bytes from elsewhere
            source_at += decode_varint(patch_bytes, &at);
            std::memcpy(out_bytes.subspan(output_at, length).data(), input_bytes.subspan(static_cast<size_t>(source_at), length).data(), length);
            source_at += static_cast<int32_t>(length);
            output_at += length;
            break;
        case 3: // the output's own earlier bytes
            target_at += decode_varint(patch_bytes, &at);
            std::memcpy(out_bytes.subspan(output_at, length).data(), out_bytes.subspan(static_cast<size_t>(target_at), length).data(), length);
            target_at += static_cast<int32_t>(length);
            output_at += length;
            break;
        default:
            break;
        }
    }

    const uint32_t input_crc = read_word(patch_bytes, &at);
    if (input_crc != crc32(input, input_size)) {
        core::log_error("INPUT CRC DID NOT MATCH: %x %x\n", input_crc, crc32(input, input_size));
    } else {
        const uint32_t output_crc = read_word(patch_bytes, &at);
        if (output_crc != crc32(out, output->size())) {
            core::log_error("OUTPUT CRC DID NOT MATCH: %x %x\n", output_crc, crc32(out, output->size()));
        } else {
            const uint32_t patch_crc = read_word(patch_bytes, &at);
            if (patch_crc == crc32(patch, patch_size - 4)) {
                return true;
            }
            core::log_error("PATCH CRC DID NOT MATCH: %x %x\n", patch_crc, crc32(patch, patch_size));
        }
    }
    output->clear();
    return false;
}

} // namespace bps
