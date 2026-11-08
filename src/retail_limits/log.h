#pragma once

// NOT ORIGINAL - the retail-limits DLL's log (thug2-limits.log next to the game's exe): what it found, what it patched.

#include <string>
#include <string_view>

namespace retail_limits {

// Lines go to `path` from now on (the file started afresh); before that, and with an empty path, nowhere.
void log_open(const std::string& path);
void log_line(std::string_view line);

} // namespace retail_limits
