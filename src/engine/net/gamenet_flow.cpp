// The network game's course (online::Hub): back to the lobby, a game started (message 0x34), a join cancelled,
// and the manager's tasks for the observer, auto-serve, the join's state and the server's start of a game
// (0x0053AF90, 0x0053B0D0, 0x0053BED0-0x0053C320, 0x0053C6E0). See gamenet_manager.h.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/stubs.h"
#include "engine/core/task_list.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "engine/maths/random.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_commands.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_observer.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_lists.h"
#include "engine/net/stats_man.h"
#include "engine/objects/smtptr.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
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
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/sys/mlp.h"

namespace online {

using namespace detail;

namespace {

constexpr uint8_t kMsgStartGame = 0x34;
constexpr uint32_t kRootWindow = 0x56A1EAE3; // root_window (a screen element)
constexpr uint32_t kMenuState = 0xCDC00E63;  // menu_state (a tag)
constexpr uint32_t kOn = 0xF649D637;         // on

// The root window (its smart pointer let go at once: the raw pointer kept, as the original).
objects::Entity* find_root_window()
{
    frontend::ElementPtr found;
    return frontend::UiTree::sole()->fetch_part(&found, kRootWindow, 0)->owner_object_;
}
constexpr uint8_t kMsgDisconnect = 0x0B; // (the client leaves the server)

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;       // netlobby
constexpr uint32_t kLevel = 0x651533EC;          // level (the network preference)
constexpr uint32_t kGameType = 0x9834C4FA;       // game_type
constexpr uint32_t kChecksum = 0x21902065;       // checksum
constexpr uint32_t kTime = 0x906B67BA;           // time
constexpr uint32_t kHorseTimeLimit = 0xC146142E; // horse_time_limit
constexpr uint32_t kTimeLimit = 0xC51BF6E3;      // time_limit
constexpr uint32_t kTargetScore = 0x71CD53A2;    // target_score
constexpr uint32_t kScore = 0xCD66C8AE;          // score
constexpr uint32_t kCtfGameType = 0x15421B73;    // ctf_game_type
constexpr uint32_t kTimedCtf = 0x938AB24B;       // timed_ctf
constexpr uint32_t kScoreReset = 0x162E8C3A;     // score_reset
constexpr uint32_t kBooleanTrue = 0xF81BC89B;    // boolean_true
constexpr uint32_t kGoals = 0x38DBE1D0;          // goals (the created goals chosen)
constexpr uint32_t kHorse = 0x9D65D0E7;          // horse
constexpr uint32_t kNetCtf = 0x6C5FF266;         // netctf
// Game types without a time limit (0 sent): netgoalattack, king, netking, netscorechallenge, scorechallenge, firefight,
// netfirefight.
constexpr uint32_t kUntimedTypes[] = {0xEC200EAA, 0x5D32129C, 0x6EF8FDA0, 0x1498240A,
                                      0xF135ECB6, 0x3D6D444F, 0xBFF33600};

// Scripts.
constexpr uint32_t kLoadPendingPlayers = 0x895819F7;         // LoadPendingPlayers
constexpr uint32_t kStartNetworkGame = 0x6CD351C5;           // StartNetworkGame
constexpr uint32_t kCreateConnectingDialog = 0xA107181A;     // CreateConnectingDialog
constexpr uint32_t kCreateJoiningDialog = 0xB0637172;        // CreateJoiningDialog
constexpr uint32_t kCreateTryingPasswordDialog = 0x4E2ED110; // CreateTryingPasswordDialog
constexpr uint32_t kEnteredNetworkGame = 0xFF898BE6;         // entered_network_game
constexpr uint32_t kDialogBoxExit = 0x666B6A9D;              // dialog_box_exit
constexpr uint32_t kCreateJoinRefusedDialog = 0x92528E98;    // CreateJoinRefusedDialog
constexpr uint32_t kReason = 0xC44777F3;                     // reason
constexpr uint32_t kCurrentMenuAnchor = 0xF53D1D83;          // current_menu_anchor
constexpr uint32_t kCreateWaitForPlayersDialog = 0xEF32B8DE; // CreateWaitForPlayersDialog
constexpr uint32_t kGameWillStart = 0x9DB2FD76;              // net_message_game_will_start
constexpr uint32_t kHomieStatusChatting = 4;                 // (the presence status while in the lobby)

constexpr uint32_t kLinkLostDialogAnchor = 0xE8E406BC;              // link_lost_dialog_anchor
constexpr uint32_t kDialogBoxAnchor = 0x3B56E746;                   // dialog_box_anchor
constexpr uint32_t kCreateLinkUnpluggedDialog = 0x751DE012;         // create_link_unplugged_dialog
constexpr uint32_t kCreateLinkUnpluggedFrontEndDialog = 0xCFA1CF26; // create_link_unplugged_front_end_dialog
constexpr uint32_t kShowJoinTimeoutNotice = 0x3F7009B4;             // ShowJoinTimeoutNotice
constexpr uint32_t kCreateLostConnectionDialog = 0xDA9AFFBF;        // CreateLostConnectionDialog
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;                      // (the skate module's next level: the main menu)
constexpr uint32_t kLinkCheckInterval = 1000;
constexpr uint32_t kSilentTimeout = 20000;  // ms without a word from a connection in the game
constexpr uint32_t kJoiningTimeout = 60000; // ... from one still joining
constexpr int kMaxResends = 20;
constexpr int kDropTimedOut = 1;
constexpr int kDropDropped = 5;

constexpr uint32_t kAutoServeWait = 2000; // ms after the host mode is set
constexpr uint32_t kAutoServeIdle = 5000; // ms in the lobby
constexpr int kPanelTime = 2000;

// PlayerRecord flags (+0x1D0).
constexpr uint32_t kPlayerCaptureBlocked = 0x8000;
// JoinerRecord flags (+0x20).
constexpr uint32_t kNewPlayerLocal = 1;
constexpr uint32_t kNewPlayerObserver = 4;

// The start of a game (message 0x34, 0x48 bytes; read by the client's handle_start_game 0x005313C0).
struct StartGameInfo {
    uint32_t game_type_;      // +0x00
    uint32_t time_limit_;     // +0x04 (0: none)
    uint32_t target_score_;   // +0x08
    uint32_t fireball_level_; // +0x0C
    uint32_t unused10;        // +0x10 (never written: the stack's)
    bool stop_at_zero_;       // +0x14 is_untimed_game
    bool reset_scores_;       // +0x15
    uint8_t game_id_;         // +0x16 (1-255, not the last game's)
    uint8_t unused17;
    uint32_t teams;           // +0x18
    uint32_t goals;           // +0x1C the goals preference's checksum
    uint32_t value20;         // +0x20 (0x00535AD0)
    int8_t crown_start_slot_; // +0x24
    uint8_t unused25[3];
    uint32_t start_info_[8]; // +0x28 the manager's start info words (+0x48)
};
static_assert(sizeof(StartGameInfo) == 0x48 && offsetof(StartGameInfo, stop_at_zero_) == 0x14 &&
              offsetof(StartGameInfo, teams) == 0x18 && offsetof(StartGameInfo, start_info_) == 0x28);

tasks::PileUp* get_logic_tasks() { return (&assets::g_main_loop_manager->logic_tasks_); }
prefs::Preferences* net_game_options(Hub* m) { return reinterpret_cast<prefs::Preferences*>(m->net_options_); }
// The preferences of the game being set up: the network game's, else the skate module's (split screen, 0x00564D80).
prefs::Preferences* game_preferences(Hub* m)
{
    return m->in_net_game()
               ? net_game_options(m)
               : get_call<prefs::Preferences*>(code_address(&modules::Skate::get_split_view_settings), get_skate());
}
uint32_t preference_checksum(prefs::Preferences* prefs, uint32_t name, uint32_t field)
{
    uint32_t value;
    prefs->fetch_setting(name)->read_hash_value(field, &value, 1);
    return value;
}
Hub* task_manager(tasks::Task<Hub>* task) { return task->content_; }

} // namespace

// 0x0053AF90
void Hub::reset_to_lobby()
{
    int teams;
    if (get_on_server()) {
        level_ = preference_checksum(net_game_options(this), kLevel, kChecksum);
        teams = get_num_teams();
    } else {
        teams = get_call<int>(code_address(&goals::MatchRules::current_num_teams), get_game_mode());
    }
    game_type_ = kNetLobby;
    get_call(code_address(&goals::MatchRules::apply_match_style), get_game_mode(), kNetLobby); // the game mode
    get_call(code_address(&goals::MatchRules::set_num_teams), get_game_mode(), teams);
    match_ended_ = false;
    clear_roster();
    if (PlayerRecord* king = is_crown_rule_active(this))
        king->give_crown(false);
    PlayerSearch search;
    for (PlayerRecord* p = start_roster_sweep(this, &search, true); p; p = step_roster_sweep(this, &search, true))
        p->clear_flags(); // carries no flag
    clear_queued_scripts();
    lobby_since_ = frame_clock::get_time(); // (back in the lobby: auto-serve waits from here)
}

uint32_t g_auto_serve_start;
bool g_waiting_shown;
uint32_t g_auto_serve_checked;
bool g_cheat_flag;

// 0x0053B0D0
void Hub::start_network_game()
{
    PlayerSearch search;
    net::NetMessageInfo desc;
    desc.make_new();
    prefs::Preferences* prefs = game_preferences(this);
    net::AppHost* server = host_;
    remove_all_new_players();
    game_type_ = preference_checksum(game_preferences(this), kGameType, kChecksum);
    choose_start_points(); // the skaters' starting points shuffled
    std::memset(team_objects_, 0, sizeof(team_objects_));
    uint32_t game_type = game_type_;
    uint32_t time_limit = prefs->get_setting_value(game_type == kHorse ? kHorseTimeLimit : kTimeLimit, kTime);
    level_ = preference_checksum(net_game_options(this), kLevel, kChecksum);
    StartGameInfo info;
    info.game_type_ = game_type_;
    info.fireball_level_ = static_cast<uint32_t>(get_fireball_level());
    bool untimed = false;
    for (uint32_t type : kUntimedTypes)
        untimed = untimed || info.game_type_ == type;
    if (untimed)
        info.time_limit_ = 0;
    else if (info.game_type_ == kNetCtf) // (capture the flag: only the timed kind)
        info.time_limit_ = prefs->get_preference_checksum(kCtfGameType, kChecksum) == kTimedCtf ? time_limit : 0;
    else
        info.time_limit_ = time_limit;
    info.reset_scores_ = prefs->get_preference_checksum(kScoreReset, kChecksum) == kBooleanTrue;
    info.stop_at_zero_ = is_untimed_game();
    info.target_score_ = prefs->get_setting_value(kTargetScore, kScore);
    info.teams = static_cast<uint32_t>(get_num_teams());
    uint32_t value = 0;
    game_preferences(this)->fetch_setting(kGoals)->read_hash_value("checksum", &value, 1);
    info.goals = value;
    info.value20 = get_team_graffiti_mode();
    do
        info.game_id_ = static_cast<uint8_t>(maths::rnd(0xFF) + 1);
    while (info.game_id_ == ended_game_id_);
    info.crown_start_slot_ = static_cast<int8_t>(crown_start_);
    std::memcpy(info.start_info_, start_points_, sizeof(info.start_info_));
    desc.content_ = &info;
    desc.byte_count_ = sizeof(info);
    desc.ident_ = kMsgStartGame;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    for (PlayerRecord* p = start_roster_sweep(this, &search, true); p; p = step_roster_sweep(this, &search, true)) {
        p->best_combo_ = 0;
        p->clear_flags(); // carries no flag
        p->flag_bits_ &= ~kPlayerCaptureBlocked;
        server->put_on_outbox(player_handle(p), &desc);
        if (!in_net_game())
            break; // (split screen: the first player only)
    }
    if (PlayerRecord* king = is_crown_rule_active(this))
        king->give_crown(false);
    last_leader_ = nullptr;  // the last leader
    last_leading_team_ = -1; // the last leading team
    clear_queued_scripts();
    clear_roster();
    g_cheat_flag = false;
    if (rating_service_->update_logged_in())
        rating_service_->start_game();
}

// 0x0053C6E0
void Hub::cancel_join()
{
    net::NetMessageInfo desc;
    desc.make_new();
    if (join_phase_ == 10 || join_phase_ == 8 || join_phase_ == kJoinStateRefused)
        return;
    net::AppHost* client = client_app_;
    desc.ident_ = kMsgDisconnect;
    send_to_server(client, &desc);
    client->transmit(false);
    disconnect_clients();
    join_expiry_job_->drop_out();
    remove_all_new_players();
    if (presence_service_->authenticated_) // (logged in to the presence)
        presence_service_->change_standing(kHomieStatusChatting, script::fetch_text("homie_status_chatting"),
                                           lobby_service_->room_name_);
}

// 0x0053BED0
void __cdecl Hub::observer_task(tasks::Task<PlayerRecord>*)
{
    Hub* manager = g_online_hub;
    if (manager->spectate_request_ == 1) { // (the next player asked for)
        manager->spectate_player(manager->next_cam_player());
        manager->spectate_request_ = 0;
    }
}

// 0x00536890
void __cdecl Hub::observer_input(pad_input::Responder<Hub>* input)
{
    Hub* manager = input->content_;
    objects::Entity* root = find_root_window();
    if (root) {
        // Not while a menu is up: the root window's menu_state tag on.
        script::Struct* tags = new_params();
        root->copy_tags_to(tags);
        uint32_t menu_state = 0; // (left as it was on the stack when the root window has no menu_state: the original)
        tags->read_hash_value(kMenuState, &menu_state, 0);
        delete_params(tags);
        if (menu_state == kOn)
            return;
    }
    if (input->control_data_->makes_ & 0x40) // (the controller's button pressed: the next player)
        manager->spectate_request_ = 1;
}

// 0x0053BF00
void __cdecl Hub::auto_serve_task(tasks::Task<Hub>* task)
{
    Hub* manager = task_manager(task);
    if (frame_clock::get_time() - g_auto_serve_start < kAutoServeWait || get_game_type() != kNetLobby ||
        manager->count_players() < 2 || manager->awaiting_start_ ||
        frame_clock::get_time() - manager->lobby_since_ <= kAutoServeIdle)
        return;
    get_run(kLoadPendingPlayers);
    get_run(kStartNetworkGame);
    manager->awaiting_start_ = true;
}

// 0x0053BF90
void __cdecl Hub::join_progress_tick(tasks::Task<Hub>* task)
{
    Hub* manager = task_manager(task);
    switch (manager->join_phase_) {
    case kJoinStateConnecting: // 1
        get_run(kCreateConnectingDialog);
        manager->join_phase_ = 0;
        break;
    case kJoinStateJoining: // 2 (none for the host's own client)
        if (!manager->client_app_->is_host_local())
            get_run(kCreateJoiningDialog);
        manager->join_phase_ = 0;
        break;
    case kJoinStateJoiningWithPassword: // 3
        get_run(kCreateTryingPasswordDialog);
        manager->join_phase_ = kJoinStateTryingPassword;
        break;
    case 6: { // joined
        NewPlayerSearch search;
        JoinerRecord* info = first_new_player(manager, &search);
        for (; info && !(info->flag_bits_ & kNewPlayerLocal); info = next_new_player(manager, &search)) {
        }
        if (info && (info->flag_bits_ & kNewPlayerObserver)) { // the local player joined to observe
            get_call(code_address(&modules::Skate::drop_skater_from_roster), get_skate(),
                     reinterpret_cast<modules::Skate*>(get_skate())->get_home_rider()); // the local skater removed
            manager->spectate_player(nullptr);
        }
        get_run(kEnteredNetworkGame);
        manager->join_phase_ = 7;
        break;
    }
    case 8: // the join over
        get_run(kDialogBoxExit);
        manager->join_expiry_job_->drop_out();
        manager->join_phase_job_->drop_out();
        break;
    case kJoinStateRefused: { // 9
        script::Struct* params = new_params();
        params->put_name_key(kReason, manager->refused_reason_);
        manager->join_phase_job_->drop_out();
        get_run(kCreateJoinRefusedDialog, params);
        delete_params(params);
        break;
    }
    case 10: // cancelled
        manager->join_phase_job_->drop_out();
        break;
    default:
        break;
    }
}

// 0x0053C1F0
void __cdecl Hub::start_game_task(tasks::Task<Hub>* task)
{
    Hub* manager = task_manager(task);
    bool& waiting_shown = g_waiting_shown;
    if (manager->count_other_players() > 0) { // players still loading
        if (manager->get_on_server()) {
            if (!waiting_shown) {
                frontend::ElementPtr menu;
                frontend::UiTree::sole()->fetch_part(&menu, kCurrentMenuAnchor, 0);
                if (!menu.get_pull()) { // (no menu up)
                    get_run(kCreateWaitForPlayersDialog);
                    waiting_shown = true;
                }
            }
        } else if (!waiting_shown) {
            manager->build_net_notice(true, kGameWillStart, nullptr, nullptr, nullptr, "netexceptionprops", false,
                                      kPanelTime);
            waiting_shown = true;
        }
        return;
    }
    if (cfunc_all_players_are_ready(nullptr, nullptr) &&    // (the script command, without parameters)
        frame_clock::get_time() >= g_net_game_start_time) { // every one ready, and the time has come
        waiting_shown = false;
        manager->start_network_game();
        manager->start_game_job_->drop_out();
    }
}

// 0x0053EB70
void __cdecl Hub::connection_task(tasks::Task<Hub>* task)
{
    Hub* manager = task_manager(task);
    uint8_t* skate = get_skate();
    uint32_t& checked = g_auto_serve_checked; // the link checked last
    if (manager->is_network_game() && frame_clock::get_time() - checked > kLinkCheckInterval) {
        bool net_game = manager->in_net_game();
        checked = frame_clock::get_time();
        // the network cable unplugged: a method of the network object (0x006EFDD8) - on the PC the shared
        // return-false stub 0x005B9E50
        if (stubs::return_false()) {
            frontend::ElementPtr dialog;
            frontend::UiTree::sole()->fetch_part(&dialog, net_game ? kLinkLostDialogAnchor : kDialogBoxAnchor, 0);
            if (!dialog.get_pull()) {
                get_run(net_game ? kCreateLinkUnpluggedDialog : kCreateLinkUnpluggedFrontEndDialog);
                return;
            }
        }
    }
    if (manager->get_on_server()) { // the players' connections watched
        PlayerSearch search;
        PlayerRecord* player = start_roster_sweep(manager, &search, true);
        while (player) {
            PlayerRecord* next = step_roster_sweep(manager, &search, true);
            net::Conn* conn = player->conn_;
            if (!conn->is_host_local()) {
                int reason = -1;
                if (conn->cur_standing_ & 1) { // (in the game)
                    if (conn->get_silence_duration() > kSilentTimeout)
                        reason = kDropTimedOut;
                    else if (conn->get_highest_resend_count() > kMaxResends)
                        reason = kDropDropped;
                } else if (conn->get_silence_duration() > kJoiningTimeout) {
                    reason = kDropTimedOut;
                }
                if (reason >= 0) {
                    objects::Entity* skater = player_skater(player);
                    bool observer = is_observer(player);
                    manager->evict_player(player, reason);
                    if (!observer)
                        get_call(code_address(&modules::Skate::drop_skater_from_roster), skate, skater);
                }
            }
            player = next;
        }
        return;
    }
    // A client: the server silent too long while joining or joined.
    net::ConnSearch search;
    net::AppHost* client = manager->client_app_;
    if (!client)
        return;
    net::Conn* conn = client->begin_conn_walk(&search);
    if (!conn || !(manager->join_phase_ == 8 || manager->join_phase_ == 7 || manager->join_phase_ == 6) ||
        conn->get_silence_duration() <= kSilentTimeout)
        return;
    if (reinterpret_cast<modules::Skate*>(skate)->level_ == kLoadMainMenu) {
        get_run(kShowJoinTimeoutNotice);
        manager->join_phase_job_->drop_out();
        manager->join_phase_ = 10;
    } else {
        get_run(kCreateLostConnectionDialog);
        manager->destroy_all_players();
        manager->remove_all_new_players(); // the joining players gone
        manager->disconnect_clients();     // the apps shut down
    }
}

} // namespace online
