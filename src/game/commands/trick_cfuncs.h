#pragma once

#include <cstdint>

namespace script {
class Context;
class Struct;
} // namespace script

// Script commands (CFunctions) 0x005B0A40-0x005B1B60: dead objects, the configurable tricks (key combos bound in the
// profile's trick_mapping or its special trick slots, trick types, display text, locking), records, soundtracks,
// controller binding and vibration, the profile lock. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace commands {

bool cfunc_flush_dead_objects(script::Struct* params, script::Context context);
bool cfunc_bind_trick_to_key_combo(script::Struct* params, script::Context context);
bool cfunc_get_key_combo_bound_to_trick(script::Struct* params, script::Context context);
bool cfunc_update_trick_mappings(script::Struct* params, script::Context context);
bool cfunc_get_configurable_tricks_from_type(script::Struct* params, script::Context context);
bool cfunc_trick_is_locked(script::Struct* params, script::Context context);
bool cfunc_get_trick_display_text(script::Struct* params, script::Context context);
bool cfunc_get_special_trick_info(script::Struct* params, script::Context context);
bool cfunc_get_trick_type(script::Struct* params, script::Context context);
bool cfunc_get_index_of_item_containing(script::Struct* params, script::Context context);
bool cfunc_get_level_records(script::Struct* params, script::Context context);
bool cfunc_reset_combo_records(script::Struct* params, script::Context context);
bool cfunc_get_number_of_trick_occurrences(script::Struct* params, script::Context context);
bool cfunc_get_num_soundtracks(script::Struct* params, script::Context context);
bool cfunc_get_soundtrack_name(script::Struct* params, script::Context context);
bool cfunc_bind_controller_to_skater(script::Struct* params, script::Context context);
bool cfunc_bind_front_end_to_controller(script::Struct* params, script::Context context);
bool cfunc_controller_bound_to_different_skater(script::Struct* params, script::Context context);
bool cfunc_controller_bound_to_skater(script::Struct* params, script::Context context);
bool cfunc_get_key_combo_array_from_trick_array(script::Struct* params, script::Context context);
bool cfunc_first_input_received(script::Struct* params, script::Context context);
bool cfunc_vibrate_controller(script::Struct* params, script::Context context);
bool cfunc_lock_current_skater_profile_index(script::Struct* params, script::Context context);
bool cfunc_set_special_trick_info(script::Struct* params, script::Context context);

} // namespace commands
