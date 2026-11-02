// The network game's join (online::Hub): the clients and the server made, the connection request and the join
// request both ways, ok_to_join, join proceed / refused, GameSpy's CD-key answer. See gamenet_manager.h.
//
// NOT ORIGINAL in it: the THUGPRO peer mode (thugpro_join.h) - our requests carry THUGPRO's trailer and machine id,
// THUGPRO's refusal reasons are shown in words, and as host the GameSpy CD-key check is left out; bounds on what the
// host reads from a request.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/checksums.h"
#include "engine/core/format.h"
#include "engine/net/net_server.h" // IWYU pragma: keep - the complete HostEnd type (converted to AppHost* here)
#include "platform/gamespy.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/chat.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_manager.h"
#include "engine/net/player_info.h"
#include "engine/net/server_list.h"
#include "engine/net/stats_man.h"
#include "engine/net/thugpro_join.h"
#include "engine/net/thugpro_peer.h"
#include "formats/byte_stream.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "engine/sys/game_config.h"
#include "engine/sys/mlp.h"
#include "platform/stopwatch.h"
#include "platform/run_log.h"
#include "engine/assets/resource_registry.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/score_list.h"
#include "platform/crt.h"
#include "platform/compiler.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "wire/gamenet_messages.h"
#include "wire/net_wire.h"
#include "engine/core/no_destroy.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy / strncpy: the original's own copies into its fixed fields)

namespace online {

std::array<char, kPasswordSize>& pending_password()
{
    static std::array<char, kPasswordSize> pending_password{}; // (winmain.cpp: -PASSWORD when joining)
    return pending_password;
}

namespace {

using net::AppHost;
using net::Conn;
using net::HandlerSlotValue;
using net::InboundMsgCtx;
using net::NetMessageInfo;

// Messages.
constexpr uint8_t kMsgConnectionRequest = 3;
constexpr uint8_t kMsgJoinRefused = 71;    // 0x47
constexpr uint8_t kMsgJoinProceed = 79;    // 0x4F
constexpr uint8_t kMsgJoinRequest = 80;    // 0x50
constexpr uint8_t kMsgServerResponse = 16; // 0x10 (the server list)

// Names.
constexpr uint32_t kNetworkId = 0xCBED746E;  // network_id (the player's name preference)
constexpr uint32_t kServerName = 0x2A5CF019; // server_name
constexpr uint32_t kUiString = 0x96875C0F;   // ui_string
constexpr uint32_t kNetLobby = 0x1C471C60;   // netlobby (the lobby's game mode)
constexpr uint32_t kCreateEnterPasswordControl = 0x7721B33A;
constexpr uint32_t kCreateGameInProgressDialog = 0x89E9B82D;
constexpr uint32_t kStatusPlaying = 0xFF4ED0D9;       // (the presence text when joining to play)
constexpr uint32_t kStatusObserving = 0x9424C562;     // (... to observe)
constexpr uint32_t kReasonBanned = 0xE5A65DEC;        // net_reason_banned
constexpr uint32_t kReasonFull = 0x7782B5B7;          // net_reason_full
constexpr uint32_t kReasonFullObservers = 0xDDE5676F; // net_reason_full_observers
constexpr uint32_t kReasonWrongPassword = 0x3104C1F1; // net_reason_wrong_password
constexpr uint32_t kReasonRankLow = 0x922B12AD;       // net_reason_rank_limit_low
constexpr uint32_t kReasonRankHigh = 0x2AB1E499;      // net_reason_rank_limit_high
constexpr uint32_t kReasonDefault = 0xBA9CB8E8;       // net_reason_default

constexpr int kJoinModeObserve = 1;
constexpr int kRankTestGreater = 2; // get_rank_test: the joiner's rank must be above the limit
constexpr int kRankTestLess = 1;    // ... below it
constexpr int kConnectionTypeModem = 1;
constexpr uint32_t kCdKeyResponseLength = 72;
constexpr int kGameSpyGameId = 1003; // THUG2 PC's (the CD-key check)
constexpr int kDeferredJoinFrames = 10;
constexpr uint16_t kJoinPort = 0x1422; // 5154
constexpr uint16_t kHostPort = 0x1421; // 5153
constexpr int kPeerLimit = 20;
constexpr uint32_t kPlayerInfoSize = 0x44;

// App flags (make_client_app / Server).
constexpr int kAppBroadcast = 0x01;
constexpr int kAppLocal = 0x08;
constexpr int kAppDynamicResend = 0x04;
constexpr int kAppForeign = 0x10;
constexpr int kAppSecure = 0x20;

// Handler flags.
constexpr uint32_t kRunForForeign = 1;
constexpr uint32_t kRunWhenLate = 2;
constexpr uint32_t kRunOnBadCrc = 4;
constexpr int kMidPriority = 0x80;
constexpr int kTopRank = 0xFF;
constexpr int kBottomRank = 0;

// The player info being made (online::JoinerRecord, player_info.h; flags: 1 local player, 4 observer, 8 pending).
static_assert(sizeof(JoinerRecord) == kPlayerInfoSize);
constexpr uint32_t kPlayerPending = kPlayerPendingObserver; // (kPlayerLocal, kPlayerObserver: player_info.h)

// GameSpy CD-key's instance data for client_authorized (12 bytes, operator new).
// (the join request being sent: the original's is on its stack)
JoinRequestBlock& join_scratch()
{
    static core::NoDestroy<JoinRequestBlock> join_scratch;
    return join_scratch.get();
}

struct AuthInstance {
    AppHost* app;
    Hub* manager;
    Conn* conn;
};

// The global passwords: the one being typed (12) and the one in use (10; resent with every request).
std::array<char, kPasswordSize>& join_password()
{
    static std::array<char, kPasswordSize> join_password{};
    return join_password;
}

net::Hub* net_session_slot() { return net::Hub::sole(); }
tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks_); }
int milli_seconds() { return static_cast<int>(frame_clock::get_time()); }
uint32_t current_local_address() { return crt::get_inet_addr(net_session_slot()->fetch_own_address()); }

