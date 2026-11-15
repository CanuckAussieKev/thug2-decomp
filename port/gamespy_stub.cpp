// NOT ORIGINAL - the macOS (arm64) port's stand-ins for the GameSpy SDK's glue (platform/gamespy.h): the SDK is a
// Windows library in this build (cmake/gamespy_sdk.cmake is not included for a portable configure), so the online
// session's calls answer with nothing - no lobby, no rooms, no buddy list, no stats: the game is played alone. Online
// play is on port/README.md's list (the SDK's own sources have a POSIX platform layer).
//
// Every function the header declares is here, with its own shape, so game/online compiles and runs as it is.

#include <unistd.h>

#include "platform/gamespy.h"

namespace game_spy {

void fix_nick(char* new_nick, const char* old_nick)
{
    // (the GameSpy SDK is not in this build)
}

void get_msleep(unsigned milliseconds)
{
    // (the SDK's own millisecond sleep, and the game's main loop waits with it: without it the loop spins)
    usleep(static_cast<useconds_t>(milliseconds) * 1000U);
}

char* game_strdup(const char* text) { return {}; }

void start_available_check(const char* game_name)
{
    // (the GameSpy SDK is not in this build)
}

int available_check_think()
{
    // (the SDK's GSIACResult: 1 is "available". The game's start-up waits in a loop until it hears that - 0 means
    // "still asking" - so this build answers at once and the game starts without an online backend.)
    return 1;
}

const char* get_unique_id() { return {}; }

void set_online_domain(const char* domain)
{
    // (the GameSpy SDK is not in this build)
}

int cd_key_init(int game_id) { return {}; }

void cd_key_shutdown()
{
    // (the GameSpy SDK is not in this build)
}

void cd_key_think()
{
    // (the GameSpy SDK is not in this build)
}

void cd_key_authenticate_user(int game_id, int local_id, uint32_t ip, const char* challenge, const char* response,
                              HookFn callback, void* instance)
{
    // (the GameSpy SDK is not in this build)
}

void cd_key_disconnect_user(int game_id, int local_id)
{
    // (the GameSpy SDK is not in this build)
}

void cd_key_disconnect_all(int game_id)
{
    // (the GameSpy SDK is not in this build)
}

void cd_key_compute_response(const char* cd_key, const char* challenge, char* response)
{
    // (the GameSpy SDK is not in this build)
}

int call_qr2_init(void* qrec, const char* ip, int base_port, const char* game_name, const char* secret_key,
                  int is_public, int nat_negotiate, HookFn server_key, HookFn player_key, HookFn team_key,
                  HookFn key_list, HookFn count, HookFn add_error, void* user_data)
{
    return {};
}

void get_qr2_think(void* qrec)
{
    // (the GameSpy SDK is not in this build)
}

void qr2_send_state_changed(void* qrec)
{
    // (the GameSpy SDK is not in this build)
}

void get_qr2_shutdown(void* qrec)
{
    // (the GameSpy SDK is not in this build)
}

void call_qr2_register_key(int key_id, const char* key)
{
    // (the GameSpy SDK is not in this build)
}

void call_qr2_buffer_add(void* buffer, const char* text)
{
    // (the GameSpy SDK is not in this build)
}

void get_qr2_buffer_add_int(void* buffer, int value)
{
    // (the GameSpy SDK is not in this build)
}

void qr2_key_buffer_add(void* key_buffer, int key_id)
{
    // (the GameSpy SDK is not in this build)
}

int nat_begin_with_socket(uintptr_t socket, int cookie, int client_index, HookFn progress, NatCompleted completed,
                          int user_data)
{
    return {};
}

void nat_think()
{
    // (the GameSpy SDK is not in this build)
}

void nat_process_data(char* data, int length, void* from)
{
    // (the GameSpy SDK is not in this build)
}

void nat_cancel(int cookie)
{
    // (the GameSpy SDK is not in this build)
}

void nat_free_negotiations()
{
    // (the GameSpy SDK is not in this build)
}

void* peer_initialize(const PeerCallbacks& callbacks) { return {}; }

int peer_set_title(void* peer, const char* title, const char* qr_secret_key, const char* sb_name,
                   const char* sb_secret_key, int sb_game_version, int sb_max_updates, int nat_negotiate,
                   const int* ping_rooms, const int* cross_ping_rooms)
{
    return {};
}

void peer_clear_title(void* peer)
{
    // (the GameSpy SDK is not in this build)
}

void peer_connect(void* peer, const char* nick, int profile, HookFn nick_error, HookFn connected, void* param,
                  int blocking)
{
    // (the GameSpy SDK is not in this build)
}

void peer_retry_with_nick(void* peer, const char* nick)
{
    // (the GameSpy SDK is not in this build)
}

void peer_fix_nick(char* new_nick, const char* old_nick)
{
    // (the GameSpy SDK is not in this build)
}

int peer_is_connected(void* peer) { return {}; }

bool peer_has_chat(void* peer) { return {}; }

void peer_disconnect(void* peer)
{
    // (the GameSpy SDK is not in this build)
}

void peer_shutdown(void* peer)
{
    // (the GameSpy SDK is not in this build)
}

void peer_think(void* peer)
{
    // (the GameSpy SDK is not in this build)
}

const char* peer_get_nick(void* peer) { return {}; }

uint32_t peer_get_public_ip(void* peer) { return {}; }

uint32_t peer_get_private_ip(void* peer) { return {}; }

void peer_set_quiet_mode(void* peer, int quiet)
{
    // (the GameSpy SDK is not in this build)
}

void peer_parse_query(void* peer, char* query, int length, void* sender)
{
    // (the GameSpy SDK is not in this build)
}

void peer_send_nat_negotiate_cookie(void* peer, uint32_t ip, int port, int cookie)
{
    // (the GameSpy SDK is not in this build)
}

void peer_list_group_rooms(void* peer, const char* fields, HookFn callback, void* param, int blocking)
{
    // (the GameSpy SDK is not in this build)
}

void peer_join_group_room(void* peer, int group_id, HookFn callback, void* param, int blocking)
{
    // (the GameSpy SDK is not in this build)
}

void peer_set_group_id(void* peer, int group_id)
{
    // (the GameSpy SDK is not in this build)
}

void peer_leave_room(void* peer, int room_type, const char* reason)
{
    // (the GameSpy SDK is not in this build)
}

void peer_message_room(void* peer, int room_type, const char* message, int message_type)
{
    // (the GameSpy SDK is not in this build)
}

void peer_enum_players(void* peer, int room_type, HookFn callback, void* param)
{
    // (the GameSpy SDK is not in this build)
}

int peer_get_player_info_no_wait(void* peer, const char* nick, uint32_t* ip, int* profile) { return {}; }

void peer_set_room_keys(void* peer, int room_type, const char* nick, int count, const char** keys, const char** values)
{
    // (the GameSpy SDK is not in this build)
}

void peer_get_room_keys(void* peer, int room_type, const char* nick, int count, const char** keys, HookFn callback,
                        void* param, int blocking)
{
    // (the GameSpy SDK is not in this build)
}

void peer_start_listing_games(void* peer, const uint8_t* fields, int count, const char* filter, HookFn callback,
                              void* param)
{
    // (the GameSpy SDK is not in this build)
}

void peer_stop_listing_games(void* peer)
{
    // (the GameSpy SDK is not in this build)
}

void peer_update_game(void* peer, void* server, int full_update)
{
    // (the GameSpy SDK is not in this build)
}

int peer_start_reporting_with_socket(void* peer, uintptr_t socket, int port) { return {}; }

void peer_stop_game(void* peer)
{
    // (the GameSpy SDK is not in this build)
}

void peer_state_changed(void* peer)
{
    // (the GameSpy SDK is not in this build)
}

const char* server_get_string_value(void* server, const char* key, const char* fallback) { return {}; }

int server_get_int_value(void* server, const char* key, int fallback) { return {}; }

int server_get_bool_value(void* server, const char* key, int fallback) { return {}; }

const char* server_get_player_string_value(void* server, int player, const char* key, const char* fallback)
{
    return {};
}

int server_get_player_int_value(void* server, int player, const char* key, int fallback) { return {}; }

int server_has_basic_keys(void* server) { return {}; }

int server_has_full_keys(void* server) { return {}; }

int server_direct_connect(void* server) { return {}; }

int server_has_private_address(void* server) { return {}; }

uint32_t server_get_public_address(void* server) { return {}; }

uint32_t server_get_private_address(void* server) { return {}; }

uint32_t server_get_public_query_port(void* server) { return {}; }

int server_get_ping(void* server) { return {}; }

int http_get_ex(const char* url, const char* headers, void* buffer, int size, void* post, int throttle, int blocking,
                HookFn progress, HookFn completed, void* param)
{
    return {};
}

int http_post_ex(const char* url, const char* headers, void* post, int throttle, int blocking, HookFn progress,
                 HookFn completed, void* param)
{
    return {};
}

void* http_new_post() { return {}; }

int http_post_add_string(void* post, const char* name, const char* value) { return {}; }

int http_post_add_file_from_memory(void* post, const char* name, const char* buffer, int size, const char* report_name,
                                   const char* content_type)
{
    return {};
}

void http_think()
{
    // (the GameSpy SDK is not in this build)
}

void http_cancel_request(int request)
{
    // (the GameSpy SDK is not in this build)
}

void http_cleanup()
{
    // (the GameSpy SDK is not in this build)
}

int track_usage(int user_id, int product_id, const char* version_id, int distribution_id, int blocking) { return {}; }

int gp_initialize(void* connection, int product_id, int namespace_id) { return {}; }

int gp_process(void* connection) { return {}; }

int gp_set_callback(void* connection, int type, HookFn callback, void* param) { return {}; }

int gp_connect(void* connection, const char* nick, const char* email, const char* password, int firewall, int blocking,
               HookFn callback, void* param)
{
    return {};
}

int gp_connect_new_user(void* connection, const char* nick, const char* unique_nick, const char* email,
                        const char* password, const char* cd_key, int firewall, int blocking, HookFn callback,
                        void* param)
{
    return {};
}

void gp_disconnect(void* connection)
{
    // (the GameSpy SDK is not in this build)
}

int gp_set_status(void* connection, int status, const char* text, const char* location) { return {}; }

int gp_send_buddy_request(void* connection, int profile, const char* reason) { return {}; }

int gp_auth_buddy_request(void* connection, int profile) { return {}; }

int gp_delete_buddy(void* connection, int profile) { return {}; }

int gp_send_buddy_message(void* connection, int profile, const char* message) { return {}; }

int gp_get_buddy_status(void* connection, int index, void* status) { return {}; }

char* stats_game_name() { return {}; }

char* stats_secret_key() { return {}; }

int stats_init_connection(int game_port) { return {}; }

int stats_is_connected() { return {}; }

void stats_close_connection()
{
    // (the GameSpy SDK is not in this build)
}

int stats_persist_think() { return {}; }

void* stats_new_game(int use_buckets) { return {}; }

void stats_free_game(void* game)
{
    // (the GameSpy SDK is not in this build)
}

const char* stats_get_challenge(void* game) { return {}; }

void stats_new_player(void* game, int number, const char* name)
{
    // (the GameSpy SDK is not in this build)
}

void stats_remove_player(void* game, int number)
{
    // (the GameSpy SDK is not in this build)
}

int stats_send_snap_shot(void* game, const char* snapshot, int final) { return {}; }

void stats_get_persist_data_values(int local_id, int profile, int type, int index, const char* keys, HookFn callback,
                                   void* instance)
{
    // (the GameSpy SDK is not in this build)
}

void stats_generate_auth(const char* challenge, const char* password, char* response)
{
    // (the GameSpy SDK is not in this build)
}

void set_server_int_bucket(void* game, const char* key, int value)
{
    // (the GameSpy SDK is not in this build)
}

void set_server_string_bucket(void* game, const char* key, const char* value)
{
    // (the GameSpy SDK is not in this build)
}

void set_player_int_bucket(void* game, const char* key, int value, int player)
{
    // (the GameSpy SDK is not in this build)
}

void set_player_string_bucket(void* game, const char* key, const char* value, int player)
{
    // (the GameSpy SDK is not in this build)
}

} // namespace game_spy
