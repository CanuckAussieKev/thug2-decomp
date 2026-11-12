#pragma once

// NOT ORIGINAL - thug2_format_check's checks of the demo files (formats/demo_format.h; format_check_demo.cpp).
namespace format_check_demo {

// A demo written and read back (every field, the input runs, the check points), a later version's chunk skipped, a
// newer version, a broken file and a missing chunk refused, the drift measure. Returns the number of failures.
int run();

} // namespace format_check_demo
