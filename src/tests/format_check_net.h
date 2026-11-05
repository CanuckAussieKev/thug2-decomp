#pragma once

// NOT ORIGINAL - thug2_format_check's network message checks (format_check_net.cpp).
namespace format_check_net {

// Check every message reader and writer against the original's layouts. Returns the number of failures.
int run();

} // namespace format_check_net
