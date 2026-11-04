// The network manager's player queries and the observer's camera (online::Hub, 0x0053A910-0x0053AF90), and the
// host mode (0x0053C320). See gamenet_manager.h.

#include <cstdint>
#include <cstring>
#include <array>
#include <gsl/assert>

#include "engine/core/manager.h"
#include "engine/net/player_info.h"
#include "engine/core/enum_ops.h"
#include "platform/gamespy.h"
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
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_observer.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "engine/sys/mlp.h"
#include "game/rider/skater_camera_component.h"
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
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"
#include "platform/compiler.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "wire/gamenet_messages.h"
#include "engine/net/net_game_state.h"
#include "engine/core/checksums.h"
#include "engine/core/str.h"
#include "wire/message_ids.h"
#include "engine/net/online_identity.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's copies of a player's name)

namespace online {

using namespace detail;

namespace {
constexpr int kRejoinDelayFrames = 10;


constexpr uint32_t kSkaterCamObject = 0x967C138C;      // the camera object (by id)
constexpr uint32_t kSkaterCamera = 0x5E43A604;         // SkaterCamera
constexpr uint32_t kWalkCamera = 0xC7D37EC2;           // WalkCamera
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl
constexpr uint32_t kFcfsPreference = 0x043EE978;       // the network preference naming the first-come notice's text

constexpr int kMaxObjectIds = 16;

// PlayerRecord flags (+0x1D0).
constexpr PlayerFlag kPlayerFcfsCandidate = PlayerFlag::kLocal | PlayerFlag::kFullyIn; // (0x00543380)
constexpr uint32_t kPlayerInfoSize = 0x1F0;


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
constexpr auto kDropTexts = std::to_array<uint32_t>({
    0xA609ACD7,                // net_message_player_quit
    kNetMessagePlayerTimedOut, //
    0xDF79BBF5,                // net_message_player_now_observing
    0xDF087FD9,                // net_message_player_banned
    0x3CD67FE6,                // net_message_player_kicked
    0xDDCDDB87,                // net_message_player_dropped
    0x2165392E,                // net_message_player_left_out
});

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;               // netlobby
constexpr uint32_t kHidePanelStuff = 0x47180228;         // hide_panel_stuff
constexpr uint32_t kShouldModulateColor = 0x11941568;    // should_modulate_color
constexpr uint32_t kNetworkEndGameSelected = 0x70E20955; // network_end_game_selected
constexpr uint32_t kTeam = 0x3B1F59E0;                   // Team
constexpr uint32_t kString0 = 0xA4B08520;                // String0
constexpr uint32_t kFlagReturned = 0x3B1F2478;           // flag_returned
constexpr uint32_t kHideCtfArrow = 0x1C318F8C;           // hide_ctf_arrow

// (The joining player's record: online::JoinerRecord, player_info.h.)

tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks()); }

// The skater camera's / walk camera's calls not rewritten as members here.
void set_camera_skater(objects::ObjectModule* camera, void* skater)
{
    core::down_cast<objects::RiderChaseView>(camera)->bind_rider(static_cast<objects::GameEntity*>(skater));
}
void set_walk_camera_skater(objects::ObjectModule* camera, void* skater)
{
    core::down_cast<objects::FootView>(camera)->bind_rider(static_cast<objects::GameEntity*>(skater));
}
void reset_walk_camera(objects::ObjectModule* camera) { core::down_cast<objects::FootView>(camera)->reset_now(); }

} // namespace