// The manager's methods.
int num_observers(Hub* m) { return m->count_observers(); }
int max_observers(Hub* m) { return m->get_spectator_limit(); }
int num_players(Hub* m) { return m->count_players(); }
int get_max_players(Hub* m) { return m->get_player_limit(); }
int rank_limit(Hub* m) { return m->get_rank_limit(); }
int rank_test(Hub* m) { return m->get_rank_test(); }
const char* get_password(Hub* m) { return m->fetch_secret(); }
void late_player_arrival(Hub* m, JoinerRecord* player, int frames) { m->add_joining_player(player, frames); }
void set_ready_state(Hub* m, bool ready) { m->set_ready_state(ready); }
void shuffle_spawn_slots(Hub* m) { m->choose_start_points(); }
void cancel_join_server(Hub* m) { m->cancel_join(); }
uint32_t active_ruleset_label() { return goals::game_mode()->get_name_key(); }
void set_maximum_players(int count) { goals::game_mode()->set_rider_ceiling(count); }
int8_t current_rank(Hub* m) { return static_cast<int8_t>(m->rating_service_->get_rank()); }
int32_t get_rating(Hub* m)
{
    // (the original reads the stats' first word, the rating, through the folded getter 0x00469490 -
    // single::PointerMember::get_pull)
    return m->rating_service_->get_personal_stats()->skill_points_;
}
bool match_underway(Hub* m) { return active_ruleset_label() != kNetLobby && !m->match_ended_; }

prefs::Preferences* net_game_options(Hub* m) { return &m->net_options_; }
const char* preference_text(Hub* m, uint32_t name)
{
    const char* text = nullptr;
    net_game_options(m)->fetch_setting(name)->fetch_text(kUiString, &text, 1);
    return text;
}

// The presence (GameSpy) and the lobby (not source yet).
bool presence_logged_in(Hub* m) { return m->presence_service_->authenticated_; }
void set_presence(Hub* m, int status, const char* text, const char* location)
{
    m->presence_service_->change_standing(status, text, location);
}
char* lobby_server_name(Hub* m) { return m->lobby_service_->server_name_.data(); }
char* lobby_peer_name(Hub* m) { return m->lobby_service_->room_name_.data(); }

// GameSpy's CD-key SDK (gcd) and THUG2's key.
void compute_cd_key_response(const char* key, const char* challenge, char* response)
{
    game_spy::cd_key_compute_response(key, challenge, response);
}
void authenticate_user(int local_id, uint32_t ip, const char* challenge, const char* response, void* instance)
{
    using HookFn = void (*)(int, int, int, char*, void*);
    game_spy::cd_key_authenticate_user(kGameSpyGameId, local_id, ip, challenge, response, &Hub::client_authorized,
                                       instance);
}

void get_sprintf(char* out, text::FormatFor<uint32_t, uint32_t, int, const char*, const char*> format, uint32_t a,
                 uint32_t b, int c, const char* d, const char* e)
{
    crt::get_sprintf(out, format, a, b, c, d, e);
}

NetMessageInfo new_desc()
{
    NetMessageInfo desc;
    return desc;
}

// The connection request (all four senders): THUG2's, or in the THUGPRO peer mode with THUGPRO's trailer (TP-R12).
// `request` must outlive the send (the message is copied when queued).
void send_connection_request(net::Guest* client, const ConnectInfo& info, thugpro::ConnectRequest* request)
{
    NetMessageInfo desc = new_desc();
    if (thugpro::peer_mode()) {
        desc.byte_count_ = thugpro::make_connect_request(info, request);
        desc.compression_ = 3; // (THUGPRO's LZHAM, as its client sends it - only if smaller)
    } else {
        util::ByteWriter writer(request->bytes.data(), sizeof(request->bytes));
        write_connect_info(writer, info);
        desc.byte_count_ = static_cast<uint16_t>(writer.position());
    }
    desc.content_ = request->bytes.data();
    desc.ident_ = kMsgConnectionRequest;
    client->send_to_host(&desc);
}

net::Guest* as_client(AppHost* app) { return core::down_cast<net::Guest>(app); }

