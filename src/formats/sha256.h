#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// NOT ORIGINAL - SHA-256 (FIPS 180-4), for the demos' seal (formats/demo_format.h): a digest over the bytes given in
// one or more pieces. Plain C++ (no platform API), so the file tools and the game agree on every machine.
namespace sha256 {

constexpr std::size_t kDigestBytes = 32;
using Digest = std::array<uint8_t, kDigestBytes>;

class Hasher {
public:
    Hasher();
    void add(std::span<const uint8_t> bytes);
    void add_u32(uint32_t value); // little-endian
    // The digest of everything added (the hasher is spent: make a new one for more).
    Digest finish();

private:
    void block(std::span<const uint8_t, 64> data);

    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> pending_{};
    std::size_t pending_size_ = 0;
    uint64_t length_ = 0; // bytes added
};

Digest of(std::span<const uint8_t> bytes);
// The digest as lower-case hex (64 characters).
std::string hex(const Digest& digest);

} // namespace sha256
