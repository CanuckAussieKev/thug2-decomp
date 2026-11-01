// The network game's join (online::Hub): the clients and the server made, the connection request and the join
// request both ways, ok_to_join, join proceed / refused, GameSpy's CD-key answer. See gamenet_manager.h.
//
// NOT ORIGINAL in it: the THUGPRO peer mode (thugpro_join.h) - our requests carry THUGPRO's trailer and machine id,
// THUGPRO's refusal reasons are shown in words, and as host the GameSpy CD-key check is left out; bounds on what the
// host reads from a request.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/net/gamespy.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/single_instance.h"
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
#include "engine/net/net_server.h"
#include "engine/net/player_info.h"
#include "engine/net/server_list.h"
#include "engine/net/stats_man.h"
#include "engine/net/thugpro_join.h"
#include "engine/net/thugpro_peer.h"
#include "engine/util/byte_stream.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "engine/sys/game_config.h"
#include "engine/sys/mlp.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
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

THUG2_MSVC_WARNING(disable : 4996) // (strcpy / strncpy: the original's own copies into its fixed fields)

namespace online {

char g_pending_password[kPasswordSize]; // (winmain.cpp: -PASSWORD when joining)

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
JoinRequestBlock g_join_scratch;

struct AuthInstance {
    AppHost* app;
    Hub* manager;
    Conn* conn;
};

char g_join_password[kPasswordSize];

// The global passwords: the one being typed (12) and the one in use (10; resent with every request).
char* pending_password() { return g_pending_password; }
char* join_password() { return g_join_password; }

net::Hub* net_session_slot() { return net::Hub::sole(); }
tasks::PileUp* get_logic_tasks() { return (&assets::g_main_loop_manager->logic_tasks_); }
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
uint32_t active_ruleset_label() { return goals::g_game_mode->get_name_key(); }
void set_maximum_players(int count) { goals::g_game_mode->set_rider_ceiling(count); }
int8_t current_rank(Hub* m) { return static_cast<int8_t>(m->rating_service_->get_rank()); }
int32_t get_rating(Hub* m)
{
    // (the original reads the stats' first word, the rating, through the folded getter 0x00469490 -
    // single::PointerMember::get_pull)
    return m->rating_service_->get_personal_stats()->skill_points_;
}
bool match_underway(Hub* m) { return active_ruleset_label() != kNetLobby && !m->match_ended_; }

prefs::Preferences* net_game_options(Hub* m) { return reinterpret_cast<prefs::Preferences*>(m->net_options_); }
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
char* lobby_server_name(Hub* m) { return m->lobby_service_->server_name_; }
char* lobby_peer_name(Hub* m) { return m->lobby_service_->room_name_; }

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

void get_sprintf(char* out, const char* format, uint32_t a, uint32_t b, int c, const char* d, const char* e)
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
        util::ByteWriter writer(request->bytes, sizeof(request->bytes));
        write_connect_info(writer, info);
        desc.byte_count_ = static_cast<uint16_t>(writer.position());
    }
    desc.content_ = request->bytes;
    desc.ident_ = kMsgConnectionRequest;
    client->send_to_host(&desc);
}

net::Guest* as_client(AppHost* app) { return static_cast<net::Guest*>(app); }

