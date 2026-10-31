#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) 0x005B34A0-0x005B3A90: two objects swapping their models (renderable models,
// skeletons, shadows - a skater wearing another object's model), all swaps undone, and whether a skater's model is
// swapped. The swaps are recorded in a table of five entries at 0x007CEDE0 (0x005AA930 keeps it). All are cdecl
// (params, script) and return whether the script's `if` should be taken.
namespace commands {

bool __cdecl cfunc_swap_object_models(script::Struct* params, script::Script* script);
bool __cdecl cfunc_restore_all_swapped_object_models(script::Struct* params, script::Script* script);
bool __cdecl cfunc_get_skater_model_is_swapped(script::Struct* params, script::Script* script);

} // namespace commands
