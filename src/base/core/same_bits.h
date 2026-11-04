#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

// NOT ORIGINAL - the comparisons the original makes with memcmp: bit for bit, so a float's -0 isn't 0 and a NaN
// equals itself, as there.
namespace core {

// Whether `value` holds exactly the float bits `bits` (0x3F000000: 0.5).
[[nodiscard]] constexpr bool has_bits(float value, uint32_t bits) { return std::bit_cast<uint32_t>(value) == bits; }

// Whether two runs of floats hold the same bits, float by float (runs of different lengths differ).
[[nodiscard]] inline bool same_bits(std::span<const float> a, std::span<const float> b)
{
    return std::ranges::equal(a, b, [](float x, float y) { return std::bit_cast<uint32_t>(x) == std::bit_cast<uint32_t>(y); });
}

// Whether two runs of bytes are the same (runs of different lengths differ).
[[nodiscard]] inline bool same_bytes(std::span<const std::byte> a, std::span<const std::byte> b) { return std::ranges::equal(a, b); }

} // namespace core