// The handlers a joining client adds (0x00534480): on every client, then on client 0 only.
struct HandlerEntry {
    uint8_t id;
    uint32_t code; // (an address: a game handler, or one of ours - Responder() maps those)
    uint32_t flags;
    bool manager_data_;
    int priority;
};
constexpr uint32_t kOurs = 0x80000000; // (+ an index into our_handler)
HandlerSlotValue our_handler(uint32_t code)
{
    switch (code) {
    case kOurs + 0:
        return &Hub::on_turned_away_msg;
    case kOurs + 1:
        return &Hub::on_go_ahead_msg;
    case kOurs + 2:
        return &online::on_chat_msg;
    case kOurs + 3:
        return &Hub::on_link_established;
    case kOurs + 4:
        return &Hub::on_admission_ask_msg;
    default:
        return reinterpret_cast<HandlerSlotValue>(code);
    }
}
const HandlerEntry kClientHandlers[] = {
    {0x42, reinterpret_cast<uint32_t>(code_address(online::handle_ready_to_play)), kRunWhenLate, true,
     kMidPriority}, // proceed to play
    {0x4D, reinterpret_cast<uint32_t>(code_address(online::on_spectate_msg)), kRunWhenLate, true,
     kMidPriority}, // observe proceed
    {0x4E, reinterpret_cast<uint32_t>(code_address(online::handle_observers_full)), kRunWhenLate, true,
     kMidPriority}, // observe refused
    {kMsgJoinRefused, kOurs + 0, kRunWhenLate, true, kMidPriority},
    {kMsgJoinProceed, kOurs + 1, kRunWhenLate, true, kMidPriority},
    {0x25, reinterpret_cast<uint32_t>(code_address(online::on_ready_ask_msg)), kRunWhenLate, true,
     kMidPriority}, // ready query
    {0x26, reinterpret_cast<uint32_t>(code_address(online::on_ready_reply_msg)), kRunWhenLate, true,
     kMidPriority}, // ready response
    {0x53, reinterpret_cast<uint32_t>(code_address(online::on_admitted_msg)), kRunWhenLate, true,
     kMidPriority}, // player info ack request
    {0x58, reinterpret_cast<uint32_t>(code_address(online::handle_crown_spawn)), 0, true,
     kMidPriority}, // the king dropped the crown
    {0x75, reinterpret_cast<uint32_t>(code_address(online::handle_flag_returned)), 0, true,
     kMidPriority}, // a player dropped a flag
    {0x3F, reinterpret_cast<uint32_t>(code_address(online::on_hud_notice_msg)), kRunWhenLate, true,
     kMidPriority}, // panel message
    {0x5E, reinterpret_cast<uint32_t>(code_address(online::handle_auto_server_notice)), kRunWhenLate, true,
     kMidPriority}, // auto server notification
    {0x5F, reinterpret_cast<uint32_t>(code_address(online::handle_fcfs_notice)), kRunWhenLate, true,
     kMidPriority}, // first-come-first-served assignment
    {0x68, reinterpret_cast<uint32_t>(code_address(online::handle_joined_team)), kRunWhenLate, true,
     kMidPriority}, // team change
    {0x69, reinterpret_cast<uint32_t>(code_address(online::handle_choose_team)), kRunWhenLate, true,
     kMidPriority},                                       // number of teams
    {0x4C, kOurs + 2, kRunWhenLate, false, kMidPriority}, // chat
    {0x7C, reinterpret_cast<uint32_t>(code_address(online::handle_cd_key_challenge)), kRunWhenLate, true,
     kMidPriority}, // challenge
};
const HandlerEntry kGameClientHandlers[] = {
    {0x6D, reinterpret_cast<uint32_t>(code_address(online::handle_selected_goals)), 0, true,
     kMidPriority}, // selected goals
    {0x34, reinterpret_cast<uint32_t>(code_address(online::handle_start_game)), 0, true, kMidPriority}, // game info
    {0x7A, reinterpret_cast<uint32_t>(code_address(online::handle_kill_team_flags)), 0, true,
     kMidPriority}, // kill the team flags
    {0x6E, reinterpret_cast<uint32_t>(code_address(online::handle_goal_completed)), 0, true,
     kMidPriority}, // a goal beaten (relayed)
    {0x6F, reinterpret_cast<uint32_t>(code_address(online::handle_goal_started)), 0, true,
     kMidPriority}, // a goal started (relayed)
    {0x70, reinterpret_cast<uint32_t>(code_address(online::handle_nothing)), 0, true,
     kMidPriority}, // toggle pro set (nothing)
    {0x46, reinterpret_cast<uint32_t>(code_address(online::on_level_switch_msg)), 0, true, kTopRank}, // change level
    {0x46, reinterpret_cast<uint32_t>(code_address(online::handle_nothing)), 0, true, kTopRank},      // (and nothing)
    {0x6C, reinterpret_cast<uint32_t>(code_address(online::handle_park_map)), 0, true, kTopRank},     // level data
    {0x88, reinterpret_cast<uint32_t>(code_address(online::handle_rails_data)), kRunOnBadCrc, true,
     kTopRank}, // rail data
    {0x87, reinterpret_cast<uint32_t>(code_address(online::on_goal_sync_msg)), kRunOnBadCrc, true,
     kTopRank}, // goals data
    {0x80, reinterpret_cast<uint32_t>(code_address(online::handle_checksum_request)), 0, true,
     kMidPriority}, // cheat checksum request
};
const HandlerEntry kRemoteGameClientHandlers[] = {
    {0x22, reinterpret_cast<uint32_t>(code_address(online::on_player_added_msg)), 0, true,
     kMidPriority}, // player create
    {0x79, reinterpret_cast<uint32_t>(code_address(online::handle_flush_important)), 0, true,
     kMidPriority}, // join accepted
};
const HandlerEntry kGameClientHandlers2[] = {
    {0x7B, reinterpret_cast<uint32_t>(code_address(online::handle_keep_alive)), 0, true,
     kMidPriority}, // wait n seconds
    {0x55, reinterpret_cast<uint32_t>(code_address(online::on_crown_change_msg)), 0, true, kMidPriority}, // new king
    {0x72, reinterpret_cast<uint32_t>(code_address(online::on_flag_scored_msg)), 0, true,
     kMidPriority}, // captured flag
    {0x74, reinterpret_cast<uint32_t>(code_address(online::on_flag_swiped_msg)), 0, true, kMidPriority},  // stole flag
    {0x71, reinterpret_cast<uint32_t>(code_address(online::on_flag_grabbed_msg)), 0, true, kMidPriority}, // took flag
    {0x73, reinterpret_cast<uint32_t>(code_address(online::on_flag_returned_msg)), 0, true,
     kMidPriority},                                                                                    // retrieved flag
    {0x5A, reinterpret_cast<uint32_t>(code_address(online::on_match_end_msg)), 0, true, kMidPriority}, // game over
    {0x6A, reinterpret_cast<uint32_t>(code_address(online::handle_game_ended)), 0, true, kMidPriority}, // end game
    {0x23, reinterpret_cast<uint32_t>(code_address(online::handle_object_update_length)), kRunForForeign | kRunWhenLate,
     true, kTopRank}, // object updates (their length)
    {0x8D, reinterpret_cast<uint32_t>(code_address(online::handle_eliminated)), 0, true,
     kMidPriority}, // a skater eliminated
    {0x8E, reinterpret_cast<uint32_t>(code_address(online::handle_eliminated)), 0, true, kMidPriority},
};
// The server's lobby handlers (0x0053C380; taken out once the game starts).
const HandlerEntry kServerHandlers[] = {
    {kMsgConnectionRequest, kOurs + 3, kRunForForeign | kRunWhenLate, true, kBottomRank},
    {0x90, reinterpret_cast<uint32_t>(code_address(online::handle_team_object)), 0, true,
     kTopRank}, // a scavenger-hunt object picked up
    {kMsgJoinRequest, kOurs + 4, kRunWhenLate, true, kMidPriority},
    {0x11, reinterpret_cast<uint32_t>(code_address(online::on_server_query_msg)), kRunForForeign | kRunWhenLate, true,
     kMidPriority}, // find server
    {0x0B, reinterpret_cast<uint32_t>(code_address(online::on_peer_left_msg)), kRunWhenLate, true,
     kMidPriority}, // disconnection request
    {0x4A, reinterpret_cast<uint32_t>(code_address(online::handle_observe_request)), kRunWhenLate, true,
     kMidPriority}, // observe
    {0x26, reinterpret_cast<uint32_t>(code_address(online::on_ready_reply_msg)), kRunWhenLate, true,
     kMidPriority}, // ready response
    {0x54, reinterpret_cast<uint32_t>(code_address(online::handle_send_game_state)), 0, true,
     kMidPriority}, // player info ack
    {0x57, reinterpret_cast<uint32_t>(code_address(online::handle_drop_carried)), 0, true,
     kMidPriority}, // moved to a restart
    {0x59, reinterpret_cast<uint32_t>(code_address(online::handle_game_ended_report)), 0, true,
     kMidPriority}, // the run ended
    {0x60, reinterpret_cast<uint32_t>(code_address(online::handle_host_options)), kRunWhenLate, true,
     kMidPriority}, // first-come-first-served requests: start game,
    {0x61, reinterpret_cast<uint32_t>(code_address(online::handle_host_options)), kRunWhenLate, true,
     kMidPriority}, // ban player, change level, toggle pro set, goal
    {0x62, reinterpret_cast<uint32_t>(code_address(online::handle_host_options)), kRunWhenLate, true,
     kMidPriority}, // selection, end game, number of teams
    {0x63, reinterpret_cast<uint32_t>(code_address(online::handle_host_options)), kRunWhenLate, true, kMidPriority},
    {0x64, reinterpret_cast<uint32_t>(code_address(online::handle_host_options)), kRunWhenLate, true, kMidPriority},
    {0x65, reinterpret_cast<uint32_t>(code_address(online::handle_host_options)), kRunWhenLate, true, kMidPriority},
    {0x66, reinterpret_cast<uint32_t>(code_address(online::handle_host_options)), kRunWhenLate, true, kMidPriority},
    {0x67, reinterpret_cast<uint32_t>(code_address(online::on_switch_squad_msg)), kRunWhenLate, true,
     kMidPriority}, // team change request
    {0x6B, reinterpret_cast<uint32_t>(code_address(online::handle_request_game_data)), kRunWhenLate, true,
     kMidPriority}, // level request
    {0x6E, reinterpret_cast<uint32_t>(code_address(online::handle_goal_completed_report)), kRunWhenLate, true,
     kMidPriority}, // a goal beaten
    {0x6F, reinterpret_cast<uint32_t>(code_address(online::handle_goal_started_report)), kRunWhenLate, true,
     kMidPriority},                                                                                 // a goal started
    {0x6C, reinterpret_cast<uint32_t>(code_address(online::handle_park_map)), 0, true, kTopRank},   // level data
    {0x88, reinterpret_cast<uint32_t>(code_address(online::handle_rails_data)), 0, true, kTopRank}, // rail data
    {0x7D, reinterpret_cast<uint32_t>(code_address(online::handle_cd_key_answer)), 0, true,
     kMidPriority}, // challenge response
    {0x81, reinterpret_cast<uint32_t>(code_address(online::handle_checksum_response)), 0, true,
     kMidPriority}, // cheat checksum response
    {0x8B, reinterpret_cast<uint32_t>(code_address(online::handle_created_data_request)), 0, true,
     kMidPriority}, // rails / goals data requests
    {0x8A, reinterpret_cast<uint32_t>(code_address(online::handle_created_data_request)), 0, true, kMidPriority},
};

