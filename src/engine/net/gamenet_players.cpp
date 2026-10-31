// The network manager's player queries and the observer's camera (online::Hub, 0x0053A910-0x0053AF90), and the
// host mode (0x0053C320). See gamenet_manager.h.

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/net/gamespy.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/stats_man.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/trick_objects.h"
#include "engine/pad_input/input_hub.h"
#include "game/skate/profile_manager.h"
#include "game/rider/rider.h"
#include "game/rider/walk_camera.h"
#include "platform/stopwatch.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_observer.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "engine/sys/mlp.h"
#include "game/rider/skater_camera_component.h"
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
#include "engine/net/buddy_man.h"

#pragma warning(disable : 4996) // (strcpy: the original's copies of a player's name)

namespace online {

using namespace detail;

namespace {

constexpr uint8_t kMsgObserveRequest = 0x4A; // (the server's handle_observe_request)
constexpr uint8_t kMsgFcfsNotice = 0x5F;     // (the client's handle_fcfs_notice)

constexpr uint32_t kSkaterCamObject = 0x967C138C;     // the camera object (by id)
constexpr uint32_t kSkaterCamera = 0x5E43A604;        // SkaterCamera
constexpr uint32_t kWalkCamera = 0xC7D37EC2;          // WalkCamera
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl
constexpr uint32_t kFcfsPreference = 0x043EE978;      // the network preference naming the first-come notice's text

constexpr int kMaxObjectIds = 16;
constexpr uint32_t kNewPlayerObserver = 4; // JoinerRecord flags
constexpr uint32_t kNewPlayerPending = 8;

// PlayerRecord flags (+0x1D0).
constexpr uint32_t kPlayerFcfsCandidate = 0x21; // (0x00543380: local or 0x20)
constexpr uint32_t kPlayerPending = 0x08;       // joined during a game: added for the next one
constexpr uint32_t kPlayerGameEnded = 0x100;    // its game has ended (message 0x59)
constexpr uint32_t kPlayerInfoSize = 0x1F0;

constexpr uint8_t kMsgPlayerGone = 0x2D;  // a player left (its object id, the reason)
constexpr uint8_t kMsgRemovedYou = 0x51;  // 'Q': kicked or banned (to its address)
constexpr uint8_t kMsgMovedOn = 0x56;     // 'V': the game went on without it
constexpr uint8_t kMsgGameOver = 0x5A;

constexpr int kGameSpyGameId = 1003; // THUG2 PC's (the CD-key SDK)
constexpr int kPanelTime = 2000;

// Why a player is dropped (evict_player's reason) and the panel message for each.
constexpr int kDropQuit = 0;
constexpr int kDropTimedOut = 1;
constexpr int kDropObserving = 2;
constexpr int kDropBanned = 3;
constexpr int kDropKicked = 4;
constexpr int kDropDropped = 5;
constexpr int kDropLeftOut = 6;
constexpr uint32_t kNetMessagePlayerTimedOut = 0x2FD5CBE0; // net_message_player_timed_out
constexpr uint32_t kDropTexts[] = {
    0xA609ACD7,                // net_message_player_quit
    kNetMessagePlayerTimedOut, //
    0xDF79BBF5,                // net_message_player_now_observing
    0xDF087FD9,                // net_message_player_banned
    0x3CD67FE6,                // net_message_player_kicked
    0xDDCDDB87,                // net_message_player_dropped
    0x2165392E,                // net_message_player_left_out
};

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;              // netlobby
constexpr uint32_t kHidePanelStuff = 0x47180228;        // hide_panel_stuff
constexpr uint32_t kShouldModulateColor = 0x11941568;   // should_modulate_color
constexpr uint32_t kNetworkEndGameSelected = 0x70E20955; // network_end_game_selected
constexpr uint32_t kTeam = 0x3B1F59E0;                  // Team
constexpr uint32_t kString0 = 0xA4B08520;               // String0
constexpr uint32_t kFlagReturned = 0x3B1F2478;          // flag_returned
constexpr uint32_t kHideCtfArrow = 0x1C318F8C;          // hide_ctf_arrow

// (The joining player's record: online::JoinerRecord, player_info.h.)
constexpr uint32_t kNewPlayerRejoining = 0x10; // (a pending player added back)

tasks::PileUp* get_logic_tasks()
{
    return reinterpret_cast<tasks::PileUp*>(reinterpret_cast<uint8_t*>(assets::g_main_loop_manager) + 4);
}

// The skater camera's / walk camera's calls not rewritten as members here.
void set_camera_skater(objects::ObjectModule* camera, void* skater)
{
    static_cast<objects::RiderChaseView*>(camera)->bind_rider(static_cast<objects::GameEntity*>(skater));
}
void set_walk_camera_skater(objects::ObjectModule* camera, void* skater)
{
    static_cast<objects::FootView*>(camera)->bind_rider(static_cast<objects::GameEntity*>(skater));
}
void reset_walk_camera(objects::ObjectModule* camera) { static_cast<objects::FootView*>(camera)->reset_now(); }

} // namespace

// 0x0053A910
void Hub::spectate_player(uint8_t* player)
{
    auto* cam_object = static_cast<objects::GameEntity*>(
        reinterpret_cast<objects::ObjectHubBase*>(reinterpret_cast<void*>(objects::g_composite_registry))->look_up_entity(kSkaterCamObject));
    objects::ObjectModule* skater_camera = cam_object->fetch_module(kSkaterCamera);
    objects::ObjectModule* walk_camera = cam_object->fetch_module(kWalkCamera);
    if (!player || !player_skater(player)) {
        set_camera_skater(skater_camera, nullptr);
        set_walk_camera_skater(walk_camera, nullptr);
        prop<int>(walk_camera, 0x18) = 3; // (frames that jump straight to the target)
        viewed_player_ = nullptr;
        return;
    }
    auto* skater = reinterpret_cast<objects::GameEntity*>(player_skater(player));
    if (player != find_own_player_record(g_online_hub)) {
        // Someone else's skater: the skater camera onto it.
        set_camera_skater(skater_camera, skater);
        set_walk_camera_skater(walk_camera, nullptr);
        prop<int>(walk_camera, 0x18) = 3;
        skater_camera->virtual_suspend(false);
        walk_camera->virtual_suspend(true);
        viewed_player_ = player;
        return;
    }
    // The local skater: the walk camera while it walks (its physics control's state says), else the skater camera.
    objects::ObjectModule* control = skater->fetch_module(kSkaterPhysicsControl);
    set_camera_skater(skater_camera, skater);
    set_walk_camera_skater(walk_camera, skater);
    prop<int>(walk_camera, 0x18) = 3;
    uint8_t* state = prop<uint8_t*>(control, 0x2C);
    if (!prop<bool>(state, 0x118) && prop<int>(state, 0x1C) == 0) {
        skater_camera->virtual_suspend(false);
        walk_camera->virtual_suspend(true);
    } else {
        skater_camera->virtual_suspend(true);
        walk_camera->virtual_suspend(false);
        reset_walk_camera(walk_camera);
    }
    viewed_player_ = player;
}

// 0x0053AA70
void Hub::send_observe_request()
{
    net::NetMessageInfo desc;
    desc.make_new();
    if (is_observer(find_own_player_record(this))) // (no local player: the original reads through null too)
        return;
    desc.ident_ = kMsgObserveRequest;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    send_to_server(client_app_, &desc);
}

// 0x0053AAC0
void Hub::assign_first_come_first_served()
{
    PlayerSearch search;
    net::NetMessageInfo desc;
    desc.make_new();
    for (uint8_t* player = start_roster_sweep(this, &search, false); player; player = step_roster_sweep(this, &search, false)) {
        if (is_own_player(player) || !(prop<uint32_t>(player, 0x1D0) & kPlayerFcfsCandidate))
            continue;
        as_player(player)->set_host_player(); // (its flag 0x200: first come, first served)
        uint32_t text;
        reinterpret_cast<prefs::Preferences*>(net_options_)
            ->fetch_setting(kFcfsPreference)
            ->read_hash_value("Checksum", &text, 1);
        desc.content_ = &text;
        desc.byte_count_ = sizeof(text);
        desc.ident_ = kMsgFcfsNotice;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        host_->put_on_outbox(player_handle(player), &desc);
        return;
    }
}

// 0x0053AC40
int Hub::reserve_obj_id_for_player()
{
    for (int id = 0; id < kMaxObjectIds; ++id) {
        bool used = false;
        PlayerSearch search;
        for (uint8_t* player = start_roster_sweep(this, &search, false); player && !used;
             player = step_roster_sweep(this, &search, false))
            used = static_cast<int>(get_skater_id(player_skater(player))) == id; // (a player without a skater: read at 0x14)
        NewPlayerSearch new_search;
        for (uint8_t* info = first_new_player(this, &new_search); info && !used; info = next_new_player(this, &new_search)) {
            uint32_t flags = prop<uint32_t>(info, 0x20);
            if ((flags & kNewPlayerObserver) && !(flags & kNewPlayerPending))
                continue; // (an observer joining takes no id)
            used = prop<int8_t>(info, 0x10) == id;
        }
        if (!used)
            return id;
    }
    return kMaxObjectIds;
}

// 0x0053AE10
uint8_t* Hub::find_player_by_sender(net::Conn* conn)
{
    PlayerSearch search;
    for (uint8_t* player = start_roster_sweep(this, &search, true); player; player = step_roster_sweep(this, &search, true))
        if (prop<net::Conn*>(player, 0x18) == conn)
            return player;
    return nullptr;
}

// 0x0053AEA0
uint8_t* Hub::find_player_slot_by_obj(uint16_t id)
{
    PlayerSearch search;
    for (uint8_t* player = start_roster_sweep(this, &search, false); player; player = step_roster_sweep(this, &search, false)) {
        objects::Entity* skater = player_skater(player);
        if (skater && get_skater_id(skater) == id)
            return player;
    }
    return nullptr;
}

// 0x0053C770
uint8_t* Hub::new_player(objects::Entity* skater, net::Conn* conn, uint32_t flags)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap_);
    void* block = memory::alloc_zeroed(kPlayerInfoSize);
    auto* player = reinterpret_cast<uint8_t*>(block ? static_cast<PlayerRecord*>(block)->make_new(flags) : nullptr);
    prop<objects::Entity*>(player, 0x14) = skater; // (out of memory: written through null, as the original)
    prop<net::Conn*>(player, 0x18) = conn;
    if (is_own_player(player)) {
        if (is_observer(player)) {
            uint8_t* input = reinterpret_cast<uint8_t*>(pad_input::g_input_manager);
            tasks::PileUp* tasks = get_logic_tasks();
            get_run(kHidePanelStuff);
            spectate_player(static_cast<uint8_t*>(next_cam_player())); // the first player to watch
            add_observer_controls(this, player, tasks, input);
        } else {
            viewed_player_ = nullptr;
        }
    } else if (is_observer(find_own_player_record(this)) && !viewed_player_) { // (no local player: read through null)
        spectate_player(player);
    }
    memory::Hub::get_pull()->leave_heap_scope();
    return player;
}

