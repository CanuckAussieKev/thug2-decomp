// The GameSpy SDK: IGN's open-source release (December 2007; BSD-3, see
// THIRD_PARTY_NOTICES.md), adapted to the calls the game makes (gamespy.h; what differs from the SDK version the game
// was built with: docs/gamespy.md).

#include "platform/gamespy.h"
#include "platform/compiler.h"

#include <cctype>
#include "engine/core/checked_index.h"
#include <cstdint>
#include <string_view>
#include <span>
#include <cstdio>
#include <cstring>
#include <array>
#include <iterator>

namespace game_spy {

void fix_nick(char* new_nick, const char* old_nick)
{
    static const auto kValid = std::to_array("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789[]\\`_^{|}-");
    const std::string_view old_text(old_nick);
    // (the new nick is at most one longer, and ends with its NUL: the caller's buffer holds that, as the original's did)
    const std::span<char> out(new_nick, old_text.size() + 2);
    size_t at = 0;
    const char first = old_text.empty() ? '\0' : old_text.front();
    if (std::isdigit(static_cast<unsigned char>(first)) || first == '-')
        core::at(out, at++) = '_';
    for (const char c : old_text)
        core::at(out, at++) = std::strchr(kValid.data(), c) ? c : '_';
    core::at(out, at) = '\0';
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
#include "engine/core/raw_view.h"
#include "platform/code_address.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
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

std::array<char, 64>& stored_domain()
{
    static std::array<char, 64> domain{{"gamespy.com"}};
    return domain;
}

template <typename Fn>
Fn as(HookFn callback)
{
    return function_at<Fn>(callback.code());
}

// NOT ORIGINAL grouping - gamespy.cpp's state (the original's globals). Made on first use, never destroyed
// (gamespy_state()).
struct GamespyState {
    // The server-browsing master for a game name: "<name>.ms<n>.<domain>", n from the SDK's own hash of the name
    // (sb_serverlist.c's StringHash over NUM_MASTER_SERVERS = 20) - the SDK's override takes a whole host name.
    std::array<char, 128> master_server_{};
    std::array<char, 96> nat_neg1_{};
    std::array<char, 96> nat_neg2_{};
    std::array<char, 96> nat_neg3_{};
};

GamespyState& gamespy_state()
{
    static core::NoDestroy<GamespyState> state;
    return state.get();
}

void set_master_server(const char* game_name)
{
    uint32_t hash = 0;
    for (const char c : std::string_view(game_name))
        hash =
            static_cast<uint32_t>(static_cast<int>(hash) * -1664117991 + std::tolower(static_cast<unsigned char>(c)));
    text::format_to(gamespy_state().master_server_, "%s.ms%d.%s", game_name, static_cast<int>(hash % 20), stored_domain().data());
    SBOverrideMasterServer = gamespy_state().master_server_.data();
}

// Names that depend on the game name (qr2's master, the browsing master): set when a name is given to the SDK.
void set_game_hosts(const char* game_name)
{
    text::format_to(qr2_hostname, "%s.master.%s", game_name, stored_domain().data());
    set_master_server(game_name);
}

// The 2007 gcd SDK asks back for re-authentication; the game's SDK had no such request (nothing to do).
void ignore_refresh_auth(int, int, int, char*, void*) {}

} // namespace

void get_msleep(unsigned milliseconds) { msleep(milliseconds); }
char* game_strdup(const char* text) { return goastrdup(text); }
void start_available_check(const char* game_name)
{
    // (the override is a whole host name: made for each check - the online service also checks "mototrax")
    text::format_to(GSIACHostname, "%s.available.%s", game_name, stored_domain().data());
    GSIStartAvailableCheck(game_name);
}
int available_check_think() { return static_cast<int>(GSIAvailableCheckThink()); }
const char* get_unique_id() { return GOAGetUniqueID(); }

void set_online_domain(const char* domain)
{
    text::format_to(stored_domain(), "%s", domain);
    text::format_to(gamespy_state().nat_neg1_, "natneg1.%s", stored_domain().data());
    text::format_to(gamespy_state().nat_neg2_, "natneg2.%s", stored_domain().data());
    text::format_to(gamespy_state().nat_neg3_, "natneg3.%s", stored_domain().data());
    Matchup1Hostname = gamespy_state().nat_neg1_.data();
    Matchup2Hostname = gamespy_state().nat_neg2_.data();
    Matchup3Hostname = gamespy_state().nat_neg3_.data();
    text::format_to(peerChatServerAddress, "peerchat.%s", stored_domain().data());
    text::format_to(GPConnectionManagerHostname, "gpcm.%s", stored_domain().data());
    text::format_to(GPSearchManagerHostname, "gpsp.%s", stored_domain().data());
    text::format_to(StatsServerHostname, "gamestats.%s", stored_domain().data());
    constexpr size_t kPtaUrlSize = 2048; // (PTA_MAX_STRING_SIZE)
    text::format_to(std::span(&gPTAMOTDURL[0], kPtaUrlSize), "http://motd.%s/motd/motd.asp", stored_domain().data());
    text::format_to(std::span(&gPTAVercheckURL[0], kPtaUrlSize), "http://motd.%s/motd/vercheck.asp", stored_domain().data());
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
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the GameSpy SDK's parameter is a non-const char* it only reads
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

namespace {

NatCompleted& nat_completed()
{
    static NatCompleted nat_completed{nullptr};
    return nat_completed;
}

// The SDK's completion callback: the user data back to the integer it was made from.
void nat_completed_relay(NegotiateResult result, SOCKET socket, sockaddr_in* address, void* user_data)
{
    if (nat_completed())
        nat_completed()(static_cast<int>(result), static_cast<int>(socket), address,
                        static_cast<int>(cell::address(user_data)));
}

} // namespace

int nat_begin_with_socket(uint32_t socket, int cookie, int client_index, HookFn progress, NatCompleted completed,
                          int user_data)
{
    nat_completed() = completed;
    return static_cast<int>(NNBeginNegotiationWithSocket(
        static_cast<SOCKET>(socket), cookie, client_index, as<NegotiateProgressFunc>(progress),
        completed ? &nat_completed_relay : nullptr, cell::at_address<void>(static_cast<uintptr_t>(user_data))));
}
void nat_think() { NNThink(); }
void nat_process_data(char* data, int length, void* from)
{
    NNProcessData(data, length, static_cast<sockaddr_in*>(from));
}
void nat_cancel(int cookie) { NNCancel(cookie); }
void nat_free_negotiations() { NNFreeNegotiateList(); }

// --- peer ---

void* peer_initialize(const PeerCallbacks& callbacks)
{
    PEERCallbacks sdk{};
    sdk.disconnected = as<peerDisconnectedCallback>(callbacks.disconnected);
    sdk.roomMessage = as<peerRoomMessageCallback>(callbacks.room_message_);
    sdk.roomUTM = as<peerRoomUTMCallback>(callbacks.room_utm_);
    sdk.roomNameChanged = as<peerRoomNameChangedCallback>(callbacks.room_name_changed_);
    sdk.roomModeChanged = as<peerRoomModeChangedCallback>(callbacks.room_mode_changed_);
    sdk.playerMessage = as<peerPlayerMessageCallback>(callbacks.player_message_);
    sdk.playerUTM = as<peerPlayerUTMCallback>(callbacks.player_utm_);
    sdk.readyChanged = as<peerReadyChangedCallback>(callbacks.ready_changed_);
    sdk.gameStarted = as<peerGameStartedCallback>(callbacks.game_started_);
    sdk.playerJoined = as<peerPlayerJoinedCallback>(callbacks.player_joined_);
    sdk.playerLeft = as<peerPlayerLeftCallback>(callbacks.player_left_);
    sdk.kicked = as<peerKickedCallback>(callbacks.kicked);
    sdk.newPlayerList = as<peerNewPlayerListCallback>(callbacks.new_player_list_);
    sdk.playerChangedNick = as<peerPlayerChangedNickCallback>(callbacks.player_changed_nick_);
    sdk.playerInfo = as<peerPlayerInfoCallback>(callbacks.player_info_);
    sdk.playerFlagsChanged = as<peerPlayerFlagsChangedCallback>(callbacks.player_flags_changed_);
    sdk.ping = as<peerPingCallback>(callbacks.ping);
    sdk.crossPing = as<peerCrossPingCallback>(callbacks.cross_ping_);
    sdk.globalKeyChanged = as<peerGlobalKeyChangedCallback>(callbacks.global_key_changed_);
    sdk.roomKeyChanged = as<peerRoomKeyChangedCallback>(callbacks.room_key_changed_);
    sdk.qrServerKey = as<peerQRServerKeyCallback>(callbacks.qr_server_key_);
    sdk.qrPlayerKey = as<peerQRPlayerKeyCallback>(callbacks.qr_player_key_);
    sdk.qrTeamKey = as<peerQRTeamKeyCallback>(callbacks.qr_team_key_);
    sdk.qrKeyList = as<peerQRKeyListCallback>(callbacks.qr_key_list_);
    sdk.qrCount = as<peerQRCountCallback>(callbacks.qr_count_);
    sdk.qrAddError = as<peerQRAddErrorCallback>(callbacks.qr_add_error_);
    sdk.qrNatNegotiateCallback = as<peerQRNatNegotiateCallback>(callbacks.qr_nat_negotiate_);
    sdk.qrPublicAddressCallback = as<peerQRPublicAddressCallback>(callbacks.qr_public_address_);
    sdk.param = callbacks.param;
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
                                         // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the SDK's PEERBool arrays, only read
                                         raw::as<PEERBool>(const_cast<int*>(ping_rooms)),
                                         // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the SDK's PEERBool arrays, only read
                                         raw::as<PEERBool>(const_cast<int*>(cross_ping_rooms))));
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
    return static_cast<int>(peerGetPlayerInfoNoWait(peer, nick, ip, profile));
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

int http_get_ex(const char* url, const char* headers, void* buffer, int size, void* post, int throttle, int blocking,
                HookFn progress, HookFn completed, void* param)
{
    return ghttpGetEx(url, headers, static_cast<char*>(buffer), size, static_cast<GHTTPPost>(post), static_cast<GHTTPBool>(throttle),
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

char* stats_game_name() { return std::data(gcd_gamename); }
char* stats_secret_key() { return std::data(gcd_secret_key); }
int stats_init_connection(int game_port) { return ::InitStatsConnection(game_port); }
int stats_is_connected() { return ::IsStatsConnected(); }
void stats_close_connection() { ::CloseStatsConnection(); }
int stats_persist_think() { return ::PersistThink(); }
void* stats_new_game(int use_buckets) { return NewGame(use_buckets); }
void stats_free_game(void* game) { FreeGame(static_cast<statsgame_t>(game)); }
const char* stats_get_challenge(void* game) { return GetChallenge(static_cast<statsgame_t>(game)); }
void stats_new_player(void* game, int number, const char* name)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the GameSpy SDK's parameter is a non-const char* it only reads
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
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the GameSpy SDK's parameter is a non-const char* it only reads
    ::GetPersistDataValues(local_id, profile, static_cast<persisttype_t>(type), index, const_cast<char*>(keys),
                           as<PersDataCallbackFn>(callback), instance);
}
void stats_generate_auth(const char* challenge, const char* password, char* response)
{
    ::GenerateAuth(challenge, password, response);
}
void set_server_int_bucket(void* game, const char* key, int value)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast, cppcoreguidelines-pro-type-const-cast): the GameSpy SDK's macro casts; the GameSpy SDK's parameter is a non-const char* it only reads
    BucketIntOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, value, bl_server, 0);
}
void set_server_string_bucket(void* game, const char* key, const char* value)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast, cppcoreguidelines-pro-type-const-cast): the GameSpy SDK's macro casts; the GameSpy SDK's parameter is a non-const char* it only reads
    BucketStringOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, const_cast<char*>(value), bl_server,
                   0);
}
void set_player_int_bucket(void* game, const char* key, int value, int player)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast, cppcoreguidelines-pro-type-const-cast): the GameSpy SDK's macro casts; the GameSpy SDK's parameter is a non-const char* it only reads
    BucketIntOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, value, bl_player, player);
}
void set_player_string_bucket(void* game, const char* key, const char* value, int player)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast, cppcoreguidelines-pro-type-const-cast): the GameSpy SDK's macro casts; the GameSpy SDK's parameter is a non-const char* it only reads
    BucketStringOp(static_cast<statsgame_t>(game), const_cast<char*>(key), bo_set, const_cast<char*>(value), bl_player,
                   player);
}

} // namespace game_spy
