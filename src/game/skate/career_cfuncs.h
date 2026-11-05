#pragma once

namespace script {
class Context;
class Struct;
} // namespace script

// Script commands (CFunctions) of the career and the run, 0x005AE990-0x005AF08F: the career's level, scores,
// classic goal counts and chapter, its goals, level flags and global flags, ProfileEquals, IsCareerMode, ending the
// run, the first trick, the final scores. All are cdecl (params, script) and return whether the script's `if` should
// be taken (true where nothing is asked).
//
// Where a command reads a parameter it asserts on (a compiled-out assert) and the parameter is missing, the original
// goes on with an uninitialised local (what the stack held): here 0.
namespace commands {

bool cfunc_career_start_level(script::Struct* params, script::Context context);           // 0x005AE990
bool cfunc_career_level_is(script::Struct* params, script::Context context);              // 0x005AE9C0
bool cfunc_get_career_level_score(script::Struct* params, script::Context context);       // 0x005AE9F0
bool cfunc_set_level_total_points(script::Struct* params, script::Context context);       // 0x005AEAA0
bool cfunc_get_classic_level_goal_count(script::Struct* params, script::Context context); // 0x005AEAE0
bool cfunc_set_classic_total_goals(script::Struct* params, script::Context context);      // 0x005AEB80
bool cfunc_get_current_chapter(script::Struct* params, script::Context context);          // 0x005AEC20
bool cfunc_set_current_chapter(script::Struct* params, script::Context context);          // 0x005AEC50
bool cfunc_career_reset(script::Struct* params, script::Context context);                 // 0x005AEC80
bool cfunc_set_goal(script::Struct* params, script::Context context);                     // 0x005AECC0
bool cfunc_un_set_goal(script::Struct* params, script::Context context);                  // 0x005AED10
bool cfunc_get_goal(script::Struct* params, script::Context context);                     // 0x005AED40
bool cfunc_just_got_goal(script::Struct* params, script::Context context);                // 0x005AED70
bool cfunc_set_flag(script::Struct* params, script::Context context);                     // 0x005AEDA0
bool cfunc_un_set_flag(script::Struct* params, script::Context context);                  // 0x005AEDD0
bool cfunc_get_flag(script::Struct* params, script::Context context);                     // 0x005AEE30
bool cfunc_just_got_flag(script::Struct* params, script::Context context);                // 0x005AEEA0
bool cfunc_set_global_flag(script::Struct* params, script::Context context);              // 0x005AEED0
bool cfunc_un_set_global_flag(script::Struct* params, script::Context context);           // 0x005AEF00
bool cfunc_get_global_flag(script::Struct* params, script::Context context);              // 0x005AEF30
bool cfunc_profile_equals(script::Struct* params, script::Context context);               // 0x005AEF60
bool cfunc_is_career_mode(script::Struct* params, script::Context context);               // 0x005AEFC0
bool cfunc_end_run(script::Struct* params, script::Context context);                      // 0x005AEFE0
bool cfunc_should_end_run(script::Struct* params, script::Context context);               // 0x005AEFF0
bool cfunc_end_run_selected(script::Struct* params, script::Context context);             // 0x005AF020
bool cfunc_all_skaters_are_idle(script::Struct* params, script::Context context);         // 0x005AF030
bool cfunc_first_trick_started(script::Struct* params, script::Context context);          // 0x005AF040
bool cfunc_first_trick_completed(script::Struct* params, script::Context context);        // 0x005AF050
bool cfunc_calculate_final_scores(script::Struct* params, script::Context context);       // 0x005AF060

} // namespace commands
