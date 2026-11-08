#pragma once

// NOT ORIGINAL - the game's exe as the retail-limits DLL sees it: its image's bytes from the image base on - the live
// process's own image, or (src/retail_limits/check_main.cpp) an exe file laid out as Windows maps it.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace retail_limits {

// How bytes are written into the image (the live image's code pages are read-only: they are unprotected for the write).
using WriteBytes = bool (*)(std::span<uint8_t> where, std::span<const uint8_t> bytes);

struct ExeImage {
    uint32_t base = 0;          // the image base's address
    std::span<uint8_t> bytes;   // the image from its base
    WriteBytes write = nullptr; // how bytes are written into it
};

// A run of the image's bytes: its address and length.
struct Run {
    uint32_t address = 0;
    uint32_t size = 0;
};
// The run's bytes in the image (empty when they aren't all in it).
std::span<uint8_t> image_bytes(const ExeImage& image, Run run);

// An exe file laid out as Windows maps it (its headers and each section at its address), for the offline check;
// empty if it isn't a 32-bit PE file. `base` is set to its image base.
std::vector<uint8_t> map_exe_file(const std::vector<uint8_t>& file, uint32_t* base);

// The bytes of a text like "68 F0 4F 01 00" (two hex digits each, spaces between); empty if it isn't one.
std::vector<uint8_t> parse_hex(std::string_view text);
// "68 F0 4F 01 00": the bytes as text.
std::string hex_text(std::span<const uint8_t> bytes);
// "0x0046F576": a number as eight hex digits.
std::string hex_word(uint32_t value);

} // namespace retail_limits
