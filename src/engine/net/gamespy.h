// The GameSpy SDK calls the game makes, behind our own functions (library code, not decompiled). The original links a
// 2003-04 build of the SDK; here it is IGN's open-source release of the SDK (December 2007, common 2.06 - BSD-3, see THIRD_PARTY_NOTICES.md), built from
// source; gamespy.cpp adapts the calls where its API grew (docs/gamespy.md lists what differs). SDK objects are opaque
// here (PEER, SBServer, qr2 buffers, GHTTPPost, statsgame_t: void*); a GPConnection is the pointer-sized slot the
// game passes (FriendsManager's first word). Callbacks are passed as code pointers with the SDK's cdecl signatures.
#pragma once

#include "engine/core/pointer_cell.h"
#include "platform/code_address.h"

#include <cstddef>
#include <cstdint>

namespace game_spy {

// The peer SDK's global callbacks (PEERCallbacks: 28 callbacks and their parameter - the same 0x74 bytes, in the same
// order, in both SDKs). Index = byte offset / 4.
using PeerCallbacks = uint32_t[0x1D];

// A callback given to the SDK: any function (its signature the SDK's) or code address (GAME_CODE), or null.
struct HookFn {
    const void* code; // (a function's address)
    template <typename F>
    HookFn(F* function) : code(code_address(function))
    {}
    HookFn(std::nullptr_t) : code(nullptr) {}
};

// peerFixNick with the rules of the SDK the game was built with: a '_' before a leading digit or '-', and every
// character outside A-Z a-z 0-9 []\`_^{|}- made '_'. (The 2007 SDK's chatFixNick has other rules - another character
// set, a '_' before @ # + : only, nicks cut at 20 characters - so peer_fix_nick is this.)
void fix_nick(char* new_nick, const char* old_nick);

// (defined in gamespy.cpp, which includes the SDK's headers)

// common
void get_msleep(unsigned milliseconds);
char* game_strdup(const char* text); // goastrdup: freed with free()
void start_available_check(const char* game_name);
int available_check_think(); // GSIACResult: 0 waiting, 1 available, 2 unavailable, 3 temporarily unavailable
const char* get_unique_id(); // GOAGetUniqueID
// The online service's domain (OnlineDomain): the SDK's host names are made from it (online_hosts.cpp).
void set_online_domain(const char* domain);

// CD key (gcd)
int cd_key_init(int game_id);
void cd_key_shutdown();
void cd_key_think();
// `callback`: void(int gameId, int local_id, int authenticated, char* message, void* instance)
void cd_key_authenticate_user(int game_id, int local_id, uint32_t ip, const char* challenge, const char* response,
                              HookFn callback, void* instance);
void cd_key_disconnect_user(int game_id, int local_id);
void cd_key_disconnect_all(int game_id);
void cd_key_compute_response(const char* cd_key, const char* challenge, char* response); // response: 73 bytes

// query and reporting (qr2); a null `qrec` is the SDK's default one
int call_qr2_init(void* qrec, const char* ip, int base_port, const char* game_name, const char* secret_key,
                  int is_public, int nat_negotiate, HookFn server_key, HookFn player_key, HookFn team_key,
                  HookFn key_list, HookFn count, HookFn add_error, void* user_data);
void get_qr2_think(void* qrec);
void qr2_send_state_changed(void* qrec);
void get_qr2_shutdown(void* qrec);
void call_qr2_register_key(int key_id, const char* key);
void call_qr2_buffer_add(void* buffer, const char* text);
void get_qr2_buffer_add_int(void* buffer, int value);
void qr2_key_buffer_add(void* key_buffer, int key_id);

// NAT negotiation
// `progress`: void(int state, void* user_data); `completed`: void(int result, SOCKET, sockaddr_in*, void* user_data)
int nat_begin_with_socket(uint32_t socket, int cookie, int client_index, HookFn progress, HookFn completed,
                          void* user_data);
void nat_think();
void nat_process_data(char* data, int length, void* from);
void nat_cancel(int cookie);
void nat_free_negotiations();

// peer (chat lobby, game listing, hosting)
void* peer_initialize(const PeerCallbacks callbacks);
int peer_set_title(void* peer, const char* title, const char* qr_secret_key, const char* sb_name,
                   const char* sb_secret_key, int sb_game_version, int sb_max_updates, int nat_negotiate,
                   const int* ping_rooms, const int* cross_ping_rooms);
void peer_clear_title(void* peer);
void peer_connect(void* peer, const char* nick, int profile, HookFn nick_error, HookFn connected, void* param,
                  int blocking);
void peer_retry_with_nick(void* peer, const char* nick);
void peer_fix_nick(char* new_nick, const char* old_nick);
int peer_is_connected(void* peer);
bool peer_has_chat(void* peer); // (its chat connection made)
void peer_disconnect(void* peer);
void peer_shutdown(void* peer);
void peer_think(void* peer);
const char* peer_get_nick(void* peer);
uint32_t peer_get_public_ip(void* peer);
uint32_t peer_get_private_ip(void* peer);
void peer_set_quiet_mode(void* peer, int quiet);
void peer_parse_query(void* peer, char* query, int length, void* sender);
void peer_send_nat_negotiate_cookie(void* peer, uint32_t ip, int port, int cookie);
void peer_list_group_rooms(void* peer, const char* fields, HookFn callback, void* param, int blocking);
void peer_join_group_room(void* peer, int group_id, HookFn callback, void* param, int blocking);
void peer_set_group_id(void* peer, int group_id);
void peer_leave_room(void* peer, int room_type, const char* reason);
void peer_message_room(void* peer, int room_type, const char* message, int message_type);
void peer_enum_players(void* peer, int room_type, HookFn callback, void* param);
int peer_get_player_info_no_wait(void* peer, const char* nick, uint32_t* ip, int* profile);
void peer_set_room_keys(void* peer, int room_type, const char* nick, int count, const char** keys, const char** values);
void peer_get_room_keys(void* peer, int room_type, const char* nick, int count, const char** keys, HookFn callback,
                        void* param, int blocking);
void peer_start_listing_games(void* peer, const uint8_t* fields, int count, const char* filter, HookFn callback,
                              void* param);
void peer_stop_listing_games(void* peer);
void peer_update_game(void* peer, void* server, int full_update);
int peer_start_reporting_with_socket(void* peer, uint32_t socket, int port);
void peer_stop_game(void* peer);
void peer_state_changed(void* peer);

// a listed server (SBServer)
const char* server_get_string_value(void* server, const char* key, const char* fallback);
int server_get_int_value(void* server, const char* key, int fallback);
int server_get_bool_value(void* server, const char* key, int fallback);
const char* server_get_player_string_value(void* server, int player, const char* key, const char* fallback);
int server_get_player_int_value(void* server, int player, const char* key, int fallback);
int server_has_basic_keys(void* server);
int server_has_full_keys(void* server);
int server_direct_connect(void* server);
int server_has_private_address(void* server);
uint32_t server_get_public_address(void* server); // network byte order
uint32_t server_get_private_address(void* server);
uint32_t server_get_public_query_port(void* server); // host byte order
int server_get_ping(void* server);

// HTTP (ghttp); requests are ints (-1: failed)
// `progress`: void(request, int state, const char*, int64 length, int64 received, int64 total, void* param);
// `completed`: int(request, int result, char* buffer, int64 length, void* param) - nonzero: the SDK frees the buffer
int http_get_ex(const char* url, const char* headers, char* buffer, int size, void* post, int throttle, int blocking,
                HookFn progress, HookFn completed, void* param);
int http_post_ex(const char* url, const char* headers, void* post, int throttle, int blocking, HookFn progress,
                 HookFn completed, void* param);
void* http_new_post();
int http_post_add_string(void* post, const char* name, const char* value);
int http_post_add_file_from_memory(void* post, const char* name, const char* buffer, int size, const char* report_name,
                                   const char* content_type);
void http_think();
void http_cancel_request(int request);
void http_cleanup();

// patching and tracking (pt): the usage report (the MOTD request)
int track_usage(int user_id, int product_id, const char* version_id, int distribution_id, int blocking);

// presence (GP)
int gp_initialize(void* connection, int product_id, int namespace_id);
int gp_process(void* connection);
int gp_set_callback(void* connection, int type, HookFn callback, void* param);
int gp_connect(void* connection, const char* nick, const char* email, const char* password, int firewall, int blocking,
               HookFn callback, void* param);
int gp_connect_new_user(void* connection, const char* nick, const char* unique_nick, const char* email,
                        const char* password, const char* cd_key, int firewall, int blocking, HookFn callback,
                        void* param);
void gp_disconnect(void* connection);
int gp_set_status(void* connection, int status, const char* text, const char* location);
int gp_send_buddy_request(void* connection, int profile, const char* reason);
int gp_auth_buddy_request(void* connection, int profile);
int gp_delete_buddy(void* connection, int profile);
int gp_send_buddy_message(void* connection, int profile, const char* message);
// GPBuddyStatus: profile, status, status string [256], location string [256], ip, port (+ the 2007 SDK's
// quietModeFlags: `status` must hold kGpBuddyStatusSize bytes)
constexpr int kGpBuddyStatusSize = 0x214;
int gp_get_buddy_status(void* connection, int index, void* status);

// stats and persistent storage (gstats)
char* stats_game_name();  // gcd_gamename[256]
char* stats_secret_key(); // gcd_secret_key[256]
int stats_init_connection(int game_port);
int stats_is_connected();
void stats_close_connection();
int stats_persist_think();
void* stats_new_game(int use_buckets);
void stats_free_game(void* game);
const char* stats_get_challenge(void* game);
void stats_new_player(void* game, int number, const char* name);
void stats_remove_player(void* game, int number);
int stats_send_snap_shot(void* game, const char* snapshot, int final);
// `callback`: PersDataCallbackFn
void stats_get_persist_data_values(int local_id, int profile, int type, int index, const char* keys, HookFn callback,
                                   void* instance);
void stats_generate_auth(const char* challenge, const char* password, char* response); // response: 33 bytes
// bucket "set" operations: the server's (index 0) or a player's
void set_server_int_bucket(void* game, const char* key, int value);
void set_server_string_bucket(void* game, const char* key, const char* value);
void set_player_int_bucket(void* game, const char* key, int value, int player);
void set_player_string_bucket(void* game, const char* key, const char* value, int player);

} // namespace game_spy
