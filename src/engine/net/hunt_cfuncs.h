#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) of the network game in 0x005B1EE0-0x005B5320: the hunt (players placing objects for
// the others to find - the positions kept in the player's PlayerRecord, announced and reported as messages), entering
// vehicles, projectiles, the lobby's keyboard chat. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace commands {

bool __cdecl cfunc_place_object(script::Struct* params, script::Script* script);
bool __cdecl cfunc_announce_hunt_started(script::Struct* params, script::Script* script);
bool __cdecl cfunc_announce_need_to_place_objects(script::Struct* params, script::Script* script);
bool __cdecl cfunc_at_least_one_object_placed(script::Struct* params, script::Script* script);
bool __cdecl cfunc_report_object_pickup(script::Struct* params, script::Script* script);
bool __cdecl cfunc_placed_less_than(script::Struct* params, script::Script* script);
bool __cdecl cfunc_broadcast_enter_vehicle(script::Struct* params, script::Script* script);
bool __cdecl cfunc_lobby_check_keyboard(script::Struct* params, script::Script* script);
bool __cdecl cfunc_broadcast_object_placements(script::Struct* params, script::Script* script);
bool __cdecl cfunc_report_object_placement(script::Struct* params, script::Script* script);
bool __cdecl cfunc_broadcast_projectile(script::Struct* params, script::Script* script);

} // namespace commands
