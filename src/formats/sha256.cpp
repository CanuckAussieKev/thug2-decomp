// SHA-256 (formats/sha256.h). NOT ORIGINAL.

#include "formats/sha256.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "base/core/checked_index.h"

namespace sha256 {

namespace {

constexpr std::size_t kBlockBytes = 64;
constexpr std::size_t kLengthBytes = 8;
constexpr unsigned kBitsPerByte = 8;
constexpr uint8_t kEndMarker = 0x80;
constexpr std::size_t kRounds = 64;
constexpr std::size_t kWordBytes = 4;
constexpr uint8_t kNibble = 0x0F;
constexpr unsigned kNibbleBits = 4;

constexpr std::array<uint32_t, 8> kStart = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                                            0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};

constexpr std::array<uint32_t, kRounds> kRoundConstants = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};

} // namespace

Hasher::Hasher() : state_(kStart) {}

void Hasher::block(std::span<const uint8_t, kBlockBytes> data)
{
    std::array<uint32_t, kRounds> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        uint32_t word = 0;
        for (std::size_t b = 0; b < kWordBytes; ++b) {
            word = (word << kBitsPerByte) | core::at(data, (i * kWordBytes) + b);
        }
        core::at(w, i) = word;
    }
    for (std::size_t i = 16; i < kRounds; ++i) {
        const uint32_t w15 = core::at(w, i - 15);
        const uint32_t w2 = core::at(w, i - 2);
        const uint32_t s0 = std::rotr(w15, 7) ^ std::rotr(w15, 18) ^ (w15 >> 3U);
        const uint32_t s1 = std::rotr(w2, 17) ^ std::rotr(w2, 19) ^ (w2 >> 10U);
        core::at(w, i) = core::at(w, i - 16) + s0 + core::at(w, i - 7) + s1;
    }
    std::array<uint32_t, 8> v = state_;
    for (std::size_t i = 0; i < kRounds; ++i) {
        const uint32_t e = v[4];
        const uint32_t a = v[0];
        const uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const uint32_t choose = (e & v[5]) ^ (~e & v[6]);
        const uint32_t first = v[7] + s1 + choose + core::at(kRoundConstants, i) + core::at(w, i);
        const uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const uint32_t majority = (a & v[1]) ^ (a & v[2]) ^ (v[1] & v[2]);
        const uint32_t second = s0 + majority;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + first;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = first + second;
    }
    for (std::size_t i = 0; i < state_.size(); ++i) {
        core::at(state_, i) += core::at(v, i);
    }
}

void Hasher::add(std::span<const uint8_t> bytes)
{
    length_ += bytes.size();
    while (!bytes.empty()) {
        const std::size_t take = std::min(kBlockBytes - pending_size_, bytes.size());
        for (std::size_t i = 0; i < take; ++i) {
            core::at(pending_, pending_size_ + i) = core::at(bytes, i);
        }
        pending_size_ += take;
        bytes = bytes.subspan(take);
        if (pending_size_ == kBlockBytes) {
            block(pending_);
            pending_size_ = 0;
        }
    }
}

void Hasher::add_u32(uint32_t value)
{
    std::array<uint8_t, kWordBytes> bytes{};
    for (std::size_t i = 0; i < kWordBytes; ++i) {
        core::at(bytes, i) = static_cast<uint8_t>(value >> (kBitsPerByte * i));
    }
    add(bytes);
}

Digest Hasher::finish()
{
    const uint64_t bits = length_ * kBitsPerByte;
    const std::array<uint8_t, 1> marker = {kEndMarker};
    add(marker);
    const std::array<uint8_t, 1> zero = {0};
    while (pending_size_ != kBlockBytes - kLengthBytes) {
        add(zero);
    }
    std::array<uint8_t, kLengthBytes> length{};
    for (std::size_t i = 0; i < kLengthBytes; ++i) {
        core::at(length, i) = static_cast<uint8_t>(bits >> (kBitsPerByte * (kLengthBytes - 1 - i)));
    }
    add(length);
    Digest digest{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
        for (std::size_t b = 0; b < kWordBytes; ++b) {
            core::at(digest, (i * kWordBytes) + b) =
                static_cast<uint8_t>(core::at(state_, i) >> (kBitsPerByte * (kWordBytes - 1 - b)));
        }
    }
    return digest;
}

Digest of(std::span<const uint8_t> bytes)
{
    Hasher hasher;
    hasher.add(bytes);
    return hasher.finish();
}

std::string hex(const Digest& digest)
{
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const uint8_t byte : digest) {
        out += kDigits.at(byte >> kNibbleBits);
        out += kDigits.at(byte & kNibble);
    }
    return out;
}

} // namespace sha256
