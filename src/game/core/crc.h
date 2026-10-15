#pragma once

#include <cstdint>

namespace Crc {

// Checksum used for every name in the game (script names, structure keys, file names).
// CRC-32 with the reflected 0xEDB88320 polynomial and no final XOR; letters are lowercased and '/' is
// treated as '\', so "Skater/Board" and "skater\board" give the same checksum.
uint32_t GenerateCRCFromString(const char* string);

// Continues a checksum with more text, using the same name rules. Returns `crc` unchanged for null/empty text.
uint32_t ExtendCRCWithString(uint32_t crc, const char* string);

// Plain CRC-32 (no final XOR) of raw bytes, with no case folding. Returns 0 for null data or size < 1.
uint32_t GenerateCRCFromBuffer(const void* data, int size);

// Like GenerateCRCFromString, but for `size` characters that needn't be null-terminated.
// Returns 0 for null data or size < 1.
uint32_t GenerateCRCCaseless(const char* data, int size);

} // namespace Crc
