#pragma once

// NOT ORIGINAL - what automation.cpp's pieces share (the channel's constants, the runner's state, the element and
// refusal helpers more than one piece uses). They were the one file's anonymous namespace; here they are in `internal`,
// and each piece that includes this has a using-directive for them (in the piece, not here: SF.7), so it sees them as
// it saw them there.

#include <windows.h>
#include <cstdint>
#include <string>
#include <gsl/pointers>

namespace frontend::automation::internal {

constexpr int kDirectionZIndex = 5;                 // a camera command's numbers: position 0-2, direction 3-5, drift 6-8

uint32_t name_hash(const std::string& name);

std::string get_lower(std::string text);

bool is_identifier(const std::string& text);

bool is_hex(const std::string& text);

} // namespace frontend::automation::internal
