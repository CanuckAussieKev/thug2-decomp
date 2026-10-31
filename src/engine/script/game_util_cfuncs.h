#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) of the game in 0x005B1B60-0x005B34A0: interpolating parameters, a texture's name from
// a path, clearing power-ups, the nearest rail point, the park's name, the special bar's colours, the default
// initials, the fireball level, a skater's last name, the directional light's debug lines, the slap game,
// a preference set from a menu. All are
// cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

bool __cdecl cfunc_interpolate_parameters(script::Struct* params, script::Script* script);
bool __cdecl cfunc_get_texture_from_path(script::Struct* params, script::Script* script);
bool __cdecl cfunc_clear_powerups(script::Struct* params, script::Script* script);
bool __cdecl cfunc_find_nearest_rail_point(script::Struct* params, script::Script* script);
bool __cdecl cfunc_get_park_name(script::Struct* params, script::Script* script);
bool __cdecl cfunc_set_special_bar_colors(script::Struct* params, script::Script* script);
bool __cdecl cfunc_get_initials_string(script::Struct* params, script::Script* script);
bool __cdecl cfunc_set_initials_string(script::Struct* params, script::Script* script);
bool __cdecl cfunc_get_fireball_level(script::Struct* params, script::Script* script);
bool __cdecl cfunc_get_skater_last_name_string(script::Struct* params, script::Script* script);
bool __cdecl cfunc_draw_directional_light_lines(script::Struct* params, script::Script* script);
// (also called directly by the game: the arguments are ignored)
bool __cdecl cfunc_in_slap_game(script::Struct* params, script::Script* script);
bool __cdecl cfunc_set_preferences_from_ui(script::Struct* params, script::Script* script);

} // namespace commands
