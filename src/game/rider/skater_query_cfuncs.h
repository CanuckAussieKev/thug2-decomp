#pragma once

namespace script {
class Context;
class Struct;
} // namespace script

// Script commands (CFunctions) asking about the skaters, 0x005AC5F0-0x005AD82F: the local skater's speed and scores
// (last trick, total, the score pot), the network game's players (accumulated scores, elimination, who is left,
// places), the career's goal count, the skater camera's mode, skater ids and sex. All are cdecl (params, script) and
// return whether the script's `if` should be taken.
namespace commands {

bool cfunc_skater_speed_greater_than(script::Struct* params, script::Context context);             // 0x005AC5F0
bool cfunc_skater_speed_less_than(script::Struct* params, script::Context context);                // 0x005AC660
bool cfunc_skater_last_score_landed_greater_than(script::Struct* params, script::Context context); // 0x005AC6D0
bool cfunc_skater_last_score_landed_less_than(script::Struct* params, script::Context context);    // 0x005AC720
bool cfunc_skater_get_last_score_landed(script::Struct* params, script::Context context);          // 0x005AC770
bool cfunc_accumulate_scores(script::Struct* params, script::Context context);                     // 0x005AC7B0
bool cfunc_eliminate_last_place_player(script::Struct* params, script::Context context);           // 0x005AC850
bool cfunc_only_one_player_left(script::Struct* params, script::Context context);                  // 0x005ACAA0
bool cfunc_any_skater_total_score_at_least(script::Struct* params, script::Context context);       // 0x005ACC20
bool cfunc_only_one_skater_left(script::Struct* params, script::Context context);                  // 0x005ACD80
bool cfunc_skater_total_score_greater_than(script::Struct* params, script::Context context);       // 0x005ACF50
bool cfunc_skater_total_score_less_than(script::Struct* params, script::Context context);          // 0x005ACFA0
bool cfunc_skater_current_score_pot_greater_than(script::Struct* params, script::Context context); // 0x005ACFF0
bool cfunc_skater_current_score_pot_less_than(script::Struct* params, script::Context context);    // 0x005AD070
bool cfunc_skater_get_score_info(script::Struct* params, script::Context context);                 // 0x005AD0D0
bool cfunc_in_first_place(script::Struct* params, script::Context context);                        // 0x005AD140
bool cfunc_in_last_place(script::Struct* params, script::Context context);                         // 0x005AD2D0
bool cfunc_in_second_to_last_place(script::Struct* params, script::Context context);               // 0x005AD450
bool cfunc_goals_greater_than(script::Struct* params, script::Context context);                    // 0x005AD5C0
bool cfunc_goals_equal_to(script::Struct* params, script::Context context);                        // 0x005AD610
bool cfunc_toggle_skater_cam_mode(script::Struct* params, script::Context context);                // 0x005AD660
bool cfunc_get_skater_id(script::Struct* params, script::Context context);                         // 0x005AD6C0
bool cfunc_skater_exists(script::Struct* params, script::Context context);                         // 0x005AD730
bool cfunc_get_skater_sex(script::Struct* params, script::Context context);                        // 0x005AD780
bool cfunc_get_current_skater_id(script::Struct* params, script::Context context);                 // 0x005AD7E0
bool cfunc_get_skater_position(script::Struct* params, script::Context context);                   // 0x005B0070
bool cfunc_set_skater_velocity(script::Struct* params, script::Context context);                   // 0x005B00F0

} // namespace commands