bool g_online_active;
bool g_open_waiting;

// 0x0053C950
void Hub::remove_player_record(uint8_t* player)
{
    if (player == viewed_player_) { // the camera onto the next player (the local skater's if there is one)
        viewed_player_ = next_cam_player();
        if (player == viewed_player_)
            viewed_player_ = nullptr;
        uint8_t* local = find_own_player_record(this);
        spectate_player(local && player_skater(local) ? local : static_cast<uint8_t*>(viewed_player_));
    }
    if (as_player(player)->holds_crown() && prop<void*>(this, 0x6C)) // it had the crown: taken off
        prop<objects::CrownObject*>(this, 0x6C)->take_off();
    if (as_player(player)->is_carrying_flag()) // it carried a flag: returned
        flag_returned(player);
    if (player)
        reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(player))[0])(player, 1);
    update_lobby_server();
    if (g_online_active && g_online_hub->in_net_game() &&
        g_query_reporting) // (reported to GameSpy's qr2: the player count changed)
        game_spy::qr2_send_state_changed(nullptr); // qr2_send_statechanged
}

// 0x0053CB70
void Hub::evict_player(uint8_t* player, int reason)
{
    char name[kNameSize];
    std::strcpy(name, player_name(player));
    if (!is_observer(player) && get_call<bool>(code_address(&goals::MatchRules::is_true), get_game_mode(), kShouldModulateColor))
        objects::g_trick_spot_hub->release_player_clusters(static_cast<int>(get_skater_id(player_skater(player)))); // its colours off the trick objects
    if (!get_on_server()) {
        remove_player_record(player);
        return;
    }
    net::Conn* conn = prop<net::Conn*>(player, 0x18);
    game_spy::cd_key_disconnect_user(kGameSpyGameId, static_cast<int>(conn->ident_)); // gcd_disconnect_user
    if (!is_observer(player)) {
        PlayerSearch search;
        for (uint8_t* other = start_roster_sweep(this, &search, true); other; other = step_roster_sweep(this, &search, true)) {
            if (is_own_player(other))
                continue;
            net::NetMessageInfo desc;
            desc.make_new();
            uint8_t msg[2] = {static_cast<uint8_t>(get_skater_id(player_skater(player))), static_cast<uint8_t>(reason)};
            desc.content_ = msg;
            desc.byte_count_ = sizeof(msg);
            desc.ident_ = kMsgPlayerGone;
            desc.queue_ = net::NetMessageInfo::kLaneOrdered;
            desc.group_id_ = kGroupGame;
            host_->put_on_outbox(player_handle(other), &desc);
        }
        if ((reason == kDropQuit || reason == kDropTimedOut || reason == kDropDropped) &&
            static_cast<RankingsManager*>(rating_service_)->update_logged_in()) // (a rated game left early)
            static_cast<RankingsManager*>(rating_service_)->remove_player(static_cast<int>(get_skater_id(player_skater(player))));
    }
    bool close = true;
    if (reason == kDropBanned || reason == kDropKicked) {
        conn = prop<net::Conn*>(player, 0x18);
        host_->deliver_to(kMsgRemovedYou, 0, nullptr, conn->remote_ip_, conn->port_no_, 0);
        if (reason == kDropBanned)
            host_->block_connection(prop<net::Conn*>(player, 0x18));
    } else if (reason == kDropLeftOut) {
        conn = prop<net::Conn*>(player, 0x18);
        host_->deliver_to(kMsgMovedOn, 0, nullptr, conn->remote_ip_, conn->port_no_, 0);
    } else if (reason == kDropObserving) {
        close = false;
    }
    if (close && prop<net::Conn*>(player, 0x18)) {
        prop<net::Conn*>(player, 0x18)->mark_stale();
        prop<net::Conn*>(player, 0x18)->drop_queued_traffic();
    }
    uint32_t text = reason >= 0 && reason < static_cast<int>(sizeof(kDropTexts) / sizeof(kDropTexts[0]))
                        ? kDropTexts[reason]
                        : kNetMessagePlayerTimedOut;
    if (!is_own_player(player))
        build_net_notice(true, text, name, nullptr, nullptr, nullptr, false, kPanelTime);
    if (reason == kDropObserving) {
        if (as_player(player)->holds_crown()) { // the crown taken off
            if (prop<void*>(this, 0x6C))
                prop<objects::CrownObject*>(this, 0x6C)->take_off();
            as_player(player)->give_crown(false);
        }
        if (as_player(player)->is_carrying_flag())
            flag_returned(player);
    } else {
        bool fcfs = as_player(player)->is_host_player() != 0;
        remove_player_record(player);
        if (fcfs) { // it was to serve next: someone else
            assign_first_come_first_served();
            if (!get_host_player() && get_game_type() != kNetLobby)
                get_run(kNetworkEndGameSelected);
        }
    }
    // Every player still playing has finished: game over (message 0x5A to every one).
    PlayerSearch search;
    for (uint8_t* p = start_roster_sweep(this, &search, false); p; p = step_roster_sweep(this, &search, false))
        if (!(prop<uint32_t>(p, 0x1D0) & kPlayerGameEnded))
            return;
    net::NetMessageInfo desc;
    desc.make_new();
    desc.ident_ = kMsgGameOver;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    for (uint8_t* p = start_roster_sweep(this, &search, true); p; p = step_roster_sweep(this, &search, true))
        host_->put_on_outbox(player_handle(p), &desc);
}