// 0x0053A910
void Hub::spectate_player(PlayerRecord* player)
{
    auto* cam_object = core::down_cast<objects::GameEntity>(objects::EntityHub::look_up_entity(kSkaterCamObject));
    objects::ObjectModule* skater_camera = cam_object->fetch_module(kSkaterCamera);
    objects::ObjectModule* walk_camera = cam_object->fetch_module(kWalkCamera);
    if (!player || !player_skater(player)) {
        set_camera_skater(skater_camera, nullptr);
        set_walk_camera_skater(walk_camera, nullptr);
        core::down_cast<objects::FootView>(walk_camera)->set_instant_frames(3); // (frames that jump straight to the target)
        viewed_player_ = nullptr;
        return;
    }
    auto* skater = player_skater(player);
    if (player != find_own_player_record(net_game_state().online_hub())) {
        // Someone else's skater: the skater camera onto it.
        set_camera_skater(skater_camera, skater);
        set_walk_camera_skater(walk_camera, nullptr);
        core::down_cast<objects::FootView>(walk_camera)->set_instant_frames(3);
        skater_camera->hold_off(false);
        walk_camera->hold_off(true);
        viewed_player_ = player;
        return;
    }
    // The local skater: the walk camera while it walks (its physics control's state says), else the skater camera.
    objects::ObjectModule* control = skater->fetch_module(kSkaterPhysicsControl);
    set_camera_skater(skater_camera, skater);
    set_walk_camera_skater(walk_camera, skater);
    core::down_cast<objects::FootView>(walk_camera)->set_instant_frames(3);
    const objects::RiderPoseState* state = core::down_cast<objects::RiderBoardPhysics>(control)->pose();
    if ((state->in_vehicle() == 0U) && state->physics_mode() == objects::RiderPoseState::PhysicsMode::kSkating) {
        skater_camera->hold_off(false);
        walk_camera->hold_off(true);
    } else {
        skater_camera->hold_off(true);
        walk_camera->hold_off(false);
        reset_walk_camera(walk_camera);
    }
    viewed_player_ = player;
}

// 0x0053AA70
void Hub::send_observe_request()
{
    net::NetMessageInfo desc;
    if (is_observer(find_own_player_record(this))) { // (no local player: the original reads through null too)
        return;
    }
    desc.set_ident(MessageId::kObserveRequest);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(MessageGroup::kGame);
    send_to_server(client_app_, &desc);
}

// 0x0053AAC0
void Hub::assign_first_come_first_served()
{
    PlayerSearch search;
    net::NetMessageInfo desc;
    for (PlayerRecord* player = start_roster_sweep(this, &search, false); player;
         player = step_roster_sweep(this, &search, false)) {
        if (is_own_player(player) || !core::any(player->flag_bits() & kPlayerFcfsCandidate)) {
            continue;
        }
        player->set_host_player(); // (its flag 0x200: first come, first served)
        uint32_t text = 0;
        net_options_
            .fetch_setting(kFcfsPreference)
            ->read_hash_value("Checksum", &text, 1);
        desc.set_content(&text);
        desc.set_byte_count(sizeof(text));
        desc.set_ident(MessageId::kFcfsNotice);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(MessageGroup::kGame);
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
        for (const PlayerRecord* player = start_roster_sweep(this, &search, false); player && !used;
             player = step_roster_sweep(this, &search, false)) {
            used = ints::as_signed(get_skater_id(player_skater(player))) ==
                   id; // (a player without a skater: read at 0x14)
        }
        NewPlayerSearch new_search;
        for (const JoinerRecord* info = first_new_player(this, &new_search); info && !used;
             info = next_new_player(this, &new_search)) {
            const PlayerFlag flags = info->flag_bits();
            if (core::any(flags & PlayerFlag::kObserver) && !core::any(flags & PlayerFlag::kPendingObserver)) {
                continue; // (an observer joining takes no id)
            }
            used = static_cast<int8_t>(info->entity_key()) == id;
        }
        if (!used) {
            return id;
        }
    }
    return kMaxObjectIds;
}

// 0x0053AE10
PlayerRecord* Hub::find_player_by_sender(net::Conn* conn)
{
    PlayerSearch search;
    for (PlayerRecord* player = start_roster_sweep(this, &search, true); player;
         player = step_roster_sweep(this, &search, true)) {
        if (player->conn() == conn) {
            return player;
        }
    }
    return nullptr;
}

// 0x0053AEA0
PlayerRecord* Hub::find_player_slot_by_obj(uint16_t id)
{
    PlayerSearch search;
    for (PlayerRecord* player = start_roster_sweep(this, &search, false); player;
         player = step_roster_sweep(this, &search, false)) {
        const objects::Entity* skater = player_skater(player);
        if (skater && get_skater_id(skater) == id) {
            return player;
        }
    }
    return nullptr;
}

