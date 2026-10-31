#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) 0x005B5770-0x005B5BE0: the skaters' set-up between levels (input reset and taken out
// of the world, shown again in their heaps), their scores (reset, refreshed, the score pot), and the nearest
// skater in reach of another team (or carrying the flag). All are cdecl (params, script) and return whether the
// script's `if` should be taken.
namespace commands {

bool __cdecl cfunc_initialize_skaters(script::Struct* params, script::Script* script);
bool __cdecl cfunc_reinsert_skaters(script::Struct* params, script::Script* script);
bool __cdecl cfunc_reset_score(script::Struct* params, script::Script* script);
bool __cdecl cfunc_update_score(script::Struct* params, script::Script* script);
bool __cdecl cfunc_reset_score_pot(script::Struct* params, script::Script* script);
bool __cdecl cfunc_get_colliding_player_and_team(script::Struct* params, script::Script* script);

} // namespace commands
