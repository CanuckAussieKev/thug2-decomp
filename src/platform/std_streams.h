#pragma once

#include <cstdint>
#include <span>
#include <string_view>

// NOT ORIGINAL - the process's standard input and error as a program given pipes sees them (the exe's -encode-video:
// app/encode_video.h - a GUI program has no console, but a parent's pipes still reach it).
namespace platform {

// `into` filled from standard input; false at its end (or with no standard input) before it is full.
bool read_standard_input(std::span<uint8_t> into);
// `text` written to standard error (nothing without one).
void write_standard_error(std::string_view text);

} // namespace platform
