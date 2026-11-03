// The network manager's small members (online::Hub, 0x00532AA0-0x005333F0): its flags and saved values, the
// network preferences it reads, and the few messages it sends on its own.

#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/task_list.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "game/park_editing/goal_editor.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"

namespace online {

namespace {

constexpr int kGoalsBufferSize = 6000;
constexpr uint16_t kLanPort = 5153; // the LAN lobby's port

// Messages.
constexpr uint8_t kMsgServerQuery = 0x11;
constexpr uint8_t kMsgReadyResponse = 0x26;
constexpr uint8_t kMsgGameEnded = 0x59;
constexpr uint8_t kGroupGame = 8; // the sequenced group of the game's messages

// Checksums.
constexpr uint32_t kChecksum = 0x21902065;           // checksum
constexpr uint32_t kRankTest = 0x2120910C;           // rank_test
constexpr uint32_t kRankTestLess = 0x3C6F2B1A;       // rank_test_less
constexpr uint32_t kRankTestGreater = 0x46D173AB;    // rank_test_greater
constexpr uint32_t kRankLimit = 0xAD6D5802;          // rank_limit
constexpr uint32_t kSkillLevel = 0x403DA0D0;         // skill_level
constexpr uint32_t kNumPlayers = 0x1CB2A6B0;         // num_players
constexpr uint32_t kTeamMode = 0xB9D44C2F;           // team_mode
constexpr uint32_t kNumObservers = 0xA8DA5CB7;       // num_observers
constexpr uint32_t kLevel = 0x651533EC;              // level
constexpr uint32_t kFireballDifficulty = 0xCE238F5D; // fireball_difficulty
constexpr uint32_t kShowNames = 0x0F7DD263;          // show_names
constexpr uint32_t kBooleanTrue = 0xF81BC89B;        // boolean_true
constexpr uint32_t kTeamsNone = 0x6A220326;          // teams_none
constexpr uint32_t kTeamsTwo = 0x768E5A1D;           // teams_two
constexpr uint32_t kTeamsThree = 0x4D04DFA7;         // teams_three
constexpr uint32_t kCtfGameType = 0x15421B73;        // ctf_game_type
constexpr uint32_t kTimedCtf = 0x938AB24B;           // timed_ctf
constexpr uint32_t kStopAtZero = 0x48E748B5;         // stop_at_zero
// num_0 ... num_9
constexpr auto kNum = std::to_array<uint32_t>({0x18CBB824, 0x6FCC88B2, 0xF6C5D908, 0x81C2E99E, 0x1FA67C3D,
                             0x68A14CAB, 0xF1A81D11, 0x86AF2D87, 0x16103016, 0x61170080});
// The game modes.
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;     // netgoalattack
constexpr uint32_t kKing = 0x5D32129C;              // king
constexpr uint32_t kNetKing = 0x6EF8FDA0;           // netking
constexpr uint32_t kNetScoreChallenge = 0x1498240A; // netscorechallenge
constexpr uint32_t kScoreChallenge = 0xF135ECB6;    // scorechallenge
constexpr uint32_t kNetCtf = 0x6C5FF266;            // netctf

// The checksum the network preference `name` is set to. On a miss (asserted, so the debug message's empty
// function is called) the original reads its uninitialised local: the slot ECX was pushed into - the manager.
uint32_t preference_checksum(Hub* manager, uint32_t name)
{
    script::Struct* preference = manager->get_online_settings()->fetch_setting(name);
    uint32_t value = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    preference->read_hash_value(kChecksum, &value, 1);
    return value;
}

// num_<first> ... num_<last> -> first + bias ...; anything else -> otherwise.
int number_of(uint32_t value, int first, int last, int bias, int otherwise)
{
    for (int n = first; n <= last; ++n) {
        if (value == core::at(kNum, n))
            return n + bias;
    }
    return otherwise;
}

// 0x004E8470: the goal editor module.
park_editing::ObjectiveAuthoring* get_goal_editor() { return park_editing::ObjectiveAuthoring::get_pull(); }

} // namespace

// 0x00532AA0
void Hub::set_join_mode(int mode)
{
    join_mode_ = mode;
    net_game_state().set_join_server_ip_succeeded(false);
}

// 0x00532AC0
int Hub::get_entry_mode() { return join_mode_; }

// 0x00532AD0
int Hub::get_hosting_role() { return host_mode_; }

// 0x00532AE0
int Hub::get_rank_test()
{
    const uint32_t value = preference_checksum(this, kRankTest);
    if (value == kRankTestLess)
        return 1;
    return value == kRankTestGreater ? 2 : 0;
}

// 0x00532B30
int Hub::get_rank_limit() { return number_of(preference_checksum(this, kRankLimit), 1, 9, -1, 9); }

// 0x00532BE0
int Hub::get_difficulty_tier() { return number_of(preference_checksum(this, kSkillLevel), 1, 5, -1, 2); }

// 0x00532C50
int Hub::get_player_limit() { return number_of(preference_checksum(this, kNumPlayers), 2, 8, 0, 2); }

// 0x00532CF0
int Hub::get_num_teams()
{
    const uint32_t value = preference_checksum(this, kTeamMode);
    if (value == kTeamsNone)
        return 0;
    if (value == kTeamsTwo)
        return 2;
    if (value == kTeamsThree)
        return 3;
    return 4; // teams_four, or anything else
}

// 0x00532D60
int Hub::get_spectator_limit() { return number_of(preference_checksum(this, kNumObservers), 0, 8, 0, 2); }

// 0x00532E30
void Hub::remove_host_task() { auto_serve_job_->drop_out(); }

// 0x00532E40 / 0x00532E50 / 0x00532E60 / 0x00532E70 / 0x00532E80 / 0x00532E90
uint32_t Hub::get_join_ip() { return join_ip_; }
uint32_t Hub::get_join_private_ip() { return join_private_ip_; }
int Hub::get_entry_port() { return join_port_; }
void Hub::set_join_ip(uint32_t ip) { join_ip_ = ip; }
void Hub::set_join_private_ip(uint32_t ip) { join_private_ip_ = ip; }
void Hub::set_entry_port(int port) { join_port_ = port; }

// 0x00532EA0 (+0x08 + index * 4: the two clients lie side by side)
net::AppHost* Hub::fetch_patron(int index) { return client_slot(index); }

// 0x00532EB0
net::AppHost* Hub::get_matchmaking_client() { return browser_client_; }

// 0x00532EC0
PlayerRecord* Hub::fetch_viewed_player() { return viewed_player_; }

// 0x00532ED0
void Hub::scan_local_network()
{
    if (!browser_client_)
        return;
    // 12 bytes: the time, then 8 bytes the original leaves uninitialised (zeroed here).
    std::array<uint32_t, 3> message = {{frame_clock::get_time(), 0, 0}};
    if (const uint32_t asked_for = net_game_state().asked_for()) {
        browser_client_->deliver_to(kMsgServerQuery, sizeof(message), message.data(), asked_for, kLanPort, 0);
        net_game_state().set_asked_for(0);
        return;
    }
    browser_client_->deliver_to(kMsgServerQuery, sizeof(message), message.data(), 0xFFFFFFFF, kLanPort, 0);
    const uint32_t command_line = net_game_state().command_line_server(); // (parse_network_params)
    if (command_line != 0xFFFFFFFF)
        browser_client_->deliver_to(kMsgServerQuery, sizeof(message), message.data(), command_line, kLanPort, 0);
}

// 0x00532F70 / 0x00532F80
prefs::Preferences* Hub::get_online_settings() { return &net_options_; }
prefs::Preferences* Hub::get_taunt_settings() { return &taunt_options_; }

// 0x00532F90
void Hub::set_start_info_words(const uint32_t* words) { std::memcpy(start_points_.data(), words, 8 * sizeof(uint32_t)); }

// 0x00532FD0
uint32_t Hub::get_start_info_word(int index) { return static_cast<uint32_t>(core::at(start_points_, index)); }

// 0x00532FE0
bool Hub::is_untimed_game()
{
    const uint32_t mode = game_type_;
    if (mode == kNetGoalAttack || mode == kKing || mode == kNetKing || mode == kNetScoreChallenge ||
        mode == kScoreChallenge)
        return true;
    if (!in_net_game())
        return false;
    prefs::Preferences* prefs = get_online_settings();
    if (mode == kNetCtf && prefs->get_preference_checksum({.preference = kCtfGameType, .field = kChecksum}) != kTimedCtf)
        return true;
    return prefs->get_preference_checksum({.preference = kStopAtZero, .field = kChecksum}) == kBooleanTrue;
}

// 0x00533060 / 0x00533070
void Hub::set_game_id(uint8_t id) { game_id_ = id; }
uint8_t Hub::get_game_id() { return game_id_; }

// 0x00533080 (on a miss: the manager's address, the uninitialised local's slot)
uint32_t Hub::get_level_preference() { return preference_checksum(this, kLevel); }

// 0x005330B0
int Hub::get_fireball_level() { return number_of(preference_checksum(this, kFireballDifficulty), 1, 5, 0, 3); }

// 0x00533130
void Hub::toggle_player_names() { show_names_ = !show_names_; }

// 0x00533140
bool Hub::wants_name_tags()
{
    uint32_t value = 0;
    get_online_settings()->fetch_setting(kShowNames)->read_hash_value(kChecksum, &value, 0);
    return value == kBooleanTrue;
}

// 0x00533180 / 0x00533190 / 0x005331A0
void Hub::set_last_leader(PlayerRecord* player) { last_leader_ = player; }
PlayerRecord* Hub::get_last_leader() { return last_leader_; }
int Hub::get_last_leading_team() { return last_leading_team_; }

// 0x005331B0 / 0x005331C0
void Hub::change_score_sync_stamp(uint32_t time) { score_synced_at_ = time; }
uint32_t Hub::fetch_score_sync_stamp() { return score_synced_at_; }

// 0x005331D0 / 0x005331E0
void Hub::set_final_scores_received(bool received) { final_scores_received_ = received; }
bool Hub::final_scores_received() { return final_scores_received_; }

// 0x005331F0 / 0x00533200
void Hub::set_entry_phase(int state) { join_phase_ = state; }
int Hub::get_entry_phase() { return join_phase_; }

// 0x00533210 / 0x00533220
objects::CrownObject* Hub::get_crown_holder() { return crown_; }
void Hub::clear_crown() { crown_ = nullptr; }

// 0x00533230
void Hub::set_crown_start_spot(char point) { crown_start_ = int{point}; }

// 0x00533240
void Hub::set_room_id(uint32_t id) { room_id_ = id; }

// 0x00533250
void Hub::send_game_ended()
{
    net::NetMessageInfo desc;
    desc.set_content(&game_id_);
    desc.set_byte_count(1);
    desc.set_ident(kMsgGameEnded);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(kGroupGame);
    core::down_cast<net::Guest>(client_app_)->send_to_host(&desc);
    ended_game_id_ = game_id_;
}

// 0x005332A0
bool Hub::game_ended_sent() { return ended_game_id_ == game_id_; }

// 0x005332B0 / 0x005332C0 / 0x005332D0
bool Hub::has_match_ended() { return match_ended_; }
void Hub::mark_match_ended() { match_ended_ = true; }
void Hub::reset_match_ended() { match_ended_ = false; }

// 0x005332E0 / 0x005332F0
bool Hub::cheating_noted() { return cheating_noted_; }
void Hub::note_cheating() { cheating_noted_ = true; }

// 0x00533300
void Hub::send_ready_response()
{
    net::NetMessageInfo desc;
    uint32_t id = last_ready_probe_id_;
    desc.set_content(&id);
    desc.set_byte_count(sizeof(id));
    desc.set_ident(kMsgReadyResponse);
    desc.set_queue(net::NetMessageInfo::kLanePriority);
    for (int i = 0; i < 2; ++i) {
        if (net::AppHost* client = fetch_patron(i))
            core::down_cast<net::Guest>(client)->send_to_host(&desc);
    }
}

// 0x00533360
void* Hub::get_pending_data(uint32_t* size)
{
    *size = pending_data_size_;
    return pending_data_;
}

// 0x00533380
bool Hub::has_created_goals() { return get_goal_editor()->count_complete_goals(0) > 0; }

// 0x005333A0
void Hub::prepare_created_goals(uint32_t level)
{
    created_goals_level_ = level;
    std::memcpy(created_goals_.data(), &level, sizeof(level));
    park_editing::ObjectiveAuthoring* editor = get_goal_editor();
    const int size = static_cast<int>(editor->write_level_goals_to_buffer(level, std::span(created_goals_).subspan(sizeof(level)).data(),
                                                                          static_cast<uint32_t>(kGoalsBufferSize)));
    created_goals_size_ = size + 4;
}

// 0x005333E0 / 0x005333F0
void* Hub::get_created_goals_message() { return created_goals_.data(); }
uint32_t Hub::get_created_goals_size() { return created_goals_size_; }

} // namespace online

