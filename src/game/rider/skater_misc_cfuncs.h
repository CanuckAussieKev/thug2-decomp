#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) of the skater in 0x005B45F0-0x005B4F80: a custom logo on a board part, the skater's
// velocity (scaled, optionally turned), and a random one of its voice's bail streams. All are cdecl (params,
// script) and return whether the script's `if` should be taken.
namespace commands {

bool __cdecl cfunc_apply_custom_logo_to_part(script::Struct* params, script::Script* script);
bool __cdecl cfunc_get_skater_velocity(script::Struct* params, script::Script* script);
bool __cdecl cfunc_play_skater_stream(script::Struct* params, script::Script* script);

} // namespace commands