// The handlers a joining client adds (0x00534480): on every client, then on client 0 only.
struct HandlerEntry {
    uint8_t id;
    HandlerSlotValue code;
    uint32_t flags;
    bool manager_data_;
    int priority;
};
constexpr auto kClientHandlers = std::to_array<HandlerEntry>({
    {.id = 0x42, .code = &online::handle_ready_to_play, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},  // proceed to play
    {.id = 0x4D, .code = &online::on_spectate_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},       // observe proceed
    {.id = 0x4E, .code = &online::handle_observers_full, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority}, // observe refused
    {.id = kMsgJoinRefused, .code = &Hub::on_turned_away_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},
    {.id = kMsgJoinProceed, .code = &Hub::on_go_ahead_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},
    {.id = 0x25, .code = &online::on_ready_ask_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},          // ready query
    {.id = 0x26, .code = &online::on_ready_reply_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},        // ready response
    {.id = 0x53, .code = &online::on_admitted_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},           // player info ack request
    {.id = 0x58, .code = &online::handle_crown_spawn, .flags = 0, .manager_data_ = true, .priority = kMidPriority},                   // the king dropped the crown
    {.id = 0x75, .code = &online::handle_flag_returned, .flags = 0, .manager_data_ = true, .priority = kMidPriority},                 // a player dropped a flag
    {.id = 0x3F, .code = &online::on_hud_notice_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},         // panel message
    {.id = 0x5E, .code = &online::handle_auto_server_notice, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority}, // auto server notification
    {.id = 0x5F, .code = &online::handle_fcfs_notice, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},        // first-come-first-served assignment
    {.id = 0x68, .code = &online::handle_joined_team, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},        // team change
    {.id = 0x69, .code = &online::handle_choose_team, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},        // number of teams
    {.id = 0x4C, .code = &online::on_chat_msg, .flags = kRunWhenLate, .manager_data_ = false, .priority = kMidPriority},              // chat
    {.id = 0x7C, .code = &online::handle_cd_key_challenge, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},   // challenge
});
constexpr auto kGameClientHandlers = std::to_array<HandlerEntry>({
    {.id = 0x6D, .code = &online::handle_selected_goals, .flags = 0, .manager_data_ = true, .priority = kMidPriority},    // selected goals
    {.id = 0x34, .code = &online::handle_start_game, .flags = 0, .manager_data_ = true, .priority = kMidPriority},        // game info
    {.id = 0x7A, .code = &online::handle_kill_team_flags, .flags = 0, .manager_data_ = true, .priority = kMidPriority},   // kill the team flags
    {.id = 0x6E, .code = &online::handle_goal_completed, .flags = 0, .manager_data_ = true, .priority = kMidPriority},    // a goal beaten (relayed)
    {.id = 0x6F, .code = &online::handle_goal_started, .flags = 0, .manager_data_ = true, .priority = kMidPriority},      // a goal started (relayed)
    {.id = 0x70, .code = &online::handle_nothing, .flags = 0, .manager_data_ = true, .priority = kMidPriority},           // toggle pro set (nothing)
    {.id = 0x46, .code = &online::on_level_switch_msg, .flags = 0, .manager_data_ = true, .priority = kTopRank},          // change level
    {.id = 0x46, .code = &online::handle_nothing, .flags = 0, .manager_data_ = true, .priority = kTopRank},               // (and nothing)
    {.id = 0x6C, .code = &online::handle_park_map, .flags = 0, .manager_data_ = true, .priority = kTopRank},              // level data
    {.id = 0x88, .code = &online::handle_rails_data, .flags = kRunOnBadCrc, .manager_data_ = true, .priority = kTopRank}, // rail data
    {.id = 0x87, .code = &online::on_goal_sync_msg, .flags = kRunOnBadCrc, .manager_data_ = true, .priority = kTopRank},  // goals data
    {.id = 0x80, .code = &online::handle_checksum_request, .flags = 0, .manager_data_ = true, .priority = kMidPriority},  // cheat checksum request
});
constexpr auto kRemoteGameClientHandlers = std::to_array<HandlerEntry>({
    {.id = 0x22, .code = &online::on_player_added_msg, .flags = 0, .manager_data_ = true, .priority = kMidPriority},    // player create
    {.id = 0x79, .code = &online::handle_flush_important, .flags = 0, .manager_data_ = true, .priority = kMidPriority}, // join accepted
});
constexpr auto kGameClientHandlers2 = std::to_array<HandlerEntry>({
    {.id = 0x7B, .code = &online::handle_keep_alive, .flags = 0, .manager_data_ = true, .priority = kMidPriority},    // wait n seconds
    {.id = 0x55, .code = &online::on_crown_change_msg, .flags = 0, .manager_data_ = true, .priority = kMidPriority},  // new king
    {.id = 0x72, .code = &online::on_flag_scored_msg, .flags = 0, .manager_data_ = true, .priority = kMidPriority},   // captured flag
    {.id = 0x74, .code = &online::on_flag_swiped_msg, .flags = 0, .manager_data_ = true, .priority = kMidPriority},   // stole flag
    {.id = 0x71, .code = &online::on_flag_grabbed_msg, .flags = 0, .manager_data_ = true, .priority = kMidPriority},  // took flag
    {.id = 0x73, .code = &online::on_flag_returned_msg, .flags = 0, .manager_data_ = true, .priority = kMidPriority}, // retrieved flag
    {.id = 0x5A, .code = &online::on_match_end_msg, .flags = 0, .manager_data_ = true, .priority = kMidPriority},     // game over
    {.id = 0x6A, .code = &online::handle_game_ended, .flags = 0, .manager_data_ = true, .priority = kMidPriority},    // end game
    {.id = 0x23, .code = &online::handle_object_update_length, .flags = kRunForForeign | kRunWhenLate, .manager_data_ = true,
     .priority = kTopRank},                                                // object updates (their length)
    {.id = 0x8D, .code = &online::handle_eliminated, .flags = 0, .manager_data_ = true, .priority = kMidPriority}, // a skater eliminated
    {.id = 0x8E, .code = &online::handle_eliminated, .flags = 0, .manager_data_ = true, .priority = kMidPriority},
});
// The server's lobby handlers (0x0053C380; taken out once the game starts).
constexpr auto kServerHandlers = std::to_array<HandlerEntry>({
    {.id = kMsgConnectionRequest, .code = &Hub::on_link_established, .flags = kRunForForeign | kRunWhenLate, .manager_data_ = true, .priority = kBottomRank},
    {.id = 0x90, .code = &online::handle_team_object, .flags = 0, .manager_data_ = true, .priority = kTopRank}, // a scavenger-hunt object picked up
    {.id = kMsgJoinRequest, .code = &Hub::on_admission_ask_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},
    {.id = 0x11, .code = &online::on_server_query_msg, .flags = kRunForForeign | kRunWhenLate, .manager_data_ = true, .priority = kMidPriority}, // find server
    {.id = 0x0B, .code = &online::on_peer_left_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},                     // disconnection request
    {.id = 0x4A, .code = &online::handle_observe_request, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},               // observe
    {.id = 0x26, .code = &online::on_ready_reply_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},                   // ready response
    {.id = 0x54, .code = &online::handle_send_game_state, .flags = 0, .manager_data_ = true, .priority = kMidPriority},                          // player info ack
    {.id = 0x57, .code = &online::handle_drop_carried, .flags = 0, .manager_data_ = true, .priority = kMidPriority},                             // moved to a restart
    {.id = 0x59, .code = &online::handle_game_ended_report, .flags = 0, .manager_data_ = true, .priority = kMidPriority},                        // the run ended
    {.id = 0x60, .code = &online::handle_host_options, .flags = kRunWhenLate, .manager_data_ = true,
     .priority = kMidPriority}, // first-come-first-served requests: start game,
    {.id = 0x61, .code = &online::handle_host_options, .flags = kRunWhenLate, .manager_data_ = true,
     .priority = kMidPriority}, // ban player, change level, toggle pro set, goal
    {.id = 0x62, .code = &online::handle_host_options, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority}, // selection, end game, number of teams
    {.id = 0x63, .code = &online::handle_host_options, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},
    {.id = 0x64, .code = &online::handle_host_options, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},
    {.id = 0x65, .code = &online::handle_host_options, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},
    {.id = 0x66, .code = &online::handle_host_options, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},
    {.id = 0x67, .code = &online::on_switch_squad_msg, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},          // team change request
    {.id = 0x6B, .code = &online::handle_request_game_data, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},     // level request
    {.id = 0x6E, .code = &online::handle_goal_completed_report, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority}, // a goal beaten
    {.id = 0x6F, .code = &online::handle_goal_started_report, .flags = kRunWhenLate, .manager_data_ = true, .priority = kMidPriority},   // a goal started
    {.id = 0x6C, .code = &online::handle_park_map, .flags = 0, .manager_data_ = true, .priority = kTopRank},                             // level data
    {.id = 0x88, .code = &online::handle_rails_data, .flags = 0, .manager_data_ = true, .priority = kTopRank},                           // rail data
    {.id = 0x7D, .code = &online::handle_cd_key_answer, .flags = 0, .manager_data_ = true, .priority = kMidPriority},                    // challenge response
    {.id = 0x81, .code = &online::handle_checksum_response, .flags = 0, .manager_data_ = true, .priority = kMidPriority},                // cheat checksum response
    {.id = 0x8B, .code = &online::handle_created_data_request, .flags = 0, .manager_data_ = true, .priority = kMidPriority},             // rails / goals data requests
    {.id = 0x8A, .code = &online::handle_created_data_request, .flags = 0, .manager_data_ = true, .priority = kMidPriority},
});

