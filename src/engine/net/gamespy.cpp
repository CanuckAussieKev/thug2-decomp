// The GameSpy SDK: IGN's open-source release (December 2007; BSD-3, see
// THIRD_PARTY_NOTICES.md), adapted to the calls the game makes (gamespy.h; what differs from the SDK version the game
// was built with: docs/gamespy.md).

#include "engine/net/gamespy.h"
#include "platform/compiler.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace game_spy {

void fix_nick(char* new_nick, const char* old_nick)
{
    static const char kValid[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789[]\\`_^{|}-";
    if (std::isdigit(static_cast<unsigned char>(*old_nick)) || *old_nick == '-')
        *new_nick++ = '_';
    for (char c; (c = *old_nick++) != '\0';)
        *new_nick++ = std::strchr(kValid, c) ? c : '_';
    *new_nick = '\0';
}

} // namespace game_spy

THUG2_MSVC_WARNING(push)
THUG2_MSVC_WARNING(disable : 4005) // (gsPlatform.h defines WIN32_LEAN_AND_MEAN again)
#include "GP/gp.h"
#include "Peer/peer.h"
#include "common/gsAvailable.h"
#include "gcdkey/gcdkeyc.h"
#include "gcdkey/gcdkeys.h"
#include "ghttp/ghttp.h"
#include "gstats/gpersist.h"
#include "gstats/gstats.h"
#include "natneg/natneg.h"
#include "pt/pt.h"
#include "qr2/qr2.h"
#include "serverbrowsing/sb_serverbrowsing.h"
THUG2_MSVC_WARNING(pop)

// Host-name overrides the SDK keeps without declaring them in a header (pt/ptMain.c), and the one the build adds
// (Peer/peerOperations.c, patched by cmake/gamespy_sdk.cmake: the chat server's name was a constant).
extern "C" {
// NOLINTNEXTLINE(readability-identifier-naming): the GameSpy SDK's own global
extern char gPTAVercheckURL[];
// NOLINTNEXTLINE(readability-identifier-naming): the GameSpy SDK's own global
extern char gPTAMOTDURL[];
// NOLINTNEXTLINE(readability-identifier-naming): the GameSpy SDK's own global
extern char peerChatServerAddress[64];
// NOLINTNEXTLINE(readability-identifier-naming): the GameSpy SDK's own global
extern char* Matchup3Hostname; // (natneg.c; natneg.h declares only the first two)
}
// (gsPlatform.h maps snprintf to the old _snprintf, which doesn't always end the string: ours is the standard one)
#undef snprintf

static_assert(sizeof(PEERCallbacks) == sizeof(game_spy::PeerCallbacks), "PEERCallbacks: 28 callbacks + param");
static_assert(sizeof(GPBuddyStatus) == game_spy::kGpBuddyStatusSize, "GPBuddyStatus with quietModeFlags");
static_assert(sizeof(SOCKET) == sizeof(uint32_t), "a 32-bit build");

namespace game_spy {

namespace {

char s_domain[64] = "gamespy.com";

template <typename Fn>
Fn as(HookFn callback)
{
    return reinterpret_cast<Fn>(const_cast<void*>(callback.code));
}

void set_host(char* out, size_t size, const char* format, const char* name)
{
    std::snprintf(out, size, format, name, s_domain);
}

// The server-browsing master for a game name: "<name>.ms<n>.<domain>", n from the SDK's own hash of the name
// (sb_serverlist.c's StringHash over NUM_MASTER_SERVERS = 20) - the SDK's override takes a whole host name.
char s_master_server[128];
void set_master_server(const char* game_name)
{
    uint32_t hash = 0;
    for (const char* c = game_name; *c; ++c)
        hash =
            static_cast<uint32_t>(static_cast<int>(hash) * -1664117991 + std::tolower(static_cast<unsigned char>(*c)));
    std::snprintf(s_master_server, sizeof(s_master_server), "%s.ms%d.%s", game_name, static_cast<int>(hash % 20),
                  s_domain);
    SBOverrideMasterServer = s_master_server;
}

// Names that depend on the game name (qr2's master, the browsing master): set when a name is given to the SDK.
void set_game_hosts(const char* game_name)
{
    set_host(qr2_hostname, sizeof(qr2_hostname), "%s.master.%s", game_name);
    set_master_server(game_name);
}

char s_nat_neg1[96];
char s_nat_neg2[96];
char s_nat_neg3[96];

// The 2007 gcd SDK asks back for re-authentication; the game's SDK had no such request (nothing to do).
void ignore_refresh_auth(int, int, int, char*, void*) {}

} // namespace

void get_msleep(unsigned milliseconds) { msleep(milliseconds); }
char* game_strdup(const char* text) { return goastrdup(text); }
void start_available_check(const char* game_name)
{
    // (the override is a whole host name: made for each check - the online service also checks "mototrax")
    std::snprintf(GSIACHostname, sizeof(GSIACHostname), "%s.available.%s", game_name, s_domain);
    GSIStartAvailableCheck(game_name);
}
int available_check_think() { return static_cast<int>(GSIAvailableCheckThink()); }
const char* get_unique_id() { return GOAGetUniqueID(); }

void set_online_domain(const char* domain)
{
    std::snprintf(s_domain, sizeof(s_domain), "%s", domain);
    std::snprintf(s_nat_neg1, sizeof(s_nat_neg1), "natneg1.%s", s_domain);
    std::snprintf(s_nat_neg2, sizeof(s_nat_neg2), "natneg2.%s", s_domain);
    std::snprintf(s_nat_neg3, sizeof(s_nat_neg3), "natneg3.%s", s_domain);
    Matchup1Hostname = s_nat_neg1;
    Matchup2Hostname = s_nat_neg2;
    Matchup3Hostname = s_nat_neg3;
    std::snprintf(peerChatServerAddress, sizeof(peerChatServerAddress), "peerchat.%s", s_domain);
    std::snprintf(GPConnectionManagerHostname, sizeof(GPConnectionManagerHostname), "gpcm.%s", s_domain);
    std::snprintf(GPSearchManagerHostname, sizeof(GPSearchManagerHostname), "gpsp.%s", s_domain);
    std::snprintf(StatsServerHostname, sizeof(StatsServerHostname), "gamestats.%s", s_domain);
    constexpr size_t kPtaUrlSize = 2048; // (PTA_MAX_STRING_SIZE)
    std::snprintf(gPTAMOTDURL, kPtaUrlSize, "http://motd.%s/motd/motd.asp", s_domain);
    std::snprintf(gPTAVercheckURL, kPtaUrlSize, "http://motd.%s/motd/vercheck.asp", s_domain);
    // (gcd_hostname stays the SDK's key.gamespy.com: the game never changed it)
}

// --- CD key ---

int cd_key_init(int game_id) { return gcd_init(game_id); }
void cd_key_shutdown() { gcd_shutdown(); }
void cd_key_think() { gcd_think(); }
void cd_key_authenticate_user(int game_id, int local_id, uint32_t ip, const char* challenge, const char* response,
                              HookFn callback, void* instance)
{
    gcd_authenticate_user(game_id, local_id, ip, challenge, response, as<AuthCallBackFn>(callback),
                          &ignore_refresh_auth, instance);
}
void cd_key_disconnect_user(int game_id, int local_id) { gcd_disconnect_user(game_id, local_id); }
void cd_key_disconnect_all(int game_id) { gcd_disconnect_all(game_id); }
void cd_key_compute_response(const char* cd_key, const char* challenge, char* response)
{
    // (the game's SDK had only the normal response: method 0)
    gcd_compute_response(const_cast<char*>(cd_key), const_cast<char*>(challenge), response, CDResponseMethod_NEWAUTH);
}

// --- qr2 ---

int call_qr2_init(void* qrec, const char* ip, int base_port, const char* game_name, const char* secret_key,
                  int is_public, int nat_negotiate, HookFn server_key, HookFn player_key, HookFn team_key,
                  HookFn key_list, HookFn count, HookFn add_error, void* user_data)
{
    set_game_hosts(game_name);
    return static_cast<int>(qr2_init(static_cast<qr2_t*>(qrec), ip, base_port, game_name, secret_key, is_public,
                                     nat_negotiate, as<qr2_serverkeycallback_t>(server_key),
                                     as<qr2_playerteamkeycallback_t>(player_key),
                                     as<qr2_playerteamkeycallback_t>(team_key), as<qr2_keylistcallback_t>(key_list),
                                     as<qr2_countcallback_t>(count), as<qr2_adderrorcallback_t>(add_error), user_data));
}
void get_qr2_think(void* qrec) { qr2_think(static_cast<qr2_t>(qrec)); }
void qr2_send_state_changed(void* qrec) { qr2_send_statechanged(static_cast<qr2_t>(qrec)); }
void get_qr2_shutdown(void* qrec) { qr2_shutdown(static_cast<qr2_t>(qrec)); }
void call_qr2_register_key(int key_id, const char* key) { qr2_register_key(key_id, key); }
void call_qr2_buffer_add(void* buffer, const char* text) { qr2_buffer_add(static_cast<qr2_buffer_t>(buffer), text); }
void get_qr2_buffer_add_int(void* buffer, int value) { qr2_buffer_add_int(static_cast<qr2_buffer_t>(buffer), value); }
void qr2_key_buffer_add(void* key_buffer, int key_id)
{
    qr2_keybuffer_add(static_cast<qr2_keybuffer_t>(key_buffer), key_id);
}

// --- NAT negotiation ---

int nat_begin_with_socket(uint32_t socket, int cookie, int client_index, HookFn progress, HookFn completed,
                          void* user_data)
{
    return static_cast<int>(NNBeginNegotiationWithSocket(static_cast<SOCKET>(socket), cookie, client_index,
                                                         as<NegotiateProgressFunc>(progress),
                                                         as<NegotiateCompletedFunc>(completed), user_data));
}
void nat_think() { NNThink(); }
void nat_process_data(char* data, int length, void* from)
{
    NNProcessData(data, length, static_cast<sockaddr_in*>(from));
}
void nat_cancel(int cookie) { NNCancel(cookie); }
void nat_free_negotiations() { NNFreeNegotiateList(); }

// --- peer ---

void* peer_initialize(const PeerCallbacks callbacks)
{
    PEERCallbacks sdk;
    std::memcpy(&sdk, callbacks, sizeof(sdk));
    return peerInitialize(&sdk);
}
int peer_set_title(void* peer, const char* title, const char* qr_secret_key, const char* sb_name,
                   const char* sb_secret_key, int sb_game_version, int sb_max_updates, int nat_negotiate,
                   const int* ping_rooms, const int* cross_ping_rooms)
{
    set_game_hosts(title);
    set_master_server(sb_name);
    return static_cast<int>(peerSetTitle(peer, title, qr_secret_key, sb_name, sb_secret_key, sb_game_version,
                                         sb_max_updates, static_cast<PEERBool>(nat_negotiate),
                                         reinterpret_cast<PEERBool*>(const_cast<int*>(ping_rooms)),
                                         reinterpret_cast<PEERBool*>(const_cast<int*>(cross_ping_rooms))));
}
void peer_clear_title(void* peer) { peerClearTitle(peer); }
void peer_connect(void* peer, const char* nick, int profile, HookFn nick_error, HookFn connected, void* param,
                  int blocking)
{
    peerConnect(peer, nick, profile, as<peerNickErrorCallback>(nick_error), as<peerConnectCallback>(connected), param,
                static_cast<PEERBool>(blocking));
}
void peer_retry_with_nick(void* peer, const char* nick) { peerRetryWithNick(peer, nick); }
void peer_fix_nick(char* new_nick, const char* old_nick)
{
    fix_nick(new_nick, old_nick);
} // (the game's rules, not peerFixNick's)
int peer_is_connected(void* peer) { return static_cast<int>(peerIsConnected(peer)); }
bool peer_has_chat(void* peer) { return peerGetChat(peer) != nullptr; }
void peer_disconnect(void* peer) { peerDisconnect(peer); }
void peer_shutdown(void* peer) { peerShutdown(peer); }
void peer_think(void* peer) { peerThink(peer); }
const char* peer_get_nick(void* peer) { return peerGetNick(peer); }
uint32_t peer_get_public_ip(void* peer) { return peerGetPublicIP(peer); }
uint32_t peer_get_private_ip(void* peer) { return peerGetPrivateIP(peer); }
void peer_set_quiet_mode(void* peer, int quiet) { peerSetQuietMode(peer, static_cast<PEERBool>(quiet)); }
void peer_parse_query(void* peer, char* query, int length, void* sender)
{
    peerParseQuery(peer, query, length, static_cast<sockaddr*>(sender));
}
void peer_send_nat_negotiate_cookie(void* peer, uint32_t ip, int port, int cookie)
{
    peerSendNatNegotiateCookie(peer, ip, static_cast<unsigned short>(port), cookie);
}
void peer_list_group_rooms(void* peer, const char* fields, HookFn callback, void* param, int blocking)
{
    peerListGroupRooms(peer, fields, as<peerListGroupRoomsCallback>(callback), param, static_cast<PEERBool>(blocking));
}
void peer_join_group_room(void* peer, int group_id, HookFn callback, void* param, int blocking)
{
    peerJoinGroupRoom(peer, group_id, as<peerJoinRoomCallback>(callback), param, static_cast<PEERBool>(blocking));
}
void peer_set_group_id(void* peer, int group_id) { peerSetGroupID(peer, group_id); }
void peer_leave_room(void* peer, int room_type, const char* reason)
{
    peerLeaveRoom(peer, static_cast<RoomType>(room_type), reason);
}
void peer_message_room(void* peer, int room_type, const char* message, int message_type)
{
    peerMessageRoom(peer, static_cast<RoomType>(room_type), message, static_cast<MessageType>(message_type));
}
void peer_enum_players(void* peer, int room_type, HookFn callback, void* param)
{
    peerEnumPlayers(peer, static_cast<RoomType>(room_type), as<peerEnumPlayersCallback>(callback), param);
}
int peer_get_player_info_no_wait(void* peer, const char* nick, uint32_t* ip, int* profile)
{
    return static_cast<int>(peerGetPlayerInfoNoWait(peer, nick, reinterpret_cast<unsigned int*>(ip), profile));
}
void peer_set_room_keys(void* peer, int room_type, const char* nick, int count, const char** keys, const char** values)
{
    peerSetRoomKeys(peer, static_cast<RoomType>(room_type), nick, count, keys, values);
}
void peer_get_room_keys(void* peer, int room_type, const char* nick, int count, const char** keys, HookFn callback,
                        void* param, int blocking)
{
    peerGetRoomKeys(peer, static_cast<RoomType>(room_type), nick, count, keys, as<peerGetRoomKeysCallback>(callback),
                    param, static_cast<PEERBool>(blocking));
}
void peer_start_listing_games(void* peer, const uint8_t* fields, int count, const char* filter, HookFn callback,
                              void* param)
{
    peerStartListingGames(peer, fields, count, filter, as<peerListingGamesCallback>(callback), param);
}
void peer_stop_listing_games(void* peer) { peerStopListingGames(peer); }
void peer_update_game(void* peer, void* server, int full_update)
{
    peerUpdateGame(peer, static_cast<SBServer>(server), static_cast<PEERBool>(full_update));
}
int peer_start_reporting_with_socket(void* peer, uint32_t socket, int port)
{
    return static_cast<int>(
        peerStartReportingWithSocket(peer, static_cast<SOCKET>(socket), static_cast<unsigned short>(port)));
}
void peer_stop_game(void* peer) { peerStopGame(peer); }
void peer_state_changed(void* peer) { peerStateChanged(peer); }

// --- a listed server ---

namespace {
SBServer get_sb(void* server) { return static_cast<SBServer>(server); }
} // namespace
const char* server_get_string_value(void* server, const char* key, const char* fallback)
{
    return SBServerGetStringValue(get_sb(server), key, fallback);
}
int server_get_int_value(void* server, const char* key, int fallback)
{
    return SBServerGetIntValue(get_sb(server), key, fallback);
}
int server_get_bool_value(void* server, const char* key, int fallback)
{
    return static_cast<int>(SBServerGetBoolValue(get_sb(server), key, static_cast<SBBool>(fallback)));
}
const char* server_get_player_string_value(void* server, int player, const char* key, const char* fallback)
{
    return SBServerGetPlayerStringValue(get_sb(server), player, key, fallback);
}
int server_get_player_int_value(void* server, int player, const char* key, int fallback)
{
    return SBServerGetPlayerIntValue(get_sb(server), player, key, fallback);
}
int server_has_basic_keys(void* server) { return static_cast<int>(SBServerHasBasicKeys(get_sb(server))); }
int server_has_full_keys(void* server) { return static_cast<int>(SBServerHasFullKeys(get_sb(server))); }
int server_direct_connect(void* server) { return static_cast<int>(SBServerDirectConnect(get_sb(server))); }
int server_has_private_address(void* server) { return static_cast<int>(SBServerHasPrivateAddress(get_sb(server))); }
uint32_t server_get_public_address(void* server) { return SBServerGetPublicInetAddress(get_sb(server)); }
uint32_t server_get_private_address(void* server) { return SBServerGetPrivateInetAddress(get_sb(server)); }
uint32_t server_get_public_query_port(void* server) { return SBServerGetPublicQueryPort(get_sb(server)); }
int server_get_ping(void* server) { return SBServerGetPing(get_sb(server)); }

// --- HTTP ---

int http_get_ex(const char* url, const char* headers, char* buffer, int size, void* post, int throttle, int blocking,
                HookFn progress, HookFn completed, void* param)
{
    return ghttpGetEx(url, headers, buffer, size, static_cast<GHTTPPost>(post), static_cast<GHTTPBool>(throttle),
                      static_cast<GHTTPBool>(blocking), as<ghttpProgressCallback>(progress),
                      as<ghttpCompletedCallback>(completed), param);
}
int http_post_ex(const char* url, const char* headers, void* post, int throttle, int blocking, HookFn progress,
                 HookFn completed, void* param)
{
    return ghttpPostEx(url, headers, static_cast<GHTTPPost>(post), static_cast<GHTTPBool>(throttle),
                       static_cast<GHTTPBool>(blocking), as<ghttpProgressCallback>(progress),
                       as<ghttpCompletedCallback>(completed), param);
}
void* http_new_post() { return ghttpNewPost(); }
int http_post_add_string(void* post, const char* name, const char* value)
{
    return static_cast<int>(ghttpPostAddString(static_cast<GHTTPPost>(post), name, value));
}
int http_post_add_file_from_memory(void* post, const char* name, const char* buffer, int size, const char* report_name,
                                   const char* content_type)
{
    return static_cast<int>(
        ghttpPostAddFileFromMemory(static_cast<GHTTPPost>(post), name, buffer, size, report_name, content_type));
}
void http_think() { ghttpThink(); }
void http_cancel_request(int request) { ghttpCancelRequest(request); }
void http_cleanup() { ghttpCleanup(); }

int track_usage(int user_id, int product_id, const char* version_id, int distribution_id, int blocking)
{
    return static_cast<int>(
        ptTrackUsage(user_id, product_id, version_id, distribution_id, static_cast<PTBool>(blocking)));
}

// --- presence ---

namespace {
GPConnection* gp(void* connection) { return static_cast<GPConnection*>(connection); }
} // namespace
int gp_initialize(void* connection, int product_id, int namespace_id)
{
    // (the 2007 SDK also takes the account system: GameSpy's own, as the game's SDK always used)
    return static_cast<int>(gpInitialize(gp(connection), product_id, namespace_id, GP_PARTNERID_GAMESPY));
}
int gp_process(void* connection) { return static_cast<int>(gpProcess(gp(connection))); }
int gp_set_callback(void* connection, int type, HookFn callback, void* param)
{
    return static_cast<int>(gpSetCallback(gp(connection), static_cast<GPEnum>(type), as<GPCallback>(callback), param));
}
int gp_connect(void* connection, const char* nick, const char* email, const char* password, int firewall, int blocking,
               HookFn callback, void* param)
{
    return static_cast<int>(gpConnect(gp(connection), nick, email, password, static_cast<GPEnum>(firewall),
                                      static_cast<GPEnum>(blocking), as<GPCallback>(callback), param));
}
int gp_connect_new_user(void* connection, const char* nick, const char* unique_nick, const char* email,
                        const char* password, const char* cd_key, int firewall, int blocking, HookFn callback,
                        void* param)
{
    return static_cast<int>(gpConnectNewUser(gp(connection), nick, unique_nick, email, password, cd_key,
                                             static_cast<GPEnum>(firewall), static_cast<GPEnum>(blocking),
                                             as<GPCallback>(callback), param));
}
void gp_disconnect(void* connection) { gpDisconnect(gp(connection)); }
int gp_set_status(void* connection, int status, const char* text, const char* location)
{
    return static_cast<int>(gpSetStatus(gp(connection), static_cast<GPEnum>(status), text, location));
}
int gp_send_buddy_request(void* connection, int profile, const char* reason)
{
    return static_cast<int>(gpSendBuddyRequest(gp(connection), profile, reason));
}
int gp_auth_buddy_request(void* connection, int profile)
{
    return static_cast<int>(gpAuthBuddyRequest(gp(connection), profile));
}
int gp_delete_buddy(void* connection, int profile) { return static_cast<int>(gpDeleteBuddy(gp(connection), profile)); }
int gp_send_buddy_message(void* connection, int profile, const char* message)
{
    return static_cast<int>(gpSendBuddyMessage(gp(connection), profile, message));
}
int gp_get_buddy_status(void* connection, int index, void* status)
{
    return static_cast<int>(gpGetBuddyStatus(gp(connection), index, static_cast<GPBuddyStatus*>(status)));
}

// --- stats ---

char* stats_game_name() { return gcd_gamename; }
char* stats_secret_key() { return gcd_secret_key; }
int stats_init_connection(int game_port) { return ::InitStatsConnection(game_port); }
int stats_is_connected() { return ::IsStatsConnected(); }
void stats_close_connection() { ::CloseStatsConnection(); }
int stats_persist_think() { return ::PersistThink(); }
void* stats_new_game(int use_buckets) { return NewGame(use_buckets); }
void stats_free_game(void* game) { FreeGame(static_cast<statsgame_t>(game)); }
const char* stats_get_challenge(void* game) { return GetChallenge(static_cast<statsgame_t>(game)); }
void stats_new_player(void* game, int number, const char* name)
{
    NewPlayer(static_cast<statsgame_t>(game), number, const_cast<char*>(name));
}
void stats_remove_player(void* game, int number) { RemovePlayer(static_cast<statsgame_t>(game), number); }
int stats_send_snap_shot(void* game, const char* snapshot, int final)
{
    return ::SendGameSnapShot(static_cast<statsgame_t>(game), snapshot, final);
}
void stats_get_persist_data_values(int local_id, int profile, int type, int index, const char* keys, HookFn callback,
                                   void* instance)
{
    ::GetPersistDataValues(local_id, profile, static_cast<persisttype_t>(type), index, const_cast<char*>(keys),
                           as<PersDataCallbackFn>(callback), instance);
}
void stats_generate_auth(const char* challenge, const char* password, char* response)
{
    ::GenerateAuth(challenge, password, response);
}
void set_server_int_bucket(void* game, const char* key, int value)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast): the GameSpy SDK's macro casts
    BucketIntOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, value, bl_server, 0);
}
void set_server_string_bucket(void* game, const char* key, const char* value)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast): the GameSpy SDK's macro casts
    BucketStringOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, const_cast<char*>(value), bl_server,
                   0);
}
void set_player_int_bucket(void* game, const char* key, int value, int player)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast): the GameSpy SDK's macro casts
    BucketIntOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, value, bl_player, player);
}
void set_player_string_bucket(void* game, const char* key, const char* value, int player)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast): the GameSpy SDK's macro casts
    BucketStringOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, const_cast<char*>(value), bl_player,
                   player);
}

} // namespace game_spy