template <size_t N>
void add_handlers(AppHost* app, Hub* manager, const HandlerEntry (&entries)[N])
{
    for (const HandlerEntry& entry : entries)
        app->dispatcher_.append_listener(entry.id, our_handler(entry.code), entry.flags,
                                         entry.manager_data_ ? manager : nullptr, entry.priority);
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
        int limit = rank_limit(this);
        int test = rank_test(this);
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
        if (*password && std::strcmp(password, msg->password) != 0) {
            reason = kRefusedPassword;
            return false;
        }
    }
    // NOT ORIGINAL: no CD-key check in the THUGPRO peer mode (TP-R12) - THUGPRO's hosts skip it (its patch over this
    // test) and its players' keys are made up. (The response ends inside its field here: the original's strlen could
    // read past a message without one.)
    if (check_cd_key && !thugpro::peer_mode() &&
        strnlen(msg->cd_key_response_, sizeof(msg->cd_key_response_)) != kCdKeyResponseLength) {
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
    ConnectInfo info;
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
    if (std::strlen(pending_password()) != 0) {
        std::strcpy(info.password, pending_password());
        std::strcpy(join_password(), pending_password());
    } else {
        std::memset(join_password(), 0, kPasswordSize);
        info.password[0] = '\0';
    }
    if (!client->is_host_local())
        std::memset(pending_password(), 0, kPasswordSize);
    info.can_queue_up_ = false;
    info.broadband = net->get_net_link_kind() != kConnectionTypeModem;
    thugpro::ConnectRequest request;
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
    ConnectInfo info;
    std::memset(&info, 0, sizeof(info));
    info.observer = join_mode_ == kJoinModeObserve;
    info.version = kVersion;
    info.can_queue_up_ = true;
    info.password[0] = '\0';
    info.broadband = net_session_slot()->get_net_link_kind() != kConnectionTypeModem;
    if (in_internet_mode())
        info.rank = current_rank(this);
    thugpro::ConnectRequest request;
    send_connection_request(as_client(client), info, &request);
    client->transmit(false);
    join_began_at_ = milli_seconds();
    get_logic_tasks()->queue_job(join_expiry_job_.get());
    join_phase_ = kJoinStateJoining;
}