template <size_t N>
void add_handlers(AppHost* app, Hub* manager, const std::array<HandlerEntry, N>& entries)
{
    for (const HandlerEntry& entry : entries)
        app->dispatcher_.append_listener(entry.id, entry.code, entry.flags, entry.manager_data_ ? manager : nullptr,
                                         entry.priority);
}

void send_refused(AppHost* app, Conn* conn, const char* data)
{
    app->deliver_to(kMsgJoinRefused, kJoinRefusedSize, data, conn->remote_ip_, conn->port_no_, 0);
}

} // namespace

// 0x0052F560
bool Hub::joins_accepted(int& reason, ConnectInfo* msg, Conn* conn, bool check_password, bool check_version,
                         bool check_cd_key)
{
    if (host_->is_conn_blocked(conn)) {
        reason = kRefusedBanned;
        return false;
    }
    if (msg->observer) {
        if (num_observers(this) >= max_observers(this)) {
            reason = kRefusedFullObservers;
            return false;
        }
    } else if (num_players(this) >= get_max_players(this)) {
        reason = kRefusedFull;
        return false;
    }
    if (check_version && msg->version != kVersion) {
        reason = kRefusedVersion;
        return false;
    }
    if (conn->is_off_machine()) {
        const int limit = rank_limit(this);
        const int test = rank_test(this);
        if (test == kRankTestGreater) {
            if (msg->rank <= limit) {
                reason = kRefusedRankLow;
                return false;
            }
        } else if (test == kRankTestLess && msg->rank >= limit) {
            reason = kRefusedRankHigh;
            return false;
        }
    }
    if (!msg->can_queue_up_ && !msg->observer && match_underway(this)) {
        reason = kRefusedInProgress;
        return false;
    }
    if (check_password) {
        const char* password = get_password(this);
        if (*password && std::strcmp(password, msg->password.data()) != 0) {
            reason = kRefusedPassword;
            return false;
        }
    }
    // NOT ORIGINAL: no CD-key check in the THUGPRO peer mode (TP-R12) - THUGPRO's hosts skip it (its patch over this
    // test) and its players' keys are made up. (The response ends inside its field here: the original's strlen could
    // read past a message without one.)
    if (check_cd_key && !thugpro::peer_mode() &&
        strnlen(msg->cd_key_response_.data(), sizeof(msg->cd_key_response_)) != kCdKeyResponseLength) {
        reason = kRefusedCdKey;
        return false;
    }
    return true;
}

// 0x00534480
void Hub::join_server(uint32_t ip, uint16_t port, int index)
{
    net::Hub* net = net_session_slot();
    const char* name = preference_text(this, kNetworkId); // (not used)
    (void)name;
    AppHost* client = client_slot(index);
    ConnectInfo info{};
    std::memset(&info, 0, sizeof(info)); // (the original sends its stack's garbage in the fields it doesn't set)
    if (client->is_host_local()) {
        // The host's own client: its connection and the server's know each other.
        Conn* server_conn = host_->opened_link(static_cast<int>(ip), port, Conn::kFlagLocal);
        Conn* conn = client->opened_link(static_cast<int>(ip), port, Conn::kFlagLocal);
        client->link_duplicate_conns(server_conn, conn);
    } else {
        as_client(client)->dial_server(ip, port);
        client->opened_link(static_cast<int>(ip), port, Conn::kFlagRemote);
        join_began_at_ = milli_seconds();
        get_logic_tasks()->queue_job(join_expiry_job_.get());
        join_phase_ = kJoinStateConnecting;
        get_logic_tasks()->queue_job(join_phase_job_.get());
        join_port_ = port;
        join_ip_ = ip;
        info.rank = current_rank(this);
    }
    info.observer = join_mode_ == kJoinModeObserve;
    info.version = kVersion;
    if (std::strlen(pending_password().data()) != 0) {
        std::strcpy(info.password.data(), pending_password().data());
        std::strcpy(join_password().data(), pending_password().data());
    } else {
        std::memset(join_password().data(), 0, kPasswordSize);
        core::at(info.password, 0) = '\0';
    }
    if (!client->is_host_local())
        std::memset(pending_password().data(), 0, kPasswordSize);
    info.can_queue_up_ = false;
    info.broadband = net->get_net_link_kind() != kConnectionTypeModem;
    thugpro::ConnectRequest request{};
    send_connection_request(as_client(client), info, &request);
    client->transmit(false);

    add_handlers(client, this, kClientHandlers);
    if (index == 0) {
        add_handlers(client_app_, this, kGameClientHandlers);
        if (!client_app_->is_host_local())
            add_handlers(client_app_, this, kRemoteGameClientHandlers);
        add_handlers(client_app_, this, kGameClientHandlers2);
    }
    spectate_request_ = 0;
}