// 0x0053C770
PlayerRecord* Hub::new_player(objects::Entity* skater, net::Conn* conn, PlayerFlag flags)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap());
    void* block = memory::alloc_zeroed(kPlayerInfoSize);
    PlayerRecord* player = block ? memory::construct_in<PlayerRecord>(block, flags) : nullptr;
    Expects(player != nullptr);
    player->set_skater(core::down_cast<objects::Rider>(skater)); // (out of memory: written through null, as the original)
    player->set_conn(conn);
    if (is_own_player(player)) {
        if (is_observer(player)) {
            pad_input::Hub* input = pad_input::get_input_manager();
            tasks::PileUp* tasks = get_logic_tasks();
            get_run(kHidePanelStuff);
            spectate_player(next_cam_player()); // the first player to watch
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

// 0x0053C950
void Hub::remove_player_record(PlayerRecord* player)
{
    if (player == viewed_player_) { // the camera onto the next player (the local skater's if there is one)
        viewed_player_ = next_cam_player();
        if (player == viewed_player_) {
            viewed_player_ = nullptr;
        }
        PlayerRecord* local = find_own_player_record(this);
        spectate_player(local && player_skater(local) ? local : viewed_player_);
    }
    if ((player->holds_crown() != 0) && crown_) { // it had the crown: taken off
        crown_->take_off();
    }
    if (player->is_carrying_flag() != 0) { // it carried a flag: returned
        flag_returned(player);
    }
    if (player) {
        player->teardown(1); // (its scalar deleting destructor, the original's slot 0)
    }
    update_lobby_server();
    if (net_game_state().online_active() && net_game_state().online_hub()->in_net_game() &&
        net_game_state().query_reporting()) {      // (reported to GameSpy's qr2: the player count changed)
        game_spy::qr2_send_state_changed(nullptr); // qr2_send_statechanged
    }
}

// 0x0053CB70
void Hub::evict_player(PlayerRecord* player, int reason)
{
    std::array<char, kNameSize> name{};
    text::copy_into(name, player_name(player));
    if (!is_observer(player) && get_game_mode()->is_true(kShouldModulateColor)) {
        objects::trick_spot_hub()->release_player_clusters(
            static_cast<int>(get_skater_id(player_skater(player)))); // its colours off the trick objects
    }
    if (!get_on_server()) {
        remove_player_record(player);
        return;
    }
    const net::Conn* conn = player->conn();
    game_spy::cd_key_disconnect_user(net::kGameSpyGameId, static_cast<int>(conn->ident())); // gcd_disconnect_user
    if (!is_observer(player)) {
        PlayerSearch search;
        for (PlayerRecord* other = start_roster_sweep(this, &search, true); other;
             other = step_roster_sweep(this, &search, true)) {
            if (is_own_player(other)) {
                continue;
            }
            net::NetMessageInfo desc;
            std::array<uint8_t, 2> msg = {{static_cast<uint8_t>(get_skater_id(player_skater(player))), static_cast<uint8_t>(reason)}};
            desc.set_content(msg.data());
            desc.set_byte_count(sizeof(msg));
            desc.set_ident(MessageId::kPlayerGone);
            desc.set_queue(net::NetMessageInfo::kLaneOrdered);
            desc.set_group_id(MessageGroup::kGame);
            host_->put_on_outbox(player_handle(other), &desc);
        }
        if ((reason == kDropQuit || reason == kDropTimedOut || reason == kDropDropped) &&
            rating_service_->update_logged_in()) { // (a rated game left early)
            rating_service_->remove_player(static_cast<int>(get_skater_id(player_skater(player))));
        }
    }
    bool close = true;
    if (reason == kDropBanned || reason == kDropKicked) {
        conn = player->conn();
        host_->deliver_to({.msg_id_ = MessageId::kRemovedYou, .size = 0}, nullptr, {.ip = conn->remote_ip(), .port = conn->port_no()}, 0);
        if (reason == kDropBanned) {
            host_->block_connection(player->conn());
        }
    } else if (reason == kDropLeftOut) {
        conn = player->conn();
        host_->deliver_to({.msg_id_ = MessageId::kMovedOn, .size = 0}, nullptr, {.ip = conn->remote_ip(), .port = conn->port_no()}, 0);
    } else if (reason == kDropObserving) {
        close = false;
    }
    if (close && player->conn()) {
        player->conn()->mark_stale();
        player->conn()->drop_queued_traffic();
    }
    const uint32_t text = reason >= 0 && reason < ints::as_signed(sizeof(kDropTexts) / sizeof(core::at(kDropTexts, 0)))
                              ? core::at(kDropTexts, reason)
                              : kNetMessagePlayerTimedOut;
    if (!is_own_player(player)) {
        build_net_notice(true, text, name.data(), nullptr, nullptr, nullptr, false, kPanelTime);
    }
    if (reason == kDropObserving) {
        if (player->holds_crown() != 0) { // the crown taken off
            if (crown_) {
                crown_->take_off();
            }
            player->give_crown(false);
        }
        if (player->is_carrying_flag() != 0) {
            flag_returned(player);
        }
    } else {
        const bool fcfs = player->is_host_player() != 0;
        remove_player_record(player);
        if (fcfs) { // it was to serve next: someone else
            assign_first_come_first_served();
            if (!get_host_player() && get_game_type() != kNetLobby) {
                get_run(kNetworkEndGameSelected);
            }
        }
    }
    // Every player still playing has finished: game over (message 0x5A to every one).
    PlayerSearch search;
    for (const PlayerRecord* p = start_roster_sweep(this, &search, false); p;
         p = step_roster_sweep(this, &search, false)) {
        if (!core::any(p->flag_bits() & PlayerFlag::kReady)) {
            return;
        }
    }
    net::NetMessageInfo desc;
    desc.set_ident(MessageId::kGameOver);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(MessageGroup::kGame);
    for (PlayerRecord* p = start_roster_sweep(this, &search, true); p; p = step_roster_sweep(this, &search, true)) {
        host_->put_on_outbox(player_handle(p), &desc);
    }
}

// 0x0053D120
void Hub::re_add_pending_players()
{
    update_lobby_server();
    net_game_state().set_open_waiting(true);
    if (net_game_state().online_active() && net_game_state().online_hub()->in_net_game() &&
        net_game_state().query_reporting() && !net_game_state().open_waiting()) { // (just set: never)
        game_spy::qr2_send_state_changed(nullptr);
    }
    int frames = kRejoinDelayFrames;
    PlayerSearch search;
    PlayerRecord* player = start_roster_sweep(this, &search, true);
    while (player) {
        PlayerRecord* next = step_roster_sweep(this, &search, true); // (before the player is destroyed)
        if (core::any(player->flag_bits() & PlayerFlag::kPendingObserver)) {
            JoinerRecord info; // (its constructor: a skater profile)
            frames += kRejoinDelayFrames;
            text::copy_into(info.label(), player_name(player));
            info.set_entity_key(static_cast<uint8_t>(reserve_obj_id_for_player()));
            info.set_conn(player->conn());
            info.flag_bits() = PlayerFlag::kRejoining;
            info.set_profile_id(player->profile_id());
            info.set_skill_points(player->skill_points());
            info.set_accumulated_score(0);
            info.set_eliminated_time(0);
            info.set_score(0);
            info.set_drive_control_mode(player->drive_control_mode());
            memory::Hub* memory = memory::Hub::get_pull();
            memory->enter_heap_scope(memory->skater_info_heap());
            info.account()->copy_from(player->account()); // its appearance
            memory::Hub::get_pull()->leave_heap_scope();
            remove_player_record(player);
            add_joining_player(&info, frames); // (added back in `frames` frames, 10 apart; then its destructor)
        }
        player = next;
    }
    for (PlayerRecord* p = start_roster_sweep(this, &search, false); p; p = step_roster_sweep(this, &search, false)) {
        if (!is_own_player(p)) {
            core::down_cast<objects::Rider>(player_skater(p))->conceal_skater(); // hidden
            core::down_cast<objects::Rider>(player_skater(p))->reset_27c();      // reset
        }
    }
}

// 0x0053D900
void Hub::destroy_all_players()
{
    PlayerSearch search;
    PlayerRecord* player = start_roster_sweep(this, &search, true);
    while (player) {
        PlayerRecord* next = step_roster_sweep(this, &search, true);
        objects::Entity* skater = player_skater(player);
        if (skater && (core::down_cast<objects::Rider>(skater)->get_skater_number() != 0 ||
                       !is_own_player(player))) { // (not the first local skater)
            modules::Skate::drop_skater_from_roster(core::down_cast<objects::Rider>(skater));
        }
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
    PlayerSearch search;
    PlayerRecord* player = start_roster_sweep(this, &search, false);
    while (player) {
        PlayerRecord* next = step_roster_sweep(this, &search, false);
        if (!core::any(player->flag_bits() & kPlayerFcfsCandidate)) {
            objects::Entity* skater = player_skater(player);
            evict_player(player, kDropLeftOut);
            modules::Skate::drop_skater_from_roster(core::down_cast<objects::Rider>(skater));
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
    void* peer = lobby_service_->remote();
    if (peer && (game_spy::peer_is_connected(peer) != 0) && lobby_service_->hosting()) {
        game_spy::peer_state_changed(peer); // (the hosted game's details to GameSpy's peer SDK)
    }
}

// (inline in remove_player_record / evict_player)
void Hub::flag_returned(PlayerRecord* player)
{
    const int team = player->get_flag_team(); // the flag's team
    constexpr size_t kKeySize = 64;
    std::array<char, kKeySize> key{};
    team_name_key(key, team);
    script::StructPtr params = script::make_struct();
    params->put_int(checksums::Name{kTeam}, team);
    params->append_word(kString0, script::QbValueKind::kString, script::fetch_text(key.data()));
    get_run(kFlagReturned, params.get());
    params.reset();
    PlayerRecord* local = find_own_player_record(this);
    if (local && !is_observer(local) && player_team(local) == team) {
        get_run(kHideCtfArrow);
    }
    player->clear_flags(); // carries no flag
}

// 0x0053E650
void Hub::enter_observe_mode()
{
    tasks::PileUp* tasks = get_logic_tasks();
    pad_input::Hub* input = pad_input::get_input_manager();
    PlayerRecord* local = find_own_player_record(this);
    if (is_observer(local)) {
        return;
    }
    if (get_on_server()) {
        evict_player(local, kDropObserving);
    }
    if (local->holds_crown() != 0) { // its crown given up
        local->give_crown(false);
        if (crown_) {
            crown_->take_off();
        }
    }
    if (local->is_carrying_flag() != 0) { // its flag returned (hide_ctf_arrow whatever the team, unlike flag_returned)
        const int team = local->get_flag_team();
        constexpr size_t kKeySize = 64;
        std::array<char, kKeySize> key{};
        team_name_key(key, team);
        script::StructPtr params = script::make_struct();
        params->put_int(checksums::Name{kTeam}, team);
        params->append_word(kString0, script::QbValueKind::kString, script::fetch_text(key.data()));
        get_run(kFlagReturned, params.get());
        params.reset();
        get_run(kHideCtfArrow);
        local->clear_flags();
    }
    local->flag_bits() |= PlayerFlag::kObserver; // an observer
    modules::Skate::drop_skater_from_roster(core::down_cast<objects::Rider>(player_skater(local)));
    local->set_skater(nullptr);
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(core::at(memory->skater_heaps(), 0));
    spectate_player(next_cam_player()); // the first player to watch
    add_observer_controls(this, local, tasks, input, -1);
    spectate_request_ = 0;
    get_run(kHidePanelStuff);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0053C320
void Hub::set_host_mode(int mode)
{
    if (mode == host_mode_) {
        return;
    }
    host_mode_ = mode;
    if (mode == kHostModeAutoServe) {
        send_observe_request();
        net_game_state().set_auto_serve_start(frame_clock::get_time()); // (the auto-serve task waits 2 s from here)
        get_logic_tasks()->queue_job(auto_serve_job_.get());
    } else if (mode == kHostModeFcfs) {
        send_observe_request();
        assign_first_come_first_served();
    }
}

} // namespace online
