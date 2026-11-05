#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include <cstddef>
#include <cstdint>
#include <vector>

// BPS ("beat") binary patches, by Near: a source, a target, and copy/read instructions between them, with
// CRC-32s of all three at the end.
namespace bps {

// The patch's own checksum (its last four bytes).
uint32_t patch_checksum(const uint8_t* patch, size_t size);

// Whether `patch` was made for `input` (its size and CRC-32).
bool made_for(const uint8_t* patch, size_t patch_size, const uint8_t* input, size_t input_size);

// Apply `patch` to `input`: true and the patched bytes in `*output`, or false (the wrong patch for the input, or a
// checksum that doesn't match; logged) with `*output` empty.
bool get_apply(const uint8_t* patch, size_t patch_size, const uint8_t* input, size_t input_size,
               std::vector<uint8_t>* output);

} // namespace bps