// 0x00534AF0
void Hub::retry_entry(AppHost* client)
{
    ConnectInfo info{};
    std::memset(&info, 0, sizeof(info));
    info.observer = join_mode_ == kJoinModeObserve;
    info.version = kVersion;
    info.can_queue_up_ = true;
    core::at(info.password, 0) = '\0';
    info.broadband = net_session_slot()->get_net_link_kind() != kConnectionTypeModem;
    if (in_internet_mode())
        info.rank = current_rank(this);
    thugpro::ConnectRequest request{};
    send_connection_request(as_client(client), info, &request);
    client->transmit(false);
    join_began_at_ = milli_seconds();
    get_logic_tasks()->queue_job(join_expiry_job_.get());
    join_phase_ = kJoinStateJoining;
}

// 0x00534BD0
void Hub::retry_entry_with_key(const char* password)
{
    std::strcpy(join_password().data(), password);
    const char* name = preference_text(this, kNetworkId); // (not used)
    (void)name;
    ConnectInfo info{};
    std::memset(&info, 0, sizeof(info));
    info.rank = current_rank(this);
    info.observer = join_mode_ == kJoinModeObserve;
    info.version = kVersion;
    info.can_queue_up_ = true;
    info.broadband = net_session_slot()->get_net_link_kind() != kConnectionTypeModem;
    std::strcpy(info.password.data(), password);
    thugpro::ConnectRequest request{};
    send_connection_request(as_client(client_app_), info, &request);
    join_began_at_ = milli_seconds();
    get_logic_tasks()->queue_job(join_expiry_job_.get());
    join_phase_ = kJoinStateJoiningWithPassword;
}

const std::array<char, 10> kUiStringText{{"ui_string"}};
const std::array<char, 19> kNetReasonGeneral{{"net_reason_general"}};
constexpr std::array<char, 27> kPresenceLocationFormat{{"10thug211:%d:%d:%d:%s (%s)"}};
const std::array<char, 13> kMatchAppName{{"Match Skate6"}};
const std::array<char, 7> kSkateAppName{{"Skate6"}};

// 0x00537220
AppHost* Hub::start_client_session(bool broadcast, bool local, bool secure, int index)
{
    net::Hub* net = net_session_slot();
    int flags = broadcast ? kAppBroadcast : 0;
    flags |= local ? kAppLocal : kAppDynamicResend | kAppForeign;
    if (secure)
        flags |= kAppSecure;
    AppHost* client = net->make_client_app(index, kSkateAppName.data(), kJoinPort, // (its name)
                                           static_cast<int>(current_local_address()), flags);
    client_slot(index) = client;
    if (index == 0 && client) {
        last_ready_probe_id_ = 0;
        script_flag_bits_ = 0;
        viewed_player_ = nullptr;
        online::set_ready_state(this, false); // (the helper: 0x00537140 through the hook)
    }
    spectator_pad_listener_ = nullptr;
    match_ended_ = false;
    match_being_set_up_ = false;
    return client_slot(index);
}

// 0x00534430
AppHost* Hub::start_matchmaking_client()
{
    net::Hub* net = net_session_slot();
    AppHost* client = net->make_client_app(0, kMatchAppName.data(), kJoinPort, static_cast<int>(current_local_address()),
                                           kAppBroadcast | kAppDynamicResend | kAppForeign | kAppSecure);
    browser_client_ = client;
    client->dispatcher_.append_listener(kMsgServerResponse, &on_server_reply_msg, kRunForForeign | kRunWhenLate, this,
                                        kMidPriority);
    return browser_client_;
}

// 0x0053C380
AppHost* Hub::start_host_session(bool local, bool secure)
{
    net::Hub* net = net_session_slot();
    int flags = kAppBroadcast | kAppDynamicResend | kAppForeign | (local ? kAppLocal : 0);
    if (secure)
        flags |= kAppSecure;
    AppHost* server =
        net->make_server_app(0, kSkateAppName.data(), kPeerLimit, kHostPort, static_cast<int>(current_local_address()), flags);
    host_ = server;
    if (host_mode_ != 0)
        host_mode_ = 0; // serve
    if (server) {
        get_logic_tasks()->queue_job(host_admit_job_.get());
        add_handlers(host_, this, kServerHandlers);
        score_synced_at_ = 0;
        awaiting_start_ = false;
        shuffle_spawn_slots(this);
        spectator_pad_listener_ = nullptr;
    }
    return host_;
}

