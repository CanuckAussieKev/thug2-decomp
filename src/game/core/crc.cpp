#include "game/core/crc.h"

#include <array>

#include "hook/hook.h"
#include "hook/shadow.h"

namespace Crc {

namespace {

// The standard CRC-32 table. The exe has the same table at 0x0067C248.
constexpr std::array<uint32_t, 256> MakeTable()
{
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}

constexpr std::array<uint32_t, 256> kTable = MakeTable();

uint32_t Step(uint32_t crc, uint8_t byte)
{
    return (crc >> 8) ^ kTable[(crc ^ byte) & 0xFF];
}

// Names are case-insensitive and treat '/' and '\' as the same character.
uint8_t NameChar(char c)
{
    if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    if (c == '/')
        c = '\\';
    return static_cast<uint8_t>(c);
}

} // namespace

// 0x00401B90
uint32_t GenerateCRCFromString(const char* string)
{
    if (!string)
        return 0;
    return ExtendCRCWithString(0xFFFFFFFF, string);
}

// 0x00401BE0
uint32_t ExtendCRCWithString(uint32_t crc, const char* string)
{
    if (!string)
        return crc;
    for (; *string; ++string)
        crc = Step(crc, NameChar(*string));
    return crc;
}

// 0x00401AA0
uint32_t GenerateCRCFromBuffer(const void* data, int size)
{
    if (!data || size < 1)
        return 0;
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < size; ++i)
        crc = Step(crc, bytes[i]);
    return crc;
}

// 0x00401AF0
// Ghidra's pseudocode shows this as an empty loop: it misses the third argument (the starting CRC, loaded
// straight into EAX) and with it the loop body and the return value. The script code chains it over tokens.
uint32_t ExtendCRCWithBuffer(const void* data, int size, uint32_t crc)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (int i = 0; i < size; ++i)
        crc = Step(crc, bytes[i]);
    return crc;
}

// 0x00401B30
uint32_t GenerateCRCCaseless(const char* data, int size)
{
    if (!data || size < 1)
        return 0;
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < size; ++i)
        crc = Step(crc, NameChar(data[i]));
    return crc;
}

} // namespace Crc

// The originals are plain cdecl functions, the same as ours.
REPLACE_SHADOW(0x00401AA0, Crc::GenerateCRCFromBuffer);
REPLACE_SHADOW(0x00401AF0, Crc::ExtendCRCWithBuffer);
REPLACE_SHADOW(0x00401B30, Crc::GenerateCRCCaseless);
REPLACE_SHADOW(0x00401B90, Crc::GenerateCRCFromString);
REPLACE_SOURCE_ONLY(0x00472E60, Crc::GenerateCRCFromString); // a jump to 0x00401B90
REPLACE_SHADOW(0x00401BE0, Crc::ExtendCRCWithString);
