// The network manager's session handlers (online::Hub): the server's answer to a LAN search, the host's options,
// the game's state for a joining player, the level change and the start of a game. See gamenet_handlers.h.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/net/gamenet_commands.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_manager.h"
#include "engine/objects/entity.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "engine/core/code_address.h"
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
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace online {

using namespace detail;

namespace {

// Names.
constexpr uint32_t kUiString = 0x96875C0F;            // ui_string
constexpr uint32_t kServerName = 0x2A5CF019;          // server_name
constexpr uint32_t kChecksum = 0x21902065;            // checksum
constexpr uint32_t kNetLobby = 0x1C471C60;            // netlobby
constexpr uint32_t kNetScavengerHunt = 0xFC13B531;    // netscavengerhunt
constexpr uint32_t kNetElimiskate = 0x10F84276;       // netelimiskate
constexpr uint32_t kNetGraffiti = 0x5E2EA50C;         // netgraffiti
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;       // netgoalattack
constexpr uint32_t kNetCtf = 0x6C5FF266;              // netctf
constexpr uint32_t kFirefight = 0x3D6D444F;           // firefight
constexpr uint32_t kNetFirefight = 0xBFF33600;        // netfirefight
constexpr uint32_t kScavenger = 0x1F3430CE;           // scavenger
constexpr uint32_t kScavengerInterval = 0x8A6B04AD;   // scavenger_interval
constexpr uint32_t kScavengerCollect = 0xD4F04E7F;    // scavenger_collect_period
constexpr uint32_t kElimiskate = 0xF6198005;          // elimiskate
constexpr uint32_t kElimiskateInterval = 0xF92E5D89;  // elimiskate_interval
constexpr uint32_t kElimiskateBreak = 0xA96CE18F;     // elimiskate_break_period
constexpr uint32_t kTargetScore = 0x71CD53A2;         // target_score
constexpr uint32_t kScore = 0xCD66C8AE;               // score
constexpr uint32_t kTime = 0x906B67BA;                // time
constexpr uint32_t kLevel = 0x651533EC;               // level
constexpr uint32_t kLoadSk5edGameplay = 0xB664035D;   // load_sk5ed_gameplay (the created park)
constexpr uint32_t kChangingLevels = 0xD39572F8;      // net_message_changing_levels
constexpr uint32_t kHideConsoleWindow = 0x53610AA4;   // hide_console_window
constexpr uint32_t kChangeLevel = 0x39C58EA1;         // change_level
constexpr uint32_t kChangeLevelFlag = 0x055ACC01;     // (a flag change_level is given)
constexpr uint32_t kShowWarning = 0xD8E34EDA;         // show_warning
constexpr uint32_t kChosenStartGame = 0x1A64CB61;     // chosen_start_game
constexpr uint32_t kEndGameSelected = 0x65A12F7B;     // fcfc_end_game_selected
constexpr uint32_t kTeamMode = 0xB9D44C2F;            // team_mode
constexpr uint32_t kTeamsNone = 0x6A220326;           // teams_none
constexpr uint32_t kTeamsTwo = 0x768E5A1D;            // teams_two
constexpr uint32_t kTeamsThree = 0x4D04DFA7;          // teams_three
constexpr uint32_t kTeamsFour = 0x85736594;           // teams_four
constexpr uint32_t kDefaultTimeLimit = 0x8D38A280;    // default_time_limit
constexpr uint32_t kTargetScoreArray = 0x50E63314;    // targetScoreArray
constexpr uint32_t kHighestScoreArray = 0x1A2C22F9;   // highestScoreArray
constexpr uint32_t kVictoryConditions = 0x94A24C5E;   // victory_conditions
constexpr uint32_t kFireballLevel = 0xCE87E4E3;       // fireball_level
constexpr uint32_t kStopAtZero = 0x48E748B5;          // stop_at_zero
constexpr uint32_t kShouldResetScores = 0x0B803C5E;   // should_reset_scores
constexpr uint32_t kStartingNewNetGame = 0xA5AE5E25;  // StartingNewNetGame
constexpr uint32_t kInitSelectedGoals = 0x26D9EDC1;   // InitSelectedGoals
constexpr uint32_t kSkaterScore = 0xD262802F;         // SkaterScore (a component)
constexpr uint32_t kUnlimitedTime = 0xF0E712D2;       // unlimited_time
constexpr uint32_t kGoalScript = 0x79F90D81;          // goal_script
constexpr uint32_t kNetGameTypeInfo = 0x2F94C542;     // net_game_type_info
constexpr uint32_t kMpGameTypeInfo = 0x4DA72272;      // mp_game_type_info

// Messages.
constexpr uint8_t kMsgServerInfo = 0x10;
constexpr uint8_t kMsgSessionBrief = 0x33;
constexpr uint8_t kMsgGameStateSent = 0x42;
constexpr uint8_t kMsgSpawnedScript = 0x49;
constexpr uint8_t kMsgKing = 0x55;
constexpr uint8_t kMsgTrickObjects = 0x5C;
constexpr uint8_t kMsgGameEnded = 0x6A;
constexpr uint8_t kMsgSelectedGoals = 0x6D;
constexpr uint8_t kMsgGoalsData = 0x87;

// The server's answer to a LAN search (message 0x10, 0xE0 bytes).
#pragma pack(push, 1)
struct HostListing {
    int8_t num_players_;          // +0x00
    int8_t max_players_;          // +0x01
    int8_t num_observers_;        // +0x02
    int8_t max_observers_;        // +0x03
    bool password;              // +0x04
    bool in_game_;                // +0x05 (not in the lobby)
    uint8_t host_mode_;           // +0x06
    bool logged_in_;              // +0x07 internet play, logged in to GameSpy
    char name[16];              // +0x08
    char level[32];             // +0x18
    char mode[32];              // +0x38
    uint8_t skill_level_;         // +0x58
    uint8_t pad59[3];
    uint32_t time;              // +0x5C the asker's
    char players[8][16];        // +0x60
};
#pragma pack(pop)
static_assert(sizeof(HostListing) == 0xE0);

// A joining player's game information (message 0x33, 0x50 bytes).
struct GameInfo {
    uint8_t crown_start_slot_;    // +0x00
    bool not_lan_;                // +0x01 (the connection type isn't 1)
    uint8_t game_id_;             // +0x02
    uint8_t pro_option_bits_;        // +0x03
    uint8_t max_players_;         // +0x04
    uint8_t pad05[3];
    uint32_t time_limit_;         // +0x08 seconds
    uint32_t game_type_;          // +0x0C
    uint32_t level;             // +0x10
    uint32_t teams;             // +0x14
    uint32_t value18;           // +0x18 (0x00535B20)
    uint32_t value_1c_;           // +0x1C (0x00535AD0)
    uint32_t value20;           // +0x20 (the game mode's 0x00553170)
    uint32_t target_score_;       // +0x24
    uint32_t start_info_[8];      // +0x28 the manager's start info words
    uint32_t value48;           // +0x48 (the game mode's 0x00553270)
    uint8_t variant;            // +0x4C scavenger hunt / elimiskate: which of its three goals is running
    uint8_t pad4d_[3];
};
static_assert(sizeof(GameInfo) == 0x50);

// strncpy(dest, source, 15) and the 16th byte zeroed.
void copy_name(char* dest, const char* source)
{
    size_t length = strnlen(source, kNameSize - 1);
    std::memcpy(dest, source, length);
    std::memset(dest + length, 0, kNameSize - length);
}

prefs::Preferences* net_game_options(Hub* manager) { return manager->get_online_settings(); }

// A goal's virtual +0xC: running.
bool goal_running(uint8_t* goal) { return reinterpret_cast<bool(__thiscall*)(uint8_t*)>(prop<void**>(goal, 0)[3])(goal); }

} // namespace

// 0x0052DD40
int __cdecl on_server_query_msg(net::InboundMsgCtx* context)
{
    PlayerSearch players;
    NewPlayerSearch new_players;
    if (context->body_size_ != 0xC)
        return kDone;
    Hub* manager = manager_of(context);
    const char* server_name;
    manager->get_online_settings()->fetch_setting(kServerName)->fetch_text(kUiString, &server_name, 1);
    const uint8_t* msg = get_message(context);
    HostListing info;
    std::memset(&info, 0, sizeof(info));
    char name[kNameSize];
    copy_name(name, server_name);
    net::AppHost* server = manager->host_;
    info.skill_level_ = static_cast<uint8_t>(manager->get_difficulty_tier());
    info.num_players_ = static_cast<int8_t>(manager->count_players());
    info.max_players_ = static_cast<int8_t>(manager->get_player_limit());
    info.num_observers_ = static_cast<int8_t>(manager->count_observers());
    info.max_observers_ = static_cast<int8_t>(manager->get_spectator_limit());
    if (info.max_players_ < info.num_players_)
        info.max_players_ = info.num_players_;
    if (info.max_observers_ < info.num_observers_)
        info.max_observers_ = info.num_observers_;
    info.password = *manager->fetch_secret() != '\0';
    info.in_game_ = get_game_type() != kNetLobby;
    info.host_mode_ = static_cast<uint8_t>(manager->get_hosting_role());
    info.logged_in_ = manager->in_internet_mode() && prop<bool>(manager->presence_service_, 0x10);
    copy_name(info.name, name);
    std::sprintf(info.level, "%s", manager->get_map_name(true));
    std::sprintf(info.mode, "%s", manager->get_game_type_name());
    // The players' names (not observers, unless 0x005434C0), then the new players' not among them yet.
    uint8_t ids[8] = {};
    int id_count = 0;
    info.time = prop<uint32_t>(msg, 0);
    int named = 0;
    for (uint8_t* player = start_roster_sweep(manager, &players, true); player; player = step_roster_sweep(manager, &players, true)) {
        if (is_observer(player) && !as_player(player)->is_pending_observer())
            continue;
        if (player_skater(player))
            ids[id_count++] = static_cast<uint8_t>(get_skater_id(player_skater(player)));
        copy_name(info.players[named], player_name(player));
        ++named;
    }
    char* slot = info.players[named];
    for (uint8_t* added = first_new_player(manager, &new_players); added; added = next_new_player(manager, &new_players)) {
        uint32_t flags = prop<uint32_t>(added, 0x20);
        if (!(flags & 0x18) && (flags & 4))
            continue;
        bool known = false;
        for (int i = 0; i < id_count; ++i) {
            if (ids[i] == added[0x10])
                known = true;
        }
        if (known)
            continue;
        std::sprintf(slot, reinterpret_cast<const char*>(added)); // (the name as the format, the original's)
        slot += kNameSize;
    }
    server->deliver_to(kMsgServerInfo, sizeof(info), &info, context->sender_conn_->remote_ip_, context->sender_conn_->port_no_, 0);
    return kContinue;
}

// 0x005301A0
int __cdecl handle_host_options(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    auto* host = static_cast<uint8_t*>(manager->get_host_player()); // (the host's own player)
    if (!host || prop<net::Conn*>(host, 0x18) != context->sender_conn_)
        return kDone;
    const uint8_t* msg = get_message(context);
    auto word = [msg](uint32_t offset) { return prop<uint32_t>(msg, offset); };
    // (the text / a field of the entry with that checksum in a global list)
    auto option_text = [manager](uint32_t list, uint32_t value) { return manager->get_choice_name(list, value); };
    auto option_field = [manager](uint32_t list, uint32_t value, uint32_t field) {
        return manager->get_choice_integer(list, value, field);
    };
    switch (context->type_code_) {
    case 0x60: {
        // The game's options, each a preference: its text and checksum.
        prefs::Preferences* prefs = net_game_options(manager);
        auto set = [&](uint32_t preference, uint32_t list, uint32_t value, bool fireball = false) {
            script::Struct* params = new_params();
            const char* text = option_text(list, value);
            if (fireball)
                option_field(list, value, kFireballLevel); // (looked up for nothing)
            params->append_word(kUiString, script::kQbvalString, text);
            params->add_component_of_type(kChecksum, script::kQbvalName, value);
            prefs->change_setting(preference, params);
            delete_params(params);
        };
        set(0x9834C4FA, 0x2F94C542, word(0x00));        // game_type: net_game_type_info
        set(0x403DA0D0, 0x48DAE9CB, word(0x08));        // skill_level: skill_level_info
        set(0xCE238F5D, 0xD7E42AF4, word(0x0C), true);  // fireball_difficulty: fireball_level_info
        set(0x043EE978, 0x8BA3F08B, word(0x10));        // player_collision: on_off_types
        set(0xE959C43A, 0x8BA3F08B, word(0x14));        // friendly_fire: on_off_types
        set(0x48E748B5, 0x7FE33468, word(0x20));        // stop_at_zero: boolean_types
        set(0x162E8C3A, 0x7FE33468, word(0x24));        // score_reset: boolean_types
        set(0x15421B73, 0x15EA4F9E, word(0x28));        // ctf_game_type: ctf_type
        set(0x38DBE1D0, 0x38DBE1D0, word(0x2C));        // goals: goals
        set(0xA8774BF7, 0xA8774BF7, word(0x30));        // team_graffiti_mode
        {   // time_limit: time_limit_options, with its time
            script::Struct* params = new_params();
            const char* text = option_text(0x834A7C01, word(0x04));
            int time = option_field(0x834A7C01, word(0x04), kTime);
            params->append_word(kUiString, script::kQbvalString, text);
            params->add_component_of_type(kChecksum, script::kQbvalName, word(0x04));
            params->add_component_of_type(kTime, script::kQbvalInteger, static_cast<uint32_t>(time));
            prefs->change_setting(0xC51BF6E3, params);
            delete_params(params);
        }
        {   // target_score: target_score_options, else capture_options, else time_limit_options (its time, in ms)
            script::Struct* params = new_params();
            const char* text = option_text(0x52CE135D, word(0x1C));
            int score = option_field(0x52CE135D, word(0x1C), kScore);
            if (!text) {
                text = option_text(0x5D67D0BE, word(0x1C));
                score = option_field(0x5D67D0BE, word(0x1C), kScore);
                if (!text) {
                    text = option_text(0x834A7C01, word(0x1C));
                    score = option_field(0x834A7C01, word(0x1C), kTime) * 1000;
                }
            }
            params->append_word(kUiString, script::kQbvalString, text);
            params->add_component_of_type(kChecksum, script::kQbvalName, word(0x1C));
            if (score > 0)
                params->add_component_of_type(kScore, script::kQbvalInteger, static_cast<uint32_t>(score));
            prefs->change_setting(kTargetScore, params);
            delete_params(params);
        }
        get_run(kChosenStartGame);
        break;
    }
    case 0x61: {
        // Kick (msg[0] = 1) or ban the msg[1]'th player, whose name must be msg + 2.
        PlayerSearch search;
        int index = 0;
        uint8_t* player = start_roster_sweep(manager, &search, true);
        for (; player; player = step_roster_sweep(manager, &search, true), ++index) {
            if (static_cast<int8_t>(msg[1]) == index)
                break;
        }
        if (!player)
            return kDone;
        if (_stricmp(reinterpret_cast<const char*>(msg + 2), player_name(player)) != 0)
            return kDone;
        objects::Entity* skater = player_skater(player);
        bool observer = is_observer(player);
        manager->evict_player(player, msg[0] == 1 ? 3 : 4);
        if (!observer)
            get_call(code_address(&modules::Skate::drop_skater_from_roster), get_skate(), skater);
        break;
    }
    case 0x62: {
        script::Struct* params = new_params();
        params->add_component_of_type(kLevel, script::kQbvalName, word(0));
        params->put_name_key(kChangeLevelFlag, kChangeLevelFlag);
        params->put_name_key(kShowWarning, kShowWarning);
        get_run(kChangeLevel, params);
        delete_params(params);
        break;
    }
    case 0x64:
        get_call(code_address(&goals::GoalManager::flip_objective_pick), objective_board(), word(0));
        break;
    case 0x65: {
        // End the game: everyone else told.
        PlayerSearch search;
        net::NetMessageInfo desc;
        desc.make_new();
        net::AppHost* server = manager->host_;
        get_run(kEndGameSelected);
        desc.ident_ = kMsgGameEnded;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        for (uint8_t* p = start_roster_sweep(manager, &search, true); p; p = step_roster_sweep(manager, &search, true)) {
            if (!is_own_player(p))
                server->put_on_outbox(player_handle(p), &desc);
        }
        break;
    }
    case 0x66: {
        // The number of teams.
        int teams = static_cast<int8_t>(msg[0]);
        script::Struct* count = new_params();
        count->put_int(0u, teams);
        script::Struct* params = new_params();
        static const char* const kTexts[] = {"None", nullptr, "2", "3", "4"};
        static const uint32_t kTeams[] = {kTeamsNone, 0, kTeamsTwo, kTeamsThree, kTeamsFour};
        if (teams >= 0 && teams <= 4 && teams != 1) {
            params->append_word(kUiString, kTexts[teams]);
            params->put_name_key(kChecksum, kTeams[teams]);
        }
        net_game_options(manager)->change_setting(kTeamMode, params);
        delete_params(params);
        cfunc_set_num_teams(count, nullptr);
        delete_params(count);
        break;
    }
    default: // (0x63: nothing)
        break;
    }
    return kContinue;
}

// 0x0052F4A0 is the level change's wait task (gamenet_tasks.cpp).

uint32_t g_level_change_time;

// 0x005311F0
int __cdecl on_level_switch_msg(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    const uint8_t* msg = get_message(context);
    manager->clear_queued_scripts();
    manager->match_ended_ = false;
    if (!msg[4])
        return kContinue;
    auto* logic_tasks = reinterpret_cast<tasks::PileUp*>(reinterpret_cast<uint8_t*>(assets::g_main_loop_manager) + 0x04);
    auto* change_task = prop<tasks::SchedulerTask*>(manager, 0xEC);
    if (change_task->is_on_task_list()) {
        if (!manager->get_on_server())
            manager->level_ = prop<uint32_t>(msg, 0);
        return kDone;
    }
    uint32_t old_level = manager->level_;
    manager->level_ = prop<uint32_t>(msg, 0);
    g_level_change_time = frame_clock::get_time() + 3000; // when to change
    if (manager->get_on_server()) {
        script::Struct* params = new_params();
        params->append_word(kUiString, script::kQbvalString, manager->get_map_name(false));
        params->add_component_of_type(kChecksum, script::kQbvalName, manager->level_);
        manager->get_online_settings()->change_setting(kLevel, params);
        delete_params(params);
        if (manager->level_ == kLoadSk5edGameplay)
            g_level_change_time += 1000;
    } else if (old_level == kLoadSk5edGameplay && manager->level_ != old_level) {
        park_editing::RailAuthoring::sole()->get_clear(); // the created park's rails gone
    }
    logic_tasks->queue_job(change_task);
    manager->build_net_notice(false, kChangingLevels, manager->get_map_name(true), nullptr, nullptr, nullptr, false,
                                   5000);
    get_run(kHideConsoleWindow);
    return kDone;
}

// 0x005313C0
int __cdecl handle_start_game(net::InboundMsgCtx* context)
{
    PlayerSearch search;
    memory::Hub* memory = memory::Hub::get_pull();
    uint8_t* skate = get_skate();
    memory->enter_heap_scope(prop<memory::MemSource*>(memory, 0x5D4));
    const uint8_t* msg = get_message(context);
    Hub* manager = manager_of(context);
    auto word = [msg](uint32_t offset) { return prop<uint32_t>(msg, offset); };
    manager->set_start_info_words(reinterpret_cast<const uint32_t*>(msg + 0x28));
    prop<int>(manager, 0x68) = static_cast<int8_t>(msg[0x24]); // the crown's spawn point
    prop<bool>(manager, 0x1B5) = false;
    for (uint8_t* p = start_roster_sweep(manager, &search, false); p; p = step_roster_sweep(manager, &search, false)) {
        if (!is_own_player(p)) {
            static_cast<objects::Rider*>(player_skater(p))->conceal_skater();
            static_cast<objects::Rider*>(player_skater(p))->reset_27c();
        }
    }
    get_call(code_address(&goals::MatchRules::apply_match_style), get_game_mode(), word(0));
    get_call(code_address(&goals::GoalManager::disengage_every_objective), get_call<void*>(code_address(&modules::Skate::get_objective_board), skate, 0));
    script::Array* types = script::fetch_list(manager->in_net_game() ? kNetGameTypeInfo : kMpGameTypeInfo);
    // The victory conditions.
    script::Struct* victory = new_params();
    if (word(4) == 0 && word(0) != kNetGoalAttack) {
        victory->add_component_of_type(kDefaultTimeLimit, script::kQbvalInteger, 0);
        if (word(0) != kFirefight && word(0) != kNetFirefight) {
            script::Array* conditions = script::alloc_script_array();
            conditions = conditions ? conditions->prepare_all() : nullptr;
            script::duplicate_array(conditions, script::fetch_list(kTargetScoreArray));
            auto* condition = reinterpret_cast<script::Struct*>(conditions->fetch_part(0));
            condition->add_component_of_type(kScore, script::kQbvalInteger, word(8));
            victory->append_array_value(kVictoryConditions, conditions);
        }
    } else {
        victory->add_component_of_type(kDefaultTimeLimit, script::kQbvalInteger, word(4));
        if (word(0) == kNetCtf) {
            script::Array* conditions = script::alloc_script_array();
            conditions = conditions ? conditions->prepare_all() : nullptr;
            script::duplicate_array(conditions, script::fetch_list(kHighestScoreArray));
            victory->append_array_value(kVictoryConditions, conditions);
        }
    }
    victory->add_component_of_type(kFireballLevel, script::kQbvalInteger, word(0xC));
    victory->add_component_of_type(kStopAtZero, script::kQbvalInteger, static_cast<uint32_t>(static_cast<int8_t>(msg[0x14])));
    victory->add_component_of_type(kShouldResetScores, script::kQbvalInteger,
                                static_cast<uint32_t>(static_cast<int8_t>(msg[0x15])));
    get_call(code_address(&modules::Skate::set_unknown370), skate, word(4));
    get_call(code_address(&goals::MatchRules::force_match_settings), get_game_mode(), victory);
    get_call(code_address(&goals::MatchRules::set_num_teams), get_game_mode(), word(0x18));
    get_call(code_address(&goals::MatchRules::set_goals_mode), get_game_mode(), word(0x1C));
    get_call(code_address(&goals::MatchRules::set_team_graffiti_mode), get_game_mode(), word(0x20));
    delete_params(victory);
    get_run(kStartingNewNetGame);
    get_call(code_address(&modules::Skate::launch_game), skate);
    manager->clear_roster();
    prop<bool>(manager, 0x1B4) = false;
    prop<bool>(manager, 0x3B) = false;
    manager->set_final_scores_received(false);
    manager->set_last_leader(nullptr);
    // The last leading team (+0x1D4) none: the original calls the setter folded at 0x005430D0 (PlayerRecord's
    // set_ready_query_owed, the same offset) on the manager.
    prop<int>(manager, 0x1D4) = -1;
    manager->set_game_id(msg[0x16]);
    if (word(0) == kNetGoalAttack)
        get_run(kInitSelectedGoals);
    manager->match_ended_ = false;
    // Every player's flags and score reset.
    for (uint8_t* p = start_roster_sweep(manager, &search, true); p; p = step_roster_sweep(manager, &search, true)) {
        as_player(p)->clear_flags();
        as_player(p)->clear_last_hit_time();
        if (objects::Entity* skater = player_skater(p)) {
            auto* score = reinterpret_cast<uint8_t*>(static_cast<objects::GameEntity*>(skater)->fetch_module(kSkaterScore)); // its score component
            if (prop<void*>(score, 0x18))
                get_call(code_address(&modules::PointTally::get_reset), prop<void*>(score, 0x18));
        }
    }
    if (uint8_t* king = is_crown_rule_active(manager))
        as_player(king)->give_crown(false);
    // The mode's goal script. (The original reads each type's checksum into its argument's slot, holding the type
    // info array at first: a type without one compares that - or the previous type's checksum.)
    auto checksum = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(types));
    for (int i = 0; i < static_cast<int>(types->extent_); ++i) {
        auto* type = reinterpret_cast<script::Struct*>(types->fetch_part(static_cast<uint32_t>(i)));
        type->read_hash_value(kChecksum, &checksum, 1);
        if (checksum != word(0))
            continue;
        script::Struct* params = new_params();
        params->put_int(kTime, static_cast<int>(word(4)));
        params->put_int(kScore, static_cast<int>(word(8)));
        params->put_int(kUnlimitedTime, word(4) == 0 ? 1 : 0);
        // (with no goal_script the original runs whatever its local held: the skate module's address)
        auto script = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(skate));
        type->read_hash_value(kGoalScript, &script, 1);
        get_run(script, params);
        delete_params(params);
        break;
    }
    memory::Hub::get_pull()->leave_heap_scope();
    return kContinue;
}

