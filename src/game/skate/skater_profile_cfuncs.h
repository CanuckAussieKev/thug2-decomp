#pragma once

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) of the skater profiles and the players' options (0x005AAEF0-0x005B0A40, the profile
// ones): vibration, auto kick and spin taps per player (the skate module's +0x394 table, 8 bytes a player), the
// current pro's display name, selecting the current skater. All are cdecl (params, script) and return whether the
// script's `if` should be taken (true where nothing is asked).
namespace commands {

bool __cdecl cfunc_restore_temporary_appearance(script::Struct* params, script::Script* script); // 0x005AAEF0
bool __cdecl cfunc_sync_player2_profile(script::Struct* params, script::Script* script);      // 0x005AAF90
bool __cdecl cfunc_reset_all_to_default_stats(script::Struct* params, script::Script* script);  // 0x005ABBF0
bool __cdecl cfunc_reset_to_default_profile(script::Struct* params, script::Script* script);   // 0x005ABE30
bool __cdecl cfunc_reset_all_to_default_profile(script::Struct* params, script::Script* script); // 0x005ABF00
bool __cdecl cfunc_for_each_skater_name(script::Struct* params, script::Script* script);       // 0x005ABF70
bool __cdecl cfunc_for_each_skater_profile(script::Struct* params, script::Script* script);    // 0x005AC0C0
bool __cdecl cfunc_get_skater_profile_info_by_name(script::Struct* params, script::Script* script); // 0x005AC1F0
bool __cdecl cfunc_set_skater_profile_info_by_name(script::Struct* params, script::Script* script); // 0x005AC280
bool __cdecl cfunc_print_current_appearance(script::Struct* params, script::Script* script);  // 0x005AC320
bool __cdecl cfunc_set_neversoft_skater(script::Struct* params, script::Script* script);      // 0x005AC340
bool __cdecl cfunc_reset_skaters(script::Struct* params, script::Script* script);            // 0x005AC3B0
bool __cdecl cfunc_set_skater_profile_info(script::Struct* params, script::Script* script);    // 0x005AC410
bool __cdecl cfunc_get_skater_profile_info(script::Struct* params, script::Script* script);    // 0x005AC470
bool __cdecl cfunc_set_skater_profile_property(script::Struct* params, script::Script* script); // 0x005AC4B0
bool __cdecl cfunc_set_classic_stat(script::Struct* params, script::Script* script);          // 0x005AC520
bool __cdecl cfunc_toggle_always_special(script::Struct* params, script::Script* script);     // 0x005AC570
bool __cdecl cfunc_vibration_is_on(script::Struct* params, script::Script* script);           // 0x005AE370
bool __cdecl cfunc_vibration_on(script::Struct* params, script::Script* script);             // 0x005AE3A0
bool __cdecl cfunc_vibration_off(script::Struct* params, script::Script* script);            // 0x005AE3D0
bool __cdecl cfunc_auto_kick_is_on(script::Struct* params, script::Script* script);            // 0x005AE400
bool __cdecl cfunc_auto_kick_on(script::Struct* params, script::Script* script);              // 0x005AE430
bool __cdecl cfunc_auto_kick_off(script::Struct* params, script::Script* script);             // 0x005AE460
bool __cdecl cfunc_spin_taps_are_on(script::Struct* params, script::Script* script);           // 0x005AE490
bool __cdecl cfunc_spin_taps_on(script::Struct* params, script::Script* script);              // 0x005AE4C0
bool __cdecl cfunc_spin_taps_off(script::Struct* params, script::Script* script);             // 0x005AE4F0
bool __cdecl cfunc_get_current_pro_display_info(script::Struct* params, script::Script* script); // 0x005AE520
bool __cdecl cfunc_select_current_skater(script::Struct* params, script::Script* script);     // 0x005AE900
bool __cdecl cfunc_award_stat_point(script::Struct* params, script::Script* script);          // 0x005AF950
bool __cdecl cfunc_award_special_trick_slot(script::Struct* params, script::Script* script);   // 0x005AF9F0
bool __cdecl cfunc_update_skater_stats(script::Struct* params, script::Script* script);       // 0x005AFAA0
bool __cdecl cfunc_get_current_skater_profile_index(script::Struct* params, script::Script* script); // 0x005AFFE0
bool __cdecl cfunc_get_custom_skater_name(script::Struct* params, script::Script* script);     // 0x005B0000
bool __cdecl cfunc_get_current_ped_pro_name(script::Struct* params, script::Script* script);    // 0x005B0030
bool __cdecl cfunc_get_stat_value(script::Struct* params, script::Script* script);            // 0x005B0280
bool __cdecl cfunc_get_num_stat_points_available(script::Struct* params, script::Script* script); // 0x005B0360
bool __cdecl cfunc_unlock_skater(script::Struct* params, script::Script* script);            // 0x005B03B0

} // namespace commands
