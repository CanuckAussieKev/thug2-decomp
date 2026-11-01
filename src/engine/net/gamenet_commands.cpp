// The network manager's script commands, first part (0x00535DA0-0x00536460): capture the flag's task, the game's
// state, the join mode, teams, the lobby's peer, the message of the day, and the first-come-first-served requests a
// client sends the server (start the game, change the level, toggle a pro set flag or a goal).

#include <cstdint>
#include <array>
#include "platform/code_address.h"

#include "engine/core/arena_alloc.h"
#include "engine/net/gamespy.h"
#include "engine/core/task_list.h"
#include "game/park_editing/park_editor.h"
#include "engine/net/gamenet_commands.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "engine/assets/resource_registry.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "engine/sys/mlp.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/down_cast.h"

namespace online {

using namespace detail;
using script::Script;
using script::Struct;

uint32_t g_net_game_start_time;

namespace {

// Messages to the server.
constexpr uint8_t kMsgBeginMatchAsk = 0x60;
constexpr uint8_t kMsgChangeLevelRequest = 0x62;
constexpr uint8_t kMsgToggleProSetRequest = 0x63;
constexpr uint8_t kMsgToggleGoalRequest = 0x64;
constexpr uint8_t kMsgParkMap = 0x6C;
constexpr uint8_t kMsgRailsData = 0x88;
constexpr uint16_t kParkMapSize = 15000;
constexpr uint16_t kRailsDataSize = 4402;

// Checksums.
constexpr uint32_t kChecksum = 0x21902065;               // checksum
constexpr uint32_t kBooleanTrue = 0xF81BC89B;            // boolean_true
constexpr uint32_t kDisplayFlagBaseWarning = 0x55AF7C02; // display_flag_base_warning
constexpr uint32_t kGameType = 0x9834C4FA;               // game_type
constexpr uint32_t kSkillLevel = 0x403DA0D0;             // skill_level
constexpr uint32_t kFireballDifficulty = 0xCE238F5D;     // fireball_difficulty
constexpr uint32_t kTimeLimit = 0xC51BF6E3;              // time_limit
constexpr uint32_t kTargetScore = 0x71CD53A2;            // target_score
constexpr uint32_t kPlayerCollision = 0x043EE978;        // player_collision
constexpr uint32_t kFriendlyFire = 0xE959C43A;           // friendly_fire
constexpr uint32_t kStopAtZero = 0x48E748B5;             // stop_at_zero
constexpr uint32_t kScoreReset = 0x162E8C3A;             // score_reset
constexpr uint32_t kCtfGameType = 0x15421B73;            // ctf_game_type
constexpr uint32_t kGoals = 0x38DBE1D0;                  // goals
constexpr uint32_t kTeamGraffitiMode = 0xA8774BF7;       // team_graffiti_mode
constexpr uint32_t kLevel = 0x651533EC;                  // level
constexpr uint32_t kCreatedPark = 0xB664035D;            // load_sk5ed_gameplay
constexpr uint32_t kName = 0xA1DC81F9;                   // name
constexpr uint32_t kFlag = 0x2E0B1465;                   // flag
constexpr uint32_t kSetFlag = 0x1BC80657;                // SetFlag
constexpr uint32_t kUnSetFlag = 0x1FF8F994;              // UnSetFlag
constexpr uint32_t kSignedDisclaimer = 0x633A324E;       // signed_disclaimer

const char* const kNumTeams = "num_teams";                  // "num_teams"
const char* const kParkMapDesc = "level data";              // "level data"
const char* const kRailsDesc = "rail data";                 // "rail data"
const char* const kBit = "bit";                             // "bit"
const char* const kParamId = "param_id";                    // "param_id"
const char* const kFirstProSetFlag = "FLAG_PROSET1_GEO_ON"; // "FLAG_PROSET1_GEO_ON"

constexpr int kProSetCount = 7;
constexpr uint32_t kFlagBaseWarningInterval = 3000; // ms
constexpr uint32_t kJustStartedTime = 5000;         // ms

uint32_t g_last_flag_base_warning;

uint32_t& last_flag_base_warning() { return g_last_flag_base_warning; }
uint32_t& net_game_start_time() { return g_net_game_start_time; }

tasks::PileUp* get_logic_tasks() { return (&assets::g_main_loop_manager->logic_tasks_); }
Hub* net_link() { return g_online_hub; }

// The lobby (online's GameSpy peer side): its peer, whether it's in a group room; the lobby's state (the manager's
// set_service_state, 0x005453E0) and services state (set_next_service_state, 0x005451B0).
void* lobby_peer(Hub* manager) { return manager->lobby_service_->get_lobby_handle(); }
bool in_group_room(Hub* manager) { return manager->lobby_service_->in_group_room(); }
void set_lobby_state(Hub* manager, int state) { manager->set_service_state(state); }
void set_services_state(Hub* manager, int state) { manager->set_next_service_state(state); }

// A message for the server through client 0, sequenced in the game's group.
void send_request(Hub* manager, uint8_t id, void* data, uint16_t length)
{
    net::NetMessageInfo desc;
    desc.ident_ = id;
    desc.content_ = data;
    desc.byte_count_ = length;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    send_to_server(manager->client_app_, &desc);
}

} // namespace

// 0x00535DA0: StartCTFGame - capture the flag's task into the logic tasks.
bool cfunc_start_ctf_game(Struct*, Script*)
{
    get_logic_tasks()->queue_job(net_link()->flag_carry_job_.get());
    return true;
}

// 0x00535DC0: EndCTFGame - its task removed.
bool cfunc_end_ctf_game(Struct*, Script*)
{
    net_link()->flag_carry_job_->drop_out();
    return true;
}

// 0x00535DE0: DisplayFlagBaseWarning - display_flag_base_warning, at most every 3 seconds.
bool cfunc_display_flag_base_warning(Struct*, Script*)
{
    if (frame_clock::get_time() - last_flag_base_warning() > kFlagBaseWarningInterval) {
        get_run(kDisplayFlagBaseWarning);
        last_flag_base_warning() = frame_clock::get_time();
    }
    return true;
}

// 0x00535E20: NetworkGamePending - a start request is waiting for the server.
bool cfunc_network_game_pending(Struct*, Script*) { return net_link()->match_being_set_up_; }

// 0x00535E30: MarkGameOver.
bool cfunc_mark_game_over(Struct*, Script*)
{
    net_link()->match_ended_ = true;
    return true;
}

// 0x00535E40: SetJoinMode <n> (an unnamed integer: 0 to play, anything else to watch).
bool cfunc_set_join_mode(Struct* params, Script*)
{
    Hub* manager = net_link();
    int mode = 0;
    params->fetch_int_value(0u, &mode, 0);
    manager->set_join_mode(mode == 0 ? 0 : 1);
    return true;
}

// 0x00535E90: GetNumTeams - the game mode's team count into the script's num_teams.
bool cfunc_get_num_teams(Struct*, Script* script)
{
    script->args_->put_int(kNumTeams, get_game_mode()->current_num_teams());
    return true;
}

// 0x00535EB0: ConnectedToPeer - the lobby has a GameSpy peer, connected (peerIsConnected).
bool cfunc_connected_to_peer(Struct*, Script*)
{
    void* peer = lobby_peer(net_link());
    return peer && game_spy::peer_is_connected(peer);
}

// 0x00535EE0: InGroupRoom - the lobby's flag (+0x134).
bool cfunc_in_group_room(Struct*, Script*) { return in_group_room(net_link()); }

// 0x00535EF0: IsAutoServing - the server of a network game in host mode 1.
bool cfunc_is_auto_serving(Struct*, Script*)
{
    Hub* manager = net_link();
    if (!manager->in_net_game() || !(manager->flag_bits_ & 1))
        return false;
    return manager->host_mode_ == 1;
}

// 0x00535F20: ChangeLevelPending - the level change's task is in a list.
bool cfunc_change_level_pending(Struct*, Script*) { return net_link()->level_change_job_->is_on_task_list(); }

// 0x00535F30: CancelMotdDownload.
bool cfunc_cancel_motd_download(Struct*, Script*)
{
    net_link()->download_done_ = true;
    return true;
}

// 0x00535F40: DownloadMotd - the lobby's state 6.
bool cfunc_download_motd(Struct*, Script*)
{
    set_lobby_state(net_link(), 6);
    return true;
}

// 0x00535F50: ServicesAreAvailable - with the services up (+0xFE0A = 1): true (if the message of the day hasn't come
// yet, services state 0 and lobby state 1 first); else services state 3, lobby state 1, false.
bool cfunc_services_are_available(Struct*, Script*)
{
    Hub* manager = net_link();
    if (manager->services_available_ == 1) {
        if (!manager->motd_received_) {
            set_services_state(manager, 0);
            set_lobby_state(manager, 1);
        }
        return true;
    }
    set_services_state(manager, 3);
    set_lobby_state(manager, 1);
    return false;
}

// 0x00535FA0: AlreadyGotMotd.
bool cfunc_already_got_motd(Struct*, Script*) { return net_link()->motd_received_; }

// 0x00535FB0: JoinServerComplete - ready to play.
bool cfunc_join_server_complete(Struct*, Script*) { return net_link()->cleared_to_start_; }

// 0x00535FC0: JustStartedNetGame - within 5 seconds of the network game's start (0x007CCDF4).
bool cfunc_just_started_net_game(Struct*, Script*)
{
    return frame_clock::get_time() - net_game_start_time() < kJustStartedTime;
}

// 0x00535FE0: FCFSRequestStartGame - the game's options from the network preferences to the server (message 0x60);
// the game is then pending.
bool cfunc_fcfs_request_start_game(Struct*, Script*)
{
    StartRequest request; // (gamenet_messages.h; +0x18 is never set: the original's stack - zero here)
    Hub* manager = net_link();
    prefs::Preferences* preferences = manager->get_online_settings();
    request.skill_level = preferences->get_preference_checksum(kSkillLevel, kChecksum);
    request.fireball_level = preferences->get_preference_checksum(kFireballDifficulty, kChecksum);
    uint32_t game_type = 0;
    manager->get_game_preferences()->fetch_setting(kGameType)->read_hash_value(kChecksum, &game_type, 1);
    request.game_type = game_type;
    request.time_limit = preferences->get_preference_checksum(kTimeLimit, kChecksum);
    request.target_score = preferences->get_preference_checksum(kTargetScore, kChecksum);
    request.player_collision = preferences->get_preference_checksum(kPlayerCollision, kChecksum);
    request.friendly_fire = preferences->get_preference_checksum(kFriendlyFire, kChecksum);
    request.stop_at_zero = preferences->get_preference_checksum(kStopAtZero, kChecksum);
    request.score_reset = preferences->get_preference_checksum(kScoreReset, kChecksum);
    request.ctf_game_type = preferences->get_preference_checksum(kCtfGameType, kChecksum);
    request.goals = preferences->get_preference_checksum(kGoals, kChecksum);
    request.team_graffiti_mode = preferences->get_preference_checksum(kTeamGraffitiMode, kChecksum);
    std::array<uint8_t, kStartRequestSize> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    write_start_request(writer, request);
    send_request(manager, kMsgBeginMatchAsk, bytes.data(), sizeof(bytes));
    manager->match_being_set_up_ = true;
    return true;
}

// 0x00536170: FCFSRequestChangeLevel level=<load script> - for the created park, its map (0x6C, 15000 bytes) and the
// rail editor's rails (0x88, 4402 bytes) streamed to the server first; then the level (0x62).
bool cfunc_fcfs_request_change_level(Struct* params, Script*)
{
    Hub* manager = net_link();
    uint32_t level = 0;
    params->read_hash_value(kLevel, &level, 0);
    auto* client = core::down_cast<net::Guest>(manager->client_app_);
    if (level == kCreatedPark) {
        void* map = park_editing::ParkBuilderMode::sole()->get_packed_park_data(false);
        client->send_bulk_to_host(kMsgParkMap, kParkMapSize, map, kParkMapDesc, kGroupGame, true, false);
        park_editing::RailAuthoring::sole(); // (the rail editor looked up once for nothing)
        void* rails = static_cast<park_editing::RailAuthoring*>(park_editing::RailAuthoring::sole())->get_packed();
        client->send_bulk_to_host(kMsgRailsData, kRailsDataSize, rails, kRailsDesc, kGroupGame, true, false);
    }
    send_request(manager, kMsgChangeLevelRequest, &level, 4);
    return true;
}

// 0x00536240: ReattemptJoinServer - the connection request again on client 0.
bool cfunc_reattempt_join_server(Struct*, Script*)
{
    Hub* manager = net_link();
    manager->retry_entry(manager->client_app_);
    return true;
}

// 0x00536260: FCFSRequestToggleProSet bit=<n> param_id=<checksum> - message 0x63: the id, then the bit's byte.
bool cfunc_fcfs_request_toggle_pro_set(Struct* params, Script*)
{
    Hub* manager = net_link();
    int bit = 0;
    uint32_t id = 0;
    params->fetch_int_value(kBit, &bit, 1);
    params->read_hash_value(kParamId, &id, 1);
    struct {
        uint32_t id;
        uint8_t bit;
        std::array<uint8_t, 3> pad; // (the original's stack)
    } request = {id, static_cast<uint8_t>(bit), {0, 0, 0}};
    send_request(manager, kMsgToggleProSetRequest, &request, sizeof(request));
    return true;
}

// 0x005362F0: FCFSRequestToggleGoalSelection name=<goal> - message 0x64.
bool cfunc_fcfs_request_toggle_goal_selection(Struct* params, Script*)
{
    Hub* manager = net_link();
    uint32_t goal = 0; // (uninitialised in the original: a missing name sends what its slot held)
    params->read_hash_value(kName, &goal, 1);
    uint32_t request = goal;
    send_request(manager, kMsgToggleGoalRequest, &request, 4);
    return true;
}

// 0x00536360: ResetProSetFlags - for each of the 7 pro set flags (FLAG_PROSET1_GEO_ON on), SetFlag or UnSetFlag
// {flag = <its number>} as script_flag_bits_ has its bit.
bool cfunc_reset_pro_set_flags(Struct*, Script*)
{
    const Hub* manager = net_link();
    for (int bit = 0; bit < kProSetCount; ++bit) {
        Struct* params = new_params();
        params->put_int(kFlag, script::fetch_int_value(kFirstProSetFlag) + bit);
        get_run((manager->script_flag_bits_ & (1u << bit)) ? kSetFlag : kUnSetFlag, params);
        delete_params(params);
    }
    return true;
}

// 0x00536460: HasSignedDisclaimer - signed_disclaimer is boolean_true.
bool cfunc_has_signed_disclaimer(Struct*, Script*)
{
    return net_link()->get_online_settings()->get_preference_checksum(kSignedDisclaimer, kChecksum) == kBooleanTrue;
}

// 0x00537100: FoundServers - the server list (+0x158) has an entry.
bool cfunc_found_servers(Struct*, Script*)
{
    const memory::ListNode* node = net_link()->servers_.next; // (the list's first node)
    int count = 0;
    // (the head's mark tested before a null node, as in the original)
    while (static_cast<intptr_t>(cell::address(node->data)) != -1 && node) {
        node = node->next;
        ++count;
    }
    return count != 0;
}

} // namespace online
