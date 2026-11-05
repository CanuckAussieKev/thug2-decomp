// The network manager's script commands, first part (0x00535DA0-0x00536460): capture the flag's task, the game's
// state, the join mode, teams, the lobby's peer, the message of the day, and the first-come-first-served requests a
// client sends the server (start the game, change the level, toggle a pro set flag or a goal).

#include <cstdint>
#include "game/online/service.h"
#include <array>
#include "engine/memory/arena_alloc.h"
#include "platform/gamespy.h"
#include "engine/memory/task_list.h"
#include "game/park_editing/park_editor.h"
#include "game/online/gamenet_commands.h"
#include "game/online/gamenet_handler_util.h"
#include "game/online/gamenet_lobby.h"
#include "game/online/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "engine/sys/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "engine/assets/resource_registry.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "engine/script/fwd.h"
#include "engine/script/context.h"
#include "game/skate/game_mode.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/online/server_list.h"
#include "game/online/score_list.h"
#include "game/online/buddy_man.h"
#include "game/online/stats_man.h"
#include "engine/sys/mlp.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "base/core/down_cast.h"
#include "game/online/net_game_state.h"
#include "wire/message_ids.h"

namespace online {

using namespace detail;
using script::Context;
using script::Struct;

namespace {

// Messages to the server.
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

uint32_t& last_flag_base_warning()
{
    static uint32_t last_flag_base_warning{};
    return last_flag_base_warning;
}

tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks()); }

bool in_group_room(Hub* manager) { return manager->lobby_service()->in_group_room(); }

// A message for the server through client 0, sequenced in the game's group.
void send_request(Hub* manager, MessageId id, void* data, uint16_t length)
{
    net::NetMessageInfo desc;
    desc.set_ident(id);
    desc.set_content(data);
    desc.set_byte_count(length);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(MessageGroup::kGame);
    send_to_server(manager->client_app(), &desc);
}

} // namespace

// 0x00535DA0: StartCTFGame - capture the flag's task into the logic tasks.
bool cfunc_start_ctf_game(Struct* /*params*/, Context /*context*/)
{
    get_logic_tasks()->queue_job(net_game_state().online_hub()->tasks().flag_carry_job().get());
    return true;
}

// 0x00535DC0: EndCTFGame - its task removed.
bool cfunc_end_ctf_game(Struct* /*params*/, Context /*context*/)
{
    net_game_state().online_hub()->tasks().flag_carry_job()->drop_out();
    return true;
}

// 0x00535DE0: DisplayFlagBaseWarning - display_flag_base_warning, at most every 3 seconds.
bool cfunc_display_flag_base_warning(Struct* /*params*/, Context /*context*/)
{
    if (frame_clock::get_time() - last_flag_base_warning() > kFlagBaseWarningInterval) {
        get_run(kDisplayFlagBaseWarning);
        last_flag_base_warning() = frame_clock::get_time();
    }
    return true;
}

// 0x00535E20: NetworkGamePending - a start request is waiting for the server.
bool cfunc_network_game_pending(Struct* /*params*/, Context /*context*/)
{
    return net_game_state().online_hub()->match_being_set_up();
}

// 0x00535E30: MarkGameOver.
bool cfunc_mark_game_over(Struct* /*params*/, Context /*context*/)
{
    net_game_state().online_hub()->set_match_ended(true);
    return true;
}

// 0x00535E40: SetJoinMode <n> (an unnamed integer: 0 to play, anything else to watch).
bool cfunc_set_join_mode(Struct* params, Context /*context*/)
{
    Hub* manager = net_game_state().online_hub();
    int mode = 0;
    params->fetch_int_value(0U, &mode, 0);
    manager->set_join_mode(mode == 0 ? 0 : 1);
    return true;
}

// 0x00535E90: GetNumTeams - the game mode's team count into the script's num_teams.
bool cfunc_get_num_teams(Struct* /*params*/, Context context)
{
    context.args()->put_int(kNumTeams, get_game_mode()->current_num_teams());
    return true;
}

// 0x00535EB0: ConnectedToPeer - the lobby has a GameSpy peer, connected (peerIsConnected).
bool cfunc_connected_to_peer(Struct* /*params*/, Context /*context*/)
{
    void* peer = net_game_state().online_hub()->lobby_service()->get_lobby_handle();
    return (peer != nullptr) && (game_spy::peer_is_connected(peer) != 0);
}

// 0x00535EE0: InGroupRoom - the lobby's flag (+0x134).
bool cfunc_in_group_room(Struct* /*params*/, Context /*context*/)
{
    return in_group_room(net_game_state().online_hub());
}