// 0x00534BD0
void Hub::retry_entry_with_key(const char* password)
{
    std::strcpy(join_password(), password);
    const char* name = preference_text(this, kNetworkId); // (not used)
    (void)name;
    ConnectInfo info;
    std::memset(&info, 0, sizeof(info));
    info.rank = current_rank(this);
    info.observer = join_mode_ == kJoinModeObserve;
    info.version = kVersion;
    info.can_queue_up_ = true;
    info.broadband = net_session_slot()->get_net_link_kind() != kConnectionTypeModem;
    std::strcpy(info.password, password);
    thugpro::ConnectRequest request;
    send_connection_request(as_client(client_app_), info, &request);
    join_began_at_ = milli_seconds();
    get_logic_tasks()->queue_job(join_expiry_job_.get());
    join_phase_ = kJoinStateJoiningWithPassword;
}

const char kUiStringText[10]{"ui_string"};
const char kNetReasonGeneral[19]{"net_reason_general"};
const char kPresenceLocationFormat[27]{"10thug211:%d:%d:%d:%s (%s)"};
const char kMatchAppName[13]{"Match Skate6"};
const char kSkateAppName[7]{"Skate6"};

// 0x00537220
AppHost* Hub::start_client_session(bool broadcast, bool local, bool secure, int index)
{
    net::Hub* net = net_session_slot();
    int flags = broadcast ? kAppBroadcast : 0;
    flags |= local ? kAppLocal : kAppDynamicResend | kAppForeign;
    if (secure)
        flags |= kAppSecure;
    AppHost* client = net->make_client_app(index, kSkateAppName, kJoinPort, // (its name)
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
    AppHost* client = net->make_client_app(0, kMatchAppName, kJoinPort, static_cast<int>(current_local_address()),
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
        net->make_server_app(0, kSkateAppName, kPeerLimit, kHostPort, static_cast<int>(current_local_address()), flags);
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
        ProceedInfo proceed;
        std::memset(&proceed, 0, sizeof(proceed));
        proceed.max_players_ = static_cast<int8_t>(get_max_players(manager));
        uint8_t proceed_bytes[kProceedInfoSize];
        util::ByteWriter writer(proceed_bytes, sizeof(proceed_bytes));
        write_proceed_info(writer, proceed);
        desc.content_ = proceed_bytes;
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
    ConnectInfo request;
    if (!read_connect_info(reader, &request))
        return net::kResultConsumed;
    ConnectInfo* msg = &request;
    thugpro::ConnectTrailer trailer;
    if (thugpro::peer_mode() && thugpro::read_connect_trailer(context->body_, context->body_size_, &trailer))
        get_log("THUGPRO join (TP-R12): a THUGPRO client asks to join: version %08X, release %u, scripts %08X%s, "
                "machine %08X",
                trailer.version, trailer.release, trailer.scripts_hash_, trailer.modified_scripts_ ? " (modified)" : "",
                trailer.machine_id_);
    int reason = 0;
    if (!manager->joins_accepted(reason, msg, conn, true, true, true)) {
        char refused[32] = {}; // (10 sent: the reason, and the challenge for kRefusedCdKey)
        refused[0] = static_cast<char>(reason);
        if (reason == kRefusedCdKey)
            std::strcpy(refused + 1, manager->challenge_);
        send_refused(context->receiver_app_, conn, refused);
        return net::kResultStop;
    }
    Conn* joined =
        context->receiver_app_->opened_link(static_cast<int>(conn->remote_ip_), conn->port_no_, Conn::kFlagRemote);
    context->sender_conn_ = joined;
    joined->set_link_speed_class(msg->broadband ? Conn::kBandwidthBroadband : Conn::kBandwidthModem);
    joined->set_transmit_period(static_cast<uint32_t>(1000 / game_config::g_frame_rate * 2)); // two frames
    // NOT ORIGINAL: in the THUGPRO peer mode no GameSpy CD-key check (TP-R12): proceed as on a LAN.
    if (manager->in_internet_mode() && !thugpro::peer_mode()) {
        auto* instance = static_cast<AuthInstance*>(memory::alloc(sizeof(AuthInstance)));
        if (instance)
            std::memset(instance, 0, sizeof(*instance));
        instance->manager = manager;
        instance->app = context->receiver_app_;
        instance->conn = joined;
        authenticate_user(static_cast<int>(joined->ident_), context->sender_conn_->remote_ip_, manager->challenge_,
                          msg->cd_key_response_, instance);
        return net::kResultPassOn;
    }
    ProceedInfo proceed;
    std::memset(&proceed, 0, sizeof(proceed)); // (the original leaves the addresses as its stack had them on a LAN)
    proceed.max_players_ = static_cast<int8_t>(get_max_players(manager));
    proceed.broadband = net->get_net_link_kind() != kConnectionTypeModem;
    if (manager->in_internet_mode()) {
        proceed.port = manager->join_port_;
        proceed.private_ip_ = manager->join_private_ip_;
        proceed.public_ip_ = manager->join_ip_;
    }
    std::strncpy(proceed.server_name_, preference_text(manager, kServerName), kServerNameSize - 1);
    proceed.server_name_[kServerNameSize - 1] = '\0';
    uint8_t proceed_bytes[kProceedInfoSize];
    util::ByteWriter writer(proceed_bytes, sizeof(proceed_bytes));
    write_proceed_info(writer, proceed);
    context->receiver_app_->deliver_to(kMsgJoinProceed, sizeof(proceed_bytes), proceed_bytes, joined->remote_ip_,
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
        char refused[kJoinRefusedSize] = {};
        refused[0] = kRefusedCdKey;
        send_refused(instance->app, instance->conn, refused);
        instance->conn->mark_stale();
    } else {
        Hub* manager = instance->manager;
        ProceedInfo proceed;
        std::memset(&proceed, 0, sizeof(proceed));
        proceed.max_players_ = static_cast<int8_t>(get_max_players(manager));
        proceed.broadband = true;
        if (manager->in_internet_mode()) {
            proceed.port = manager->join_port_;
            proceed.private_ip_ = manager->join_private_ip_;
            proceed.public_ip_ = manager->join_ip_;
        }
        std::strncpy(proceed.server_name_, preference_text(manager, kServerName), kServerNameSize - 1);
        proceed.server_name_[kServerNameSize - 1] = '\0';
        uint8_t proceed_bytes[kProceedInfoSize];
        util::ByteWriter writer(proceed_bytes, sizeof(proceed_bytes));
        write_proceed_info(writer, proceed);
        instance->app->deliver_to(kMsgJoinProceed, sizeof(proceed_bytes), proceed_bytes, instance->conn->remote_ip_,
                                  instance->conn->port_no_, 0);
    }
    memory::Hub::get_pull()->dealloc(instance);
}

// 0x0052FA10
int Hub::on_admission_ask_msg(InboundMsgCtx* context)
{
    Conn* conn = context->sender_conn_;
    auto* manager = static_cast<Hub*>(context->content_);
    void* skate = reinterpret_cast<void*>(modules::g_skate_module);
    context->acknowledge_ = true;
    // NOT ORIGINAL: a request shorter than THUG2's header isn't read (the original reads past it).
    util::ByteReader reader(context->body_, context->body_size_);
    JoinRequestHeader request;
    if (!read_join_request(reader, &request))
        return net::kResultConsumed;
    JoinRequestHeader* msg = &request;
    int reason = 0;
    if (conn->is_off_machine() && !manager->joins_accepted(reason, msg, conn, false, false, false)) {
        NetMessageInfo desc = new_desc();
        char refused[kJoinRefusedSize] = {};
        refused[0] = static_cast<char>(reason);
        desc.content_ = refused;
        desc.byte_count_ = kJoinRefusedSize;
        desc.ident_ = kMsgJoinRefused;
        context->receiver_app_->put_on_outbox(conn->hdl_, &desc);
        context->sender_conn_->mark_stale();
        return net::kResultStop;
    }
    void* memory = memory::alloc_zeroed(kPlayerInfoSize);
    auto* player = memory ? new (memory) JoinerRecord : nullptr;
    std::strncpy(player->label_, msg->name, kNameSize - 1);
    player->label_[kNameSize - 1] = '\0';
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
        if (!memory::g_manager_ready)
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
            char key[0x80];
            char response[0x80];
            get_cd_key(key);
            compute_cd_key_response(key, manager->challenge_, response);
            authenticate_user(0, context->sender_conn_->remote_ip_, manager->challenge_, response, nullptr);
        }
        return net::kResultPassOn;
    }
    if (!msg->observer) {
        // NOT ORIGINAL: the appearance read from a copy as long as the most a request holds (a request cut short
        // reads zeros, not past the message).
        uint8_t appearance[kAppearanceSize];
        size_t size = reader.remaining();
        if (size > kAppearanceSize)
            size = kAppearanceSize;
        std::memset(appearance, 0, kAppearanceSize);
        reader.bytes(appearance, size);
        player->account_->decode_from_bytes(appearance, sizeof(appearance));
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
    ProceedInfo answer;
    if (!read_proceed_info(reader, &answer))
        return net::kResultConsumed;
    ProceedInfo* proceed = &answer;
    if (conn->is_off_machine()) {
        conn->set_link_speed_class(proceed->broadband ? Conn::kBandwidthBroadband : Conn::kBandwidthModem);
        manager->join_ip_ = proceed->public_ip_;
        manager->join_private_ip_ = proceed->private_ip_;
        manager->join_port_ = proceed->port;
        if (presence_logged_in(manager)) {
            std::strcpy(lobby_server_name(manager), proceed->server_name_);
            char location[0x1000];
            get_sprintf(location, kPresenceLocationFormat, manager->join_ip_, manager->join_private_ip_,
                        manager->join_port_, lobby_server_name(manager), lobby_peer_name(manager));
            const char* status = script::fetch_text(manager->join_mode_ == 0 ? kStatusPlaying : kStatusObserving);
            set_presence(manager, 2, status, location);
        }
    }
    set_maximum_players(proceed->max_players_);
    const char* name = preference_text(manager, kNetworkId);
    JoinRequestBlock* info = &g_join_scratch; // (the original's is on its stack: 4 KB)
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
    std::strncpy(info->name, name, kNameSize - 1);
    info->name[kNameSize - 1] = '\0';
    std::strcpy(info->password, join_password());
    int size = 0;
    if (!info->observer) {
        auto* profile = modules::g_skate_module->fetch_account(0);
        net::ConnSearch search;
        if (context->receiver_app_->begin_conn_walk(&search)->get_link_speed_class() == Conn::kBandwidthModem)
            narrowband = true; // (a narrowband server gets no face)
        size = profile->emit_into_buffer(info->appearance, kAppearanceSize, narrowband);
    }
    static uint8_t request_bytes[kJoinInfoHeaderSize + kAppearanceSize]; // (the message is copied when queued)
    util::ByteWriter writer(request_bytes, sizeof(request_bytes));
    write_join_request(writer, *info);
    writer.bytes(info->appearance, static_cast<size_t>(size));
    desc.content_ = request_bytes;
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
    const char* msg = context->body_;
    int reason = static_cast<int8_t>(msg[0]);
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
            manager->refused_reason_ = checksums::hash_from_text(kNetReasonGeneral);
            break;
        }
        // The host's challenge (after the reason): answered with our key, in a new connection request.
        char key[0x80];
        char response[0x80];
        // NOT ORIGINAL: the challenge read from the message's own 9 bytes (NUL-ended here).
        char challenge[kJoinRefusedSize] = {};
        uint32_t challenge_size = context->body_size_ > 1 ? context->body_size_ - 1 : 0;
        std::memcpy(challenge, msg + 1,
                    challenge_size < kJoinRefusedSize - 1u ? challenge_size : kJoinRefusedSize - 1u);
        get_cd_key(key);
        compute_cd_key_response(key, challenge, response);
        net::Hub* net = net_session_slot();
        ConnectInfo info;
        std::memset(&info, 0, sizeof(info));
        std::strcpy(info.cd_key_response_, response);
        const char* name = nullptr; // (not used)
        net_game_options(manager)->fetch_setting(kNetworkId)->fetch_text(kUiStringText, &name, 1);
        info.rank = current_rank(manager);
        info.observer = manager->join_mode_ == kJoinModeObserve;
        info.version = kVersion;
        info.can_queue_up_ = true;
        info.broadband = net->get_net_link_kind() != kConnectionTypeModem;
        std::strcpy(info.password, join_password());
        thugpro::ConnectRequest request;
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
        if (uint32_t text = thugpro::refused_reason_text(reason))
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
