#pragma once

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) for arrays, 0x0044AF50-0x0044B530: searching, sizes, and elements of arrays of arrays.
// All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

bool __cdecl cfunc_array_contains(script::Struct* params, script::Script* script);    // 0x0044AF50
bool __cdecl cfunc_get_array_size(script::Struct* params, script::Script* script);    // 0x0044B030
bool __cdecl cfunc_get_2d_array_data(script::Struct* params, script::Script* script); // 0x0044B0F0
bool __cdecl cfunc_get_3d_array_data(script::Struct* params, script::Script* script); // 0x0044B300
bool __cdecl cfunc_get_nd_array_data(script::Struct* params, script::Script* script); // 0x0044B530

} // namespace commands