// 0x0053D120
void Hub::re_add_pending_players()
{
    update_lobby_server();
    g_open_waiting = true;
    if (g_online_active && g_online_hub->in_net_game() &&
        g_query_reporting && g_open_waiting != 1) // (just set: never)
        game_spy::qr2_send_state_changed(nullptr);
    int frames = 10;
    PlayerSearch search;
    uint8_t* player = start_roster_sweep(this, &search, true);
    while (player) {
        uint8_t* next = step_roster_sweep(this, &search, true); // (before the player is destroyed)
        if (prop<uint32_t>(player, 0x1D0) & kPlayerPending) {
            JoinerRecord info;
            info.make_new(); // (a skater profile)
            frames += 10;
            std::strcpy(info.label_, player_name(player));
            info.entity_key_ = static_cast<uint8_t>(reserve_obj_id_for_player());
            info.conn_ = prop<net::Conn*>(player, 0x18);
            info.flag_bits_ = kNewPlayerRejoining;
            info.profile_id_ = prop<int32_t>(player, 0x28);
            info.skill_points_ = prop<int32_t>(player, 0x2C);
            info.unknown38_ = 0;
            info.unknown3c_ = 0;
            info.unknown34_ = 0;
            info.drive_control_mode_ = prop<int32_t>(player, 0x48);
            memory::Hub* memory = memory::Hub::get_pull();
            memory->enter_heap_scope(memory->skater_info_heap_);
            info.account_->copy_from(prop<objects::RiderIdentity*>(player, 0x4C)); // its appearance
            memory::Hub::get_pull()->leave_heap_scope();
            remove_player_record(player);
            add_joining_player(&info, frames); // (added back in `frames` frames, 10 apart)
            info.destroy_body();
        }
        player = next;
    }
    for (uint8_t* p = start_roster_sweep(this, &search, false); p; p = step_roster_sweep(this, &search, false)) {
        if (!is_own_player(p)) {
            static_cast<objects::Rider*>(player_skater(p))->conceal_skater(); // hidden
            static_cast<objects::Rider*>(player_skater(p))->reset_27c();   // reset
        }
    }
}

