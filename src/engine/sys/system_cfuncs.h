#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// The engine's platform and timer script commands (CFunctions), 0x0044E3A0-0x0044E4D4: which platform this is,
// seeding the random numbers, a stopwatch (ResetTimer / TimeGreaterThan) and the game clock's times. All are cdecl
// (params, script) and return whether the script's `if` should be taken.
namespace commands {

bool cfunc_is_win32(script::Struct* params, script::Script* script);          // 0x0044E3A0
bool cfunc_get_platform(script::Struct* params, script::Script* script);      // 0x0044E3B0
bool cfunc_randomize(script::Struct* params, script::Script* script);         // 0x0044E3D0
bool cfunc_reset_timer(script::Struct* params, script::Script* script);       // 0x0044E3F0
bool cfunc_time_greater_than(script::Struct* params, script::Script* script); // 0x0044E400
bool cfunc_get_start_time(script::Struct* params, script::Script* script);    // 0x0044E460
bool cfunc_get_elapsed_time(script::Struct* params, script::Script* script);  // 0x0044E490

} // namespace commands
