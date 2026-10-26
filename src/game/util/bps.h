#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include <cstddef>
#include <cstdint>

// BPS ("beat") binary patches, by Near: a source, a target, and copy/read instructions between them, with
// CRC-32s of all three at the end.
namespace Bps {

// The patch's own checksum (its last four bytes).
uint32_t PatchChecksum(const uint8_t* patch, size_t size);

// Apply `patch` to `input`: true and a malloc'd `*output` of `*outputSize` bytes, or false (the wrong patch for the
// input, or a checksum that doesn't match; logged) with `*output` null.
bool Apply(const uint8_t* patch, size_t patchSize, const uint8_t* input, size_t inputSize, uint8_t** output,
           size_t* outputSize);

} // namespace Bps
