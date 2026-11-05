#pragma once

namespace script {
class Context;
class Struct;
} // namespace script

// Script commands (CFunctions) of the game modes' runs, 0x005AF090-0x005AF74F and 0x005AFB40-0x005AFD7F: the split
// screen options, competitions (scores, places, rounds), horse (its runs, players, panel strings), the trick
// objects given away, the game mode's score options, a skater's bonus, whether there's a local skater. All are cdecl
// (params, script) and return whether the script's `if` should be taken (true where nothing is asked).
namespace commands {

bool cfunc_apply_split_screen_options(script::Struct* params, script::Context context);  // 0x005AF090
bool cfunc_start_competition(script::Struct* params, script::Context context);           // 0x005AF200
bool cfunc_start_competition_run(script::Struct* params, script::Context context);       // 0x005AF320
bool cfunc_end_competition_run(script::Struct* params, script::Context context);         // 0x005AF340
bool cfunc_place_is(script::Struct* params, script::Context context);                    // 0x005AF3A0
bool cfunc_round_is(script::Struct* params, script::Context context);                    // 0x005AF3D0
bool cfunc_end_competition(script::Struct* params, script::Context context);             // 0x005AF400
bool cfunc_competition_ended(script::Struct* params, script::Context context);           // 0x005AF440
bool cfunc_start_horse(script::Struct* params, script::Context context);                 // 0x005AF450
bool cfunc_horse_ended(script::Struct* params, script::Context context);                 // 0x005AF470
bool cfunc_horse_status_equals(script::Struct* params, script::Context context);         // 0x005AF480
bool cfunc_start_horse_run(script::Struct* params, script::Context context);             // 0x005AF4A0
bool cfunc_end_horse_run(script::Struct* params, script::Context context);               // 0x005AF4C0
bool cfunc_switch_horse_players(script::Struct* params, script::Context context);        // 0x005AF4E0
bool cfunc_apply_to_horse_panel_string(script::Struct* params, script::Context context); // 0x005AF500
bool cfunc_get_horse_string(script::Struct* params, script::Context context);            // 0x005AF660
bool cfunc_is_current_horse_skater(script::Struct* params, script::Context context);     // 0x005AF700
bool cfunc_trick_off_all_objects(script::Struct* params, script::Context context);       // 0x005AFB40
bool cfunc_set_score_degradation(script::Struct* params, script::Context context);       // 0x005AFB80
bool cfunc_set_trick_score_tracking(script::Struct* params, script::Context context);    // 0x005AFBC0
bool cfunc_set_score_accumulation(script::Struct* params, script::Context context);      // 0x005AFC00
bool cfunc_add_bonus(script::Struct* params, script::Context context);                   // 0x005AFC70
bool cfunc_get_bonus_value(script::Struct* params, script::Context context);             // 0x005AFCF0
bool cfunc_local_skater_exists(script::Struct* params, script::Context context);         // 0x005AFD60

} // namespace commands
