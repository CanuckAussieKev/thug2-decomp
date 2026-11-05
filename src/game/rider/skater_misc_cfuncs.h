#pragma once

#include <cstdint>

namespace script {
class Context;
class Struct;
} // namespace script

// Script commands (CFunctions) of the skater in 0x005B45F0-0x005B4F80: a custom logo on a board part, the skater's
// velocity (scaled, optionally turned), and a random one of its voice's bail streams. All are cdecl (params,
// script) and return whether the script's `if` should be taken.
namespace commands {

bool cfunc_apply_custom_logo_to_part(script::Struct* params, script::Context context);
bool cfunc_get_skater_velocity(script::Struct* params, script::Context context);
bool cfunc_play_skater_stream(script::Struct* params, script::Context context);

} // namespace commands
