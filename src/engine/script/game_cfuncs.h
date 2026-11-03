#pragma once

#include <cstdint>
#include <array>
#include "engine/core/checksums.h"
#include "engine/maths/vec4.h"

namespace gfx {
class CtodManager;
}

namespace script {
class Struct;
class Script;
} // namespace script
namespace render {
struct WorldScene;
}
namespace objects {
class Entity;
}

// Script commands (CFunctions) of the skate game, 0x0059F610-0x005A5360 (after skate_cfuncs.h's): the viewer,
// controllers, preferences, the asynchronous loads, pausing, the game flow, the network state (hosting, joining,
// the server list, observers), the game modes, debouncing, objects made from structures. All are cdecl
// (params, script) and return whether the script's `if` should be taken.
namespace commands {
// The sizes of the net identity texts the scripts read and set.
constexpr size_t kNetworkIdBytes = 16;
constexpr size_t kNetworkPasswordBytes = 12;
constexpr size_t kHostedGameNameBytes = 16;
// What node_command does (the nodes of a name= or prefix=).
constexpr int kNodeActionCreate = 0;
constexpr int kNodeActionKill = 1;
constexpr int kNodeActionShow = 2;
constexpr int kNodeActionHide = 3;
constexpr int kNodeActionShatter = 4;
constexpr int kNodeActionAlive = 5;
constexpr int kNodeActionColour = 6;
constexpr int kNodeActionTerrain = 7;

// The network names (net_state_cfuncs.cpp; the command line sets them, winmain.cpp).
std::array<char, kNetworkIdBytes>& network_id();
std::array<char, kNetworkPasswordBytes>& network_password();
std::array<char, kHostedGameNameBytes>& hosted_game_name();

// --- the viewer, controllers, node arrays, preferences (game_cfuncs.cpp) --------------------------------------

bool cfunc_set_viewer_anim(script::Struct* params, script::Script* script);               // 0x0059F610
bool cfunc_set_viewer_lod_dist(script::Struct* params, script::Script* script);           // 0x0059F650
bool cfunc_reload_viewer_anim(script::Struct* params, script::Script* script);            // 0x0059F6C0
bool cfunc_check_for_holes(script::Struct* params, script::Script* script);               // 0x0059F730
bool cfunc_stop_second_controller_check(script::Struct* params, script::Script* script);  // 0x0059F740
bool cfunc_quit_game(script::Struct* params, script::Script* script);                     // 0x0059F760
bool cfunc_is_internet_game_host(script::Struct* params, script::Script* script);         // 0x0059F770
bool cfunc_is_joining_internet_game(script::Struct* params, script::Script* script);      // 0x0059F780
bool cfunc_trying_to_join_game(script::Struct* params, script::Script* script);           // 0x0059F7A0
bool cfunc_try_join_server_ip_success(script::Struct* params, script::Script* script);    // 0x0059F7B0
bool cfunc_gs_enable_net(script::Struct* params, script::Script* script);                 // 0x0059F7E0
bool cfunc_gs_disable_net(script::Struct* params, script::Script* script);                // 0x0059F7F0
bool cfunc_wait_for_async_load_to_finish(script::Struct* params, script::Script* script); // 0x0059F800
bool cfunc_get_custom_skater_filename(script::Struct* params, script::Script* script);    // 0x0059F930
bool cfunc_set_custom_skater_filename(script::Struct* params, script::Script* script);    // 0x0059F960
bool cfunc_controller_pressed(script::Struct* params, script::Script* script);            // 0x0059F9A0
bool cfunc_get_analogue_info(script::Struct* params, script::Script* script);             // 0x0059FA40
bool cfunc_node_array_man_load(script::Struct* params, script::Script* script);           // 0x0059FB30
bool cfunc_node_array_man_unload(script::Struct* params, script::Script* script);         // 0x0059FB90
bool cfunc_get_preference_string(script::Struct* params, script::Script* script);         // 0x0059FBC0
bool cfunc_get_preference_password(script::Struct* params, script::Script* script);       // 0x0059FC40
bool cfunc_get_preference_checksum(script::Struct* params, script::Script* script);       // 0x0059FD00
bool cfunc_toggle_net_metrics(script::Struct* params, script::Script* script);            // 0x0059FD80
bool cfunc_set_view_mode(script::Struct* params, script::Script* script);                 // 0x0059FD90
bool cfunc_set_viewer_model(script::Struct* params, script::Script* script);              // 0x005A1C80
bool cfunc_get_viewer_object_id(script::Struct* params, script::Script* script);          // 0x005A1D40
bool cfunc_reload_anim(script::Struct* params, script::Script* script);                   // 0x005A1D80
bool cfunc_debounce(script::Struct* params, script::Script* script);                      // 0x005A1E70
bool cfunc_start_keyboard_handler(script::Struct* params, script::Script* script);        // 0x005A2050
bool cfunc_stop_keyboard_handler(script::Struct* params, script::Script* script);         // 0x005A2080
bool cfunc_spawn_second_controller_check(script::Struct* params, script::Script* script); // 0x005A2100
bool cfunc_move_node(script::Struct* params, script::Script* script);                     // 0x005A21C0
bool cfunc_set_active_camera(script::Struct* params, script::Script* script);             // 0x005A2370
bool cfunc_show_tracking(script::Struct* params, script::Script* script);                 // 0x005A23E0
bool cfunc_choose_face_for_facemap(script::Struct* params, script::Script* script);       // 0x005A2790
bool cfunc_skater_is_custom(script::Struct* params, script::Script* script);              // 0x005A29A0
bool cfunc_set_scene_color(script::Struct* params, script::Script* script);               // 0x005A29E0
bool cfunc_add_restarts_to_menu(script::Struct* params, script::Script* script);          // 0x005A4260
bool cfunc_add_warp_points_to_menu(script::Struct* params, script::Script* script);       // 0x005A4560
bool cfunc_add_faces_to_menu(script::Struct* params, script::Script* script);             // 0x005A4730

// 0x0059F820: the second controller check's task (SpawnSecondControllerCheck): runs the script 0x07C28B45 when
// both controllers are plugged in, else 0x9C9DFCF8 - every frame while the task is on.
void second_pad_watch_tick(void* task);
// 0x0059F890: make the camera of the object `id` (its 0xC4E311FA component) the camera of `viewport`; with
// `take_current_view`, the object first moves to where the viewport's camera is now (and the component is told).
// False if there's no such object.
bool make_camera_active(checksums::Name id, int viewport, bool take_current_view);
// 0x0059FB10: a manager walk's callback: the object deletes itself (unless locked) if its id is `*id`.
void destroy_object_with_id(objects::Entity* object, void* id);

// --- cleaning up, object flags, skater scripts, pausing, the flash (game_flow_cfuncs.cpp) --------------------------

bool cfunc_cleanup(script::Struct* params, script::Script* script);           // 0x0059FE50
bool cfunc_make_skater_goto(script::Struct* params, script::Script* script);  // 0x005A0300
bool cfunc_make_skater_gosub(script::Struct* params, script::Script* script); // 0x005A03C0
bool cfunc_spawn_sound(script::Struct* params, script::Script* script);       // 0x005A0460
bool cfunc_pause_skater(script::Struct* params, script::Script* script);      // 0x005A0520
bool cfunc_un_pause_skater(script::Struct* params, script::Script* script);   // 0x005A0580
bool cfunc_pause_game(script::Struct* params, script::Script* script);        // 0x005A05C0
bool cfunc_un_pause_game(script::Struct* params, script::Script* script);     // 0x005A05E0
bool cfunc_game_is_paused(script::Struct* params, script::Script* script);    // 0x005A05F0
bool cfunc_pause_objects(script::Struct* params, script::Script* script);     // 0x005A0620
bool cfunc_un_pause_objects(script::Struct* params, script::Script* script);  // 0x005A0630
bool cfunc_reset_clock(script::Struct* params, script::Script* script);       // 0x005A0640
bool cfunc_pads_plugged_in(script::Struct* params, script::Script* script);   // 0x005A0670
bool cfunc_do_flash(script::Struct* params, script::Script* script);          // 0x005A0680
bool cfunc_clear_flag(script::Struct* params, script::Script* script);        // 0x005A3B00
bool cfunc_send_flag(script::Struct* params, script::Script* script);         // 0x005A3B20
bool cfunc_query_flag(script::Struct* params, script::Script* script);        // 0x005A3B40
bool cfunc_send_exception(script::Struct* params, script::Script* script);    // 0x005A3B60

// --- servers, the network mode and setup, going online, observing (net_state_cfuncs.cpp) ------------------------

bool cfunc_start_server(script::Struct* params, script::Script* script);               // 0x005A0880
bool cfunc_leave_server(script::Struct* params, script::Script* script);               // 0x005A08D0
bool cfunc_join_server(script::Struct* params, script::Script* script);                // 0x005A08E0
bool cfunc_set_network_mode(script::Struct* params, script::Script* script);           // 0x005A0A00
bool cfunc_profile_tasks(script::Struct* params, script::Script* script);              // 0x005A0A60
bool cfunc_use_network_preferences(script::Struct* params, script::Script* script);    // 0x005A0AB0
bool cfunc_test_net_setup(script::Struct* params, script::Script* script);             // 0x005A0AC0
bool cfunc_need_to_test_net_setup(script::Struct* params, script::Script* script);     // 0x005A0BD0
bool cfunc_can_change_devices(script::Struct* params, script::Script* script);         // 0x005A0BE0
bool cfunc_load_network_drivers(script::Struct* params, script::Script* script);       // 0x005A0BF0
bool cfunc_connect_to_internet(script::Struct* params, script::Script* script);        // 0x005A0C60
bool cfunc_cancel_connect_to_internet(script::Struct* params, script::Script* script); // 0x005A0CC0
bool cfunc_is_online(script::Struct* params, script::Script* script);                  // 0x005A0CD0
bool cfunc_disconnect_from_internet(script::Struct* params, script::Script* script);   // 0x005A0CE0
bool cfunc_enable_actuators(script::Struct* params, script::Script* script);           // 0x005A0D20
bool cfunc_in_net_game(script::Struct* params, script::Script* script);                // 0x005A0D80
bool cfunc_is_observing(script::Struct* params, script::Script* script);               // 0x005A0D90
bool cfunc_set_slomo(script::Struct* params, script::Script* script);                  // 0x005A0E50
bool cfunc_toggle_player_names(script::Struct* params, script::Script* script);        // 0x005A0EB0
bool cfunc_set_current_game_type(script::Struct* params, script::Script* script);      // 0x005A0EF0
bool cfunc_dump_net_message_stats(script::Struct* params, script::Script* script);     // 0x005A0F00
bool cfunc_set_server_mode(script::Struct* params, script::Script* script);            // 0x005A1020
bool cfunc_notify_bail_done(script::Struct* params, script::Script* script);           // 0x005A1050
bool cfunc_enter_observer_mode(script::Struct* params, script::Script* script);        // 0x005A10D0
bool cfunc_observe_next_skater(script::Struct* params, script::Script* script);        // 0x005A10E0
bool cfunc_refresh_server_list(script::Struct* params, script::Script* script);        // 0x005A10F0
bool cfunc_start_server_list(script::Struct* params, script::Script* script);          // 0x005A1120
bool cfunc_start_lobby_list(script::Struct* params, script::Script* script);           // 0x005A1130
bool cfunc_stop_server_list(script::Struct* params, script::Script* script);           // 0x005A1160
bool cfunc_free_server_list(script::Struct* params, script::Script* script);           // 0x005A1170
// (these two return 1 or 0 in all of EAX: some of their callers read it)
bool cfunc_in_split_screen_game(script::Struct* params, script::Script* script); // 0x005A1180
bool cfunc_in_multiplayer_game(script::Struct* params, script::Script* script);  // 0x005A11B0
bool cfunc_on_server(script::Struct* params, script::Script* script);           // 0x005A1220
bool cfunc_toggle_scores(script::Struct* params, script::Script* script);       // 0x005A12D0

// --- game modes and the level (game_flow_cfuncs.cpp) ------------------------------------------------------------

bool cfunc_round_increment(script::Struct* params, script::Script* script);                // 0x005A11C0
bool cfunc_game_mode_equals(script::Struct* params, script::Script* script);               // 0x005A11D0
bool cfunc_get_current_level(script::Struct* params, script::Script* script);              // 0x005A1230
bool cfunc_set_level_structure_name(script::Struct* params, script::Script* script);       // 0x005A1270
bool cfunc_get_level_structure_name(script::Struct* params, script::Script* script);       // 0x005A12A0
bool cfunc_restart_level(script::Struct* params, script::Script* script);                  // 0x005A12C0
bool cfunc_game_flow(script::Struct* params, script::Script* script);                      // 0x005A12E0
bool cfunc_clear_omnigons(script::Struct* params, script::Script* script);                 // 0x005A1320
bool cfunc_broadcast_omnigon(script::Struct* params, script::Script* script);              // 0x005A1540
bool cfunc_set_screen_mode(script::Struct* params, script::Script* script);                // 0x005A1720
bool cfunc_set_screen_mode_from_game_mode(script::Struct* params, script::Script* script); // 0x005A1770
bool cfunc_try_cheat_string(script::Struct* params, script::Script* script);               // 0x005A1840
bool cfunc_level_is(script::Struct* params, script::Script* script);                       // 0x005A1A90
bool cfunc_set_stat_override(script::Struct* params, script::Script* script);              // 0x005A1F80
bool cfunc_toggle_rails(script::Struct* params, script::Script* script);                   // 0x005A1FB0
bool cfunc_get_time_limit(script::Struct* params, script::Script* script);                 // 0x005A1FD0
bool cfunc_get_goals_mode(script::Struct* params, script::Script* script);                 // 0x005A1FF0
bool cfunc_get_team_graffiti_mode(script::Struct* params, script::Script* script);         // 0x005A2010
bool cfunc_get_game_mode(script::Struct* params, script::Script* script);                  // 0x005A2030
bool cfunc_set_tester_script(script::Struct* params, script::Script* script);              // 0x005A2090
bool cfunc_kill_tester_script(script::Struct* params, script::Script* script);             // 0x005A20F0
bool cfunc_auto_rail(script::Struct* params, script::Script* script);                      // 0x005A21A0
bool cfunc_spawn_skater_script(script::Struct* params, script::Script* script);            // 0x005A4170

// --- the lobby and network options (net_state_cfuncs.cpp) -------------------------------------------------------

bool cfunc_join_with_password(script::Struct* params, script::Script* script);           // 0x005A1680
bool cfunc_load_pending_players(script::Struct* params, script::Script* script);         // 0x005A1820
bool cfunc_launch_queued_scripts(script::Struct* params, script::Script* script);        // 0x005A1830
bool cfunc_start_network_lobby(script::Struct* params, script::Script* script);          // 0x005A1AC0
bool cfunc_observers_allowed(script::Struct* params, script::Script* script);            // 0x005A1AD0
bool cfunc_num_players_allowed(script::Struct* params, script::Script* script);          // 0x005A1AF0
bool cfunc_auto_dns(script::Struct* params, script::Script* script);                     // 0x005A1B30
bool cfunc_using_default_master_servers(script::Struct* params, script::Script* script); // 0x005A1B70
bool cfunc_using_dhcp(script::Struct* params, script::Script* script);                   // 0x005A1BB0
bool cfunc_in_internet_mode(script::Struct* params, script::Script* script);             // 0x005A1BF0
bool cfunc_entering_net_game(script::Struct* params, script::Script* script);            // 0x005A1C00
bool cfunc_device_chosen(script::Struct* params, script::Script* script);                // 0x005A1C30
bool cfunc_game_is_over(script::Struct* params, script::Script* script);                 // 0x005A1C70
bool cfunc_try_join_internet_game(script::Struct* params, script::Script* script);       // 0x005A28A0
bool cfunc_try_join_server_ip(script::Struct* params, script::Script* script);           // 0x005A28D0
bool cfunc_try_join_server_ip_cancel(script::Struct* params, script::Script* script);    // 0x005A2960
bool cfunc_force_server_list_refresh(script::Struct* params, script::Script* script);    // 0x005A2990
bool cfunc_init_prefs_beenox(script::Struct* params, script::Script* script);            // 0x005A4D90
// 0x005A4900: the network preferences set from the host settings' globals (server name, password, network id,
// players, team mode, skill level).
void init_beenox_preferences();

// --- the viewer camera on the scenes, node objects (node_object_cfuncs.cpp) --------------------------------------

bool cfunc_center_camera(script::Struct* params, script::Script* script);         // 0x005A2D60
bool cfunc_create_from_structure(script::Struct* params, script::Script* script); // 0x005A34D0
bool cfunc_parse_node_array(script::Struct* params, script::Script* script);      // 0x005A34E0
bool cfunc_re_load_node_array(script::Struct* params, script::Script* script);    // 0x005A39F0
// 0x005A2A00: the viewer's camera object placed to look at a scene's sectors (all, or those named `only`).
void fit_viewer_camera(render::WorldScene* scene, float scale, const maths::Vec4& angles, uint32_t only);
// 0x005A2E50 / 0x005A32A0: the object of a node (its structure / its name) made by its class.
bool create_from_node(script::Struct* node);
bool create_from_node_name(uint32_t name);
// 0x005A32C0: the object of the node `name` killed (switched off) by its class.
bool kill_from_node_name(uint32_t name, script::Script* script);
bool cfunc_create(script::Struct* params, script::Script* script);           // 0x005A4FF0
bool cfunc_set_object_color(script::Struct* params, script::Script* script); // 0x005A5010
bool cfunc_kill(script::Struct* params, script::Script* script);             // 0x005A5030
bool cfunc_visible(script::Struct* params, script::Script* script);          // 0x005A5050
bool cfunc_invisible(script::Struct* params, script::Script* script);        // 0x005A5070
bool cfunc_shatter(script::Struct* params, script::Script* script);          // 0x005A5090
// 0x005A3B80 / 0x005A3D10 / 0x005A3E80 / 0x005A3FD0 / 0x005A40B0: a node's thing (by its name) hidden or shown (in
// viewport 1, or 2 with `second_viewport` 1), alive?, coloured, shattered, given a terrain.
bool set_node_hidden(uint32_t name, bool hide, int second_viewport);
bool node_is_alive(uint32_t name);
bool set_node_color(uint32_t name, script::Struct* params);
bool shatter_node(uint32_t name);
bool set_node_terrain(uint32_t name, script::Struct* params);
// 0x005A4DA0: one of those (or create / kill) on the node `name`: 0 create, 1 kill, 2 show, 3 hide, 4 show and
// shatter, 5 alive?, 6 colour, 7 terrain.
bool node_command(checksums::Name node, int action, script::Script* script, script::Struct* params);
// 0x005A4EF0 (8 callers): node_command on name= or the nodes of prefix=.
bool node_command_by_name(script::Struct* params, script::Script* script, int action);

// What flag_command_by_name does to the objects: set / clear / test a script flag (of 32), set / clear object flag
// bits, send an exception.
constexpr int kFlagActionSet = 0;
constexpr int kFlagActionClear = 1;
constexpr int kFlagActionTest = 2;
constexpr int kFlagActionSetObjectFlags = 3;
constexpr int kFlagActionClearObjectFlags = 4;
constexpr int kFlagActionException = 5;
// 0x005A01D0: the flag (a number) or exception (a checksum) of `params`, done to the objects with the id name= or
// those of the nodes prefix=; false without either. Whether it was done (for a test: whether one has the flag).
bool flag_command_by_name(script::Struct* params, script::Script* script, int action);
// 0x005A01B0: its manager walk's callback (`data`: {id, value, action, result, found}): the command done if the
// object has the id.
void apply_flag_command_to_object(objects::Entity* object, void* data);

} // namespace commands
