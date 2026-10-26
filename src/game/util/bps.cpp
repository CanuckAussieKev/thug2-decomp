// BPS binary patches.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/util/bps.h"

#include <cstdlib>
#include <cstring>

#include "game/sys/debug_log.h"

namespace Bps {

namespace {

using DebugLog::Level;

// The standard CRC-32 (zlib's).
uint32_t Crc32(const uint8_t* data, size_t size)
{
    static uint32_t table[256];
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

uint8_t ReadByte(const uint8_t* buffer, size_t* offset)
{
    return buffer[(*offset)++];
}

uint32_t ReadWord(const uint8_t* buffer, size_t* offset)
{
    uint32_t value = ReadByte(buffer, offset);
    value |= ReadByte(buffer, offset) << 8;
    value |= ReadByte(buffer, offset) << 16;
    value |= static_cast<uint32_t>(ReadByte(buffer, offset)) << 24;
    return value;
}

// A variable-length number: 7 bits per byte, the last byte's top bit set (each continuation adds one more).
uint64_t ReadNumber(const uint8_t* buffer, size_t* offset)
{
    uint64_t result = 0;
    uint64_t shift = 0;
    for (;;) {
        uint8_t byte = ReadByte(buffer, offset);
        result += static_cast<int>((byte & 0x7F) << static_cast<int>(shift)); // (32-bit shifts, as upstream)
        if (byte & 0x80)
            break;
        shift += 7;
        result += static_cast<int>(1 << static_cast<int>(shift));
    }
    return result;
}

// A signed relative offset: the low bit is the sign.
int32_t ReadOffset(const uint8_t* buffer, size_t* offset)
{
    auto value = static_cast<int32_t>(ReadNumber(buffer, offset));
    return (value & 1) ? -(value >> 1) : (value >> 1);
}

} // namespace

uint32_t PatchChecksum(const uint8_t* patch, size_t size)
{
    size_t offset = size - 4;
    return ReadWord(patch, &offset);
}

bool Apply(const uint8_t* patch, size_t patchSize, const uint8_t* input, size_t inputSize, uint8_t** output,
           size_t* outputSize)
{
    size_t at = 0;
    char magic[5];
    for (int i = 0; i < 4; ++i)
        magic[i] = static_cast<char>(ReadByte(patch, &at));
    magic[4] = '\0';
    if (std::strcmp(magic, "BPS1") != 0) {
        DebugLog::Printf(Level::kError, "BPS magic number doesn't match expected: %s\n", magic);
        return false;
    }
    auto expectedInputSize = static_cast<uint32_t>(ReadNumber(patch, &at));
    if (expectedInputSize != inputSize) {
        DebugLog::Printf(Level::kError, "Patch input of unexpected size!  expected: %d actual: %d\n", expectedInputSize,
                         inputSize);
        return false;
    }
    *outputSize = static_cast<size_t>(ReadNumber(patch, &at));
    *output = static_cast<uint8_t*>(std::malloc(*outputSize));
    at += static_cast<uint32_t>(ReadNumber(patch, &at)); // (the metadata, skipped)

    size_t outputAt = 0;
    int32_t sourceAt = 0;
    int32_t targetAt = 0;
    while (at < patchSize - 12) {
        auto head = static_cast<uint32_t>(ReadNumber(patch, &at));
        uint32_t length = (head >> 2) + 1;
        switch (head & 3) {
        case 0: // the input's bytes at the same place
            std::memcpy(*output + outputAt, input + outputAt, length);
            outputAt += length;
            break;
        case 1: // bytes from the patch
            std::memcpy(*output + outputAt, patch + at, length);
            at += length;
            outputAt += length;
            break;
        case 2: // the input's bytes from elsewhere
            sourceAt += ReadOffset(patch, &at);
            std::memcpy(*output + outputAt, input + sourceAt, length);
            sourceAt += length;
            outputAt += length;
            break;
        case 3: // the output's own earlier bytes
            targetAt += ReadOffset(patch, &at);
            std::memcpy(*output + outputAt, *output + targetAt, length);
            targetAt += length;
            outputAt += length;
            break;
        }
    }

    uint32_t inputCrc = ReadWord(patch, &at);
    if (inputCrc != Crc32(input, inputSize)) {
        DebugLog::Printf(Level::kError, "INPUT CRC DID NOT MATCH: %x %x\n", inputCrc, Crc32(input, inputSize));
    } else {
        uint32_t outputCrc = ReadWord(patch, &at);
        if (outputCrc != Crc32(*output, *outputSize)) {
            DebugLog::Printf(Level::kError, "OUTPUT CRC DID NOT MATCH: %x %x\n", outputCrc, Crc32(*output, *outputSize));
        } else {
            uint32_t patchCrc = ReadWord(patch, &at);
            if (patchCrc == Crc32(patch, patchSize - 4))
                return true;
            DebugLog::Printf(Level::kError, "PATCH CRC DID NOT MATCH: %x %x\n", patchCrc, Crc32(patch, patchSize));
        }
    }
    std::free(*output);
    *output = nullptr;
    return false;
}

} // namespace Bps
