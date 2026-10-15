#pragma once

#include <cstdint>

namespace Crc {

// Checksum used for every name in the game (script names, structure keys, file names).
// CRC-32 with the reflected 0xEDB88320 polynomial and no final XOR; letters are lowercased and '/' is
// treated as '\', so "Skater/Board" and "skater\board" give the same checksum.
uint32_t GenerateCRCFromString(const char* string);

} // namespace Crc