// 0x0052F790
int Hub::on_link_established(InboundMsgCtx* context)
{
    Conn* conn = context->sender_conn_;
    auto* manager = static_cast<Hub*>(context->content_);
    if (!conn->is_off_machine()) {
        // The host's own client: proceed at once (only the player count filled in).
        NetMessageInfo desc = new_desc();
        ProceedInfo proceed{};
        std::memset(&proceed, 0, sizeof(proceed));
        proceed.max_players_ = static_cast<int8_t>(get_max_players(manager));
        std::array<uint8_t, kProceedInfoSize> proceed_bytes{};
        util::ByteWriter writer(proceed_bytes.data(), sizeof(proceed_bytes));
        write_proceed_info(writer, proceed);
        desc.content_ = proceed_bytes.data();
        desc.ident_ = kMsgJoinProceed;
        desc.byte_count_ = sizeof(proceed_bytes);
        context->receiver_app_->put_on_outbox(conn->hdl_, &desc);
        return net::kResultPassOn;
    }
    if (!(context->wire_options_ & kRunForForeign))
        return net::kResultConsumed;
    net::Hub* net = net_session_slot();
    if (!context->receiver_app_->allows_outside_peers())
        return net::kResultConsumed;
    // NOT ORIGINAL: a request shorter than THUG2's isn't read (the original reads past it).
    util::ByteReader reader(context->body_, context->body_size_);
    ConnectInfo request{};
    if (!read_connect_info(reader, &request))
        return net::kResultConsumed;
    ConnectInfo* msg = &request;
    thugpro::ConnectTrailer trailer{};
    if (thugpro::peer_mode() && thugpro::read_connect_trailer(context->body_, context->body_size_, &trailer))
        get_log("THUGPRO join (TP-R12): a THUGPRO client asks to join: version %08X, release %u, scripts %08X%s, "
                "machine %08X",
                trailer.version, trailer.release, trailer.scripts_hash_, trailer.modified_scripts_ ? " (modified)" : "",
                trailer.machine_id_);
    int reason = 0;
    if (!manager->joins_accepted(reason, msg, conn, true, true, true)) {
        std::array<char, 32> refused = {}; // (10 sent: the reason, and the challenge for kRefusedCdKey)
        core::at(refused, 0) = static_cast<char>(reason);
        if (reason == kRefusedCdKey)
            std::strcpy(std::span<char>(refused).subspan(1).data(), manager->challenge_.data());
        send_refused(context->receiver_app_, conn, refused.data());
        return net::kResultStop;
    }
    Conn* joined =
        context->receiver_app_->opened_link(static_cast<int>(conn->remote_ip_), conn->port_no_, Conn::kFlagRemote);
    context->sender_conn_ = joined;
    joined->set_link_speed_class(msg->broadband ? Conn::kBandwidthBroadband : Conn::kBandwidthModem);
    joined->set_transmit_period(static_cast<uint32_t>(1000 / game_config::frame_rate() * 2)); // two frames
    // NOT ORIGINAL: in the THUGPRO peer mode no GameSpy CD-key check (TP-R12): proceed as on a LAN.
    if (manager->in_internet_mode() && !thugpro::peer_mode()) {
        auto* instance = static_cast<AuthInstance*>(memory::alloc(sizeof(AuthInstance)));
        if (instance)
            std::memset(instance, 0, sizeof(*instance));
        instance->manager = manager;
        instance->app = context->receiver_app_;
        instance->conn = joined;
        authenticate_user(static_cast<int>(joined->ident_), context->sender_conn_->remote_ip_, manager->challenge_.data(),
                          msg->cd_key_response_.data(), instance);
        return net::kResultPassOn;
    }
    ProceedInfo proceed{};
    std::memset(&proceed, 0, sizeof(proceed)); // (the original leaves the addresses as its stack had them on a LAN)
    proceed.max_players_ = static_cast<int8_t>(get_max_players(manager));
    proceed.broadband = net->get_net_link_kind() != kConnectionTypeModem;
    if (manager->in_internet_mode()) {
        proceed.port = manager->join_port_;
        proceed.private_ip_ = manager->join_private_ip_;
        proceed.public_ip_ = manager->join_ip_;
    }
    std::strncpy(proceed.server_name_.data(), preference_text(manager, kServerName), kServerNameSize - 1);
    core::at(proceed.server_name_, kServerNameSize - 1) = '\0';
    std::array<uint8_t, kProceedInfoSize> proceed_bytes{};
    util::ByteWriter writer(proceed_bytes.data(), sizeof(proceed_bytes));
    write_proceed_info(writer, proceed);
    context->receiver_app_->deliver_to(kMsgJoinProceed, sizeof(proceed_bytes), proceed_bytes.data(), joined->remote_ip_,
                                       joined->port_no_, 0);
    return net::kResultPassOn;
}

// 0x00536530
void Hub::client_authorized(int /*gameId*/, int /*local_id*/, int authenticated, char* /*error*/, void* data)
{
    auto* instance = static_cast<AuthInstance*>(data);
    if (!instance)
        return;
    if (!authenticated) {
        std::array<char, kJoinRefusedSize> refused = {};
        core::at(refused, 0) = kRefusedCdKey;
        send_refused(instance->app, instance->conn, refused.data());
        instance->conn->mark_stale();
    } else {
        Hub* manager = instance->manager;
        ProceedInfo proceed{};
        std::memset(&proceed, 0, sizeof(proceed));
        proceed.max_players_ = static_cast<int8_t>(get_max_players(manager));
        proceed.broadband = true;
        if (manager->in_internet_mode()) {
            proceed.port = manager->join_port_;
            proceed.private_ip_ = manager->join_private_ip_;
            proceed.public_ip_ = manager->join_ip_;
        }
        std::strncpy(proceed.server_name_.data(), preference_text(manager, kServerName), kServerNameSize - 1);
        core::at(proceed.server_name_, kServerNameSize - 1) = '\0';
        std::array<uint8_t, kProceedInfoSize> proceed_bytes{};
        util::ByteWriter writer(proceed_bytes.data(), sizeof(proceed_bytes));
        write_proceed_info(writer, proceed);
        instance->app->deliver_to(kMsgJoinProceed, sizeof(proceed_bytes), proceed_bytes.data(), instance->conn->remote_ip_,
                                  instance->conn->port_no_, 0);
    }
    memory::Hub::get_pull()->dealloc(instance);
}

