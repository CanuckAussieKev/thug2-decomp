#pragma once

#include <array>
#include <bit>
#include <cstdint>

// NOT ORIGINAL - a colour packed in a 32-bit word, as the game keeps its colours (RGBA or ARGB words), and its four
// bytes in memory order: byte 0 is the word's low byte (an RGBA word's red, an ARGB word's blue). The original read
// and wrote the bytes of the word in place; these give them as values.
namespace colour {

using Bytes = std::array<uint8_t, 4>;

// The word's bytes, low byte first.
constexpr Bytes bytes_of(uint32_t packed) noexcept { return std::bit_cast<Bytes>(packed); }
// The word made of four bytes, low byte first.
constexpr uint32_t packed_of(const Bytes& bytes) noexcept { return std::bit_cast<uint32_t>(bytes); }

// A colour word as a type of its own (I.24: it can't be passed where a count, an index or a flag is meant), in the
// byte order its user keeps (RGBA or ARGB). Made from a raw word only explicitly; `.value()` gives the word back.
// The same size and code as the uint32_t it holds.
class Packed {
public:
    constexpr Packed() = default;
    constexpr explicit Packed(uint32_t value) noexcept : value_(value) {}
    [[nodiscard]] constexpr uint32_t value() const noexcept { return value_; }
    constexpr bool operator==(const Packed&) const = default;

private:
    uint32_t value_ = 0;
};

} // namespace colour
