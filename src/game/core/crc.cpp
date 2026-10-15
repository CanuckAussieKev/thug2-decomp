#include "game/core/crc.h"

#include <array>

#include "hook/hook.h"

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

} // namespace

// 0x00401B90
uint32_t GenerateCRCFromString(const char* string)
{
    if (!string)
        return 0;

    uint32_t crc = 0xFFFFFFFF;
    for (; *string; ++string) {
        char c = *string;
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c == '/')
            c = '\\';
        crc = (crc >> 8) ^ kTable[(crc ^ static_cast<uint8_t>(c)) & 0xFF];
    }
    return crc;
}

} // namespace Crc

// The original is a plain cdecl function taking the string on the stack, the same as ours.
REPLACE(0x00401B90, Crc::GenerateCRCFromString);
