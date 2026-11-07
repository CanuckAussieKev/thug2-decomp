#pragma once

#include <cstdint>
#include <span>

#include "base/core/api.h"

namespace checksums {

// NOT ORIGINAL - the checksum of a name (a script, a structure key, an object's or a part's name - the CRC below),
// as a type of its own so that it can't be passed where an id, a count or a flag is meant (Core Guideline I.24).
// Made from a raw value only explicitly (`checksums::Name{0x...}`, or a reader of the boundary layer); `.value()`
// gives the 32 bits back where they are stored or compared with the original's fields. The same size and code as the
// uint32_t it holds.
class Name {
public:
    constexpr Name() = default;
    constexpr explicit Name(uint32_t value) noexcept : value_(value) {}
    [[nodiscard]] constexpr uint32_t value() const noexcept { return value_; }
    constexpr bool operator==(const Name&) const = default;

private:
    uint32_t value_ = 0;
};

// Checksum used for every name in the game (script names, structure keys, file names).
// CRC-32 with the reflected 0xEDB88320 polynomial and no final XOR; letters are lowercased and '/' is
// treated as '\', so "Skater/Board" and "skater\board" give the same checksum.
THUG2_API uint32_t hash_from_text(const char* string);

// Continues a checksum with more text, using the same name rules. Returns `crc` unchanged for null/empty text.
THUG2_API uint32_t fold_text_into_checksum(uint32_t crc, const char* string);

// Plain CRC-32 (no final XOR) of raw bytes, with no case folding. Returns 0 for null data or size < 1.
uint32_t generate_crc_from_buffer(const void* data, int size);

// Continue `crc` over raw bytes (no case folding). Returns `crc` unchanged for no bytes.
uint32_t extend_crc_with_buffer(std::span<const uint8_t> bytes, uint32_t crc);

// Like hash_from_text, but for `size` characters that needn't be null-terminated.
// Returns 0 for null data or size < 1.
uint32_t generate_crc_caseless(const char* data, int size);

} // namespace checksums