// 0x0053D900
void Hub::destroy_all_players()
{
    uint8_t* skate = get_skate();
    PlayerSearch search;
    uint8_t* player = start_roster_sweep(this, &search, true);
    while (player) {
        uint8_t* next = step_roster_sweep(this, &search, true);
        objects::Entity* skater = player_skater(player);
        if (skater && (static_cast<objects::Rider*>(skater)->get_skater_number() != 0 || !is_own_player(player))) // (not the first local skater)
            get_call(code_address(&modules::Skate::drop_skater_from_roster), skate, skater);
        remove_player_record(player);
        player = next;
    }
}

// 0x0053E910
void Hub::remove_players()
{
    destroy_all_players();
    remove_all_new_players(); // the joining players too
}

// 0x0053E920
void Hub::drop_players_not_ready()
{
    uint8_t* skate = get_skate();
    PlayerSearch search;
    uint8_t* player = start_roster_sweep(this, &search, false);
    while (player) {
        uint8_t* next = step_roster_sweep(this, &search, false);
        if (!(prop<uint32_t>(player, 0x1D0) & kPlayerFcfsCandidate)) {
            objects::Entity* skater = player_skater(player);
            evict_player(player, kDropLeftOut);
            get_call(code_address(&modules::Skate::drop_skater_from_roster), skate, skater);
        }
        player = next;
    }
}

