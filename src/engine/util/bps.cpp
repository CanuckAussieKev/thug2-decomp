// BPS binary patches.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "engine/util/bps.h"

#include <cstring>
#include <array>

#include "platform/debug_log.h"

namespace bps {

namespace {

using debug_log::StageMap;

// The standard CRC-32 (zlib's).
uint32_t crc32(const uint8_t* data, size_t size)
{
    static std::array<uint32_t, 256> table;
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    uint32_t crc = 0xFFFFFFFF;
    while (size--)
        crc = table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

uint8_t read_byte(const uint8_t* buffer, size_t* offset) { return buffer[(*offset)++]; }

uint32_t read_word(const uint8_t* buffer, size_t* offset)
{
    uint32_t value = read_byte(buffer, offset);
    value |= read_byte(buffer, offset) << 8;
    value |= read_byte(buffer, offset) << 16;
    value |= static_cast<uint32_t>(read_byte(buffer, offset)) << 24;
    return value;
}

// A variable-length number: 7 bits per byte, the last byte's top bit set (each continuation adds one more).
uint64_t read_number(const uint8_t* buffer, size_t* offset)
{
    uint64_t result = 0;
    uint64_t shift = 0;
    for (;;) {
        const uint8_t byte = read_byte(buffer, offset);
        result += static_cast<int>((byte & 0x7F) << static_cast<int>(shift)); // (32-bit shifts, as upstream)
        if (byte & 0x80)
            break;
        shift += 7;
        result += static_cast<int>(1 << static_cast<int>(shift));
    }
    return result;
}

// A signed relative offset: the low bit is the sign.
int32_t decode_varint(const uint8_t* buffer, size_t* offset)
{
    auto value = static_cast<int32_t>(read_number(buffer, offset));
    return (value & 1) ? -(value >> 1) : (value >> 1);
}

} // namespace

uint32_t patch_checksum(const uint8_t* patch, size_t size)
{
    size_t offset = size - 4;
    return read_word(patch, &offset);
}

// NOT ORIGINAL - the source's size (the first number after the magic) and CRC-32 (12 bytes from the end) checked
// before patching.
bool made_for(const uint8_t* patch, size_t patch_size, const uint8_t* input, size_t input_size)
{
    if (!patch || patch_size < 16 || !input || std::memcmp(patch, "BPS1", 4) != 0)
        return false;
    size_t at = 4;
    if (read_number(patch, &at) != input_size)
        return false;
    size_t crc_at = patch_size - 12;
    return read_word(patch, &crc_at) == crc32(input, input_size);
}

bool get_apply(const uint8_t* patch, size_t patch_size, const uint8_t* input, size_t input_size,
               std::vector<uint8_t>* output)
{
    size_t at = 0;
    std::array<char, 5> magic{};
    for (int i = 0; i < 4; ++i)
        magic[i] = static_cast<char>(read_byte(patch, &at));
    magic[4] = '\0';
    if (std::strcmp(magic.data(), "BPS1") != 0) {
        debug_log::printf(StageMap::kError, "BPS magic number doesn't match expected: %s\n", magic.data());
        return false;
    }
    auto expected_input_size = static_cast<uint32_t>(read_number(patch, &at));
    if (expected_input_size != input_size) {
        debug_log::printf(StageMap::kError, "Patch input of unexpected size!  expected: %d actual: %d\n",
                          expected_input_size, input_size);
        return false;
    }
    output->assign(static_cast<size_t>(read_number(patch, &at)), 0);
    uint8_t* out = output->data();
    at += static_cast<uint32_t>(read_number(patch, &at)); // (the metadata, skipped)

    size_t output_at = 0;
    int32_t source_at = 0;
    int32_t target_at = 0;
    while (at < patch_size - 12) {
        auto head = static_cast<uint32_t>(read_number(patch, &at));
        const uint32_t length = (head >> 2) + 1;
        // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
        switch (head & 3) {
        case 0: // the input's bytes at the same place
            std::memcpy(out + output_at, input + output_at, length);
            output_at += length;
            break;
        case 1: // bytes from the patch
            std::memcpy(out + output_at, patch + at, length);
            at += length;
            output_at += length;
            break;
        case 2: // the input's bytes from elsewhere
            source_at += decode_varint(patch, &at);
            std::memcpy(out + output_at, input + source_at, length);
            source_at += static_cast<int32_t>(length);
            output_at += length;
            break;
        case 3: // the output's own earlier bytes
            target_at += decode_varint(patch, &at);
            std::memcpy(out + output_at, out + target_at, length);
            target_at += static_cast<int32_t>(length);
            output_at += length;
            break;
        }
    }

    const uint32_t input_crc = read_word(patch, &at);
    if (input_crc != crc32(input, input_size)) {
        debug_log::printf(StageMap::kError, "INPUT CRC DID NOT MATCH: %x %x\n", input_crc, crc32(input, input_size));
    } else {
        const uint32_t output_crc = read_word(patch, &at);
        if (output_crc != crc32(out, output->size())) {
            debug_log::printf(StageMap::kError, "OUTPUT CRC DID NOT MATCH: %x %x\n", output_crc,
                              crc32(out, output->size()));
        } else {
            const uint32_t patch_crc = read_word(patch, &at);
            if (patch_crc == crc32(patch, patch_size - 4))
                return true;
            debug_log::printf(StageMap::kError, "PATCH CRC DID NOT MATCH: %x %x\n", patch_crc,
                              crc32(patch, patch_size));
        }
    }
    output->clear();
    return false;
}

} // namespace bps
