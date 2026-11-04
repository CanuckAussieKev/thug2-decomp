#pragma once

#include <array>
#include <cstddef>

#include "engine/core/format.h"

// NOT ORIGINAL - error lines into thug2-decomp.log from code that can't include the platform's log (the base layer
// sees no OS): a sink the platform layer installs first thing in WinMain (platform::install_output_sinks), which
// writes the line as debug_log::printf(StageMap::kError, ...) does. Without one (the check tools) a line goes nowhere.
namespace core {

constexpr std::size_t kErrorLineSize = 1024; // the buffer a line is formatted into (as debug_log's)

using ErrorLineSink = void (*)(const char* line);
void set_error_line_sink(ErrorLineSink sink);

// A formatted line to the sink, as it is (the line end is the caller's).
void error_line(const char* line);

// (the format checked against the arguments when it's compiled: text::format)
template <typename... Args>
void log_error(text::FormatFor<Args...> format, Args... args)
{
    std::array<char, kErrorLineSize> line{};
    text::format_to(line, format, args...);
    error_line(line.data());
}

} // namespace core