// 0x0053EE70
void Hub::remove_players_and_apps()
{
    destroy_all_players();
    remove_all_new_players();
    disconnect_clients(); // the apps shut down
}

// (inline in remove_player_record / re_add_pending_players)
void Hub::update_lobby_server()
{
    void* peer = prop<void*>(lobby_service_, 0x140);
    if (peer && game_spy::peer_is_connected(peer) && prop<bool>(lobby_service_, 0x139))
        game_spy::peer_state_changed(peer); // (the hosted game's details to GameSpy's peer SDK)
}

// (inline in remove_player_record / evict_player)
void Hub::flag_returned(uint8_t* player)
{
    int team = as_player(player)->get_flag_team(); // the flag's team
    char key[64];
    team_name_key(key, team);
    script::Struct* params = new_params();
    params->put_int(kTeam, team);
    params->append_word(kString0, script::kQbvalString, script::fetch_text(key));
    get_run(kFlagReturned, params);
    delete_params(params);
    uint8_t* local = find_own_player_record(this);
    if (local && !is_observer(local) && player_team(local) == team)
        get_run(kHideCtfArrow);
    as_player(player)->clear_flags(); // carries no flag
}

// 0x0053E650
void Hub::enter_observe_mode()
{
    uint8_t* skate = get_skate();
    tasks::PileUp* tasks = get_logic_tasks();
    uint8_t* input = reinterpret_cast<uint8_t*>(pad_input::g_input_manager);
    uint8_t* local = find_own_player_record(this);
    if (is_observer(local))
        return;
    if (get_on_server())
        evict_player(local, kDropObserving);
    if (as_player(local)->holds_crown()) { // its crown given up
        as_player(local)->give_crown(false);
        if (prop<void*>(this, 0x6C))
            prop<objects::CrownObject*>(this, 0x6C)->take_off();
    }
    if (as_player(local)->is_carrying_flag()) { // its flag returned (hide_ctf_arrow whatever the team, unlike flag_returned)
        int team = as_player(local)->get_flag_team();
        char key[64];
        team_name_key(key, team);
        script::Struct* params = new_params();
        params->put_int(kTeam, team);
        params->append_word(kString0, script::kQbvalString, script::fetch_text(key));
        get_run(kFlagReturned, params);
        delete_params(params);
        get_run(kHideCtfArrow);
        as_player(local)->clear_flags();
    }
    prop<uint32_t>(local, 0x1D0) |= 4; // an observer
    get_call(code_address(&modules::Skate::drop_skater_from_roster), skate, player_skater(local));
    prop<objects::Entity*>(local, 0x14) = nullptr;
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_heaps_[0]);
    spectate_player(static_cast<uint8_t*>(next_cam_player())); // the first player to watch
    add_observer_controls(this, local, tasks, input, -1);
    spectate_request_ = 0;
    get_run(kHidePanelStuff);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0053C320
void Hub::set_host_mode(int mode)
{
    if (mode == host_mode_)
        return;
    host_mode_ = mode;
    if (mode == kHostModeAutoServe) {
        send_observe_request();
        g_auto_serve_start = frame_clock::get_time(); // (the auto-serve task waits 2 s from here)
        get_logic_tasks()->queue_job(prop<tasks::SchedulerTask*>(this, 0xF4));
    } else if (mode == kHostModeFcfs) {
        send_observe_request();
        assign_first_come_first_served();
    }
}

} // namespace online