// 0x00535EF0: IsAutoServing - the server of a network game in host mode 1.
bool cfunc_is_auto_serving(Struct* /*params*/, Context /*context*/)
{
    Hub* manager = net_game_state().online_hub();
    if (!manager->in_net_game() || ((manager->flag_bits() & 1) == 0)) {
        return false;
    }
    return manager->host_mode() == 1;
}

// 0x00535F20: ChangeLevelPending - the level change's task is in a list.
bool cfunc_change_level_pending(Struct* /*params*/, Context /*context*/)
{
    return net_game_state().online_hub()->tasks().level_change_job()->is_on_task_list();
}

// 0x00535F30: CancelMotdDownload.
bool cfunc_cancel_motd_download(Struct* /*params*/, Context /*context*/)
{
    net_game_state().online_hub()->set_download_done(true);
    return true;
}

// 0x00535F40: DownloadMotd - the lobby's state 6.
bool cfunc_download_motd(Struct* /*params*/, Context /*context*/)
{
    net_game_state().online_hub()->set_service_state(online::ServiceState::kNewsMessage);
    return true;
}

// 0x00535F50: ServicesAreAvailable - with the services up (+0xFE0A = 1): true (if the message of the day hasn't come
// yet, services state 0 and lobby state 1 first); else services state 3, lobby state 1, false.
bool cfunc_services_are_available(Struct* /*params*/, Context /*context*/)
{
    Hub* manager = net_game_state().online_hub();
    if (manager->services_available()) {
        if (!manager->motd_received()) {
            manager->set_next_service_state(ServiceState::kWaiting);
            manager->set_service_state(ServiceState::kWake);
        }
        return true;
    }
    manager->set_next_service_state(ServiceState::kStart);
    manager->set_service_state(ServiceState::kWake);
    return false;
}

// 0x00535FA0: AlreadyGotMotd.
bool cfunc_already_got_motd(Struct* /*params*/, Context /*context*/)
{
    return net_game_state().online_hub()->motd_received();
}

// 0x00535FB0: JoinServerComplete - ready to play.
bool cfunc_join_server_complete(Struct* /*params*/, Context /*context*/)
{
    return net_game_state().online_hub()->cleared_to_start();
}

// 0x00535FC0: JustStartedNetGame - within 5 seconds of the network game's start (0x007CCDF4).
bool cfunc_just_started_net_game(Struct* /*params*/, Context /*context*/)
{
    return frame_clock::get_time() - net_game_state().net_game_start_time() < kJustStartedTime;
}

// 0x00535FE0: FCFSRequestStartGame - the game's options from the network preferences to the server (message 0x60);
// the game is then pending.
bool cfunc_fcfs_request_start_game(Struct* /*params*/, Context /*context*/)
{
    StartRequest request; // (gamenet_messages.h; +0x18 is never set: the original's stack - zero here)
    Hub* manager = net_game_state().online_hub();
    prefs::Preferences* preferences = manager->settings().get_online_settings();
    request.skill_level = preferences->get_preference_checksum({.preference = kSkillLevel, .field = kChecksum});
    request.fireball_level = preferences->get_preference_checksum({.preference = kFireballDifficulty, .field = kChecksum});
    uint32_t game_type = 0;
    manager->get_game_preferences()->fetch_setting(kGameType)->read_hash_value(kChecksum, &game_type, 1);
    request.game_type = game_type;
    request.time_limit = preferences->get_preference_checksum({.preference = kTimeLimit, .field = kChecksum});
    request.target_score = preferences->get_preference_checksum({.preference = kTargetScore, .field = kChecksum});
    request.player_collision = preferences->get_preference_checksum({.preference = kPlayerCollision, .field = kChecksum});
    request.friendly_fire = preferences->get_preference_checksum({.preference = kFriendlyFire, .field = kChecksum});
    request.stop_at_zero = preferences->get_preference_checksum({.preference = kStopAtZero, .field = kChecksum});
    request.score_reset = preferences->get_preference_checksum({.preference = kScoreReset, .field = kChecksum});
    request.ctf_game_type = preferences->get_preference_checksum({.preference = kCtfGameType, .field = kChecksum});
    request.goals = preferences->get_preference_checksum({.preference = kGoals, .field = kChecksum});
    request.team_graffiti_mode = preferences->get_preference_checksum({.preference = kTeamGraffitiMode, .field = kChecksum});
    std::array<uint8_t, kStartRequestSize> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    write_start_request(writer, request);
    send_request(manager, MessageId::kStartRequest, bytes.data(), sizeof(bytes));
    manager->set_match_being_set_up(true);
    return true;
}

