// The network manager's script commands, second part (0x00538050-0x00539570): capture the flag's flags, the crown,
// starting and ending a network game, teams, the host, readiness, and the player list menu with its options dialog.

#include <cstdint>
#include "engine/core/code_address.h"
#include <cstring>

#include "engine/core/manager.h"
#include "engine/net/gamespy.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/maths/vec4.h"
#include "engine/net/buddy_man.h"
#include "engine/net/buddy_status.h"
#include "engine/net/gamenet_commands.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/player_info.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/assets/resource_registry.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/stats_man.h"
#include "engine/sys/mlp.h"

namespace online {

using namespace detail;
using script::Script;
using script::Struct;

uint8_t g_game_reported;

namespace {

// Messages.
constexpr uint8_t kMsgGameOver = 0x5A;
constexpr uint8_t kMsgEndGameRequest = 0x65;
constexpr uint8_t kMsgNumTeamsRequest = 0x66;
constexpr uint8_t kMsgJoinTeamRequest = 0x67;
constexpr uint8_t kMsgTeamChange = 0x68;
constexpr uint8_t kMsgNumTeams = 0x69;
constexpr uint8_t kMsgGameEnded = 0x6A;
constexpr uint8_t kMsgSelectedGoals = 0x6D;
constexpr uint8_t kMsgTeamGame = 0x7A;

constexpr uint32_t kFlagsServer = 1; // the manager's flags
constexpr uint32_t kFlagsInternet = 4;
constexpr uint32_t kRemoveMark = 0x400; // a player's flags: being removed (its options dialog is up)

// Checksums.
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;             // netgoalattack
constexpr uint32_t kAutoStarting = 0x65C14F78;              // net_message_auto_starting_game
constexpr uint32_t kStarting = 0x831E8336;                  // net_message_starting_game
constexpr uint32_t kDoBackendRetry = 0x1D02CE3B;            // do_backend_retry
constexpr uint32_t kPrepareSkaterForMove = 0xF1B110ED;      // PrepareSkaterForMove
constexpr uint32_t kBlue = 0x61C9354B;                      // blue
constexpr uint32_t kRed = 0x059EA070;                       // red
constexpr uint32_t kGreen = 0x2F6511DE;                     // green
constexpr uint32_t kIndex = 0x7F8C98FE;                     // index
constexpr uint32_t kName = 0xA1DC81F9;                      // name
constexpr uint32_t kProfile = 0x7EA855F0;                   // profile
constexpr uint32_t kNick = 0xD6F4D0C8;                      // nick
constexpr uint32_t kAllowAddHomie = 0xBA3F599F;             // allow_add_homie
constexpr uint32_t kAllowRemoveHomie = 0x5CBA31EC;          // allow_remove_homie
constexpr uint32_t kCreatePlayerOptionsDialog = 0xF95F6E22; // create_player_options_dialog
constexpr uint32_t kText = 0xC4745838;                      // text
constexpr uint32_t kCentered = 0x2A434D05;                  // centered
constexpr uint32_t kId = 0x40C698AF;                        // id
constexpr uint32_t kPadChooseScript = 0x2B0CFCA1;           // pad_choose_script
constexpr uint32_t kChooseSelectedPlayer = 0x405A36F9;      // choose_selected_player
constexpr uint32_t kRgba = 0x3F6BCDBA;                      // rgba
constexpr uint32_t kUnfocusParams = 0xF974BF7C;             // unfocus_params
constexpr uint32_t kPadChooseParams = 0xB8437568;           // pad_choose_params
constexpr uint32_t kThemeMenuAddItem = 0xC16BF7BD;          // theme_menu_add_item
constexpr uint32_t kCrownSpawnType = 0xAF86421B; // (a node's Type: the crown's starting point; the name isn't known)

const char* const kTeam = "team";                     // "team"
const char* const kNetStatusProps = "netstatusprops"; // "netstatusprops"
const char* const kType = "Type";                     // "Type"
const char* const kBit = "bit";                       // "bit"
const char* const kParamId = "param_id";              // "param_id"

constexpr int kPlayerMenuIdBase = 123456;   // the menu items' ids: this plus the player's place in the list
constexpr uint32_t kJoinTeamInterval = 150; // ms between team requests
constexpr int kMaxCrownSpawns = 10;

Hub* net_link() { return g_online_hub; }
tasks::PileUp* get_logic_tasks() { return (&assets::g_main_loop_manager->logic_tasks_); }

uint32_t g_last_team_request;

uint32_t& last_team_request() { return g_last_team_request; }
uint32_t& net_game_start_time() { return g_net_game_start_time; }
uint8_t& game_reported() { return g_game_reported; } // GameSpy told a game is on

bool is_head(const memory::ListNode* node) { return reinterpret_cast<intptr_t>(node->data) == -1; }
memory::ListNode* get_next_node(const memory::ListNode* node) { return is_head(node->next) ? nullptr : node->next; }
PlayerRecord* data_of(const memory::ListNode* node) { return node ? static_cast<PlayerRecord*>(node->data) : nullptr; }
memory::ListNode* first_player_node(Hub* manager) { return get_next_node(&manager->players_); }

enum class Recipients { kAll, kRemote, kObservers };
// `desc` through the server to the players (all, those not local, the observers).
void send_to_players(Hub* manager, net::AppHost* server, net::NetMessageInfo* desc, Recipients recipients)
{
    for (memory::ListNode* node = first_player_node(manager); PlayerRecord* player = data_of(node);
         node = get_next_node(node)) {
        if (recipients == Recipients::kRemote && is_own_player(player))
            continue;
        if (recipients == Recipients::kObservers && !is_observer(player))
            continue;
        server->put_on_outbox(player_handle(player), desc);
    }
}
void init_desc(net::NetMessageInfo& desc, uint8_t id)
{
    desc = net::NetMessageInfo();
    desc.ident_ = id;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
}

// The lobby's GameSpy peer told the game's state changed (connected and hosting), and GameSpy's reporting told the
// game is on / off (once, as `on` changes).
void report_game_state(Hub* manager, bool on)
{
    auto* lobby = manager->lobby_service_.get();
    void* peer = lobby->get_lobby_handle();
    if (peer && game_spy::peer_is_connected(peer) && lobby->is_hosting())
        game_spy::peer_state_changed(peer);
    // (the byte compared with 1 when starting, tested for non-zero when ending, as in the original)
    uint8_t& reported = game_reported();
    if (on ? reported == 1 : reported == 0)
        return;
    reported = on ? 1 : 0;
    if (g_online_active && g_online_hub->in_net_game() && g_query_reporting)
        game_spy::qr2_send_state_changed(nullptr);
}

// The GameSpy presence (+0x18): logged in (its flag at +0x10, through the getter folded at 0x005480B0), `profile` a
// buddy (0x00549CB0).
bool mark_friend_online(Hub* manager) { return manager->presence_service_->authenticated_; }
bool is_buddy(Hub* manager, int profile) { return manager->presence_service_->is_buddy(profile); }

} // namespace

// 0x00538050: TeamFlagTaken team=<n> - a player (no observers) carries that team's flag (0x005433D0).
bool cfunc_team_flag_taken(Struct* params, Script*)
{
    Hub* manager = net_link();
    PlayerSearch search;
    int team = 0; // (uninitialised in the original)
    params->fetch_int_value(kTeam, &team, 0);
    for (PlayerRecord* player = manager->begin_player_walk(&search, false); player;
         player = manager->step_player_walk(&search, false)) {
        if (player->get_flag_team() == team)
            return true;
    }
    return false;
}

// 0x00538170: SpawnCrown - the crown made (+0x6C), placed at a node of the crown's type (the manager's random +0x68
// modulo their count; at most 10 counted, no bounds check) and worn by the king of the hill if there is one.
bool cfunc_spawn_crown(Struct*, Script*)
{
    Hub* manager = net_link();
    auto* crown = new objects::CrownObject;
    int spawn = manager->crown_start_;
    manager->crown_ = crown;
    uint32_t names[kMaxCrownSpawns];
    Struct* nodes[kMaxCrownSpawns];
    int count = 0;
    level_nodes::start_node_walk(0);
    for (Struct* node = level_nodes::get_next_node(); node; node = level_nodes::get_next_node()) {
        uint32_t type = 0;
        node->read_hash_value(kType, &type, 0);
        if (type == kCrownSpawnType) {
            names[count] = level_nodes::get_node_name(node); // (kept, never used)
            nodes[count] = node;
            ++count;
        }
    }
    level_nodes::end_node_walk();
    (void)names;
    Struct* node = nodes[spawn % count]; // (no node of the type divides by zero, as in the original)
    maths::Vec4 position;
    level_nodes::fetch_place(node, &position);
    manager->crown_->init_from_node(objects::g_composite_registry, node);
    if (auto* king = manager->get_hill_ruler())
        manager->crown_->put_on(reinterpret_cast<objects::GameEntity*>(player_skater(king)));
    return true;
}

// 0x00538280: StartNetworkGame - GameSpy told; unless the game start or level change is under way: the game pending,
// its start time (now, or 3 seconds ahead in a network game - 8 for goal attack), the start task added; in a network
// game the players told (the panel message; 0x7A in a team game; for goal attack the selected goals, 0x6D).
bool cfunc_start_network_game(Struct*, Script*)
{
    Hub* manager = net_link();
    tasks::PileUp* tasks = get_logic_tasks();
    report_game_state(manager, true);
    if (manager->start_game_job_->is_on_task_list() || manager->level_change_job_->is_on_task_list())
        return true;
    manager->match_being_set_up_ = true;
    if (!manager->in_net_game())
        net_game_start_time() = frame_clock::get_time();
    else if (manager->get_game_type_preference() == kNetGoalAttack)
        net_game_start_time() = frame_clock::get_time() + 8000;
    else
        net_game_start_time() = frame_clock::get_time() + 3000;
    tasks->queue_job(manager->start_game_job_.get());
    if (manager->in_net_game()) {
        uint32_t text = manager->host_mode_ == 1 ? kAutoStarting : kStarting;
        manager->build_net_notice(true, text, manager->get_game_type_name(), nullptr, nullptr, kNetStatusProps, true,
                                  3000);
        if (is_squad_match()) {
            net::NetMessageInfo desc;
            net::AppHost* server = manager->host_;
            init_desc(desc, kMsgTeamGame);
            send_to_players(manager, server, &desc, Recipients::kAll);
        }
    }
    if (manager->in_net_game() && manager->get_game_type_preference() == kNetGoalAttack) {
        // The selected goals: a byte 1, then each selected goal's id (each told first - its slot 29), then 0.
        uint8_t buffer[0x1000];
        net::NetMessageInfo desc;
        uint8_t* goals = objective_board();
        net::AppHost* server = manager->host_;
        buffer[0] = 1;
        uint8_t* end = buffer + 1;
        int count = reinterpret_cast<goals::GoalManager*>(goals)
                        ->goals_.num_; // (its first word, read by the getter folded at 0x00469490)
        for (int i = 0; i < count; ++i) {
            uint8_t* goal = get_call<uint8_t*>(code_address(&goals::GoalManager::get_objective_at), goals, i);
            if (reinterpret_cast<goals::Objective*>(goal)->is_picked()) {
                reinterpret_cast<goals::Objective*>(goal)->un_beat(); // (the original's vtable slot 29)
                uint32_t id = reinterpret_cast<goals::Objective*>(goal)->get_objective_key();
                std::memcpy(end, &id, 4);
                end += 4;
            }
        }
        std::memset(end, 0, 4);
        desc.content_ = buffer;
        desc.ident_ = kMsgSelectedGoals;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        desc.byte_count_ = static_cast<uint16_t>(static_cast<uint16_t>(end - buffer) + 4);
        send_to_players(manager, server, &desc, Recipients::kRemote);
    }
    return true;
}

// 0x00538620: SendGameOverToObservers - on the server, game over (0x5A) to every observer.
bool cfunc_send_game_over_to_observers(Struct*, Script*)
{
    Hub* manager = net_link();
    if (!(manager->flag_bits_ & kFlagsServer))
        return true;
    net::NetMessageInfo desc;
    init_desc(desc, kMsgGameOver);
    send_to_players(manager, manager->host_, &desc, Recipients::kObservers);
    return true;
}

// 0x00538720: EndNetworkGame - GameSpy told; on the server do_backend_retry and "game ended" (0x6A) to the remote
// players, on a client the request to end it (0x65) to the server.
bool cfunc_end_network_game(Struct*, Script*)
{
    Hub* manager = net_link();
    report_game_state(manager, false);
    net::NetMessageInfo desc;
    if (manager->flag_bits_ & kFlagsServer) {
        desc = net::NetMessageInfo();
        net::AppHost* server = manager->host_;
        get_run(kDoBackendRetry);
        desc.ident_ = kMsgGameEnded;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        send_to_players(manager, server, &desc, Recipients::kRemote);
    } else {
        init_desc(desc, kMsgEndGameRequest);
        send_to_server(manager->client_app_, &desc);
    }
    return true;
}

// 0x005388C0: GetMyTeam - the local player's team into the script's team.
bool cfunc_get_my_team(Struct*, Script* script)
{
    if (PlayerRecord* local = net_link()->get_own_player())
        script->args_->put_int(kTeam, player_team(local));
    return true;
}

// 0x005388F0: SetNumTeams <n> - on a client the request (0x66) to the server; on the server the game mode's team
// count set (a local skater put back to be moved when teams start now - PrepareSkaterForMove, 0x00566370), then (with a
// server app) the count to the remote players (0x69) and every player of a team no longer there (no observers) moved
// to team 0 - each told to the remote players (0x68: its skater's id, 0).
bool cfunc_set_num_teams(Struct* params, Script*)
{
    Hub* manager = net_link();
    uint8_t* skate = detail::get_skate();
    PlayerSearch search;
    net::NetMessageInfo desc;
    int teams = 0;
    params->fetch_int_value(0u, &teams, 0);
    if (!(manager->flag_bits_ & kFlagsServer)) {
        net::NetMessageInfo request;
        uint8_t count = static_cast<uint8_t>(teams);
        request.content_ = &count;
        request.byte_count_ = 1;
        request.ident_ = kMsgNumTeamsRequest;
        request.queue_ = net::NetMessageInfo::kLaneOrdered;
        request.group_id_ = kGroupGame;
        send_to_server(manager->client_app_, &request);
        return true;
    }
    if (teams < 1 || is_squad_match()) {
        get_call(code_address(&goals::MatchRules::set_num_teams), get_game_mode(), teams);
    } else {
        get_call(code_address(&goals::MatchRules::set_num_teams), get_game_mode(), teams);
        if (reinterpret_cast<modules::Skate*>(skate)->get_home_rider()) { // the local skater
            get_run(kPrepareSkaterForMove, nullptr, reinterpret_cast<modules::Skate*>(skate)->get_home_rider());
            get_call(code_address(&modules::Skate::teleport_to_spawn), skate,
                     reinterpret_cast<modules::Skate*>(skate)->get_home_rider(), 0, 0);
        }
    }
    net::AppHost* server = manager->host_;
    if (!server)
        return true;
    uint8_t count = static_cast<uint8_t>(teams);
    desc.content_ = &count;
    desc.byte_count_ = 1;
    desc.ident_ = kMsgNumTeams;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    send_to_players(manager, server, &desc, Recipients::kRemote);
    uint8_t change[2];
    desc.content_ = change;
    desc.byte_count_ = 2;
    desc.ident_ = kMsgTeamChange;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    for (auto* player = manager->begin_player_walk(&search, false); player;
         player = manager->step_player_walk(&search, false)) {
        if (player_team(player) < teams)
            continue;
        change[1] = 0;
        change[0] = static_cast<uint8_t>(get_skater_id(player_skater(player)));
        send_to_players(manager, server, &desc, Recipients::kRemote);
        player->team_ = 0;
    }
    return true;
}

// 0x00538C90: JoinTeam <blue | red | green | other> - the local player's skater's request (0x67: its id, the team
// 1 / 0 / 2 / 3) to the server, at most every 150 ms. False without a local player with a skater.
bool cfunc_join_team(Struct* params, Script*)
{
    Hub* manager = net_link();
    net::NetMessageInfo desc;
    auto* local = manager->get_own_player();
    if (!local || !player_skater(local))
        return false;
    uint32_t color = 0; // (uninitialised in the original: without one, its slot decides - most likely team 3)
    params->read_hash_value(0u, &color, 0);
    uint8_t request[2];
    request[1] = color == kBlue ? 1 : color == kRed ? 0 : color == kGreen ? 2 : 3;
    request[0] = static_cast<uint8_t>(get_skater_id(player_skater(local)));
    net::AppHost* client = manager->client_app_;
    if (frame_clock::get_time() - last_team_request() >= kJoinTeamInterval) {
        last_team_request() = frame_clock::get_time();
        desc.content_ = request;
        desc.byte_count_ = 2;
        desc.ident_ = kMsgJoinTeamRequest;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        send_to_server(client, &desc);
    }
    return true;
}

// 0x00538D70: IsHost - in a network game: on the server, host mode 0 (serving); on a client, the local player is the
// server's.
bool cfunc_is_host(Struct*, Script*)
{
    Hub* manager = net_link();
    if (!manager->in_net_game())
        return false;
    if (manager->flag_bits_ & kFlagsServer)
        return manager->host_mode_ == 0;
    void* local = manager->get_own_player();
    return local && static_cast<PlayerRecord*>(local)->is_host_player() != 0;
}

// 0x00538DC0: AllPlayersAreReady - on the server, every player's connection is ready (+0x60 bit 0); true elsewhere.
bool cfunc_all_players_are_ready(Struct*, Script*)
{
    Hub* manager = net_link();
    if (!(manager->flag_bits_ & kFlagsServer))
        return true;
    for (memory::ListNode* node = first_player_node(manager); PlayerRecord* player = data_of(node);
         node = get_next_node(node)) {
        if (!(static_cast<uint8_t>(player->conn_->cur_standing_) & 1))
            return false;
    }
    return true;
}

// 0x00538E70: CreatePlayerOptions index=<n> name=<string> - the index'th player (observers too), if its name is
// `name` (case aside): marked (flags 0x400) and create_player_options_dialog {[profile, nick, allow_add_homie /
// allow_remove_homie - on the internet, logged in to GameSpy, the player has a profile], name}.
bool cfunc_create_player_options(Struct* params, Script*)
{
    Hub* manager = net_link();
    int index = 0;
    const char* name = nullptr;
    params->fetch_int_value(kIndex, &index, 0);
    params->fetch_text(kName, &name, 0);
    memory::ListNode* node = first_player_node(manager);
    PlayerRecord* player = data_of(node);
    if (!player)
        return true;
    while (index != 0) {
        --index;
        node = get_next_node(node);
        player = data_of(node);
        if (!player)
            return true;
    }
    if (_stricmp(player_name(player), name) != 0)
        return true;
    player->flag_bits_ |= kRemoveMark;
    Struct* dialog = new_params();
    int profile = player->profile_id_;
    if ((manager->flag_bits_ & kFlagsInternet) && profile && mark_friend_online(manager)) {
        dialog->put_int(kProfile, player->profile_id_);
        dialog->append_word(kNick, player_name(player));
        dialog->put_name_key(0u, is_buddy(manager, player->profile_id_) ? kAllowRemoveHomie : kAllowAddHomie);
    }
    dialog->append_word(kName, script::kQbvalString, name);
    get_run(kCreatePlayerOptionsDialog, dialog);
    delete_params(dialog);
    return true;
}

// 0x00539080: FillPlayerListMenu - (in the front end's heap) a menu item for every player but the local one:
// theme_menu_add_item {text = its name, centered, id = 123456 + its place in the list, pad_choose_script =
// choose_selected_player, rgba (grey; a buddy's red channel 0... - see below), unfocus_params {rgba},
// pad_choose_params {index, name}}.
bool cfunc_fill_player_list_menu(Struct*, Script*)
{
    Hub* manager = net_link();
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->front_end_heap_);
    int index = 0;
    for (memory::ListNode* node = first_player_node(manager); PlayerRecord* player = data_of(node);
         node = get_next_node(node)) {
        if (is_own_player(player)) {
            ++index;
            continue;
        }
        // The colour: grey (128, 128, 128, 128); a GameSpy buddy's (128, 128, 0, 128); another logged-in player's
        // with a profile (0, 128, 0, 128).
        script::Array* array_memory = script::alloc_script_array();
        script::Array* rgba = array_memory ? new (array_memory) script::Array : nullptr;
        rgba->configure_size_kind(4, script::kQbvalInteger);
        uint32_t blue;
        int profile = player->profile_id_;
        if (profile && mark_friend_online(manager)) {
            if (is_buddy(manager, player->profile_id_)) {
                rgba->set_element(0, 0x80);
                rgba->set_element(1, 0x80);
            } else {
                rgba->set_element(0, 0);
                rgba->set_element(1, 0x80);
            }
            blue = 0;
        } else {
            rgba->set_element(0, 0x80);
            rgba->set_element(1, 0x80);
            blue = 0x80;
        }
        rgba->set_element(2, blue);
        rgba->set_element(3, 0x80);
        Struct* item = new_params();
        item->append_word(kText, player_name(player));
        item->put_name_key(kCentered, kCentered);
        item->put_name_key(kId, static_cast<uint32_t>(index + kPlayerMenuIdBase));
        item->put_name_key(kPadChooseScript, kChooseSelectedPlayer);
        Struct* unfocus = new_params();
        unfocus->append_array_value(kRgba, rgba);
        item->append_array_value(kRgba, rgba);
        item->put_sub_struct(kUnfocusParams, unfocus);
        Struct* choose = new_params();
        choose->put_int(kIndex, index);
        choose->append_word(kName, player_name(player));
        item->put_sub_struct(kPadChooseParams, choose);
        get_run(kThemeMenuAddItem, item);
        delete_params(item);
        delete_params(choose);
        delete_params(unfocus);
        if (rgba) {
            rgba->get_clear();
            script::delete_array(rgba);
        }
        ++index;
    }
    memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x00539470: RemovePlayer - a jump to CreatePlayerOptions (the same command under a second name).
bool cfunc_remove_player(Struct* params, Script* script) { return cfunc_create_player_options(params, script); }

// 0x00539480: CancelRemovePlayer - every player's (no observers) removal mark (flags 0x400) cleared.
bool cfunc_cancel_remove_player(Struct*, Script*)
{
    Hub* manager = net_link();
    PlayerSearch search;
    for (auto* player = manager->begin_player_walk(&search, false); player;
         player = manager->step_player_walk(&search, false))
        player->flag_bits_ &= ~kRemoveMark;
    return true;
}

// 0x00539570: ToggleProSet bit=<n> param_id=<checksum> - Hub::ToggleProSet.
bool cfunc_toggle_pro_set(Struct* params, Script*)
{
    Hub* manager = net_link();
    int bit;
    uint32_t id;
    params->fetch_int_value(kBit, &bit, 1);
    params->read_hash_value(kParamId, &id, 1);
    manager->toggle_pro_set(static_cast<uint8_t>(bit), id);
    return true;
}

} // namespace online
