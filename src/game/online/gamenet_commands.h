#pragma once

#include <cstdint>

namespace script {
class Context;
class Struct;
} // namespace script

// The network manager's script commands (CFunctions, online::Hub, 0x00535DA0-0x00539570): capture the flag,
// the game's state, the join mode, the lobby and the message of the day, first-come-first-served requests to the server
// (start, level, pro set, goals), teams, the crown, starting and ending network games, the player list
// (gamenet_commands.cpp, ...). All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace online {

// --- gamenet_commands.cpp: 0x00535DA0-0x00536460 ---
bool cfunc_start_ctf_game(script::Struct* params, script::Context context);                     // 0x00535DA0
bool cfunc_end_ctf_game(script::Struct* params, script::Context context);                       // 0x00535DC0
bool cfunc_display_flag_base_warning(script::Struct* params, script::Context context);          // 0x00535DE0
bool cfunc_network_game_pending(script::Struct* params, script::Context context);               // 0x00535E20
bool cfunc_mark_game_over(script::Struct* params, script::Context context);                     // 0x00535E30
bool cfunc_set_join_mode(script::Struct* params, script::Context context);                      // 0x00535E40
bool cfunc_get_num_teams(script::Struct* params, script::Context context);                      // 0x00535E90
bool cfunc_connected_to_peer(script::Struct* params, script::Context context);                  // 0x00535EB0
bool cfunc_in_group_room(script::Struct* params, script::Context context);                      // 0x00535EE0
bool cfunc_is_auto_serving(script::Struct* params, script::Context context);                    // 0x00535EF0
bool cfunc_change_level_pending(script::Struct* params, script::Context context);               // 0x00535F20
bool cfunc_cancel_motd_download(script::Struct* params, script::Context context);               // 0x00535F30
bool cfunc_download_motd(script::Struct* params, script::Context context);                      // 0x00535F40
bool cfunc_services_are_available(script::Struct* params, script::Context context);             // 0x00535F50
bool cfunc_already_got_motd(script::Struct* params, script::Context context);                   // 0x00535FA0
bool cfunc_join_server_complete(script::Struct* params, script::Context context);               // 0x00535FB0
bool cfunc_just_started_net_game(script::Struct* params, script::Context context);              // 0x00535FC0
bool cfunc_fcfs_request_start_game(script::Struct* params, script::Context context);            // 0x00535FE0
bool cfunc_fcfs_request_change_level(script::Struct* params, script::Context context);          // 0x00536170
bool cfunc_reattempt_join_server(script::Struct* params, script::Context context);              // 0x00536240
bool cfunc_fcfs_request_toggle_pro_set(script::Struct* params, script::Context context);        // 0x00536260
bool cfunc_fcfs_request_toggle_goal_selection(script::Struct* params, script::Context context); // 0x005362F0
bool cfunc_reset_pro_set_flags(script::Struct* params, script::Context context);                // 0x00536360
bool cfunc_has_signed_disclaimer(script::Struct* params, script::Context context);              // 0x00536460
bool cfunc_found_servers(script::Struct* params, script::Context context);                      // 0x00537100

// --- gamenet_game_commands.cpp: 0x00538050-0x00539570 ---
bool cfunc_team_flag_taken(script::Struct* params, script::Context context);             // 0x00538050
bool cfunc_spawn_crown(script::Struct* params, script::Context context);                 // 0x00538170
bool cfunc_start_network_game(script::Struct* params, script::Context context);          // 0x00538280
bool cfunc_send_game_over_to_observers(script::Struct* params, script::Context context); // 0x00538620
bool cfunc_end_network_game(script::Struct* params, script::Context context);            // 0x00538720
bool cfunc_get_my_team(script::Struct* params, script::Context context);                 // 0x005388C0
bool cfunc_set_num_teams(script::Struct* params, script::Context context);               // 0x005388F0
bool cfunc_join_team(script::Struct* params, script::Context context);                   // 0x00538C90
bool cfunc_is_host(script::Struct* params, script::Context context);                     // 0x00538D70
bool cfunc_all_players_are_ready(script::Struct* params, script::Context context);       // 0x00538DC0
bool cfunc_create_player_options(script::Struct* params, script::Context context);       // 0x00538E70
bool cfunc_fill_player_list_menu(script::Struct* params, script::Context context);       // 0x00539080
bool cfunc_remove_player(script::Struct* params, script::Context context);               // 0x00539470
bool cfunc_cancel_remove_player(script::Struct* params, script::Context context);        // 0x00539480
bool cfunc_toggle_pro_set(script::Struct* params, script::Context context);              // 0x00539570

// (the first-come-first-served start request, message 0x60 - the host's game options from the network preferences:
// StartRequest in gamenet_messages.h)

} // namespace online
