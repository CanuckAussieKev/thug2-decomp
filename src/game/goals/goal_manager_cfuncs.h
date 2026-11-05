#pragma once

// The goal manager's script commands (GoalManager_..., 0x005591C0-0x0055B98F and 0x0055D3F0-0x0055E97F;
// goal_manager_cfuncs*.cpp): each on the skate module's goal manager (goal_manager.h), most on one goal - the one
// named by the command's Name (asserted). All are cdecl (params, script) and return whether the script's `if` should
// be taken (true where nothing is asked).

namespace script {
class Context;
class Struct;
} // namespace script

namespace goals {

// --- goal_manager_cfuncs.cpp: 0x005591C0-0x0055997F ---
bool cfunc_goal_manager_edit_goal(script::Struct* params, script::Context context);              // 0x005591C0
bool cfunc_goal_manager_activate_goal(script::Struct* params, script::Context context);          // 0x00559230
bool cfunc_goal_manager_deactivate_goal(script::Struct* params, script::Context context);        // 0x00559290
bool cfunc_goal_manager_clear_last_goal(script::Struct* params, script::Context context);        // 0x005592F0
bool cfunc_goal_manager_win_goal(script::Struct* params, script::Context context);               // 0x00559310
bool cfunc_goal_manager_lose_goal(script::Struct* params, script::Context context);              // 0x00559350
bool cfunc_goal_manager_deactivate_all_goals(script::Struct* params, script::Context context);   // 0x00559390
bool cfunc_goal_manager_activate_all_goals(script::Struct* params, script::Context context);     // 0x005593D0
bool cfunc_goal_manager_uninitialize_all_goals(script::Struct* params, script::Context context); // 0x005593F0
bool cfunc_goal_manager_uninitialize_goal(script::Struct* params, script::Context context);      // 0x00559410
bool cfunc_goal_manager_initialize_all_goals(script::Struct* params, script::Context context);   // 0x00559490
bool cfunc_goal_manager_initialize_all_selected_goals(script::Struct* params,
                                                      script::Context context); // 0x005594B0
bool cfunc_goal_manager_activate_all_selected_goals(script::Struct* params,
                                                    script::Context context);                // 0x005594D0
bool cfunc_goal_manager_goal_is_active(script::Struct* params, script::Context context);     // 0x005594F0
bool cfunc_goal_manager_set_goal_timer(script::Struct* params, script::Context context);     // 0x00559540
bool cfunc_goal_manager_zero_goal_timer(script::Struct* params, script::Context context);    // 0x005595D0
bool cfunc_goal_manager_pause_all_goals(script::Struct* params, script::Context context);    // 0x00559630
bool cfunc_goal_manager_un_pause_all_goals(script::Struct* params, script::Context context); // 0x00559650
bool cfunc_goal_manager_set_goal_flag(script::Struct* params, script::Context context);      // 0x00559670
bool cfunc_goal_manager_has_seen_goal(script::Struct* params, script::Context context);      // 0x00559720
bool cfunc_goal_manager_all_flags_set(script::Struct* params, script::Context context);      // 0x00559770
bool cfunc_goal_manager_goal_flag_set(script::Struct* params, script::Context context);      // 0x005597C0
bool cfunc_goal_manager_can_retry_goal(script::Struct* params, script::Context context);     // 0x00559850
bool cfunc_goal_manager_get_goal_params(script::Struct* params, script::Context context);    // 0x00559870
bool cfunc_goal_manager_can_start_goal(script::Struct* params, script::Context context);     // 0x005598D0
bool cfunc_goal_manager_next_race_waypoint(script::Struct* params, script::Context context); // 0x005598E0
// 0x00559930: GoalManager_NextHorseSpot and GoalManager_NextTourSpot.
bool cfunc_goal_manager_next_waypoint(script::Struct* params, script::Context context);

// --- goal_manager_cfuncs2.cpp: 0x00559980-0x0055A6FF ---
bool cfunc_goal_manager_create_goal_name(script::Struct* params, script::Context context);          // 0x00559980
bool cfunc_goal_manager_get_level_prefix(script::Struct* params, script::Context context);          // 0x00559AF0
bool cfunc_goal_manager_set_graffiti_mode(script::Struct* params, script::Context context);         // 0x00559B40
bool cfunc_goal_manager_has_won_goal(script::Struct* params, script::Context context);              // 0x00559B80
bool cfunc_goal_manager_goal_is_selected(script::Struct* params, script::Context context);          // 0x00559BC0
bool cfunc_goal_manager_toggle_goal_selection(script::Struct* params, script::Context context);     // 0x00559C10
bool cfunc_goal_manager_deselect_all_goals(script::Struct* params, script::Context context);        // 0x00559C80
bool cfunc_goal_manager_goals_are_selected(script::Struct* params, script::Context context);        // 0x00559CA0
bool cfunc_goal_manager_got_counter_object(script::Struct* params, script::Context context);        // 0x00559CC0
bool cfunc_goal_manager_counter_goal_done(script::Struct* params, script::Context context);         // 0x00559D20
bool cfunc_goal_manager_add_goal_point(script::Struct* params, script::Context context);            // 0x00559D70
bool cfunc_goal_manager_spend_goal_points(script::Struct* params, script::Context context);         // 0x00559DB0
bool cfunc_goal_manager_has_goal_points(script::Struct* params, script::Context context);           // 0x00559DF0
bool cfunc_goal_manager_clear_goal_points(script::Struct* params, script::Context context);         // 0x00559E30
bool cfunc_goal_manager_get_number_of_goal_points(script::Struct* params, script::Context context); // 0x00559E50
bool cfunc_goal_manager_has_beaten_goal_with_proset(script::Struct* params,
                                                    script::Context context);                       // 0x00559E90
bool cfunc_goal_manager_get_proset_not_prefix(script::Struct* params, script::Context context);     // 0x00559EC0
bool cfunc_goal_manager_level_load(script::Struct* params, script::Context context);                // 0x00559F70
bool cfunc_goal_manager_unlock_all_goals(script::Struct* params, script::Context context);          // 0x00559F90
bool cfunc_goal_manager_deactivate_current_goal(script::Struct* params, script::Context context);   // 0x00559FB0
bool cfunc_goal_manager_set_start_time(script::Struct* params, script::Context context);            // 0x00559FD0
bool cfunc_goal_manager_update_combo_timer(script::Struct* params, script::Context context);        // 0x0055A020
bool cfunc_goal_manager_set_start_height(script::Struct* params, script::Context context);          // 0x0055A070
bool cfunc_goal_manager_check_height_record(script::Struct* params, script::Context context);       // 0x0055A0C0
bool cfunc_goal_manager_check_distance_record(script::Struct* params, script::Context context);     // 0x0055A110
bool cfunc_goal_manager_play_goal_start_stream(script::Struct* params, script::Context context);    // 0x0055A160
bool cfunc_goal_manager_play_goal_wait_stream(script::Struct* params, script::Context context);     // 0x0055A1B0
bool cfunc_goal_manager_play_goal_win_stream(script::Struct* params, script::Context context);      // 0x0055A200
bool cfunc_goal_manager_stop_current_stream(script::Struct* params, script::Context context);       // 0x0055A250
bool cfunc_goal_manager_turn_pro(script::Struct* params, script::Context context);                  // 0x0055A2A0
bool cfunc_goal_manager_pause_goal(script::Struct* params, script::Context context);                // 0x0055A2C0
bool cfunc_goal_manager_un_pause_goal(script::Struct* params, script::Context context);             // 0x0055A310
bool cfunc_goal_manager_pause_competition(script::Struct* params, script::Context context);         // 0x0055A360
bool cfunc_goal_manager_un_pause_competition(script::Struct* params, script::Context context);      // 0x0055A3B0
bool cfunc_goal_manager_goal_is_locked(script::Struct* params, script::Context context);            // 0x0055A400
bool cfunc_goal_manager_is_in_competition(script::Struct* params, script::Context context);         // 0x0055A430
bool cfunc_goal_manager_get_goal_animations(script::Struct* params, script::Context context);       // 0x0055A470
bool cfunc_goal_manager_play_goal_stream(script::Struct* params, script::Context context);          // 0x0055A500
bool cfunc_goal_manager_end_run_called(script::Struct* params, script::Context context);            // 0x0055A520
bool cfunc_goal_manager_clear_end_run(script::Struct* params, script::Context context);             // 0x0055A570
bool cfunc_goal_manager_finished_end_of_run(script::Struct* params, script::Context context);       // 0x0055A5C0
bool cfunc_goal_manager_started_end_of_run(script::Struct* params, script::Context context);        // 0x0055A610
bool cfunc_goal_manager_add_temp_special_trick(script::Struct* params, script::Context context);    // 0x0055A660
bool cfunc_goal_manager_remove_temp_special_trick(script::Struct* params, script::Context context); // 0x0055A6B0

// --- goal_manager_cfuncs3.cpp: 0x0055A700-0x0055B98F ---
bool cfunc_goal_manager_get_trick_from_key_combo(script::Struct* params, script::Context context);
bool cfunc_goal_manager_unlock_goal(script::Struct* params, script::Context context);
bool cfunc_goal_manager_quick_start_goal(script::Struct* params, script::Context context);
bool cfunc_goal_manager_initialize_goal(script::Struct* params, script::Context context);
bool cfunc_goal_manager_goal_is_expired(script::Struct* params, script::Context context);
bool cfunc_goal_manager_expire_goal(script::Struct* params, script::Context context);
bool cfunc_goal_manager_add_time(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_number_collected(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_number_of_flags(script::Struct* params, script::Context context);
bool cfunc_goal_manager_is_pro(script::Struct* params, script::Context context);
bool cfunc_goal_manager_reset_goal_flags(script::Struct* params, script::Context context);
bool cfunc_goal_manager_color_trick_objects(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_number_of_times_goal_started(script::Struct* params, script::Context context);
bool cfunc_goal_manager_goal_exists(script::Struct* params, script::Context context);
bool cfunc_goal_manager_set_can_start_goal(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_last_goal_id(script::Struct* params, script::Context context);
bool cfunc_goal_manager_clear_tetris_tricks(script::Struct* params, script::Context context);
bool cfunc_goal_manager_announce_goal_started(script::Struct* params, script::Context context);
bool cfunc_goal_manager_set_end_run_type(script::Struct* params, script::Context context);
bool cfunc_goal_manager_set_should_deactivate_on_expire(script::Struct* params, script::Context context);
bool cfunc_goal_manager_reset_career(script::Struct* params, script::Context context);
bool cfunc_goal_manager_update_family_trees(script::Struct* params, script::Context context);
bool cfunc_goal_manager_suspend_goal_ped_logic(script::Struct* params, script::Context context);
bool cfunc_goal_manager_set_difficulty_level(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_difficulty_level(script::Struct* params, script::Context context);
bool cfunc_goal_manager_restart_stage(script::Struct* params, script::Context context);
bool cfunc_goal_manager_can_restart_stage(script::Struct* params, script::Context context);
bool cfunc_goal_manager_set_goal_chapters_and_stages(script::Struct* params, script::Context context);
bool cfunc_goal_manager_advance_stage(script::Struct* params, script::Context context);
bool cfunc_goal_manager_film_goal_checkpoint(script::Struct* params, script::Context context);
bool cfunc_goal_manager_start_filming(script::Struct* params, script::Context context);
bool cfunc_goal_manager_set_sponsor(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_sponsor(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_team(script::Struct* params, script::Context context);
bool cfunc_goal_manager_set_team_member(script::Struct* params, script::Context context);
bool cfunc_goal_manager_kill_team_members(script::Struct* params, script::Context context);
bool cfunc_goal_manager_set_team_name(script::Struct* params, script::Context context);
bool cfunc_goal_manager_run_last_stage_script(script::Struct* params, script::Context context);
bool cfunc_goal_manager_goal_initialized(script::Struct* params, script::Context context);
bool cfunc_goal_manager_unload_last_fam(script::Struct* params, script::Context context);
bool cfunc_goal_manager_stop_last_stream(script::Struct* params, script::Context context);
bool cfunc_goal_manager_hide_all_goal_peds(script::Struct* params, script::Context context);
bool cfunc_goal_manager_set_trick_beat_pattern(script::Struct* params, script::Context context);
bool cfunc_goal_manager_reset_trick_beat_timer(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_goal_manager_params(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_active_reward_points(script::Struct* params, script::Context context);
bool cfunc_goal_manager_get_goal_reward_points(script::Struct* params, script::Context context);
bool cfunc_goal_collect_active(script::Struct* params, script::Context context);

// --- goal_manager_events.cpp: 0x0055D3F0-0x0055D954 ---
bool cfunc_goal_manager_add_goal(script::Struct* params, script::Context context);                  // 0x0055D3F0
bool cfunc_goal_manager_remove_goal(script::Struct* params, script::Context context);               // 0x0055D450
bool cfunc_goal_manager_remove_all_goals(script::Struct* params, script::Context context);          // 0x0055D4D0
bool cfunc_goal_manager_restart_last_goal(script::Struct* params, script::Context context);         // 0x0055D4F0
bool cfunc_goal_manager_level_unload(script::Struct* params, script::Context context);              // 0x0055D510
bool cfunc_goal_manager_num_goals_beaten_in_level(script::Struct* params, script::Context context); // 0x0055D530
bool cfunc_goal_manager_add_goal_choices(script::Struct* params, script::Context context);          // 0x0055D590
bool cfunc_goal_manager_get_goal_list(script::Struct* params, script::Context context);             // 0x0055D600
bool cfunc_goal_manager_replace_trick_text(script::Struct* params, script::Context context);        // 0x0055D7D0
bool cfunc_goal_manager_get_goal_attempt_info(script::Struct* params, script::Context context);     // 0x0055D8E0
bool cfunc_goal_manager_get_view_goals_info(script::Struct* params, script::Context context);       // 0x0055D910

// --- goal_manager_update.cpp: 0x0055E860-0x0055EB3F ---
bool cfunc_goal_manager_has_active_goals(script::Struct* params, script::Context context);          // 0x0055E860
bool cfunc_goal_manager_un_beat_all_goals(script::Struct* params, script::Context context);         // 0x0055E8A0
bool cfunc_goal_manager_add_view_goals_list(script::Struct* params, script::Context context);       // 0x0055E8C0
bool cfunc_goal_manager_goal_should_expire(script::Struct* params, script::Context context);        // 0x0055E8E0
bool cfunc_goal_manager_update_all_goals(script::Struct* params, script::Context context);          // 0x0055EAC0
bool cfunc_goal_manager_get_time_left_in_net_game(script::Struct* params, script::Context context); // 0x0055EAE0
bool cfunc_goal_manager_get_unlimited_time_in_net_game(script::Struct* params,
                                                       script::Context context); // 0x0055EB20

} // namespace goals
