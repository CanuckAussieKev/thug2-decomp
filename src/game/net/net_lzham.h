#pragma once

#include <cstddef>
#include <cstdint>

// NOT ORIGINAL - compression method 3 of the network library's compressed message (id 0x12): LZHAM 1.0, as THUGPRO
// (Morten1337's thug2 mod) sends it - its player create (34), join request (80) and connection request (3) - with its
// parameters: a 2^15 dictionary (LZHAM's smallest), the "uber" level, no helper threads, no flags; one LZHAM stream per
// message, no header of its own (the compressed message's own header carries the expanded size). Parameters from
// Morten1337's thugpro_proxy (Net/Encoding.cpp), used with his permission; the codec is the public lzham_codec
// (CMakeLists.txt, THIRD_PARTY_NOTICES.md). Register: tools/compat/thugpro/README.md, TP-R11.
//
// Unlike THUGPRO's, both directions here are bounded: the output never goes past the buffer it's given.
namespace Net::Lzham {

constexpr uint8_t kMethod = 3;
constexpr uint32_t kDictionarySizeLog2 = 15;

// `size` bytes of `in` compressed into `out` (`capacity` bytes): the compressed size, or 0 if it failed or wouldn't fit.
size_t Compress(const uint8_t* in, size_t size, uint8_t* out, size_t capacity);
// `size` bytes of an LZHAM stream expanded into `out`: true and *outSize = the expanded size, or false (*outSize
// untouched) if the stream is bad, ends early, or expands to more than `capacity` bytes.
bool Decompress(const uint8_t* in, size_t size, uint8_t* out, size_t capacity, size_t* outSize);

} // namespace Net::Lzham