// 0x00536170: FCFSRequestChangeLevel level=<load script> - for the created park, its map (0x6C, 15000 bytes) and the
// rail editor's rails (0x88, 4402 bytes) streamed to the server first; then the level (0x62).
bool cfunc_fcfs_request_change_level(Struct* params, Context /*context*/)
{
    Hub* manager = net_game_state().online_hub();
    uint32_t level = 0;
    params->read_hash_value(kLevel, &level, 0);
    auto* client = core::down_cast<net::Guest>(manager->client_app());
    if (level == kCreatedPark) {
        void* map = park_editing::ParkBuilderMode::get_packed_park_data(false)->bytes().data();
        client->send_bulk_to_host(MessageId::kParkMap, kParkMapSize, map, kParkMapDesc, MessageGroup::kGame, true, false);
        park_editing::RailAuthoring::sole(); // (the rail editor looked up once for nothing)
        void* rails = static_cast<park_editing::RailAuthoring*>(park_editing::RailAuthoring::sole())->get_packed();
        client->send_bulk_to_host(MessageId::kRailsData, kRailsDataSize, rails, kRailsDesc, MessageGroup::kGame, true, false);
    }
    send_request(manager, MessageId::kChangeLevelRequest, &level, 4);
    return true;
}

// 0x00536240: ReattemptJoinServer - the connection request again on client 0.
bool cfunc_reattempt_join_server(Struct* /*params*/, Context /*context*/)
{
    Hub* manager = net_game_state().online_hub();
    manager->retry_entry(manager->client_app());
    return true;
}

// 0x00536260: FCFSRequestToggleProSet bit=<n> param_id=<checksum> - message 0x63: the id, then the bit's byte.
bool cfunc_fcfs_request_toggle_pro_set(Struct* params, Context /*context*/)
{
    Hub* manager = net_game_state().online_hub();
    int bit = 0;
    uint32_t id = 0;
    params->fetch_int_value(kBit, &bit, 1);
    params->read_hash_value(kParamId, &id, 1);
    struct {
        uint32_t id;
        uint8_t bit;
        std::array<uint8_t, 3> pad; // (the original's stack)
    } request = {.id = id, .bit = static_cast<uint8_t>(bit), .pad = {0, 0, 0}};
    send_request(manager, MessageId::kToggleProSetRequest, &request, sizeof(request));
    return true;
}

// 0x005362F0: FCFSRequestToggleGoalSelection name=<goal> - message 0x64.
bool cfunc_fcfs_request_toggle_goal_selection(Struct* params, Context /*context*/)
{
    Hub* manager = net_game_state().online_hub();
    uint32_t goal = 0; // (uninitialised in the original: a missing name sends what its slot held)
    params->read_hash_value(kName, &goal, 1);
    uint32_t request = goal;
    send_request(manager, MessageId::kToggleGoalRequest, &request, 4);
    return true;
}

// 0x00536360: ResetProSetFlags - for each of the 7 pro set flags (FLAG_PROSET1_GEO_ON on), SetFlag or UnSetFlag
// {flag = <its number>} as script_flag_bits_ has its bit.
bool cfunc_reset_pro_set_flags(Struct* /*params*/, Context /*context*/)
{
    const Hub* manager = net_game_state().online_hub();
    for (int bit = 0; bit < kProSetCount; ++bit) {
        const script::StructPtr params = script::make_struct();
        params->put_int(checksums::Name{kFlag}, script::fetch_int_value(kFirstProSetFlag) + bit);
        get_run(((manager->script_flag_bits() & (1U << bit)) != 0U) ? kSetFlag : kUnSetFlag, params.get());
    }
    return true;
}

// 0x00536460: HasSignedDisclaimer - signed_disclaimer is boolean_true.
bool cfunc_has_signed_disclaimer(Struct* /*params*/, Context /*context*/)
{
    return net_game_state().online_hub()->settings().get_online_settings()->get_preference_checksum(
               {.preference = kSignedDisclaimer, .field = kChecksum}) == kBooleanTrue;
}

// 0x00537100: FoundServers - the server list (+0x158) has an entry.
bool cfunc_found_servers(Struct* /*params*/, Context /*context*/)
{
    const memory::ListNode* node = net_game_state().online_hub()->servers().next(); // (the list's first node)
    int count = 0;
    // (the head's mark tested before a null node, as in the original)
    while (node->data() != memory::ListNode::head_marker() && node) {
        node = node->next();
        ++count;
    }
    return count != 0;
}

} // namespace online
