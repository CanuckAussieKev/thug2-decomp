#include "base/core/checksums.h"
#include "base/core/checked_index.h"
#include "base/core/cursor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace checksums {

namespace {
constexpr uint32_t kByteValueCount = 256;
constexpr int kBitsInByte = 8;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kCrcPolynomial = 0xEDB88320U; // CRC-32's, bit-reversed
constexpr uint32_t kInitialCrc = 0xFFFFFFFF;     // the register the checksum starts from (no final inversion)

// The standard CRC-32 table. The exe has the same table at 0x0067C248.
constexpr std::array<uint32_t, kByteValueCount> make_table()
{
    std::array<uint32_t, kByteValueCount> table{};
    for (uint32_t i = 0; i < kByteValueCount; ++i) {
        uint32_t entry = i;
        for (int bit = 0; bit < kBitsInByte; ++bit) {
            entry = ((entry & 1) != 0U) ? kCrcPolynomial ^ (entry >> 1) : entry >> 1;
        }
        core::at(table, i) = entry;
    }
    return table;
}

constexpr std::array<uint32_t, 256> kTable = make_table();

uint32_t get_step(uint32_t crc, uint8_t byte) { return (crc >> kBitsInByte) ^ core::at(kTable, (crc ^ byte) & kByteMask); }

// Names are case-insensitive and treat '/' and '\' as the same character.
uint8_t name_char(char character)
{
    if (character >= 'A' && character <= 'Z') {
        character += 'a' - 'A';
    }
    if (character == '/') {
        character = '\\';
    }
    return static_cast<uint8_t>(character);
}

} // namespace

// 0x00401B90
uint32_t hash_from_text(const char* string)
{
    if (!string) {
        return 0;
    }
    return fold_text_into_checksum(kInitialCrc, string);
}

// 0x00401BE0
uint32_t fold_text_into_checksum(uint32_t crc, const char* string)
{
    if (!string) {
        return crc;
    }
    for (core::CStringCursor at(string); *at != '\0'; ++at) {
        crc = get_step(crc, name_char(*at));
    }
    return crc;
}

// 0x00401AA0
uint32_t generate_crc_from_buffer(const void* data, int size)
{
    if (!data || size < 1) {
        return 0;
    }
    uint32_t crc = kInitialCrc;
    for (const uint8_t byte : std::span(static_cast<const uint8_t*>(data), static_cast<std::size_t>(size))) {
        crc = get_step(crc, byte);
    }
    return crc;
}

// 0x00401AF0
// Ghidra's pseudocode shows this as an empty loop: it misses the third argument (the starting CRC, loaded
// straight into EAX) and with it the loop body and the return value. The script code chains it over tokens.
uint32_t extend_crc_with_buffer(std::span<const uint8_t> bytes, uint32_t crc)
{
    for (const uint8_t byte : bytes) {
        crc = get_step(crc, byte);
    }
    return crc;
}

// 0x00401B30
uint32_t generate_crc_caseless(const char* data, int size)
{
    if (!data || size < 1) {
        return 0;
    }
    uint32_t crc = kInitialCrc;
    for (const char character : std::span(data, static_cast<std::size_t>(size))) {
        crc = get_step(crc, name_char(character));
    }
    return crc;
}

} // namespace checksums