// 0x0052FA10
int Hub::on_admission_ask_msg(InboundMsgCtx* context)
{
    Conn* conn = context->sender_conn_;
    auto* manager = static_cast<Hub*>(context->content_);
    modules::Skate* skate = modules::Skate::sole();
    context->acknowledge_ = true;
    // NOT ORIGINAL: a request shorter than THUG2's header isn't read (the original reads past it).
    util::ByteReader reader(context->body_, context->body_size_);
    JoinRequestHeader request{};
    if (!read_join_request(reader, &request))
        return net::kResultConsumed;
    JoinRequestHeader* msg = &request;
    int reason = 0;
    if (conn->is_off_machine() && !manager->joins_accepted(reason, msg, conn, false, false, false)) {
        NetMessageInfo desc = new_desc();
        std::array<char, kJoinRefusedSize> refused = {};
        core::at(refused, 0) = static_cast<char>(reason);
        desc.content_ = refused.data();
        desc.byte_count_ = kJoinRefusedSize;
        desc.ident_ = kMsgJoinRefused;
        context->receiver_app_->put_on_outbox(conn->hdl_, &desc);
        context->sender_conn_->mark_stale();
        return net::kResultStop;
    }
    void* memory = memory::alloc_zeroed(kPlayerInfoSize);
    auto* player = memory ? memory::construct_in<JoinerRecord>(memory) : nullptr;
    std::strncpy(player->label_.data(), msg->name.data(), kNameSize - 1);
    core::at(player->label_, kNameSize - 1) = '\0';
    player->entity_key_ = static_cast<uint8_t>(manager->reserve_obj_id_for_player());
    player->conn_ = context->sender_conn_;
    player->flag_bits_ = 0;
    player->profile_id_ = msg->profile;
    player->skill_points_ = msg->rating;
    player->drive_control_mode_ = msg->drive_scheme_;
    player->score_ = 0;
    if (msg->observer)
        player->flag_bits_ = kPlayerObserver;
    else if (match_underway(manager))
        player->flag_bits_ |= kPlayerPending | kPlayerObserver;
    if (context->sender_conn_->is_host_local()) {
        // The host's own player: its profile copied through a buffer.
        if (!memory::manager_ready_flag())
            memory::ensure_manager();
        auto* buffer = static_cast<uint8_t*>(memory::Hub::get_pull()->carve(kAppearanceSize, true, nullptr));
        auto* profile = static_cast<modules::Skate*>(skate)->fetch_account(
            static_cast<int>(static_cast<int8_t>(player->entity_key_)));
        profile->emit_into_buffer(buffer, kAppearanceSize, 0);
        player->account_->decode_from_bytes(buffer, kAppearanceSize);
        memory::Hub::get_pull()->dealloc(buffer);
        player->flag_bits_ = kPlayerLocal;
        late_player_arrival(manager, player, 0);
        player->~JoinerRecord();
        memory::Hub::get_pull()->dealloc(player);
        // On the internet the host checks its own CD key too: an answer nobody reads (no instance). NOT ORIGINAL:
        // not in the THUGPRO peer mode (TP-R12).
        if (manager->in_internet_mode() && !thugpro::peer_mode()) {
            std::array<char, 0x80> key{};
            std::array<char, 0x80> response{};
            get_cd_key(key.data());
            compute_cd_key_response(key.data(), manager->challenge_.data(), response.data());
            authenticate_user(0, context->sender_conn_->remote_ip_, manager->challenge_.data(), response.data(), nullptr);
        }
        return net::kResultPassOn;
    }
    if (!msg->observer) {
        // NOT ORIGINAL: the appearance read from a copy as long as the most a request holds (a request cut short
        // reads zeros, not past the message).
        std::array<uint8_t, kAppearanceSize> appearance{};
        size_t size = reader.remaining();
        if (size > kAppearanceSize)
            size = kAppearanceSize;
        std::memset(appearance.data(), 0, kAppearanceSize);
        reader.bytes(appearance.data(), size);
        player->account_->decode_from_bytes(appearance.data(), sizeof(appearance));
    }
    late_player_arrival(manager, player, kDeferredJoinFrames);
    player->~JoinerRecord();
    memory::Hub::get_pull()->dealloc(player);
    // Busy until it's in: no game messages meanwhile.
    context->sender_conn_->cur_standing_ = (context->sender_conn_->cur_standing_ & ~Conn::kConnReady) | Conn::kStatus2;
    return net::kResultPassOn;
}