// 0x00532460
int __cdecl handle_send_game_state(net::InboundMsgCtx* context)
{
    net::Hub* net = net::Hub::sole();
    uint8_t* skate = get_skate();
    Hub* manager = manager_of(context);
    uint8_t* player = find_player_by_sender(manager, context->sender_conn_);
    if (!player || (prop<uint32_t>(player, 0x1D0) & 0x40))
        return kContinue;
    net::NetMessageInfo desc;
    desc.make_new();
    frame_clock::get_time(); // (not used)
    prop<uint32_t>(player, 0x1D0) |= 0x40; // told
    GameInfo info;
    std::memset(&info, 0, sizeof(info));
    info.max_players_ = static_cast<uint8_t>(manager->get_player_limit());
    info.value48 = get_call<uint32_t>(code_address(&goals::MatchRules::get_round), get_game_mode());
    info.variant = 0;
    net::AppHost* server = manager->host_;
    if (is_own_player(player)) {
        manager->set_ready_state(true); // (ready to play)
    } else {
        if (manager->has_match_ended()) {
            info.game_type_ = kNetLobby;
            info.time_limit_ = 0;
            info.value20 = 0;
            info.target_score_ = 0;
        } else {
            info.game_type_ = get_game_type();
            info.time_limit_ = get_call<uint32_t>(code_address(&goals::MatchRules::get_goal_time_left), get_game_mode()) / 1000;
            info.value20 = get_call<uint32_t>(code_address(&goals::MatchRules::get_time_limit), get_game_mode());
            info.target_score_ = static_cast<uint32_t>(manager->get_online_settings()->get_setting_value(kTargetScore, kScore));
            if (get_game_type() == kNetScavengerHunt || get_game_type() == kNetElimiskate) {
                bool scavenger = get_game_type() == kNetScavengerHunt;
                uint8_t* goals = objective_board();
                if (goal_running(get_goal(goals, scavenger ? kScavenger : kElimiskate)))
                    info.variant = 0;
                else if (goal_running(get_goal(goals, scavenger ? kScavengerInterval : kElimiskateInterval)))
                    info.variant = 1;
                else if (goal_running(get_goal(goals, scavenger ? kScavengerCollect : kElimiskateBreak)))
                    info.variant = 2;
            }
        }
        info.game_id_ = manager->get_game_id();
        info.level = manager->fetch_stage();
        info.teams = get_call<uint32_t>(code_address(&goals::MatchRules::current_num_teams), get_game_mode());
        info.value18 = manager->get_goals_preference();
        info.value_1c_ = manager->get_team_graffiti_mode();
        info.pro_option_bits_ = prop<uint8_t>(manager, 0x194);
        info.crown_start_slot_ = prop<uint8_t>(manager, 0x68);
        std::memcpy(info.start_info_, &prop<uint32_t>(manager, 0x48), sizeof(info.start_info_));
        info.not_lan_ = net->get_net_link_kind() != 1;
        desc.content_ = &info;
        desc.byte_count_ = sizeof(info);
        desc.ident_ = kMsgSessionBrief;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        server->put_on_outbox(player_handle(player), &desc);
        if (manager->has_created_goals()) {
            manager->prepare_created_goals(manager->fetch_stage());
            void* goals = manager->get_created_goals_message();
            auto size = static_cast<uint16_t>(manager->get_created_goals_size());
            server->send_bulk_message(player_handle(player), kMsgGoalsData, size, goals, "goals data", kGroupGame, false,
                                  true);
        }
        // The scripts spawned so far (the list at +0x150): 0x49 each.
        auto* node = prop<memory::ListNode*>(manager, 0x150);
        node = node->data == reinterpret_cast<void*>(-1) ? nullptr : node;
        for (auto* item = node ? static_cast<uint8_t*>(node->data) : nullptr; item;) {
            desc.make_new();
            uint32_t spawned[4] = {prop<uint32_t>(item, 0x1C), prop<uint32_t>(item, 0x18), prop<uint32_t>(item, 0x14),
                                   0};
            desc.content_ = spawned;
            desc.byte_count_ = sizeof(spawned);
            desc.ident_ = kMsgSpawnedScript;
            desc.queue_ = net::NetMessageInfo::kLaneOrdered;
            desc.group_id_ = kGroupGame;
            server->put_on_outbox(player_handle(player), &desc);
            node = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            if (!node)
                break;
            item = static_cast<uint8_t*>(node->data);
        }
        if (!is_own_player(player))
            get_call(code_address(&modules::Skate::send_cheats), skate, player);
        if (is_observer(player) && get_game_type() == kNetGraffiti) {
            // An observer of graffiti: the trick objects' state.
            desc.make_new();
            static uint8_t buffer[0x1100]; // (the original's is on its stack)
            int size = objects::g_trick_spot_hub->write_graffiti_state(buffer);
            server->send_bulk_message(player_handle(player), kMsgTrickObjects, static_cast<uint16_t>(size), buffer,
                                  "TrickObj Buffer", kGroupGame, true, false);
        }
        // The goals chosen (goal attack): a byte, then the selected goals' names, then 0.
        desc.make_new();
        uint8_t* goals = objective_board();
        static uint8_t selected[0x1000];
        selected[0] = 0;
        uint8_t* out = selected + 1;
        int count = prop<int>(goals, 0); // (its first word, read by the getter folded at 0x00469490)
        for (int i = 0; i < count; ++i) {
            uint8_t* goal = get_call<uint8_t*>(code_address(&goals::GoalManager::get_objective_at), goals, i);
            if (get_call<bool>(code_address(&goals::Objective::is_picked), goal)) {
                uint32_t name = get_call<uint32_t>(code_address(&goals::Objective::get_objective_key), goal);
                std::memcpy(out, &name, 4);
                out += 4;
            }
        }
        std::memset(out, 0, 4);
        desc.content_ = selected;
        desc.ident_ = kMsgSelectedGoals;
        desc.byte_count_ = static_cast<uint16_t>(out - selected + 4);
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        server->put_on_outbox(player_handle(player), &desc);
        if (uint8_t* king = is_crown_rule_active(manager)) {
            desc.make_new();
            uint8_t id = static_cast<uint8_t>(get_skater_id(player_skater(king)));
            desc.content_ = &id;
            desc.byte_count_ = 1;
            desc.ident_ = kMsgKing;
            desc.queue_ = net::NetMessageInfo::kLaneOrdered;
            desc.group_id_ = kGroupGame;
            server->put_on_outbox(player_handle(player), &desc);
        }
    }
    // The end of the game's state (0x42).
    net::NetMessageInfo done;
    done.make_new();
    done.ident_ = kMsgGameStateSent;
    done.queue_ = net::NetMessageInfo::kLaneOrdered;
    done.group_id_ = kGroupGame;
    server->put_on_outbox(player_handle(player), &done);
    if (manager->in_net_game()) {
        manager->send_faces_to(player);
        manager->send_tag_textures_to(player);
    }
    return kContinue;
}

} // namespace online