// 0x0052E490
int Hub::on_go_ahead_msg(InboundMsgCtx* context)
{
    net::Hub* net = net_session_slot();
    NetMessageInfo desc = new_desc();
    Conn* conn = context->sender_conn_;
    auto* manager = static_cast<Hub*>(context->content_);
    // (NOT ORIGINAL: one too short is dropped - the original read past it -, and the server's name is cut to its field)
    util::ByteReader reader(context->body_, context->body_size_);
    ProceedInfo answer{};
    if (!read_proceed_info(reader, &answer))
        return net::kResultConsumed;
    ProceedInfo* proceed = &answer;
    if (conn->is_off_machine()) {
        conn->set_link_speed_class(proceed->broadband ? Conn::kBandwidthBroadband : Conn::kBandwidthModem);
        manager->join_ip_ = proceed->public_ip_;
        manager->join_private_ip_ = proceed->private_ip_;
        manager->join_port_ = proceed->port;
        if (presence_logged_in(manager)) {
            std::strcpy(lobby_server_name(manager), proceed->server_name_.data());
            std::array<char, 0x1000> location{};
            get_sprintf(location.data(), kPresenceLocationFormat.data(), manager->join_ip_, manager->join_private_ip_,
                        manager->join_port_, lobby_server_name(manager), lobby_peer_name(manager));
            const char* status = script::fetch_text(manager->join_mode_ == 0 ? kStatusPlaying : kStatusObserving);
            set_presence(manager, 2, status, location.data());
        }
    }
    set_maximum_players(proceed->max_players_);
    const char* name = preference_text(manager, kNetworkId);
    JoinRequestBlock* info = &join_scratch(); // (the original's is on its stack: 4 KB)
    std::memset(static_cast<JoinRequestHeader*>(info), 0, sizeof(JoinRequestHeader));
    // NOT ORIGINAL: in the THUGPRO peer mode the profile is our machine id, as THUGPRO's client sends it (TP-R12).
    info->profile =
        thugpro::peer_mode() ? static_cast<int32_t>(thugpro::get_machine_id()) : manager->presence_service_->account_;
    info->rating = get_rating(manager);
    info->observer = manager->join_mode_ == kJoinModeObserve;
    info->version = kVersion;
    info->can_queue_up_ = true;
    bool narrowband = false;
    if (net->get_net_link_kind() == kConnectionTypeModem) {
        narrowband = true;
        info->broadband = false;
    } else {
        narrowband = false;
        info->broadband = true;
    }
    std::strncpy(info->name.data(), name, kNameSize - 1);
    core::at(info->name, kNameSize - 1) = '\0';
    std::strcpy(info->password.data(), join_password().data());
    int size = 0;
    if (!info->observer) {
        auto* profile = modules::Skate::sole()->fetch_account(0);
        net::ConnSearch search;
        if (context->receiver_app_->begin_conn_walk(&search)->get_link_speed_class() == Conn::kBandwidthModem)
            narrowband = true; // (a narrowband server gets no face)
        size = profile->emit_into_buffer(info->appearance.data(), kAppearanceSize, narrowband);
    }
    static std::array<uint8_t, kJoinInfoHeaderSize + kAppearanceSize> request_bytes; // (the message is copied when queued)
    util::ByteWriter writer(request_bytes.data(), sizeof(request_bytes));
    write_join_request(writer, *info);
    writer.bytes(info->appearance.data(), static_cast<size_t>(size));
    desc.content_ = request_bytes.data();
    desc.byte_count_ = static_cast<uint16_t>(writer.position());
    desc.ident_ = kMsgJoinRequest;
    desc.compression_ = 2; // LZSS (in the peer mode it goes as THUGPRO's LZHAM: compress_message)
    if (context->sender_conn_->is_off_machine())
        desc.queue_ = NetMessageInfo::kLaneOrdered;
    as_client(context->receiver_app_)->send_to_host(&desc);
    manager->join_phase_ = kJoinStateJoining;
    return net::kResultConsumed;
}

// 0x0052E1E0
int Hub::on_turned_away_msg(InboundMsgCtx* context)
{
    auto* manager = static_cast<Hub*>(context->content_);
    const int reason = int{net::wire::value_at<int8_t>(context->body_)};
    switch (reason) {
    case kRefusedBanned:
        manager->refused_reason_ = kReasonBanned;
        break;
    case kRefusedFull:
        manager->refused_reason_ = kReasonFull;
        break;
    case kRefusedFullObservers:
        manager->refused_reason_ = kReasonFullObservers;
        break;
    case kRefusedCdKey: {
        if (manager->join_phase_ == kJoinStateCdKey) {
            // Answered already: refused.
            manager->refused_reason_ = checksums::hash_from_text(kNetReasonGeneral.data());
            break;
        }
        // The host's challenge (after the reason): answered with our key, in a new connection request.
        std::array<char, 0x80> key{};
        std::array<char, 0x80> response{};
        // NOT ORIGINAL: the challenge read from the message's own 9 bytes (NUL-ended here).
        std::array<char, kJoinRefusedSize> challenge = {};
        const uint32_t challenge_size = context->body_size_ > 1 ? context->body_size_ - 1 : 0;
        std::memcpy(challenge.data(), net::wire::text_at(context->body_, 1),
                    challenge_size < kJoinRefusedSize - 1u ? challenge_size : kJoinRefusedSize - 1u);
        get_cd_key(key.data());
        compute_cd_key_response(key.data(), challenge.data(), response.data());
        net::Hub* net = net_session_slot();
        ConnectInfo info{};
        std::memset(&info, 0, sizeof(info));
        std::strcpy(info.cd_key_response_.data(), response.data());
        const char* name = nullptr; // (not used)
        net_game_options(manager)->fetch_setting(kNetworkId)->fetch_text(kUiStringText.data(), &name, 1);
        info.rank = current_rank(manager);
        info.observer = manager->join_mode_ == kJoinModeObserve;
        info.version = kVersion;
        info.can_queue_up_ = true;
        info.broadband = net->get_net_link_kind() != kConnectionTypeModem;
        std::strcpy(info.password.data(), join_password().data());
        thugpro::ConnectRequest request{};
        send_connection_request(as_client(manager->client_app_), info, &request);
        manager->join_phase_ = kJoinStateCdKey;
        return net::kResultPassOn;
    }
    case kRefusedPassword:
        if (manager->join_phase_ == kJoinStateTryingPassword) {
            manager->refused_reason_ = kReasonWrongPassword;
            break;
        }
        // Ask for it.
        memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->front_end_heap_);
        script::launch_script(kCreateEnterPasswordControl, nullptr, nullptr, false, nullptr);
        manager->join_expiry_job_->drop_out();
        memory::Hub::get_pull()->leave_heap_scope();
        return net::kResultPassOn;
    case kRefusedInProgress:
        script::launch_script(kCreateGameInProgressDialog, nullptr, nullptr, false, nullptr);
        manager->join_expiry_job_->drop_out();
        return net::kResultPassOn;
    case kRefusedRankLow:
        manager->refused_reason_ = kReasonRankLow;
        break;
    case kRefusedRankHigh:
        manager->refused_reason_ = kReasonRankHigh;
        break;
    default:
        // (the wrong version too.) NOT ORIGINAL: THUGPRO's reasons 31-37 in words (TP-R13).
        if (const uint32_t text = thugpro::refused_reason_text(reason))
            manager->refused_reason_ = text;
        else
            manager->refused_reason_ = kReasonDefault;
        break;
    }
    cancel_join_server(manager);
    manager->join_phase_ = kJoinStateRefused;
    return net::kResultMsgFreed;
}

} // namespace online
